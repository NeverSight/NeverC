//=== X86CallingConv.cpp - X86 Custom Calling Convention Impl   -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of custom routines for the X86
// Calling Convention that aren't done by tablegen.
//
//===----------------------------------------------------------------------===//

#include "X86CallingConv.h"
#include "X86Subtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/NeverCCallConv.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <algorithm>

using namespace llvm;

static ArrayRef<MCPhysReg> CC_X86_VectorCallGetSSEs(const MVT &ValVT) {
  if (ValVT.is512BitVector()) {
    static const MCPhysReg RegListZMM[] = {X86::ZMM0, X86::ZMM1, X86::ZMM2,
                                           X86::ZMM3, X86::ZMM4, X86::ZMM5};
    return ArrayRef(std::begin(RegListZMM), std::end(RegListZMM));
  }

  if (ValVT.is256BitVector()) {
    static const MCPhysReg RegListYMM[] = {X86::YMM0, X86::YMM1, X86::YMM2,
                                           X86::YMM3, X86::YMM4, X86::YMM5};
    return ArrayRef(std::begin(RegListYMM), std::end(RegListYMM));
  }

  static const MCPhysReg RegListXMM[] = {X86::XMM0, X86::XMM1, X86::XMM2,
                                         X86::XMM3, X86::XMM4, X86::XMM5};
  return ArrayRef(std::begin(RegListXMM), std::end(RegListXMM));
}

static ArrayRef<MCPhysReg> CC_X86_64_VectorCallGetGPRs() {
  static const MCPhysReg RegListGPR[] = {X86::RCX, X86::RDX, X86::R8, X86::R9};
  return ArrayRef(std::begin(RegListGPR), std::end(RegListGPR));
}

static bool CC_X86_VectorCallAssignRegister(unsigned &ValNo, MVT &ValVT,
                                            MVT &LocVT,
                                            CCValAssign::LocInfo &LocInfo,
                                            ISD::ArgFlagsTy &ArgFlags,
                                            CCState &State) {

  ArrayRef<MCPhysReg> RegList = CC_X86_VectorCallGetSSEs(ValVT);

  for (auto Reg : RegList) {
    if (!State.isAllocated(Reg)) {
      unsigned AssigedReg = State.AllocateReg(Reg);
      assert(AssigedReg == Reg && "Expecting a valid register allocation");
      State.addLoc(
          CCValAssign::getReg(ValNo, ValVT, AssigedReg, LocVT, LocInfo));
      return true;
    }
    if (State.IsShadowAllocatedReg(Reg)) {
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return true;
    }
  }

  llvm_unreachable("Frontend should ensure that hva marked vectors will have "
                   "an available register.");
  return false;
}

/// Vectorcall calling convention has special handling for vector types or
/// HVA for 64 bit arch.
/// For HVAs shadow registers might be allocated on the first pass
/// and actual XMM registers are allocated on the second pass.
/// For vector types, actual XMM registers are allocated on the first pass.
/// \return true if registers were allocated and false otherwise.
static bool CC_X86_64_VectorCall(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                 CCValAssign::LocInfo &LocInfo,
                                 ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  // On the second pass, go through the HVAs only.
  if (ArgFlags.isSecArgPass()) {
    if (ArgFlags.isHva())
      return CC_X86_VectorCallAssignRegister(ValNo, ValVT, LocVT, LocInfo,
                                             ArgFlags, State);
    return true;
  }

  // Process only vector types as defined by vectorcall spec:
  // "A vector type is either a floating-point type, for example,
  //  a float or double, or an SIMD vector type, for example, __m128 or __m256".
  if (!(ValVT.isFloatingPoint() ||
        (ValVT.isVector() && ValVT.getSizeInBits() >= 128))) {
    // If R9 was already assigned it means that we are after the fourth element
    // and because this is not an HVA / Vector type, we need to allocate
    // shadow XMM register.
    if (State.isAllocated(X86::R9)) {
      // Assign shadow XMM register.
      (void)State.AllocateReg(CC_X86_VectorCallGetSSEs(ValVT));
    }

    return false;
  }

  if (!ArgFlags.isHva() || ArgFlags.isHvaStart()) {
    // Assign shadow GPR register.
    (void)State.AllocateReg(CC_X86_64_VectorCallGetGPRs());

    // Assign XMM register - (shadow for HVA and non-shadow for non HVA).
    if (unsigned Reg = State.AllocateReg(CC_X86_VectorCallGetSSEs(ValVT))) {
      // In Vectorcall Calling convention, additional shadow stack can be
      // created on top of the basic 32 bytes of win64.
      // It can happen if the fifth or sixth argument is vector type or HVA.
      // At that case for each argument a shadow stack of 8 bytes is allocated.
      const TargetRegisterInfo *TRI =
          State.getMachineFunction().getSubtarget().getRegisterInfo();
      if (TRI->regsOverlap(Reg, X86::XMM4) || TRI->regsOverlap(Reg, X86::XMM5))
        State.AllocateStack(8, Align(8));

      if (!ArgFlags.isHva()) {
        State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
        return true; // Allocated a register - Stop the search.
      }
    }
  }

  // If this is an HVA - Stop the search,
  // otherwise continue the search.
  return ArgFlags.isHva();
}

static bool CC_X86_AnyReg_Error(unsigned &, MVT &, MVT &,
                                CCValAssign::LocInfo &, ISD::ArgFlagsTy &,
                                CCState &) {
  llvm_unreachable("The AnyReg calling convention is only supported by the "
                   "stackmap and patchpoint intrinsics.");
  // gracefully fallback to X86 C calling convention on Release builds.
  return false;
}

/// X86 interrupt handlers can only take one or two stack arguments, but if
/// there are two arguments, they are in the opposite order from the standard
/// convention. Therefore, we have to look at the argument count up front before
/// allocating stack for each argument.
static bool CC_X86_Intr(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                        CCValAssign::LocInfo &LocInfo,
                        ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  size_t ArgCount = State.getMachineFunction().getFunction().arg_size();
  unsigned SlotSize = 8;
  unsigned Offset;
  if (ArgCount == 1 && ValNo == 0) {
    // If we have one argument, the argument is five stack slots big, at fixed
    // offset zero.
    Offset = State.AllocateStack(5 * SlotSize, Align(4));
  } else if (ArgCount == 2 && ValNo == 0) {
    // If we have two arguments, the stack slot is *after* the error code
    // argument. Pretend it doesn't consume stack space, and account for it when
    // we assign the second argument.
    Offset = SlotSize;
  } else if (ArgCount == 2 && ValNo == 1) {
    // If this is the second of two arguments, it must be the error code. It
    // appears first on the stack, and is then followed by the five slot
    // interrupt struct.
    Offset = 0;
    (void)State.AllocateStack(6 * SlotSize, Align(4));
  } else {
    report_fatal_error("unsupported x86 interrupt prototype");
  }

  // FIXME: This should be accounted for in
  // X86FrameLowering::getFrameIndexReference, not here.
  if (ArgCount == 2)
    Offset += SlotSize;

  State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
  return true;
}

static bool CC_X86_64_Pointer(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                              CCValAssign::LocInfo &LocInfo,
                              ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  if (LocVT != MVT::i64) {
    LocVT = MVT::i64;
    LocInfo = CCValAssign::ZExt;
  }
  return false;
}

// Provides entry points of CC_X86 and RetCC_X86.
#include "X86GenCallingConv.inc"

//===----------------------------------------------------------------------===//
//   NeverC data-driven custom calling convention (CallingConv::NeverC_Custom)
//===----------------------------------------------------------------------===//
//
// The register layout is *not* described in tablegen. It is read from the
// callee's "neverc-callconv" string attribute (parsed by
// llvm/CodeGen/NeverCCallConv.h) and applied here. See CallingConv.h for the
// rationale.

/// Translate a NeverC spec register name into a 64-bit GPR or an XMM register.
/// GPR names are always given in their 64-bit form (rax, r8, ...); the caller
/// narrows them to the right sub-register for i32 values. Returns an invalid
/// register for unknown names.
MCRegister llvm::neverCParseX86Reg(StringRef Name) {
  return StringSwitch<MCRegister>(Name.trim())
#define NEVERC_X86_GPR(NAME, REG) .CaseLower(NAME, X86::REG)
#define NEVERC_X86_XMM(NAME, REG) .CaseLower(NAME, X86::REG)
#include "X86NeverCRegNames.def"
      .Default(MCRegister());
}

/// True if \p R is one of the XMM registers in the spec table (single source of
/// truth: the same .def that maps names to registers).
static bool neverCRegIsXMM(MCRegister R) {
  switch (R.id()) {
  default:
    return false;
#define NEVERC_X86_XMM(NAME, REG) case X86::REG:
#include "X86NeverCRegNames.def"
    return true;
  }
}

/// Resolve the active spec for a CCState.
///
/// Caller side (LowerCall/LowerCallResult): the callee's spec was injected, so
/// use it verbatim -- even if empty, which simply means this call has no custom
/// layout. Callee side (LowerFormalArguments/LowerReturn): nothing was injected,
/// so read the spec from the function's own attribute. The injection flag keeps
/// the caller side from ever reading the *caller's* attribute by mistake.
static StringRef neverCGetPlanString(CCState &State) {
  if (State.isNeverCSpecInjected())
    return State.getNeverCCustomSpec();
  const Function &F = State.getMachineFunction().getFunction();
  if (F.hasFnAttribute(neverc::CallConvPlanAttrName))
    return F.getFnAttribute(neverc::CallConvPlanAttrName)
        .getValueAsString();
  return StringRef();
}

/// True for value types we route through the XMM argument/return list.
static bool neverCIsXMMType(MVT LocVT) {
  return LocVT == MVT::f16 || LocVT == MVT::f32 || LocVT == MVT::f64 ||
         (LocVT.isVector() && LocVT.getSizeInBits() <= 128);
}

/// Promote i1/i8/i16 to i32 the same way the standard X86 conventions do.
static void neverCPromoteSmallInt(MVT &LocVT, CCValAssign::LocInfo &LocInfo,
                                  ISD::ArgFlagsTy ArgFlags) {
  if (LocVT == MVT::i1 || LocVT == MVT::i8 || LocVT == MVT::i16) {
    LocVT = MVT::i32;
    if (ArgFlags.isSExt())
      LocInfo = CCValAssign::SExt;
    else if (ArgFlags.isZExt())
      LocInfo = CCValAssign::ZExt;
    else
      LocInfo = CCValAssign::AExt;
  }
}

/// Try to assign one value to a register from \p GPRs (integers) or \p XMMs
/// (floats/vectors). Returns true and records the assignment on success.
static bool neverCAssignReg(unsigned ValNo, MVT ValVT, MVT LocVT,
                            CCValAssign::LocInfo LocInfo, CCState &State,
                            ArrayRef<StringRef> GPRs, ArrayRef<StringRef> XMMs) {
  if (LocVT == MVT::i32 || LocVT == MVT::i64) {
    unsigned Bits = LocVT == MVT::i32 ? 32 : 64;
    for (StringRef Name : GPRs) {
      MCRegister Reg64 = neverCParseX86Reg(Name);
      if (!Reg64.isValid())
        continue;
      MCRegister Reg = getX86SubSuperRegister(Reg64, Bits);
      if (Reg.isValid() && !State.isAllocated(Reg)) {
        State.AllocateReg(Reg);
        State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
        return true;
      }
    }
  } else if (neverCIsXMMType(LocVT)) {
    for (StringRef Name : XMMs) {
      MCRegister Reg = neverCParseX86Reg(Name);
      if (Reg.isValid() && !State.isAllocated(Reg)) {
        State.AllocateReg(Reg);
        State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
        return true;
      }
    }
  }
  return false;
}

/// Spill one value to the stack (8-byte aligned, 16 for >8-byte types).
static void neverCAssignStack(unsigned ValNo, MVT ValVT, MVT LocVT,
                              CCValAssign::LocInfo LocInfo, CCState &State) {
  uint64_t SizeBytes =
      std::max<uint64_t>(LocVT.getStoreSize().getFixedValue(), 8);
  Align StackAlign(SizeBytes > 8 ? 16 : 8);
  unsigned Offset = State.AllocateStack(SizeBytes, StackAlign);
  State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
}

static bool neverCAssignPlanLocation(
    unsigned ValNo, MVT ValVT, MVT LocVT,
    CCValAssign::LocInfo LocInfo, CCState &State,
    const neverc::CCPlanLocation &Location) {
  if (Location.Kind == neverc::CCPlanLocationKind::Register) {
    MCRegister Register(Location.RegisterNumber);
    if (LocVT == MVT::i32 || LocVT == MVT::i64) {
      if (neverCRegIsXMM(Register))
        return false;
      Register = getX86SubSuperRegister(
          Register, LocVT == MVT::i32 ? 32 : 64);
    } else if (neverCIsXMMType(LocVT)) {
      if (!neverCRegIsXMM(Register))
        return false;
    } else {
      return false;
    }
    if (!Register.isValid() || State.isAllocated(Register))
      return false;
    State.AllocateReg(Register);
    State.addLoc(CCValAssign::getReg(
        ValNo, ValVT, Register, LocVT, LocInfo));
    return true;
  }

  if (Location.Kind != neverc::CCPlanLocationKind::Stack ||
      Location.StackOffset < State.getStackSize())
    return false;
  if (Location.StackOffset > State.getStackSize())
    State.AllocateStack(
        Location.StackOffset - State.getStackSize(), Align(1));
  const int64_t Offset =
      State.AllocateStack(Location.Size, Align(Location.Alignment));
  if (Offset < 0 ||
      static_cast<uint64_t>(Offset) != Location.StackOffset)
    return false;
  State.addLoc(CCValAssign::getMem(
      ValNo, ValVT, static_cast<unsigned>(Offset), LocVT, LocInfo));
  return true;
}

/// Positional argument allocator: argument number \p ValNo uses
/// Spec.Args[ValNo], which is either a register name or the stack keyword. The
/// stack keyword, running past the end of the list, a type-mismatched register
/// (e.g. an XMM name for an integer argument), or an already-used register all
/// fall back to a stack slot.
static void neverCAssignPositional(unsigned ValNo, MVT ValVT, MVT LocVT,
                                   CCValAssign::LocInfo LocInfo, CCState &State,
                                   const neverc::CustomCCSpec &Spec) {
  if (ValNo < Spec.Args.size()) {
    StringRef Tok = Spec.Args[ValNo].trim();
    if (!neverc::isStackToken(Tok)) {
      MCRegister R = neverCParseX86Reg(Tok);
      if (R.isValid()) {
        bool RegIsXMM = neverCRegIsXMM(R);
        if ((LocVT == MVT::i32 || LocVT == MVT::i64) && !RegIsXMM) {
          MCRegister Use =
              getX86SubSuperRegister(R, LocVT == MVT::i32 ? 32 : 64);
          if (Use.isValid() && !State.isAllocated(Use)) {
            State.AllocateReg(Use);
            State.addLoc(CCValAssign::getReg(ValNo, ValVT, Use, LocVT, LocInfo));
            return;
          }
        } else if (neverCIsXMMType(LocVT) && RegIsXMM) {
          if (!State.isAllocated(R)) {
            State.AllocateReg(R);
            State.addLoc(CCValAssign::getReg(ValNo, ValVT, R, LocVT, LocInfo));
            return;
          }
        }
      }
    }
  }
  neverCAssignStack(ValNo, ValVT, LocVT, LocInfo, State);
}

// IMPORTANT: a top-level CCAssignFn returns *false* once it has handled
// (assigned) a value, and *true* to mean "not handled" -- which makes
// CCState::Analyze*/Check* treat the value as unallocatable (fatal for args).
// The neverCAssignReg helper above uses the opposite, more natural convention
// (true == "I assigned a register"), so the wrappers translate between the two.

bool llvm::CC_X86_NeverC(unsigned ValNo, MVT ValVT, MVT LocVT,
                         CCValAssign::LocInfo LocInfo, ISD::ArgFlagsTy ArgFlags,
                         CCState &State) {
  if (State.getCallingConv() == CallingConv::NeverC_Custom) {
    StringRef PlanText = neverCGetPlanString(State);
    if (!PlanText.empty()) {
      neverc::CustomCCPlan Plan;
      const bool ValidPlan =
          neverc::parseCustomCCPlan(PlanText, Plan);
      const Function &F = State.getMachineFunction().getFunction();
      if (!ValidPlan) {
        if (ValNo == 0)
          F.getContext().diagnose(DiagnosticInfoUnsupported(
              F, "malformed NeverC calling convention plan"));
      } else if (const neverc::CCPlanLocation *Location =
                     Plan.findArgument(ValNo)) {
        if (State.isVarArg() && ValNo == 0)
          F.getContext().diagnose(DiagnosticInfoUnsupported(
              F,
              "NeverC custom calling convention does not support variadic "
              "functions: '" +
                  F.getName() + "'"));
        MVT UseVT = LocVT;
        CCValAssign::LocInfo UseLI = LocInfo;
        neverCPromoteSmallInt(UseVT, UseLI, ArgFlags);
        if (neverCAssignPlanLocation(
                ValNo, ValVT, UseVT, UseLI, State, *Location))
          return false;
        if (ValNo == 0)
          F.getContext().diagnose(DiagnosticInfoUnsupported(
              F, "NeverC calling convention plan cannot be "
                 "materialized by the X86 backend"));
      }
    }
  }
  return CC_X86(ValNo, ValVT, LocVT, LocInfo, ArgFlags, State);
}

bool llvm::RetCC_X86_NeverC(unsigned ValNo, MVT ValVT, MVT LocVT,
                            CCValAssign::LocInfo LocInfo,
                            ISD::ArgFlagsTy ArgFlags, CCState &State) {
  if (State.getCallingConv() == CallingConv::NeverC_Custom) {
    StringRef PlanText = neverCGetPlanString(State);
    if (!PlanText.empty()) {
      neverc::CustomCCPlan Plan;
      if (neverc::parseCustomCCPlan(PlanText, Plan))
        if (const neverc::CCPlanLocation *Location =
                Plan.findReturn(ValNo)) {
        MVT UseVT = LocVT;
        CCValAssign::LocInfo UseLI = LocInfo;
        neverCPromoteSmallInt(UseVT, UseLI, ArgFlags);
        if (neverCAssignPlanLocation(
                ValNo, ValVT, UseVT, UseLI, State, *Location))
          return false;
        }
    }
  }
  return RetCC_X86(ValNo, ValVT, LocVT, LocInfo, ArgFlags, State);
}
