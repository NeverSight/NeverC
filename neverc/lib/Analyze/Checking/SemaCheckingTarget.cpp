#include "SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/Core/SyncScope.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <bitset>
#include <cassert>
#include <optional>
#include <tuple>
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// ARM / AArch64 builtin validation
// ===----------------------------------------------------------------------===

enum ArmStreamingType { ArmNonStreaming, ArmStreaming, ArmStreamingCompatible };

bool Sema::ParseSVEImmChecks(
    CallExpr *TheCall,
    llvm::SmallVector<std::tuple<int, int, int>, 3> &ImmChecks) {
  // Perform all the immediate checks for this builtin call.
  bool HasError = false;
  for (auto &I : ImmChecks) {
    int ArgNum, CheckTy, ElementSizeInBits;
    std::tie(ArgNum, CheckTy, ElementSizeInBits) = I;

    typedef bool (*OptionSetCheckFnTy)(int64_t Value);

    // Function that checks whether the operand (ArgNum) is an immediate
    // that is one of the predefined values.
    auto CheckImmediateInSet = [&](OptionSetCheckFnTy CheckImm,
                                   int ErrDiag) -> bool {
      Expr *Arg = TheCall->getArg(ArgNum);
      llvm::APSInt Imm;
      if (SemaBuiltinConstantArg(TheCall, ArgNum, Imm))
        return true;

      if (!CheckImm(Imm.getSExtValue()))
        return Diag(TheCall->getBeginLoc(), ErrDiag) << Arg->getSourceRange();
      return false;
    };

    switch ((SVETypeFlags::ImmCheckType)CheckTy) {
    case SVETypeFlags::ImmCheck0_31:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 31))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_13:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 13))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck1_16:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1, 16))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_7:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 7))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck1_1:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1, 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck1_3:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1, 3))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck1_7:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1, 7))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckExtract:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0,
                                      (2048 / ElementSizeInBits) - 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckShiftRight:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1, ElementSizeInBits))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckShiftRightNarrow:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 1,
                                      ElementSizeInBits / 2))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckShiftLeft:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0,
                                      ElementSizeInBits - 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckLaneIndex:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0,
                                      (128 / (1 * ElementSizeInBits)) - 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckLaneIndexCompRotate:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0,
                                      (128 / (2 * ElementSizeInBits)) - 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckLaneIndexDot:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0,
                                      (128 / (4 * ElementSizeInBits)) - 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckComplexRot90_270:
      if (CheckImmediateInSet([](int64_t V) { return V == 90 || V == 270; },
                              diag::err_rotation_argument_to_cadd))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheckComplexRotAll90:
      if (CheckImmediateInSet(
              [](int64_t V) {
                return V == 0 || V == 90 || V == 180 || V == 270;
              },
              diag::err_rotation_argument_to_cmla))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_1:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 1))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_2:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 2))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_3:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 3))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_0:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 0))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_15:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 15))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck0_255:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 0, 255))
        HasError = true;
      break;
    case SVETypeFlags::ImmCheck2_4_Mul2:
      if (SemaBuiltinConstantArgRange(TheCall, ArgNum, 2, 4) ||
          SemaBuiltinConstantArgMultiple(TheCall, ArgNum, 2))
        HasError = true;
      break;
    }
  }

  return HasError;
}

ArmStreamingType getArmStreamingFnType(const FunctionDecl *FD) {
  if (FD->hasAttr<ArmLocallyStreamingAttr>())
    return ArmStreaming;
  if (const auto *T = FD->getType()->getAs<FunctionProtoType>()) {
    if (T->getAArch64SMEAttributes() & FunctionType::SME_PStateSMEnabledMask)
      return ArmStreaming;
    if (T->getAArch64SMEAttributes() & FunctionType::SME_PStateSMCompatibleMask)
      return ArmStreamingCompatible;
  }
  return ArmNonStreaming;
}

void checkArmStreamingBuiltin(Sema &S, CallExpr *TheCall,
                              const FunctionDecl *FD,
                              ArmStreamingType BuiltinType) {
  ArmStreamingType FnType = getArmStreamingFnType(FD);
  if (FnType == ArmStreaming && BuiltinType == ArmNonStreaming) {
    S.Diag(TheCall->getBeginLoc(), diag::warn_attribute_arm_sm_incompat_builtin)
        << TheCall->getSourceRange() << "streaming";
  }

  if (FnType == ArmStreamingCompatible &&
      BuiltinType != ArmStreamingCompatible) {
    S.Diag(TheCall->getBeginLoc(), diag::warn_attribute_arm_sm_incompat_builtin)
        << TheCall->getSourceRange() << "streaming compatible";
    return;
  }

  if (FnType == ArmNonStreaming && BuiltinType == ArmStreaming) {
    S.Diag(TheCall->getBeginLoc(), diag::warn_attribute_arm_sm_incompat_builtin)
        << TheCall->getSourceRange() << "non-streaming";
  }
}

bool hasSMEZAState(const FunctionDecl *FD) {
  if (FD->hasAttr<ArmNewZAAttr>())
    return true;
  if (const auto *T = FD->getType()->getAs<FunctionProtoType>())
    if (T->getAArch64SMEAttributes() & FunctionType::SME_PStateZASharedMask)
      return true;
  return false;
}

bool hasSMEZAState(unsigned BuiltinID) {
  switch (BuiltinID) {
  default:
    return false;
#define GET_SME_BUILTIN_HAS_ZA_STATE
#include "neverc/Foundation/arm_sme_builtins_za_state.td.h"
#undef GET_SME_BUILTIN_HAS_ZA_STATE
  }
}

bool Sema::CheckSMEBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall) {
  if (const FunctionDecl *FD = getCurFunctionDecl()) {
    std::optional<ArmStreamingType> BuiltinType;

    switch (BuiltinID) {
#define GET_SME_STREAMING_ATTRS
#include "neverc/Foundation/arm_sme_streaming_attrs.td.h"
#undef GET_SME_STREAMING_ATTRS
    }

    if (BuiltinType)
      checkArmStreamingBuiltin(*this, TheCall, FD, *BuiltinType);

    if (hasSMEZAState(BuiltinID) && !hasSMEZAState(FD))
      Diag(TheCall->getBeginLoc(),
           diag::warn_attribute_arm_za_builtin_no_za_state)
          << TheCall->getSourceRange();
  }

  // Range check SME intrinsics that take immediate values.
  llvm::SmallVector<std::tuple<int, int, int>, 3> ImmChecks;

  switch (BuiltinID) {
  default:
    return false;
#define GET_SME_IMMEDIATE_CHECK
#include "neverc/Foundation/arm_sme_sema_rangechecks.td.h"
#undef GET_SME_IMMEDIATE_CHECK
  }

  return ParseSVEImmChecks(TheCall, ImmChecks);
}

bool Sema::CheckSVEBuiltinFunctionCall(unsigned BuiltinID, CallExpr *TheCall) {
  if (const FunctionDecl *FD = getCurFunctionDecl()) {
    std::optional<ArmStreamingType> BuiltinType;

    switch (BuiltinID) {
#define GET_SVE_STREAMING_ATTRS
#include "neverc/Foundation/arm_sve_streaming_attrs.td.h"
#undef GET_SVE_STREAMING_ATTRS
    }
    if (BuiltinType)
      checkArmStreamingBuiltin(*this, TheCall, FD, *BuiltinType);
  }
  // Range check SVE intrinsics that take immediate values.
  llvm::SmallVector<std::tuple<int, int, int>, 3> ImmChecks;

  switch (BuiltinID) {
  default:
    return false;
#define GET_SVE_IMMEDIATE_CHECK
#include "neverc/Foundation/arm_sve_sema_rangechecks.td.h"
#undef GET_SVE_IMMEDIATE_CHECK
  }

  return ParseSVEImmChecks(TheCall, ImmChecks);
}

bool Sema::CheckNeonBuiltinFunctionCall(const TargetInfo &TI,
                                        unsigned BuiltinID, CallExpr *TheCall) {
  if (const FunctionDecl *FD = getCurFunctionDecl()) {

    switch (BuiltinID) {
    default:
      break;
#define GET_NEON_BUILTINS
#define TARGET_BUILTIN(id, ...) case NEON::BI##id:
#define BUILTIN(id, ...) case NEON::BI##id:
#include "neverc/Foundation/arm_neon.td.h"
      checkArmStreamingBuiltin(*this, TheCall, FD, ArmNonStreaming);
      break;
#undef TARGET_BUILTIN
#undef BUILTIN
#undef GET_NEON_BUILTINS
    }
  }

  llvm::APSInt Result;
  uint64_t mask = 0;
  unsigned TV = 0;
  int PtrArgNum = -1;
  bool HasConstPtr = false;
  switch (BuiltinID) {
#define GET_NEON_OVERLOAD_CHECK
#include "neverc/Foundation/arm_fp16.td.h"
#undef GET_NEON_OVERLOAD_CHECK
  }

  // For NEON intrinsics which are overloaded on vector element type, validate
  // the immediate which specifies which variant to emit.
  unsigned ImmArg = TheCall->getNumArgs() - 1;
  if (mask) {
    if (SemaBuiltinConstantArg(TheCall, ImmArg, Result))
      return true;

    TV = Result.getLimitedValue(64);
    if ((TV > 63) || (mask & (1ULL << TV)) == 0)
      return Diag(TheCall->getBeginLoc(), diag::err_invalid_neon_type_code)
             << TheCall->getArg(ImmArg)->getSourceRange();
  }

  if (PtrArgNum >= 0) {
    Expr *Arg = TheCall->getArg(PtrArgNum);
    if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Arg))
      Arg = ICE->getSubExpr();
    ExprResult RHS = DefaultFunctionArrayLvalueConversion(Arg);
    QualType RHSTy = RHS.get()->getType();

    llvm::Triple::ArchType Arch = TI.getTriple().getArch();
    bool IsPolyUnsigned = Arch == llvm::Triple::aarch64;
    bool IsInt64Long = TI.getInt64Type() == TargetInfo::SignedLong;
    QualType EltTy =
        getNeonEltType(NeonTypeFlags(TV), Context, IsPolyUnsigned, IsInt64Long);
    if (HasConstPtr)
      EltTy = EltTy.withConst();
    QualType LHSTy = Context.getPointerType(EltTy);
    AssignConvertType ConvTy;
    ConvTy = CheckSingleAssignmentConstraints(LHSTy, RHS);
    if (RHS.isInvalid())
      return true;
    if (DiagnoseAssignmentResult(ConvTy, Arg->getBeginLoc(), LHSTy, RHSTy,
                                 RHS.get(), AA_Assigning))
      return true;
  }

  // For NEON intrinsics which take an immediate value as part of the
  // instruction, range check them here.
  unsigned i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default:
    return false;
#define GET_NEON_IMMEDIATE_CHECK
#undef GET_NEON_IMMEDIATE_CHECK
  }

  return SemaBuiltinConstantArgRange(TheCall, i, l, u + l);
}

bool Sema::CheckARMBuiltinExclusiveCall(unsigned BuiltinID, CallExpr *TheCall,
                                        unsigned MaxWidth) {
  assert((BuiltinID == AArch64::BI__builtin_arm_ldrex ||
          BuiltinID == AArch64::BI__builtin_arm_ldaex ||
          BuiltinID == AArch64::BI__builtin_arm_strex ||
          BuiltinID == AArch64::BI__builtin_arm_stlex) &&
         "unexpected AArch64 builtin");
  bool IsLdrex = BuiltinID == AArch64::BI__builtin_arm_ldrex ||
                 BuiltinID == AArch64::BI__builtin_arm_ldaex;

  DeclRefExpr *DRE =
      cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());

  // Ensure that we have the proper number of arguments.
  if (checkArgCount(*this, TheCall, IsLdrex ? 1 : 2))
    return true;

  // Inspect the pointer argument of the atomic builtin.  This should always be
  // a pointer type, whose element is an integral scalar or pointer type.
  // Because it is a pointer type, we don't have to worry about any implicit
  // casts here.
  Expr *PointerArg = TheCall->getArg(IsLdrex ? 0 : 1);
  ExprResult PointerArgRes = DefaultFunctionArrayLvalueConversion(PointerArg);
  if (PointerArgRes.isInvalid())
    return true;
  PointerArg = PointerArgRes.get();

  const PointerType *pointerType = PointerArg->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return true;
  }

  // ldrex takes a "const volatile T*" and strex takes a "volatile T*". Our next
  // task is to insert the appropriate casts into the AST. First work out just
  // what the appropriate type is.
  QualType ValType = pointerType->getPointeeType();
  QualType AddrType = ValType.getUnqualifiedType().withVolatile();
  if (IsLdrex)
    AddrType.addConst();

  // Issue a warning if the cast is dodgy.
  CastKind CastNeeded = CK_NoOp;
  if (!AddrType.isAtLeastAsQualifiedAs(ValType)) {
    CastNeeded = CK_BitCast;
    Diag(DRE->getBeginLoc(), diag::ext_typecheck_convert_discards_qualifiers)
        << PointerArg->getType() << Context.getPointerType(AddrType)
        << AA_Passing << PointerArg->getSourceRange();
  }

  // Finally, do the cast and replace the argument with the corrected version.
  AddrType = Context.getPointerType(AddrType);
  PointerArgRes = ImpCastExprToType(PointerArg, AddrType, CastNeeded);
  if (PointerArgRes.isInvalid())
    return true;
  PointerArg = PointerArgRes.get();

  TheCall->setArg(IsLdrex ? 0 : 1, PointerArg);

  // In general, we allow ints, floats and pointers to be loaded and stored.
  if (!ValType->isIntegerType() && !ValType->isAnyPointerType() &&
      !ValType->isFloatingType()) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer_intfltptr)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return true;
  }

  // But AArch64 doesn't have instructions to deal with 128-bit versions.
  if (Context.getTypeSize(ValType) > MaxWidth) {
    assert(MaxWidth == 64 && "Diagnostic unexpectedly inaccurate");
    Diag(DRE->getBeginLoc(), diag::err_atomic_exclusive_builtin_pointer_size)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return true;
  }

  if (IsLdrex) {
    TheCall->setType(ValType);
    return false;
  }
  ExprResult ValArg = TheCall->getArg(0);
  InitializedEntity Entity = InitializedEntity::InitializeParameter(
      Context, ValType, /*consume*/ false);
  ValArg = PerformCopyInitialization(Entity, SourceLocation(), ValArg);
  if (ValArg.isInvalid())
    return true;
  TheCall->setArg(0, ValArg.get());

  // __builtin_arm_strex always returns an int. It's marked as such in the .def,
  // but the custom checker bypasses all default analysis.
  TheCall->setType(Context.IntTy);
  return false;
}

bool Sema::CheckAArch64BuiltinFunctionCall(const TargetInfo &TI,
                                           unsigned BuiltinID,
                                           CallExpr *TheCall) {
  if (BuiltinID == AArch64::BI__builtin_arm_ldrex ||
      BuiltinID == AArch64::BI__builtin_arm_ldaex ||
      BuiltinID == AArch64::BI__builtin_arm_strex ||
      BuiltinID == AArch64::BI__builtin_arm_stlex) {
    return CheckARMBuiltinExclusiveCall(BuiltinID, TheCall, 128);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_prefetch) {
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 1) ||
           SemaBuiltinConstantArgRange(TheCall, 2, 0, 3) ||
           SemaBuiltinConstantArgRange(TheCall, 3, 0, 1) ||
           SemaBuiltinConstantArgRange(TheCall, 4, 0, 1);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_rsr64 ||
      BuiltinID == AArch64::BI__builtin_arm_wsr64 ||
      BuiltinID == AArch64::BI__builtin_arm_rsr128 ||
      BuiltinID == AArch64::BI__builtin_arm_wsr128)
    return SemaBuiltinARMSpecialReg(BuiltinID, TheCall, 0, 5, true);

  // Memory Tagging Extensions (MTE) Intrinsics
  if (BuiltinID == AArch64::BI__builtin_arm_irg ||
      BuiltinID == AArch64::BI__builtin_arm_addg ||
      BuiltinID == AArch64::BI__builtin_arm_gmi ||
      BuiltinID == AArch64::BI__builtin_arm_ldg ||
      BuiltinID == AArch64::BI__builtin_arm_stg ||
      BuiltinID == AArch64::BI__builtin_arm_subp) {
    return SemaBuiltinARMMemoryTaggingCall(BuiltinID, TheCall);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_rsr ||
      BuiltinID == AArch64::BI__builtin_arm_rsrp ||
      BuiltinID == AArch64::BI__builtin_arm_wsr ||
      BuiltinID == AArch64::BI__builtin_arm_wsrp)
    return SemaBuiltinARMSpecialReg(BuiltinID, TheCall, 0, 5, true);

  // Only check the valid encoding range. Any constant in this range would be
  // converted to a register of the form S1_2_C3_C4_5. Let the hardware throw
  // an exception for incorrect registers. This matches MSVC behavior.
  if (BuiltinID == AArch64::BI_ReadStatusReg ||
      BuiltinID == AArch64::BI_WriteStatusReg)
    return SemaBuiltinConstantArgRange(TheCall, 0, 0, 0x7fff);

  if (BuiltinID == AArch64::BI__getReg)
    return SemaBuiltinConstantArgRange(TheCall, 0, 0, 31);

  if (BuiltinID == AArch64::BI__break)
    return SemaBuiltinConstantArgRange(TheCall, 0, 0, 0xffff);

  if (CheckNeonBuiltinFunctionCall(TI, BuiltinID, TheCall))
    return true;

  if (CheckSVEBuiltinFunctionCall(BuiltinID, TheCall))
    return true;

  if (CheckSMEBuiltinFunctionCall(BuiltinID, TheCall))
    return true;

  // For intrinsics which take an immediate value as part of the instruction,
  // range check them here.
  unsigned i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default:
    return false;
  case AArch64::BI__builtin_arm_dmb:
  case AArch64::BI__builtin_arm_dsb:
  case AArch64::BI__builtin_arm_isb:
    l = 0;
    u = 15;
    break;
  case AArch64::BI__builtin_arm_tcancel:
    l = 0;
    u = 65535;
    break;
  }

  return SemaBuiltinConstantArgRange(TheCall, i, l, u + l);
}

bool semaBuiltinCpuSupports(Sema &S, const TargetInfo &TI, CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(0);

  if (!isa<StringLiteral>(Arg->IgnoreParenImpCasts()))
    return S.Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
           << Arg->getSourceRange();

  llvm::StringRef Feature =
      cast<StringLiteral>(Arg->IgnoreParenImpCasts())->getString();
  if (!TI.validateCpuSupports(Feature))
    return S.Diag(TheCall->getBeginLoc(), diag::err_invalid_cpu_supports)
           << Arg->getSourceRange();
  return false;
}

bool semaBuiltinCpuIs(Sema &S, const TargetInfo &TI, CallExpr *TheCall) {
  Expr *Arg = TheCall->getArg(0);

  if (!isa<StringLiteral>(Arg->IgnoreParenImpCasts()))
    return S.Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
           << Arg->getSourceRange();

  llvm::StringRef Feature =
      cast<StringLiteral>(Arg->IgnoreParenImpCasts())->getString();
  if (!TI.validateCpuIs(Feature))
    return S.Diag(TheCall->getBeginLoc(), diag::err_invalid_cpu_is)
           << Arg->getSourceRange();
  return false;
}


// ===----------------------------------------------------------------------===
// X86 builtin validation
// ===----------------------------------------------------------------------===

bool Sema::CheckX86BuiltinRoundingOrSAE(unsigned BuiltinID, CallExpr *TheCall) {
  // Indicates if this instruction has rounding control or just SAE.
  bool HasRC = false;

  unsigned ArgNum = 0;
  switch (BuiltinID) {
  default:
    return false;
  case X86::BI__builtin_ia32_vcvttsd2si32:
  case X86::BI__builtin_ia32_vcvttsd2si64:
  case X86::BI__builtin_ia32_vcvttsd2usi32:
  case X86::BI__builtin_ia32_vcvttsd2usi64:
  case X86::BI__builtin_ia32_vcvttss2si32:
  case X86::BI__builtin_ia32_vcvttss2si64:
  case X86::BI__builtin_ia32_vcvttss2usi32:
  case X86::BI__builtin_ia32_vcvttss2usi64:
  case X86::BI__builtin_ia32_vcvttsh2si32:
  case X86::BI__builtin_ia32_vcvttsh2si64:
  case X86::BI__builtin_ia32_vcvttsh2usi32:
  case X86::BI__builtin_ia32_vcvttsh2usi64:
    ArgNum = 1;
    break;
  case X86::BI__builtin_ia32_maxpd512:
  case X86::BI__builtin_ia32_maxps512:
  case X86::BI__builtin_ia32_minpd512:
  case X86::BI__builtin_ia32_minps512:
  case X86::BI__builtin_ia32_maxph512:
  case X86::BI__builtin_ia32_minph512:
    ArgNum = 2;
    break;
  case X86::BI__builtin_ia32_vcvtph2pd512_mask:
  case X86::BI__builtin_ia32_vcvtph2psx512_mask:
  case X86::BI__builtin_ia32_cvtps2pd512_mask:
  case X86::BI__builtin_ia32_cvttpd2dq512_mask:
  case X86::BI__builtin_ia32_cvttpd2qq512_mask:
  case X86::BI__builtin_ia32_cvttpd2udq512_mask:
  case X86::BI__builtin_ia32_cvttpd2uqq512_mask:
  case X86::BI__builtin_ia32_cvttps2dq512_mask:
  case X86::BI__builtin_ia32_cvttps2qq512_mask:
  case X86::BI__builtin_ia32_cvttps2udq512_mask:
  case X86::BI__builtin_ia32_cvttps2uqq512_mask:
  case X86::BI__builtin_ia32_vcvttph2w512_mask:
  case X86::BI__builtin_ia32_vcvttph2uw512_mask:
  case X86::BI__builtin_ia32_vcvttph2dq512_mask:
  case X86::BI__builtin_ia32_vcvttph2udq512_mask:
  case X86::BI__builtin_ia32_vcvttph2qq512_mask:
  case X86::BI__builtin_ia32_vcvttph2uqq512_mask:
  case X86::BI__builtin_ia32_exp2pd_mask:
  case X86::BI__builtin_ia32_exp2ps_mask:
  case X86::BI__builtin_ia32_getexppd512_mask:
  case X86::BI__builtin_ia32_getexpps512_mask:
  case X86::BI__builtin_ia32_getexpph512_mask:
  case X86::BI__builtin_ia32_rcp28pd_mask:
  case X86::BI__builtin_ia32_rcp28ps_mask:
  case X86::BI__builtin_ia32_rsqrt28pd_mask:
  case X86::BI__builtin_ia32_rsqrt28ps_mask:
  case X86::BI__builtin_ia32_vcomisd:
  case X86::BI__builtin_ia32_vcomiss:
  case X86::BI__builtin_ia32_vcomish:
  case X86::BI__builtin_ia32_vcvtph2ps512_mask:
    ArgNum = 3;
    break;
  case X86::BI__builtin_ia32_cmppd512_mask:
  case X86::BI__builtin_ia32_cmpps512_mask:
  case X86::BI__builtin_ia32_cmpsd_mask:
  case X86::BI__builtin_ia32_cmpss_mask:
  case X86::BI__builtin_ia32_cmpsh_mask:
  case X86::BI__builtin_ia32_vcvtsh2sd_round_mask:
  case X86::BI__builtin_ia32_vcvtsh2ss_round_mask:
  case X86::BI__builtin_ia32_cvtss2sd_round_mask:
  case X86::BI__builtin_ia32_getexpsd128_round_mask:
  case X86::BI__builtin_ia32_getexpss128_round_mask:
  case X86::BI__builtin_ia32_getexpsh128_round_mask:
  case X86::BI__builtin_ia32_getmantpd512_mask:
  case X86::BI__builtin_ia32_getmantps512_mask:
  case X86::BI__builtin_ia32_getmantph512_mask:
  case X86::BI__builtin_ia32_maxsd_round_mask:
  case X86::BI__builtin_ia32_maxss_round_mask:
  case X86::BI__builtin_ia32_maxsh_round_mask:
  case X86::BI__builtin_ia32_minsd_round_mask:
  case X86::BI__builtin_ia32_minss_round_mask:
  case X86::BI__builtin_ia32_minsh_round_mask:
  case X86::BI__builtin_ia32_rcp28sd_round_mask:
  case X86::BI__builtin_ia32_rcp28ss_round_mask:
  case X86::BI__builtin_ia32_reducepd512_mask:
  case X86::BI__builtin_ia32_reduceps512_mask:
  case X86::BI__builtin_ia32_reduceph512_mask:
  case X86::BI__builtin_ia32_rndscalepd_mask:
  case X86::BI__builtin_ia32_rndscaleps_mask:
  case X86::BI__builtin_ia32_rndscaleph_mask:
  case X86::BI__builtin_ia32_rsqrt28sd_round_mask:
  case X86::BI__builtin_ia32_rsqrt28ss_round_mask:
    ArgNum = 4;
    break;
  case X86::BI__builtin_ia32_fixupimmpd512_mask:
  case X86::BI__builtin_ia32_fixupimmpd512_maskz:
  case X86::BI__builtin_ia32_fixupimmps512_mask:
  case X86::BI__builtin_ia32_fixupimmps512_maskz:
  case X86::BI__builtin_ia32_fixupimmsd_mask:
  case X86::BI__builtin_ia32_fixupimmsd_maskz:
  case X86::BI__builtin_ia32_fixupimmss_mask:
  case X86::BI__builtin_ia32_fixupimmss_maskz:
  case X86::BI__builtin_ia32_getmantsd_round_mask:
  case X86::BI__builtin_ia32_getmantss_round_mask:
  case X86::BI__builtin_ia32_getmantsh_round_mask:
  case X86::BI__builtin_ia32_rangepd512_mask:
  case X86::BI__builtin_ia32_rangeps512_mask:
  case X86::BI__builtin_ia32_rangesd128_round_mask:
  case X86::BI__builtin_ia32_rangess128_round_mask:
  case X86::BI__builtin_ia32_reducesd_mask:
  case X86::BI__builtin_ia32_reducess_mask:
  case X86::BI__builtin_ia32_reducesh_mask:
  case X86::BI__builtin_ia32_rndscalesd_round_mask:
  case X86::BI__builtin_ia32_rndscaless_round_mask:
  case X86::BI__builtin_ia32_rndscalesh_round_mask:
    ArgNum = 5;
    break;
  case X86::BI__builtin_ia32_vcvtsd2si64:
  case X86::BI__builtin_ia32_vcvtsd2si32:
  case X86::BI__builtin_ia32_vcvtsd2usi32:
  case X86::BI__builtin_ia32_vcvtsd2usi64:
  case X86::BI__builtin_ia32_vcvtss2si32:
  case X86::BI__builtin_ia32_vcvtss2si64:
  case X86::BI__builtin_ia32_vcvtss2usi32:
  case X86::BI__builtin_ia32_vcvtss2usi64:
  case X86::BI__builtin_ia32_vcvtsh2si32:
  case X86::BI__builtin_ia32_vcvtsh2si64:
  case X86::BI__builtin_ia32_vcvtsh2usi32:
  case X86::BI__builtin_ia32_vcvtsh2usi64:
  case X86::BI__builtin_ia32_sqrtpd512:
  case X86::BI__builtin_ia32_sqrtps512:
  case X86::BI__builtin_ia32_sqrtph512:
    ArgNum = 1;
    HasRC = true;
    break;
  case X86::BI__builtin_ia32_addph512:
  case X86::BI__builtin_ia32_divph512:
  case X86::BI__builtin_ia32_mulph512:
  case X86::BI__builtin_ia32_subph512:
  case X86::BI__builtin_ia32_addpd512:
  case X86::BI__builtin_ia32_addps512:
  case X86::BI__builtin_ia32_divpd512:
  case X86::BI__builtin_ia32_divps512:
  case X86::BI__builtin_ia32_mulpd512:
  case X86::BI__builtin_ia32_mulps512:
  case X86::BI__builtin_ia32_subpd512:
  case X86::BI__builtin_ia32_subps512:
  case X86::BI__builtin_ia32_cvtsi2sd64:
  case X86::BI__builtin_ia32_cvtsi2ss32:
  case X86::BI__builtin_ia32_cvtsi2ss64:
  case X86::BI__builtin_ia32_cvtusi2sd64:
  case X86::BI__builtin_ia32_cvtusi2ss32:
  case X86::BI__builtin_ia32_cvtusi2ss64:
  case X86::BI__builtin_ia32_vcvtusi2sh:
  case X86::BI__builtin_ia32_vcvtusi642sh:
  case X86::BI__builtin_ia32_vcvtsi2sh:
  case X86::BI__builtin_ia32_vcvtsi642sh:
    ArgNum = 2;
    HasRC = true;
    break;
  case X86::BI__builtin_ia32_cvtdq2ps512_mask:
  case X86::BI__builtin_ia32_cvtudq2ps512_mask:
  case X86::BI__builtin_ia32_vcvtpd2ph512_mask:
  case X86::BI__builtin_ia32_vcvtps2phx512_mask:
  case X86::BI__builtin_ia32_cvtpd2ps512_mask:
  case X86::BI__builtin_ia32_cvtpd2dq512_mask:
  case X86::BI__builtin_ia32_cvtpd2qq512_mask:
  case X86::BI__builtin_ia32_cvtpd2udq512_mask:
  case X86::BI__builtin_ia32_cvtpd2uqq512_mask:
  case X86::BI__builtin_ia32_cvtps2dq512_mask:
  case X86::BI__builtin_ia32_cvtps2qq512_mask:
  case X86::BI__builtin_ia32_cvtps2udq512_mask:
  case X86::BI__builtin_ia32_cvtps2uqq512_mask:
  case X86::BI__builtin_ia32_cvtqq2pd512_mask:
  case X86::BI__builtin_ia32_cvtqq2ps512_mask:
  case X86::BI__builtin_ia32_cvtuqq2pd512_mask:
  case X86::BI__builtin_ia32_cvtuqq2ps512_mask:
  case X86::BI__builtin_ia32_vcvtdq2ph512_mask:
  case X86::BI__builtin_ia32_vcvtudq2ph512_mask:
  case X86::BI__builtin_ia32_vcvtw2ph512_mask:
  case X86::BI__builtin_ia32_vcvtuw2ph512_mask:
  case X86::BI__builtin_ia32_vcvtph2w512_mask:
  case X86::BI__builtin_ia32_vcvtph2uw512_mask:
  case X86::BI__builtin_ia32_vcvtph2dq512_mask:
  case X86::BI__builtin_ia32_vcvtph2udq512_mask:
  case X86::BI__builtin_ia32_vcvtph2qq512_mask:
  case X86::BI__builtin_ia32_vcvtph2uqq512_mask:
  case X86::BI__builtin_ia32_vcvtqq2ph512_mask:
  case X86::BI__builtin_ia32_vcvtuqq2ph512_mask:
    ArgNum = 3;
    HasRC = true;
    break;
  case X86::BI__builtin_ia32_addsh_round_mask:
  case X86::BI__builtin_ia32_addss_round_mask:
  case X86::BI__builtin_ia32_addsd_round_mask:
  case X86::BI__builtin_ia32_divsh_round_mask:
  case X86::BI__builtin_ia32_divss_round_mask:
  case X86::BI__builtin_ia32_divsd_round_mask:
  case X86::BI__builtin_ia32_mulsh_round_mask:
  case X86::BI__builtin_ia32_mulss_round_mask:
  case X86::BI__builtin_ia32_mulsd_round_mask:
  case X86::BI__builtin_ia32_subsh_round_mask:
  case X86::BI__builtin_ia32_subss_round_mask:
  case X86::BI__builtin_ia32_subsd_round_mask:
  case X86::BI__builtin_ia32_scalefph512_mask:
  case X86::BI__builtin_ia32_scalefpd512_mask:
  case X86::BI__builtin_ia32_scalefps512_mask:
  case X86::BI__builtin_ia32_scalefsd_round_mask:
  case X86::BI__builtin_ia32_scalefss_round_mask:
  case X86::BI__builtin_ia32_scalefsh_round_mask:
  case X86::BI__builtin_ia32_cvtsd2ss_round_mask:
  case X86::BI__builtin_ia32_vcvtss2sh_round_mask:
  case X86::BI__builtin_ia32_vcvtsd2sh_round_mask:
  case X86::BI__builtin_ia32_sqrtsd_round_mask:
  case X86::BI__builtin_ia32_sqrtss_round_mask:
  case X86::BI__builtin_ia32_sqrtsh_round_mask:
  case X86::BI__builtin_ia32_vfmaddsd3_mask:
  case X86::BI__builtin_ia32_vfmaddsd3_maskz:
  case X86::BI__builtin_ia32_vfmaddsd3_mask3:
  case X86::BI__builtin_ia32_vfmaddss3_mask:
  case X86::BI__builtin_ia32_vfmaddss3_maskz:
  case X86::BI__builtin_ia32_vfmaddss3_mask3:
  case X86::BI__builtin_ia32_vfmaddsh3_mask:
  case X86::BI__builtin_ia32_vfmaddsh3_maskz:
  case X86::BI__builtin_ia32_vfmaddsh3_mask3:
  case X86::BI__builtin_ia32_vfmaddpd512_mask:
  case X86::BI__builtin_ia32_vfmaddpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubpd512_mask3:
  case X86::BI__builtin_ia32_vfmaddps512_mask:
  case X86::BI__builtin_ia32_vfmaddps512_maskz:
  case X86::BI__builtin_ia32_vfmaddps512_mask3:
  case X86::BI__builtin_ia32_vfmsubps512_mask3:
  case X86::BI__builtin_ia32_vfmaddph512_mask:
  case X86::BI__builtin_ia32_vfmaddph512_maskz:
  case X86::BI__builtin_ia32_vfmaddph512_mask3:
  case X86::BI__builtin_ia32_vfmsubph512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask:
  case X86::BI__builtin_ia32_vfmaddsubpd512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubpd512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddpd512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask:
  case X86::BI__builtin_ia32_vfmaddsubps512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubps512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddps512_mask3:
  case X86::BI__builtin_ia32_vfmaddsubph512_mask:
  case X86::BI__builtin_ia32_vfmaddsubph512_maskz:
  case X86::BI__builtin_ia32_vfmaddsubph512_mask3:
  case X86::BI__builtin_ia32_vfmsubaddph512_mask3:
  case X86::BI__builtin_ia32_vfmaddcsh_mask:
  case X86::BI__builtin_ia32_vfmaddcsh_round_mask:
  case X86::BI__builtin_ia32_vfmaddcsh_round_mask3:
  case X86::BI__builtin_ia32_vfmaddcph512_mask:
  case X86::BI__builtin_ia32_vfmaddcph512_maskz:
  case X86::BI__builtin_ia32_vfmaddcph512_mask3:
  case X86::BI__builtin_ia32_vfcmaddcsh_mask:
  case X86::BI__builtin_ia32_vfcmaddcsh_round_mask:
  case X86::BI__builtin_ia32_vfcmaddcsh_round_mask3:
  case X86::BI__builtin_ia32_vfcmaddcph512_mask:
  case X86::BI__builtin_ia32_vfcmaddcph512_maskz:
  case X86::BI__builtin_ia32_vfcmaddcph512_mask3:
  case X86::BI__builtin_ia32_vfmulcsh_mask:
  case X86::BI__builtin_ia32_vfmulcph512_mask:
  case X86::BI__builtin_ia32_vfcmulcsh_mask:
  case X86::BI__builtin_ia32_vfcmulcph512_mask:
    ArgNum = 4;
    HasRC = true;
    break;
  }

  llvm::APSInt Result;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  // Make sure rounding mode is either ROUND_CUR_DIRECTION or ROUND_NO_EXC bit
  // is set. If the intrinsic has rounding control(bits 1:0), make sure its only
  // combined with ROUND_NO_EXC. If the intrinsic does not have rounding
  // control, allow ROUND_NO_EXC and ROUND_CUR_DIRECTION together.
  if (Result == 4 /*ROUND_CUR_DIRECTION*/ || Result == 8 /*ROUND_NO_EXC*/ ||
      (!HasRC && Result == 12 /*ROUND_CUR_DIRECTION|ROUND_NO_EXC*/) ||
      (HasRC && Result.getZExtValue() >= 8 && Result.getZExtValue() <= 11))
    return false;

  return Diag(TheCall->getBeginLoc(), diag::err_x86_builtin_invalid_rounding)
         << Arg->getSourceRange();
}

// Check if the gather/scatter scale is legal.
bool Sema::CheckX86BuiltinGatherScatterScale(unsigned BuiltinID,
                                             CallExpr *TheCall) {
  unsigned ArgNum = 0;
  switch (BuiltinID) {
  default:
    return false;
  case X86::BI__builtin_ia32_gatherpfdpd:
  case X86::BI__builtin_ia32_gatherpfdps:
  case X86::BI__builtin_ia32_gatherpfqpd:
  case X86::BI__builtin_ia32_gatherpfqps:
  case X86::BI__builtin_ia32_scatterpfdpd:
  case X86::BI__builtin_ia32_scatterpfdps:
  case X86::BI__builtin_ia32_scatterpfqpd:
  case X86::BI__builtin_ia32_scatterpfqps:
    ArgNum = 3;
    break;
  case X86::BI__builtin_ia32_gatherd_pd:
  case X86::BI__builtin_ia32_gatherd_pd256:
  case X86::BI__builtin_ia32_gatherq_pd:
  case X86::BI__builtin_ia32_gatherq_pd256:
  case X86::BI__builtin_ia32_gatherd_ps:
  case X86::BI__builtin_ia32_gatherd_ps256:
  case X86::BI__builtin_ia32_gatherq_ps:
  case X86::BI__builtin_ia32_gatherq_ps256:
  case X86::BI__builtin_ia32_gatherd_q:
  case X86::BI__builtin_ia32_gatherd_q256:
  case X86::BI__builtin_ia32_gatherq_q:
  case X86::BI__builtin_ia32_gatherq_q256:
  case X86::BI__builtin_ia32_gatherd_d:
  case X86::BI__builtin_ia32_gatherd_d256:
  case X86::BI__builtin_ia32_gatherq_d:
  case X86::BI__builtin_ia32_gatherq_d256:
  case X86::BI__builtin_ia32_gather3div2df:
  case X86::BI__builtin_ia32_gather3div2di:
  case X86::BI__builtin_ia32_gather3div4df:
  case X86::BI__builtin_ia32_gather3div4di:
  case X86::BI__builtin_ia32_gather3div4sf:
  case X86::BI__builtin_ia32_gather3div4si:
  case X86::BI__builtin_ia32_gather3div8sf:
  case X86::BI__builtin_ia32_gather3div8si:
  case X86::BI__builtin_ia32_gather3siv2df:
  case X86::BI__builtin_ia32_gather3siv2di:
  case X86::BI__builtin_ia32_gather3siv4df:
  case X86::BI__builtin_ia32_gather3siv4di:
  case X86::BI__builtin_ia32_gather3siv4sf:
  case X86::BI__builtin_ia32_gather3siv4si:
  case X86::BI__builtin_ia32_gather3siv8sf:
  case X86::BI__builtin_ia32_gather3siv8si:
  case X86::BI__builtin_ia32_gathersiv8df:
  case X86::BI__builtin_ia32_gathersiv16sf:
  case X86::BI__builtin_ia32_gatherdiv8df:
  case X86::BI__builtin_ia32_gatherdiv16sf:
  case X86::BI__builtin_ia32_gathersiv8di:
  case X86::BI__builtin_ia32_gathersiv16si:
  case X86::BI__builtin_ia32_gatherdiv8di:
  case X86::BI__builtin_ia32_gatherdiv16si:
  case X86::BI__builtin_ia32_scatterdiv2df:
  case X86::BI__builtin_ia32_scatterdiv2di:
  case X86::BI__builtin_ia32_scatterdiv4df:
  case X86::BI__builtin_ia32_scatterdiv4di:
  case X86::BI__builtin_ia32_scatterdiv4sf:
  case X86::BI__builtin_ia32_scatterdiv4si:
  case X86::BI__builtin_ia32_scatterdiv8sf:
  case X86::BI__builtin_ia32_scatterdiv8si:
  case X86::BI__builtin_ia32_scattersiv2df:
  case X86::BI__builtin_ia32_scattersiv2di:
  case X86::BI__builtin_ia32_scattersiv4df:
  case X86::BI__builtin_ia32_scattersiv4di:
  case X86::BI__builtin_ia32_scattersiv4sf:
  case X86::BI__builtin_ia32_scattersiv4si:
  case X86::BI__builtin_ia32_scattersiv8sf:
  case X86::BI__builtin_ia32_scattersiv8si:
  case X86::BI__builtin_ia32_scattersiv8df:
  case X86::BI__builtin_ia32_scattersiv16sf:
  case X86::BI__builtin_ia32_scatterdiv8df:
  case X86::BI__builtin_ia32_scatterdiv16sf:
  case X86::BI__builtin_ia32_scattersiv8di:
  case X86::BI__builtin_ia32_scattersiv16si:
  case X86::BI__builtin_ia32_scatterdiv8di:
  case X86::BI__builtin_ia32_scatterdiv16si:
    ArgNum = 4;
    break;
  }

  llvm::APSInt Result;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
    return true;

  if (Result == 1 || Result == 2 || Result == 4 || Result == 8)
    return false;

  return Diag(TheCall->getBeginLoc(), diag::err_x86_builtin_invalid_scale)
         << Arg->getSourceRange();
}

enum { TileRegLow = 0, TileRegHigh = 7 };

bool Sema::CheckX86BuiltinTileArgumentsRange(CallExpr *TheCall,
                                             llvm::ArrayRef<int> ArgNums) {
  for (int ArgNum : ArgNums) {
    if (SemaBuiltinConstantArgRange(TheCall, ArgNum, TileRegLow, TileRegHigh))
      return true;
  }
  return false;
}

bool Sema::CheckX86BuiltinTileDuplicate(CallExpr *TheCall,
                                        llvm::ArrayRef<int> ArgNums) {
  // Because the max number of tile register is TileRegHigh + 1, so here we use
  // each bit to represent the usage of them in bitset.
  std::bitset<TileRegHigh + 1> ArgValues;
  for (int ArgNum : ArgNums) {
    llvm::APSInt Result;
    if (SemaBuiltinConstantArg(TheCall, ArgNum, Result))
      return true;
    int ArgExtValue = Result.getExtValue();
    assert((ArgExtValue >= TileRegLow && ArgExtValue <= TileRegHigh) &&
           "Incorrect tile register num.");
    if (ArgValues.test(ArgExtValue))
      return Diag(TheCall->getBeginLoc(),
                  diag::err_x86_builtin_tile_arg_duplicate)
             << TheCall->getArg(ArgNum)->getSourceRange();
    ArgValues.set(ArgExtValue);
  }
  return false;
}

bool Sema::CheckX86BuiltinTileRangeAndDuplicate(CallExpr *TheCall,
                                                llvm::ArrayRef<int> ArgNums) {
  return CheckX86BuiltinTileArgumentsRange(TheCall, ArgNums) ||
         CheckX86BuiltinTileDuplicate(TheCall, ArgNums);
}

bool Sema::CheckX86BuiltinTileArguments(unsigned BuiltinID, CallExpr *TheCall) {
  switch (BuiltinID) {
  default:
    return false;
  case X86::BI__builtin_ia32_tileloadd64:
  case X86::BI__builtin_ia32_tileloaddt164:
  case X86::BI__builtin_ia32_tilestored64:
  case X86::BI__builtin_ia32_tilezero:
    return CheckX86BuiltinTileArgumentsRange(TheCall, 0);
  case X86::BI__builtin_ia32_tdpbssd:
  case X86::BI__builtin_ia32_tdpbsud:
  case X86::BI__builtin_ia32_tdpbusd:
  case X86::BI__builtin_ia32_tdpbuud:
  case X86::BI__builtin_ia32_tdpbf16ps:
  case X86::BI__builtin_ia32_tdpfp16ps:
  case X86::BI__builtin_ia32_tcmmimfp16ps:
  case X86::BI__builtin_ia32_tcmmrlfp16ps:
    return CheckX86BuiltinTileRangeAndDuplicate(TheCall, {0, 1, 2});
  }
}
bool Sema::CheckX86BuiltinFunctionCall(const TargetInfo &TI, unsigned BuiltinID,
                                       CallExpr *TheCall) {
  if (BuiltinID == X86::BI__builtin_cpu_supports)
    return semaBuiltinCpuSupports(*this, TI, TheCall);

  if (BuiltinID == X86::BI__builtin_cpu_is)
    return semaBuiltinCpuIs(*this, TI, TheCall);

  // If the intrinsic has rounding or SAE make sure its valid.
  if (CheckX86BuiltinRoundingOrSAE(BuiltinID, TheCall))
    return true;

  // If the intrinsic has a gather/scatter scale immediate make sure its valid.
  if (CheckX86BuiltinGatherScatterScale(BuiltinID, TheCall))
    return true;

  // If the intrinsic has a tile arguments, make sure they are valid.
  if (CheckX86BuiltinTileArguments(BuiltinID, TheCall))
    return true;

  // For intrinsics which take an immediate value as part of the instruction,
  // range check them here.
  int i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default:
    return false;
  case X86::BI__builtin_ia32_vec_ext_v2si:
  case X86::BI__builtin_ia32_vec_ext_v2di:
  case X86::BI__builtin_ia32_vextractf128_pd256:
  case X86::BI__builtin_ia32_vextractf128_ps256:
  case X86::BI__builtin_ia32_vextractf128_si256:
  case X86::BI__builtin_ia32_extract128i256:
  case X86::BI__builtin_ia32_extractf64x4_mask:
  case X86::BI__builtin_ia32_extracti64x4_mask:
  case X86::BI__builtin_ia32_extractf32x8_mask:
  case X86::BI__builtin_ia32_extracti32x8_mask:
  case X86::BI__builtin_ia32_extractf64x2_256_mask:
  case X86::BI__builtin_ia32_extracti64x2_256_mask:
  case X86::BI__builtin_ia32_extractf32x4_256_mask:
  case X86::BI__builtin_ia32_extracti32x4_256_mask:
    i = 1;
    l = 0;
    u = 1;
    break;
  case X86::BI__builtin_ia32_vec_set_v2di:
  case X86::BI__builtin_ia32_vinsertf128_pd256:
  case X86::BI__builtin_ia32_vinsertf128_ps256:
  case X86::BI__builtin_ia32_vinsertf128_si256:
  case X86::BI__builtin_ia32_insert128i256:
  case X86::BI__builtin_ia32_insertf32x8:
  case X86::BI__builtin_ia32_inserti32x8:
  case X86::BI__builtin_ia32_insertf64x4:
  case X86::BI__builtin_ia32_inserti64x4:
  case X86::BI__builtin_ia32_insertf64x2_256:
  case X86::BI__builtin_ia32_inserti64x2_256:
  case X86::BI__builtin_ia32_insertf32x4_256:
  case X86::BI__builtin_ia32_inserti32x4_256:
    i = 2;
    l = 0;
    u = 1;
    break;
  case X86::BI__builtin_ia32_vpermilpd:
  case X86::BI__builtin_ia32_vec_ext_v4hi:
  case X86::BI__builtin_ia32_vec_ext_v4si:
  case X86::BI__builtin_ia32_vec_ext_v4sf:
  case X86::BI__builtin_ia32_vec_ext_v4di:
  case X86::BI__builtin_ia32_extractf32x4_mask:
  case X86::BI__builtin_ia32_extracti32x4_mask:
  case X86::BI__builtin_ia32_extractf64x2_512_mask:
  case X86::BI__builtin_ia32_extracti64x2_512_mask:
    i = 1;
    l = 0;
    u = 3;
    break;
  case X86::BI_mm_prefetch:
  case X86::BI__builtin_ia32_vec_ext_v8hi:
  case X86::BI__builtin_ia32_vec_ext_v8si:
    i = 1;
    l = 0;
    u = 7;
    break;
  case X86::BI__builtin_ia32_sha1rnds4:
  case X86::BI__builtin_ia32_blendpd:
  case X86::BI__builtin_ia32_shufpd:
  case X86::BI__builtin_ia32_vec_set_v4hi:
  case X86::BI__builtin_ia32_vec_set_v4si:
  case X86::BI__builtin_ia32_vec_set_v4di:
  case X86::BI__builtin_ia32_shuf_f32x4_256:
  case X86::BI__builtin_ia32_shuf_f64x2_256:
  case X86::BI__builtin_ia32_shuf_i32x4_256:
  case X86::BI__builtin_ia32_shuf_i64x2_256:
  case X86::BI__builtin_ia32_insertf64x2_512:
  case X86::BI__builtin_ia32_inserti64x2_512:
  case X86::BI__builtin_ia32_insertf32x4:
  case X86::BI__builtin_ia32_inserti32x4:
    i = 2;
    l = 0;
    u = 3;
    break;
  case X86::BI__builtin_ia32_vpermil2pd:
  case X86::BI__builtin_ia32_vpermil2pd256:
  case X86::BI__builtin_ia32_vpermil2ps:
  case X86::BI__builtin_ia32_vpermil2ps256:
    i = 3;
    l = 0;
    u = 3;
    break;
  case X86::BI__builtin_ia32_cmpb128_mask:
  case X86::BI__builtin_ia32_cmpw128_mask:
  case X86::BI__builtin_ia32_cmpd128_mask:
  case X86::BI__builtin_ia32_cmpq128_mask:
  case X86::BI__builtin_ia32_cmpb256_mask:
  case X86::BI__builtin_ia32_cmpw256_mask:
  case X86::BI__builtin_ia32_cmpd256_mask:
  case X86::BI__builtin_ia32_cmpq256_mask:
  case X86::BI__builtin_ia32_cmpb512_mask:
  case X86::BI__builtin_ia32_cmpw512_mask:
  case X86::BI__builtin_ia32_cmpd512_mask:
  case X86::BI__builtin_ia32_cmpq512_mask:
  case X86::BI__builtin_ia32_ucmpb128_mask:
  case X86::BI__builtin_ia32_ucmpw128_mask:
  case X86::BI__builtin_ia32_ucmpd128_mask:
  case X86::BI__builtin_ia32_ucmpq128_mask:
  case X86::BI__builtin_ia32_ucmpb256_mask:
  case X86::BI__builtin_ia32_ucmpw256_mask:
  case X86::BI__builtin_ia32_ucmpd256_mask:
  case X86::BI__builtin_ia32_ucmpq256_mask:
  case X86::BI__builtin_ia32_ucmpb512_mask:
  case X86::BI__builtin_ia32_ucmpw512_mask:
  case X86::BI__builtin_ia32_ucmpd512_mask:
  case X86::BI__builtin_ia32_ucmpq512_mask:
  case X86::BI__builtin_ia32_vpcomub:
  case X86::BI__builtin_ia32_vpcomuw:
  case X86::BI__builtin_ia32_vpcomud:
  case X86::BI__builtin_ia32_vpcomuq:
  case X86::BI__builtin_ia32_vpcomb:
  case X86::BI__builtin_ia32_vpcomw:
  case X86::BI__builtin_ia32_vpcomd:
  case X86::BI__builtin_ia32_vpcomq:
  case X86::BI__builtin_ia32_vec_set_v8hi:
  case X86::BI__builtin_ia32_vec_set_v8si:
    i = 2;
    l = 0;
    u = 7;
    break;
  case X86::BI__builtin_ia32_vpermilpd256:
  case X86::BI__builtin_ia32_roundps:
  case X86::BI__builtin_ia32_roundpd:
  case X86::BI__builtin_ia32_roundps256:
  case X86::BI__builtin_ia32_roundpd256:
  case X86::BI__builtin_ia32_getmantpd128_mask:
  case X86::BI__builtin_ia32_getmantpd256_mask:
  case X86::BI__builtin_ia32_getmantps128_mask:
  case X86::BI__builtin_ia32_getmantps256_mask:
  case X86::BI__builtin_ia32_getmantpd512_mask:
  case X86::BI__builtin_ia32_getmantps512_mask:
  case X86::BI__builtin_ia32_getmantph128_mask:
  case X86::BI__builtin_ia32_getmantph256_mask:
  case X86::BI__builtin_ia32_getmantph512_mask:
  case X86::BI__builtin_ia32_vec_ext_v16qi:
  case X86::BI__builtin_ia32_vec_ext_v16hi:
    i = 1;
    l = 0;
    u = 15;
    break;
  case X86::BI__builtin_ia32_pblendd128:
  case X86::BI__builtin_ia32_blendps:
  case X86::BI__builtin_ia32_blendpd256:
  case X86::BI__builtin_ia32_shufpd256:
  case X86::BI__builtin_ia32_roundss:
  case X86::BI__builtin_ia32_roundsd:
  case X86::BI__builtin_ia32_rangepd128_mask:
  case X86::BI__builtin_ia32_rangepd256_mask:
  case X86::BI__builtin_ia32_rangepd512_mask:
  case X86::BI__builtin_ia32_rangeps128_mask:
  case X86::BI__builtin_ia32_rangeps256_mask:
  case X86::BI__builtin_ia32_rangeps512_mask:
  case X86::BI__builtin_ia32_getmantsd_round_mask:
  case X86::BI__builtin_ia32_getmantss_round_mask:
  case X86::BI__builtin_ia32_getmantsh_round_mask:
  case X86::BI__builtin_ia32_vec_set_v16qi:
  case X86::BI__builtin_ia32_vec_set_v16hi:
    i = 2;
    l = 0;
    u = 15;
    break;
  case X86::BI__builtin_ia32_vec_ext_v32qi:
    i = 1;
    l = 0;
    u = 31;
    break;
  case X86::BI__builtin_ia32_cmpps:
  case X86::BI__builtin_ia32_cmpss:
  case X86::BI__builtin_ia32_cmppd:
  case X86::BI__builtin_ia32_cmpsd:
  case X86::BI__builtin_ia32_cmpps256:
  case X86::BI__builtin_ia32_cmppd256:
  case X86::BI__builtin_ia32_cmpps128_mask:
  case X86::BI__builtin_ia32_cmppd128_mask:
  case X86::BI__builtin_ia32_cmpps256_mask:
  case X86::BI__builtin_ia32_cmppd256_mask:
  case X86::BI__builtin_ia32_cmpps512_mask:
  case X86::BI__builtin_ia32_cmppd512_mask:
  case X86::BI__builtin_ia32_cmpsd_mask:
  case X86::BI__builtin_ia32_cmpss_mask:
  case X86::BI__builtin_ia32_vec_set_v32qi:
    i = 2;
    l = 0;
    u = 31;
    break;
  case X86::BI__builtin_ia32_permdf256:
  case X86::BI__builtin_ia32_permdi256:
  case X86::BI__builtin_ia32_permdf512:
  case X86::BI__builtin_ia32_permdi512:
  case X86::BI__builtin_ia32_vpermilps:
  case X86::BI__builtin_ia32_vpermilps256:
  case X86::BI__builtin_ia32_vpermilpd512:
  case X86::BI__builtin_ia32_vpermilps512:
  case X86::BI__builtin_ia32_pshufd:
  case X86::BI__builtin_ia32_pshufd256:
  case X86::BI__builtin_ia32_pshufd512:
  case X86::BI__builtin_ia32_pshufhw:
  case X86::BI__builtin_ia32_pshufhw256:
  case X86::BI__builtin_ia32_pshufhw512:
  case X86::BI__builtin_ia32_pshuflw:
  case X86::BI__builtin_ia32_pshuflw256:
  case X86::BI__builtin_ia32_pshuflw512:
  case X86::BI__builtin_ia32_vcvtps2ph:
  case X86::BI__builtin_ia32_vcvtps2ph_mask:
  case X86::BI__builtin_ia32_vcvtps2ph256:
  case X86::BI__builtin_ia32_vcvtps2ph256_mask:
  case X86::BI__builtin_ia32_vcvtps2ph512_mask:
  case X86::BI__builtin_ia32_rndscaleps_128_mask:
  case X86::BI__builtin_ia32_rndscalepd_128_mask:
  case X86::BI__builtin_ia32_rndscaleps_256_mask:
  case X86::BI__builtin_ia32_rndscalepd_256_mask:
  case X86::BI__builtin_ia32_rndscaleps_mask:
  case X86::BI__builtin_ia32_rndscalepd_mask:
  case X86::BI__builtin_ia32_rndscaleph_mask:
  case X86::BI__builtin_ia32_reducepd128_mask:
  case X86::BI__builtin_ia32_reducepd256_mask:
  case X86::BI__builtin_ia32_reducepd512_mask:
  case X86::BI__builtin_ia32_reduceps128_mask:
  case X86::BI__builtin_ia32_reduceps256_mask:
  case X86::BI__builtin_ia32_reduceps512_mask:
  case X86::BI__builtin_ia32_reduceph128_mask:
  case X86::BI__builtin_ia32_reduceph256_mask:
  case X86::BI__builtin_ia32_reduceph512_mask:
  case X86::BI__builtin_ia32_prold512:
  case X86::BI__builtin_ia32_prolq512:
  case X86::BI__builtin_ia32_prold128:
  case X86::BI__builtin_ia32_prold256:
  case X86::BI__builtin_ia32_prolq128:
  case X86::BI__builtin_ia32_prolq256:
  case X86::BI__builtin_ia32_prord512:
  case X86::BI__builtin_ia32_prorq512:
  case X86::BI__builtin_ia32_prord128:
  case X86::BI__builtin_ia32_prord256:
  case X86::BI__builtin_ia32_prorq128:
  case X86::BI__builtin_ia32_prorq256:
  case X86::BI__builtin_ia32_fpclasspd128_mask:
  case X86::BI__builtin_ia32_fpclasspd256_mask:
  case X86::BI__builtin_ia32_fpclassps128_mask:
  case X86::BI__builtin_ia32_fpclassps256_mask:
  case X86::BI__builtin_ia32_fpclassps512_mask:
  case X86::BI__builtin_ia32_fpclasspd512_mask:
  case X86::BI__builtin_ia32_fpclassph128_mask:
  case X86::BI__builtin_ia32_fpclassph256_mask:
  case X86::BI__builtin_ia32_fpclassph512_mask:
  case X86::BI__builtin_ia32_fpclasssd_mask:
  case X86::BI__builtin_ia32_fpclassss_mask:
  case X86::BI__builtin_ia32_fpclasssh_mask:
  case X86::BI__builtin_ia32_pslldqi128_byteshift:
  case X86::BI__builtin_ia32_pslldqi256_byteshift:
  case X86::BI__builtin_ia32_pslldqi512_byteshift:
  case X86::BI__builtin_ia32_psrldqi128_byteshift:
  case X86::BI__builtin_ia32_psrldqi256_byteshift:
  case X86::BI__builtin_ia32_psrldqi512_byteshift:
  case X86::BI__builtin_ia32_kshiftliqi:
  case X86::BI__builtin_ia32_kshiftlihi:
  case X86::BI__builtin_ia32_kshiftlisi:
  case X86::BI__builtin_ia32_kshiftlidi:
  case X86::BI__builtin_ia32_kshiftriqi:
  case X86::BI__builtin_ia32_kshiftrihi:
  case X86::BI__builtin_ia32_kshiftrisi:
  case X86::BI__builtin_ia32_kshiftridi:
    i = 1;
    l = 0;
    u = 255;
    break;
  case X86::BI__builtin_ia32_vperm2f128_pd256:
  case X86::BI__builtin_ia32_vperm2f128_ps256:
  case X86::BI__builtin_ia32_vperm2f128_si256:
  case X86::BI__builtin_ia32_permti256:
  case X86::BI__builtin_ia32_pblendw128:
  case X86::BI__builtin_ia32_pblendw256:
  case X86::BI__builtin_ia32_blendps256:
  case X86::BI__builtin_ia32_pblendd256:
  case X86::BI__builtin_ia32_palignr128:
  case X86::BI__builtin_ia32_palignr256:
  case X86::BI__builtin_ia32_palignr512:
  case X86::BI__builtin_ia32_alignq512:
  case X86::BI__builtin_ia32_alignd512:
  case X86::BI__builtin_ia32_alignd128:
  case X86::BI__builtin_ia32_alignd256:
  case X86::BI__builtin_ia32_alignq128:
  case X86::BI__builtin_ia32_alignq256:
  case X86::BI__builtin_ia32_vcomisd:
  case X86::BI__builtin_ia32_vcomiss:
  case X86::BI__builtin_ia32_shuf_f32x4:
  case X86::BI__builtin_ia32_shuf_f64x2:
  case X86::BI__builtin_ia32_shuf_i32x4:
  case X86::BI__builtin_ia32_shuf_i64x2:
  case X86::BI__builtin_ia32_shufpd512:
  case X86::BI__builtin_ia32_shufps:
  case X86::BI__builtin_ia32_shufps256:
  case X86::BI__builtin_ia32_shufps512:
  case X86::BI__builtin_ia32_dbpsadbw128:
  case X86::BI__builtin_ia32_dbpsadbw256:
  case X86::BI__builtin_ia32_dbpsadbw512:
  case X86::BI__builtin_ia32_vpshldd128:
  case X86::BI__builtin_ia32_vpshldd256:
  case X86::BI__builtin_ia32_vpshldd512:
  case X86::BI__builtin_ia32_vpshldq128:
  case X86::BI__builtin_ia32_vpshldq256:
  case X86::BI__builtin_ia32_vpshldq512:
  case X86::BI__builtin_ia32_vpshldw128:
  case X86::BI__builtin_ia32_vpshldw256:
  case X86::BI__builtin_ia32_vpshldw512:
  case X86::BI__builtin_ia32_vpshrdd128:
  case X86::BI__builtin_ia32_vpshrdd256:
  case X86::BI__builtin_ia32_vpshrdd512:
  case X86::BI__builtin_ia32_vpshrdq128:
  case X86::BI__builtin_ia32_vpshrdq256:
  case X86::BI__builtin_ia32_vpshrdq512:
  case X86::BI__builtin_ia32_vpshrdw128:
  case X86::BI__builtin_ia32_vpshrdw256:
  case X86::BI__builtin_ia32_vpshrdw512:
    i = 2;
    l = 0;
    u = 255;
    break;
  case X86::BI__builtin_ia32_fixupimmpd512_mask:
  case X86::BI__builtin_ia32_fixupimmpd512_maskz:
  case X86::BI__builtin_ia32_fixupimmps512_mask:
  case X86::BI__builtin_ia32_fixupimmps512_maskz:
  case X86::BI__builtin_ia32_fixupimmsd_mask:
  case X86::BI__builtin_ia32_fixupimmsd_maskz:
  case X86::BI__builtin_ia32_fixupimmss_mask:
  case X86::BI__builtin_ia32_fixupimmss_maskz:
  case X86::BI__builtin_ia32_fixupimmpd128_mask:
  case X86::BI__builtin_ia32_fixupimmpd128_maskz:
  case X86::BI__builtin_ia32_fixupimmpd256_mask:
  case X86::BI__builtin_ia32_fixupimmpd256_maskz:
  case X86::BI__builtin_ia32_fixupimmps128_mask:
  case X86::BI__builtin_ia32_fixupimmps128_maskz:
  case X86::BI__builtin_ia32_fixupimmps256_mask:
  case X86::BI__builtin_ia32_fixupimmps256_maskz:
  case X86::BI__builtin_ia32_pternlogd512_mask:
  case X86::BI__builtin_ia32_pternlogd512_maskz:
  case X86::BI__builtin_ia32_pternlogq512_mask:
  case X86::BI__builtin_ia32_pternlogq512_maskz:
  case X86::BI__builtin_ia32_pternlogd128_mask:
  case X86::BI__builtin_ia32_pternlogd128_maskz:
  case X86::BI__builtin_ia32_pternlogd256_mask:
  case X86::BI__builtin_ia32_pternlogd256_maskz:
  case X86::BI__builtin_ia32_pternlogq128_mask:
  case X86::BI__builtin_ia32_pternlogq128_maskz:
  case X86::BI__builtin_ia32_pternlogq256_mask:
  case X86::BI__builtin_ia32_pternlogq256_maskz:
  case X86::BI__builtin_ia32_vsm3rnds2:
    i = 3;
    l = 0;
    u = 255;
    break;
  case X86::BI__builtin_ia32_gatherpfdpd:
  case X86::BI__builtin_ia32_gatherpfdps:
  case X86::BI__builtin_ia32_gatherpfqpd:
  case X86::BI__builtin_ia32_gatherpfqps:
  case X86::BI__builtin_ia32_scatterpfdpd:
  case X86::BI__builtin_ia32_scatterpfdps:
  case X86::BI__builtin_ia32_scatterpfqpd:
  case X86::BI__builtin_ia32_scatterpfqps:
    i = 4;
    l = 2;
    u = 3;
    break;
  case X86::BI__builtin_ia32_reducesd_mask:
  case X86::BI__builtin_ia32_reducess_mask:
  case X86::BI__builtin_ia32_rndscalesd_round_mask:
  case X86::BI__builtin_ia32_rndscaless_round_mask:
  case X86::BI__builtin_ia32_rndscalesh_round_mask:
  case X86::BI__builtin_ia32_reducesh_mask:
    i = 4;
    l = 0;
    u = 255;
    break;
  case X86::BI__builtin_ia32_cmpccxadd32:
  case X86::BI__builtin_ia32_cmpccxadd64:
    i = 3;
    l = 0;
    u = 15;
    break;
  }

  // Note that we don't force a hard error on the range check here, allowing
  // macro-generated dead code to potentially have out-of-
  // range values. These need to code generate, but don't need to necessarily
  // make any sense. We use a warning that defaults to an error.
  return SemaBuiltinConstantArgRange(TheCall, i, l, u, /*RangeIsError*/ false);
}


bool Sema::SemaBuiltinARMMemoryTaggingCall(unsigned BuiltinID,
                                           CallExpr *TheCall) {
  if (BuiltinID == AArch64::BI__builtin_arm_irg) {
    if (checkArgCount(*this, TheCall, 2))
      return true;
    Expr *Arg0 = TheCall->getArg(0);
    Expr *Arg1 = TheCall->getArg(1);

    ExprResult FirstArg = DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;
    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();
    TheCall->setArg(0, FirstArg.get());

    ExprResult SecArg = DefaultLvalueConversion(Arg1);
    if (SecArg.isInvalid())
      return true;
    QualType SecArgType = SecArg.get()->getType();
    if (!SecArgType->isIntegerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_integer)
             << "second" << SecArgType << Arg1->getSourceRange();

    // Derive the return type from the pointer argument.
    TheCall->setType(FirstArgType);
    return false;
  }

  if (BuiltinID == AArch64::BI__builtin_arm_addg) {
    if (checkArgCount(*this, TheCall, 2))
      return true;

    Expr *Arg0 = TheCall->getArg(0);
    ExprResult FirstArg = DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;
    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();
    TheCall->setArg(0, FirstArg.get());

    // Derive the return type from the pointer argument.
    TheCall->setType(FirstArgType);

    // Second arg must be an constant in range [0,15]
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, 15);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_gmi) {
    if (checkArgCount(*this, TheCall, 2))
      return true;
    Expr *Arg0 = TheCall->getArg(0);
    Expr *Arg1 = TheCall->getArg(1);

    ExprResult FirstArg = DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;
    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();

    QualType SecArgType = Arg1->getType();
    if (!SecArgType->isIntegerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_integer)
             << "second" << SecArgType << Arg1->getSourceRange();
    TheCall->setType(Context.IntTy);
    return false;
  }

  if (BuiltinID == AArch64::BI__builtin_arm_ldg ||
      BuiltinID == AArch64::BI__builtin_arm_stg) {
    if (checkArgCount(*this, TheCall, 1))
      return true;
    Expr *Arg0 = TheCall->getArg(0);
    ExprResult FirstArg = DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;

    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();
    TheCall->setArg(0, FirstArg.get());

    // Derive the return type from the pointer argument.
    if (BuiltinID == AArch64::BI__builtin_arm_ldg)
      TheCall->setType(FirstArgType);
    return false;
  }

  if (BuiltinID == AArch64::BI__builtin_arm_subp) {
    Expr *ArgA = TheCall->getArg(0);
    Expr *ArgB = TheCall->getArg(1);

    ExprResult ArgExprA = DefaultFunctionArrayLvalueConversion(ArgA);
    ExprResult ArgExprB = DefaultFunctionArrayLvalueConversion(ArgB);

    if (ArgExprA.isInvalid() || ArgExprB.isInvalid())
      return true;

    QualType ArgTypeA = ArgExprA.get()->getType();
    QualType ArgTypeB = ArgExprB.get()->getType();

    auto isNull = [&](Expr *E) -> bool {
      return E->isNullPointerConstant(Context,
                                      Expr::NPC_ValueDependentIsNotNull);
    };

    // argument should be either a pointer or null
    if (!ArgTypeA->isAnyPointerType() && !isNull(ArgA))
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_null_or_pointer)
             << "first" << ArgTypeA << ArgA->getSourceRange();

    if (!ArgTypeB->isAnyPointerType() && !isNull(ArgB))
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_null_or_pointer)
             << "second" << ArgTypeB << ArgB->getSourceRange();

    // Ensure Pointee types are compatible
    if (ArgTypeA->isAnyPointerType() && !isNull(ArgA) &&
        ArgTypeB->isAnyPointerType() && !isNull(ArgB)) {
      QualType pointeeA = ArgTypeA->getPointeeType();
      QualType pointeeB = ArgTypeB->getPointeeType();
      if (!Context.typesAreCompatible(
              Context.getCanonicalType(pointeeA).getUnqualifiedType(),
              Context.getCanonicalType(pointeeB).getUnqualifiedType())) {
        return Diag(TheCall->getBeginLoc(),
                    diag::err_typecheck_sub_ptr_compatible)
               << ArgTypeA << ArgTypeB << ArgA->getSourceRange()
               << ArgB->getSourceRange();
      }
    }

    // at least one argument should be pointer type
    if (!ArgTypeA->isAnyPointerType() && !ArgTypeB->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_any2arg_pointer)
             << ArgTypeA << ArgTypeB << ArgA->getSourceRange();

    if (isNull(ArgA)) // adopt type of the other pointer
      ArgExprA = ImpCastExprToType(ArgExprA.get(), ArgTypeB, CK_NullToPointer);

    if (isNull(ArgB))
      ArgExprB = ImpCastExprToType(ArgExprB.get(), ArgTypeA, CK_NullToPointer);

    TheCall->setArg(0, ArgExprA.get());
    TheCall->setArg(1, ArgExprB.get());
    TheCall->setType(Context.LongLongTy);
    return false;
  }
  assert(false && "Unhandled AArch64 MTE intrinsic");
  return true;
}

bool Sema::SemaBuiltinARMSpecialReg(unsigned BuiltinID, CallExpr *TheCall,
                                    int ArgNum, unsigned ExpectedFieldNum,
                                    bool AllowName) {
  bool IsAArch64Builtin = BuiltinID == AArch64::BI__builtin_arm_rsr64 ||
                          BuiltinID == AArch64::BI__builtin_arm_wsr64 ||
                          BuiltinID == AArch64::BI__builtin_arm_rsr128 ||
                          BuiltinID == AArch64::BI__builtin_arm_wsr128 ||
                          BuiltinID == AArch64::BI__builtin_arm_rsr ||
                          BuiltinID == AArch64::BI__builtin_arm_rsrp ||
                          BuiltinID == AArch64::BI__builtin_arm_wsr ||
                          BuiltinID == AArch64::BI__builtin_arm_wsrp;
  assert(IsAArch64Builtin && "Unexpected AArch64 builtin.");
  bool IsARMBuiltin = false;

  Expr *Arg = TheCall->getArg(ArgNum);

  if (!isa<StringLiteral>(Arg->IgnoreParenImpCasts()))
    return Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
           << Arg->getSourceRange();

  llvm::StringRef Reg =
      cast<StringLiteral>(Arg->IgnoreParenImpCasts())->getString();
  llvm::SmallVector<llvm::StringRef, 6> Fields;
  Reg.split(Fields, ":");

  if (Fields.size() != ExpectedFieldNum && !(AllowName && Fields.size() == 1))
    return Diag(TheCall->getBeginLoc(), diag::err_arm_invalid_specialreg)
           << Arg->getSourceRange();

  // If the string is the name of a register then we cannot check that it is
  // valid here but if the string is of one the forms described in ACLE then we
  // can check that the supplied fields are integers and within the valid
  // ranges.
  if (Fields.size() > 1) {
    bool FiveFields = Fields.size() == 5;

    bool ValidString = true;
    if (IsARMBuiltin) {
      ValidString &= Fields[0].starts_with_insensitive("cp") ||
                     Fields[0].starts_with_insensitive("p");
      if (ValidString)
        Fields[0] = Fields[0].drop_front(
            Fields[0].starts_with_insensitive("cp") ? 2 : 1);

      ValidString &= Fields[2].starts_with_insensitive("c");
      if (ValidString)
        Fields[2] = Fields[2].drop_front(1);

      if (FiveFields) {
        ValidString &= Fields[3].starts_with_insensitive("c");
        if (ValidString)
          Fields[3] = Fields[3].drop_front(1);
      }
    }

    llvm::SmallVector<int, 5> Ranges;
    if (FiveFields)
      Ranges.append({IsAArch64Builtin ? 1 : 15, 7, 15, 15, 7});
    else
      Ranges.append({15, 7, 15});

    for (unsigned i = 0; i < Fields.size(); ++i) {
      int IntField;
      ValidString &= !Fields[i].getAsInteger(10, IntField);
      ValidString &= (IntField >= 0 && IntField <= Ranges[i]);
    }

    if (!ValidString)
      return Diag(TheCall->getBeginLoc(), diag::err_arm_invalid_specialreg)
             << Arg->getSourceRange();
  } else if (IsAArch64Builtin && Fields.size() == 1) {
    // This code validates writes to PSTATE registers.

    // Not a write.
    if (TheCall->getNumArgs() != 2)
      return false;

    // The 128-bit system register accesses do not touch PSTATE.
    if (BuiltinID == AArch64::BI__builtin_arm_rsr128 ||
        BuiltinID == AArch64::BI__builtin_arm_wsr128)
      return false;

    // These are the named PSTATE accesses using "MSR (immediate)" instructions,
    // along with the upper limit on the immediates allowed.
    auto MaxLimit = llvm::StringSwitch<std::optional<unsigned>>(Reg)
                        .CaseLower("spsel", 15)
                        .CaseLower("daifclr", 15)
                        .CaseLower("daifset", 15)
                        .CaseLower("pan", 15)
                        .CaseLower("uao", 15)
                        .CaseLower("dit", 15)
                        .CaseLower("ssbs", 15)
                        .CaseLower("tco", 15)
                        .CaseLower("allint", 1)
                        .CaseLower("pm", 1)
                        .Default(std::nullopt);

    // If this is not a named PSTATE, just continue without validating, as this
    // will be lowered to an "MSR (register)" instruction directly
    if (!MaxLimit)
      return false;

    // Here we only allow constants in the range for that pstate, as required by
    // the ACLE.
    //
    // While NeverC also accepts the names of system registers in its ACLE
    // intrinsics, we prevent this with the PSTATE names used in MSR (immediate)
    // as the value written via a register is different to the value used as an
    // immediate to have the same effect. e.g., for the instruction `msr tco,
    // x0`, it is bit 25 of register x0 that is written into PSTATE.TCO, but
    // with `msr tco, #imm`, it is bit 0 of xN that is written into PSTATE.TCO.
    //
    // If a programmer wants to codegen the MSR (register) form of `msr tco,
    // xN`, they can still do so by specifying the register using five
    // colon-separated numbers in a string.
    return SemaBuiltinConstantArgRange(TheCall, 1, 0, *MaxLimit);
  }

  return false;
}

