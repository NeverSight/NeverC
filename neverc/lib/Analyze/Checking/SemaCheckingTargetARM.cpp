#include "Checking/SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Foundation/Core/SyncScope.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
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

  if (BuiltinID == AArch64::BI__builtin_arm_scvtf_fixed ||
      BuiltinID == AArch64::BI__builtin_arm_ucvtf_fixed ||
      BuiltinID == AArch64::BI__builtin_arm_fcvtzs_fixed ||
      BuiltinID == AArch64::BI__builtin_arm_fcvtzu_fixed) {
    if (SemaBuiltinConstantArgRange(TheCall, 2, 0, 1))
      return true;
    if (TheCall->getArg(2)->isValueDependent())
      return false;

    llvm::APSInt Is64;
    if (SemaBuiltinConstantArg(TheCall, 2, Is64))
      return true;
    return SemaBuiltinConstantArgRange(TheCall, 1, 1,
                                       Is64.getZExtValue() ? 64 : 32);
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

// ===----------------------------------------------------------------------===
// ARM memory tagging & special-register builtins
// ===----------------------------------------------------------------------===

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
