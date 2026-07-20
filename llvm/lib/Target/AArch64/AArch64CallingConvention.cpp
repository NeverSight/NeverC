//=== AArch64CallingConvention.cpp - AArch64 CC impl ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the table-generated and custom routines for the AArch64
// Calling Convention.
//
//===----------------------------------------------------------------------===//

#include "AArch64CallingConvention.h"
#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/NeverCCallConv.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include <algorithm>
using namespace llvm;

static const MCPhysReg XRegList[] = {AArch64::X0, AArch64::X1, AArch64::X2,
                                     AArch64::X3, AArch64::X4, AArch64::X5,
                                     AArch64::X6, AArch64::X7};
static const MCPhysReg HRegList[] = {AArch64::H0, AArch64::H1, AArch64::H2,
                                     AArch64::H3, AArch64::H4, AArch64::H5,
                                     AArch64::H6, AArch64::H7};
static const MCPhysReg SRegList[] = {AArch64::S0, AArch64::S1, AArch64::S2,
                                     AArch64::S3, AArch64::S4, AArch64::S5,
                                     AArch64::S6, AArch64::S7};
static const MCPhysReg DRegList[] = {AArch64::D0, AArch64::D1, AArch64::D2,
                                     AArch64::D3, AArch64::D4, AArch64::D5,
                                     AArch64::D6, AArch64::D7};
static const MCPhysReg QRegList[] = {AArch64::Q0, AArch64::Q1, AArch64::Q2,
                                     AArch64::Q3, AArch64::Q4, AArch64::Q5,
                                     AArch64::Q6, AArch64::Q7};
static const MCPhysReg ZRegList[] = {AArch64::Z0, AArch64::Z1, AArch64::Z2,
                                     AArch64::Z3, AArch64::Z4, AArch64::Z5,
                                     AArch64::Z6, AArch64::Z7};

static bool finishStackBlock(SmallVectorImpl<CCValAssign> &PendingMembers,
                             MVT LocVT, ISD::ArgFlagsTy &ArgFlags,
                             CCState &State, Align SlotAlign) {
  if (LocVT.isScalableVector()) {
    const AArch64Subtarget &Subtarget = static_cast<const AArch64Subtarget &>(
        State.getMachineFunction().getSubtarget());
    const AArch64TargetLowering *TLI = Subtarget.getTargetLowering();

    // We are about to reinvoke the CCAssignFn auto-generated handler. If we
    // don't unset these flags we will get stuck in an infinite loop forever
    // invoking the custom handler.
    ArgFlags.setInConsecutiveRegs(false);
    ArgFlags.setInConsecutiveRegsLast(false);

    // The calling convention for passing SVE tuples states that in the event
    // we cannot allocate enough registers for the tuple we should still leave
    // any remaining registers unallocated. However, when we call the
    // CCAssignFn again we want it to behave as if all remaining registers are
    // allocated. This will force the code to pass the tuple indirectly in
    // accordance with the PCS.
    bool RegsAllocated[8];
    for (int I = 0; I < 8; I++) {
      RegsAllocated[I] = State.isAllocated(ZRegList[I]);
      State.AllocateReg(ZRegList[I]);
    }

    auto &It = PendingMembers[0];
    CCAssignFn *AssignFn =
        TLI->CCAssignFnForCall(State.getCallingConv(), /*IsVarArg=*/false);
    if (AssignFn(It.getValNo(), It.getValVT(), It.getValVT(), CCValAssign::Full,
                 ArgFlags, State))
      llvm_unreachable("Call operand has unhandled type");

    // Return the flags to how they were before.
    ArgFlags.setInConsecutiveRegs(true);
    ArgFlags.setInConsecutiveRegsLast(true);

    // Return the register state back to how it was before, leaving any
    // unallocated registers available for other smaller types.
    for (int I = 0; I < 8; I++)
      if (!RegsAllocated[I])
        State.DeallocateReg(ZRegList[I]);

    // All pending members have now been allocated
    PendingMembers.clear();
    return true;
  }

  unsigned Size = LocVT.getSizeInBits() / 8;
  for (auto &It : PendingMembers) {
    It.convertToMem(State.AllocateStack(Size, SlotAlign));
    State.addLoc(It);
    SlotAlign = Align(1);
  }

  // All pending members have now been allocated
  PendingMembers.clear();
  return true;
}

/// The Darwin variadic PCS places anonymous arguments in 8-byte stack slots. An
/// [N x Ty] type must still be contiguous in memory though.
static bool CC_AArch64_Custom_Stack_Block(unsigned &ValNo, MVT &ValVT,
                                          MVT &LocVT,
                                          CCValAssign::LocInfo &LocInfo,
                                          ISD::ArgFlagsTy &ArgFlags,
                                          CCState &State) {
  SmallVectorImpl<CCValAssign> &PendingMembers = State.getPendingLocs();

  // Add the argument to the list to be allocated once we know the size of the
  // block.
  PendingMembers.push_back(
      CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));

  if (!ArgFlags.isInConsecutiveRegsLast())
    return true;

  return finishStackBlock(PendingMembers, LocVT, ArgFlags, State, Align(8));
}

/// Given an [N x Ty] block, it should be passed in a consecutive sequence of
/// registers. If no such sequence is available, mark the rest of the registers
/// of that type as used and place the argument on the stack.
static bool CC_AArch64_Custom_Block(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                    CCValAssign::LocInfo &LocInfo,
                                    ISD::ArgFlagsTy &ArgFlags, CCState &State) {
  const AArch64Subtarget &Subtarget = static_cast<const AArch64Subtarget &>(
      State.getMachineFunction().getSubtarget());

  // Try to allocate a contiguous block of registers, each of the correct
  // size to hold one member.
  ArrayRef<MCPhysReg> RegList;
  if (LocVT.SimpleTy == MVT::i64)
    RegList = XRegList;
  else if (LocVT.SimpleTy == MVT::f16)
    RegList = HRegList;
  else if (LocVT.SimpleTy == MVT::f32 || LocVT.is32BitVector())
    RegList = SRegList;
  else if (LocVT.SimpleTy == MVT::f64 || LocVT.is64BitVector())
    RegList = DRegList;
  else if (LocVT.SimpleTy == MVT::f128 || LocVT.is128BitVector())
    RegList = QRegList;
  else if (LocVT.isScalableVector())
    RegList = ZRegList;
  else {
    // Not an array we want to split up after all.
    return false;
  }

  SmallVectorImpl<CCValAssign> &PendingMembers = State.getPendingLocs();

  // Add the argument to the list to be allocated once we know the size of the
  // block.
  PendingMembers.push_back(
      CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));

  if (!ArgFlags.isInConsecutiveRegsLast())
    return true;

  unsigned RegResult = State.AllocateRegBlock(RegList, PendingMembers.size());
  if (RegResult) {
    for (auto &It : PendingMembers) {
      It.convertToReg(RegResult);
      State.addLoc(It);
      ++RegResult;
    }
    PendingMembers.clear();
    return true;
  }

  if (!LocVT.isScalableVector()) {
    // Mark all regs in the class as unavailable
    for (auto Reg : RegList)
      State.AllocateReg(Reg);
  }

  const Align StackAlign =
      State.getMachineFunction().getDataLayout().getStackAlignment();
  const Align MemAlign = ArgFlags.getNonZeroMemAlign();
  Align SlotAlign = std::min(MemAlign, StackAlign);
  if (!Subtarget.isTargetDarwin())
    SlotAlign = std::max(SlotAlign, Align(8));

  return finishStackBlock(PendingMembers, LocVT, ArgFlags, State, SlotAlign);
}

// TableGen provides definitions of the calling convention analysis entry
// points.
#include "AArch64GenCallingConv.inc"

//===----------------------------------------------------------------------===//
//   NeverC data-driven custom calling convention (CallingConv::NeverC_Custom)
//===----------------------------------------------------------------------===//
//
// Same data-driven model as the X86 backend: the register layout comes from the
// callee's "neverc-callconv" attribute (parsed by llvm/CodeGen/NeverCCallConv.h)
// rather than tablegen. GPR names are x0..x28 (the allocator picks W for i32 and
// X for i64/pointers); vector names are v0..v31 (H/S/D/Q chosen by type).

namespace {
struct A64GPR {
  MCRegister X, W;
};
struct A64Vec {
  MCRegister Q, D, S, H;
};
} // namespace

static A64GPR neverCA64ParseGPR(StringRef Name) {
  return StringSwitch<A64GPR>(Name.trim())
#define NEVERC_A64_GPR(NAME, XR, WR)                                            \
  .CaseLower(NAME, A64GPR{AArch64::XR, AArch64::WR})
#include "AArch64NeverCRegNames.def"
      .Default(A64GPR{});
}

static A64Vec neverCA64ParseVec(StringRef Name) {
  return StringSwitch<A64Vec>(Name.trim())
#define NEVERC_A64_VEC(NAME, QR, DR, SR, HR)                                    \
  .CaseLower(NAME, A64Vec{AArch64::QR, AArch64::DR, AArch64::SR, AArch64::HR})
#include "AArch64NeverCRegNames.def"
      .Default(A64Vec{});
}

MCRegister llvm::neverCParseA64Reg(StringRef Name) {
  A64GPR G = neverCA64ParseGPR(Name);
  if (G.X.isValid())
    return G.X;
  A64Vec V = neverCA64ParseVec(Name);
  if (V.Q.isValid())
    return V.Q; // full 128-bit vector reg (callee-saves and mask agree at all widths)
  return MCRegister();
}

static StringRef neverCA64GetPlanString(CCState &State) {
  if (State.isNeverCSpecInjected())
    return State.getNeverCCustomSpec();
  const Function &F = State.getMachineFunction().getFunction();
  if (F.hasFnAttribute(neverc::CallConvPlanAttrName))
    return F.getFnAttribute(neverc::CallConvPlanAttrName)
        .getValueAsString();
  return StringRef();
}

static bool neverCA64IsFP(MVT LocVT) {
  return LocVT == MVT::f16 || LocVT == MVT::f32 || LocVT == MVT::f64 ||
         LocVT == MVT::f128 ||
         (LocVT.isVector() && LocVT.getSizeInBits() <= 128);
}

static void neverCA64PromoteSmallInt(MVT &LocVT, CCValAssign::LocInfo &LocInfo,
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

/// Resolve a spec token to the matching-width physreg for LocVT. Returns an
/// invalid register when the token doesn't name a register of the right class.
static MCRegister neverCA64RegForToken(StringRef Tok, MVT LocVT) {
  if (LocVT == MVT::i32 || LocVT == MVT::i64) {
    A64GPR G = neverCA64ParseGPR(Tok);
    if (!G.X.isValid())
      return MCRegister();
    return LocVT == MVT::i64 ? G.X : G.W;
  }
  if (neverCA64IsFP(LocVT)) {
    A64Vec V = neverCA64ParseVec(Tok);
    if (!V.Q.isValid())
      return MCRegister();
    if (LocVT == MVT::f16)
      return V.H;
    if (LocVT == MVT::f32)
      return V.S;
    if (LocVT == MVT::f64)
      return V.D;
    return V.Q; // f128 / 128-bit vector
  }
  return MCRegister();
}

static MCRegister neverCA64RegForPlan(MCRegister Register, MVT LocVT) {
  if (LocVT == MVT::i32 || LocVT == MVT::i64) {
#define NEVERC_A64_GPR(NAME, XR, WR)                                            \
  if (Register == AArch64::XR || Register == AArch64::WR)                       \
    return LocVT == MVT::i64 ? AArch64::XR : AArch64::WR;
#include "AArch64NeverCRegNames.def"
    return MCRegister();
  }
  if (neverCA64IsFP(LocVT)) {
#define NEVERC_A64_VEC(NAME, QR, DR, SR, HR)                                    \
  if (Register == AArch64::QR || Register == AArch64::DR ||                     \
      Register == AArch64::SR || Register == AArch64::HR) {                     \
    if (LocVT == MVT::f16)                                                       \
      return AArch64::HR;                                                        \
    if (LocVT == MVT::f32)                                                       \
      return AArch64::SR;                                                        \
    if (LocVT == MVT::f64)                                                       \
      return AArch64::DR;                                                        \
    return AArch64::QR;                                                          \
  }
#include "AArch64NeverCRegNames.def"
  }
  return MCRegister();
}

static bool neverCA64AssignReg(unsigned ValNo, MVT ValVT, MVT LocVT,
                               CCValAssign::LocInfo LocInfo, CCState &State,
                               ArrayRef<StringRef> GPRs,
                               ArrayRef<StringRef> Vecs) {
  bool IsInt = LocVT == MVT::i32 || LocVT == MVT::i64;
  for (StringRef Name : (IsInt ? GPRs : Vecs)) {
    MCRegister Reg = neverCA64RegForToken(Name, LocVT);
    if (Reg.isValid() && !State.isAllocated(Reg)) {
      State.AllocateReg(Reg);
      State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
      return true;
    }
  }
  return false;
}

static void neverCA64AssignStack(unsigned ValNo, MVT ValVT, MVT LocVT,
                                 CCValAssign::LocInfo LocInfo, CCState &State) {
  uint64_t Size = std::max<uint64_t>(LocVT.getStoreSize().getFixedValue(), 8);
  Align A(Size > 8 ? 16 : 8);
  unsigned Off = State.AllocateStack(Size, A);
  State.addLoc(CCValAssign::getMem(ValNo, ValVT, Off, LocVT, LocInfo));
}

static bool neverCA64AssignPlanLocation(
    unsigned ValNo, MVT ValVT, MVT LocVT,
    CCValAssign::LocInfo LocInfo, CCState &State,
    const neverc::CCPlanLocation &Location) {
  if (Location.Kind == neverc::CCPlanLocationKind::Register) {
    MCRegister Register =
        neverCA64RegForPlan(MCRegister(Location.RegisterNumber), LocVT);
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

static void neverCA64AssignPositional(unsigned ValNo, MVT ValVT, MVT LocVT,
                                      CCValAssign::LocInfo LocInfo,
                                      CCState &State,
                                      const neverc::CustomCCSpec &Spec) {
  if (ValNo < Spec.Args.size()) {
    StringRef Tok = Spec.Args[ValNo].trim();
    if (!neverc::isStackToken(Tok)) {
      MCRegister Reg = neverCA64RegForToken(Tok, LocVT);
      if (Reg.isValid() && !State.isAllocated(Reg)) {
        State.AllocateReg(Reg);
        State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
        return;
      }
    }
  }
  neverCA64AssignStack(ValNo, ValVT, LocVT, LocInfo, State);
}

bool llvm::CC_AArch64_NeverC(unsigned ValNo, MVT ValVT, MVT LocVT,
                             CCValAssign::LocInfo LocInfo,
                             ISD::ArgFlagsTy ArgFlags, CCState &State) {
  StringRef PlanText = neverCA64GetPlanString(State);
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
      neverCA64PromoteSmallInt(UseVT, UseLI, ArgFlags);
      if (neverCA64AssignPlanLocation(
              ValNo, ValVT, UseVT, UseLI, State, *Location))
        return false;
      if (ValNo == 0)
        F.getContext().diagnose(DiagnosticInfoUnsupported(
            F, "NeverC calling convention plan cannot be "
               "materialized by the AArch64 backend"));
    }
  }
  return CC_AArch64_AAPCS(ValNo, ValVT, LocVT, LocInfo, ArgFlags, State);
}

bool llvm::RetCC_AArch64_NeverC(unsigned ValNo, MVT ValVT, MVT LocVT,
                                CCValAssign::LocInfo LocInfo,
                                ISD::ArgFlagsTy ArgFlags, CCState &State) {
  StringRef PlanText = neverCA64GetPlanString(State);
  if (!PlanText.empty()) {
    neverc::CustomCCPlan Plan;
    if (neverc::parseCustomCCPlan(PlanText, Plan))
      if (const neverc::CCPlanLocation *Location =
              Plan.findReturn(ValNo)) {
      MVT UseVT = LocVT;
      CCValAssign::LocInfo UseLI = LocInfo;
      neverCA64PromoteSmallInt(UseVT, UseLI, ArgFlags);
      if (neverCA64AssignPlanLocation(
              ValNo, ValVT, UseVT, UseLI, State, *Location))
        return false;
      }
  }
  return RetCC_AArch64_AAPCS(ValNo, ValVT, LocVT, LocInfo, ArgFlags, State);
}
