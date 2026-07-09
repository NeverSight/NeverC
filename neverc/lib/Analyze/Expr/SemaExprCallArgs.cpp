//===- SemaExprCallArgs.cpp - Call argument conversion & checking ---===//
//
// Extracted from SemaExpr.cpp (mechanical move, no logic change).
//
//===----------------------------------------------------------------------===//

#include "Expr/SemaExprUtils.h"
#include "Expr/TreeTransform.h"
#include "neverc/Analyze/Designator.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaFixItUtils.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Call expressions & argument conversion
// ===----------------------------------------------------------------------===

bool Sema::ConvertArgumentsForCall(CallExpr *Call, Expr *Fn,
                                   FunctionDecl *FDecl,
                                   const FunctionProtoType *Proto,
                                   llvm::ArrayRef<Expr *> Args,
                                   SourceLocation RParenLoc) {
  // Bail out early if calling a builtin with custom typechecking.
  if (FDecl)
    if (unsigned ID = FDecl->getBuiltinID())
      if (Context.BuiltinInfo.hasCustomTypechecking(ID))
        return false;

  // C99 6.5.2.2p7 - the arguments are implicitly converted, as if by
  // assignment, to the types of the corresponding parameter, ...
  unsigned NumParams = Proto->getNumParams();
  bool Invalid = false;
  unsigned MinArgs = FDecl ? FDecl->getMinRequiredArguments() : NumParams;

  // If too few arguments are available (and we don't have default
  // arguments for the remaining parameters), don't make the call.
  if (Args.size() < NumParams) {
    if (Args.size() < MinArgs) {
      if (MinArgs == 1 && FDecl && FDecl->getParamDecl(0)->getDeclName())
        Diag(RParenLoc,
             MinArgs == NumParams && !Proto->isVariadic()
                 ? diag::err_typecheck_call_too_few_args_one
                 : diag::err_typecheck_call_too_few_args_at_least_one)
            << FDecl->getParamDecl(0) << Fn->getSourceRange();
      else
        Diag(RParenLoc, MinArgs == NumParams && !Proto->isVariadic()
                            ? diag::err_typecheck_call_too_few_args
                            : diag::err_typecheck_call_too_few_args_at_least)
            << MinArgs << static_cast<unsigned>(Args.size())
            << Fn->getSourceRange();

      if (FDecl && !FDecl->getBuiltinID())
        Diag(FDecl->getLocation(), diag::note_callee_decl)
            << FDecl << FDecl->getParametersSourceRange();

      return true;
    }
    // We reserve space for the default arguments when we create
    // the call expression, before calling ConvertArgumentsForCall.
    assert((Call->getNumArgs() == NumParams) &&
           "We should have reserved space for the default arguments before!");
  }

  // If too many are passed and not variadic, error on the extras and drop
  // them.
  if (Args.size() > NumParams) {
    if (!Proto->isVariadic()) {
      if (NumParams == 1 && FDecl && FDecl->getParamDecl(0)->getDeclName())
        Diag(Args[NumParams]->getBeginLoc(),
             MinArgs == NumParams
                 ? diag::err_typecheck_call_too_many_args_one
                 : diag::err_typecheck_call_too_many_args_at_most_one)
            << FDecl->getParamDecl(0) << static_cast<unsigned>(Args.size())
            << Fn->getSourceRange()
            << SourceRange(Args[NumParams]->getBeginLoc(),
                           Args.back()->getEndLoc());
      else
        Diag(Args[NumParams]->getBeginLoc(),
             MinArgs == NumParams
                 ? diag::err_typecheck_call_too_many_args
                 : diag::err_typecheck_call_too_many_args_at_most)
            << NumParams << static_cast<unsigned>(Args.size())
            << Fn->getSourceRange()
            << SourceRange(Args[NumParams]->getBeginLoc(),
                           Args.back()->getEndLoc());

      if (FDecl && !FDecl->getBuiltinID())
        Diag(FDecl->getLocation(), diag::note_callee_decl)
            << FDecl << FDecl->getParametersSourceRange();

      // This deletes the extra arguments.
      Call->shrinkNumArgs(NumParams);
      return true;
    }
  }
  llvm::SmallVector<Expr *, 8> AllArgs;
  VariadicCallType CallType =
      (Proto && Proto->isVariadic()) ? VariadicFunction : VariadicDoesNotApply;

  Invalid = GatherArgumentsForCall(Call->getBeginLoc(), FDecl, Proto, 0, Args,
                                   AllArgs, CallType);
  if (Invalid)
    return true;
  unsigned TotalNumArgs = AllArgs.size();
  for (unsigned i = 0; i < TotalNumArgs; ++i)
    Call->setArg(i, AllArgs[i]);

  Call->computeDependence();
  return false;
}

bool Sema::GatherArgumentsForCall(SourceLocation CallLoc, FunctionDecl *FDecl,
                                  const FunctionProtoType *Proto,
                                  unsigned FirstParam,
                                  llvm::ArrayRef<Expr *> Args,
                                  llvm::SmallVectorImpl<Expr *> &AllArgs,
                                  VariadicCallType CallType) {
  unsigned NumParams = Proto->getNumParams();
  bool Invalid = false;
  size_t ArgIx = 0;

  // Hoist loop-invariant FDecl / context queries out of the per-param loop.
  bool IsNeverCStringRuntimeFn = FDecl && isNeverCStringRuntimeFD(FDecl);
  bool IsNeverCStringCStrFn = FDecl && isNeverCStringBorrowedViewFD(FDecl);
  bool InsideStringRuntime =
      IsNeverCStringRuntimeFn && isInsideNeverCStringRuntime();

  // Continue to check argument types (even if we have too few/many args).
  for (unsigned i = FirstParam; i < NumParams; i++) {
    QualType ProtoArgType = Proto->getParamType(i);

    Expr *Arg;
    ParmVarDecl *Param = FDecl ? FDecl->getParamDecl(i) : nullptr;
    if (ArgIx < Args.size()) {
      Arg = Args[ArgIx++];

      if (RequireCompleteType(Arg->getBeginLoc(), ProtoArgType,
                              diag::err_call_incomplete_argument, Arg))
        return true;

      bool IsNeverCStringParam =
          (IsNeverCStringRuntimeFn || IsNeverCStringCStrFn) &&
          this->isNeverCStringType(ProtoArgType);
      bool IsNeverCStringRuntimeArg =
          IsNeverCStringParam && IsNeverCStringRuntimeFn;
      bool IsNeverCStringCStrArg = IsNeverCStringParam && IsNeverCStringCStrFn;

      if (this->isNeverCStringType(ProtoArgType)) {
        if (StringLiteral *SL = getNeverCStringLiteral(Arg)) {
          ExprResult LiteralView =
              buildNeverCStringLiteral(*this, ProtoArgType, Arg, SL);
          if (LiteralView.isInvalid())
            return true;
          Arg = LiteralView.get();
        }
      }

      bool ArgIsNeverCString =
          (IsNeverCStringCStrArg || IsNeverCStringRuntimeArg)
              ? this->isNeverCStringType(Arg->getType())
              : false;

      if (IsNeverCStringCStrArg && ArgIsNeverCString &&
          Arg->getValueKind() == VK_PRValue) {
        Diag(Arg->getExprLoc(), diag::err_neverc_string_cstr_temporary)
            << Arg->getSourceRange();
        return true;
      }

      ExprResult ArgE;
      bool PassNeverCStringLValueDirect =
          IsNeverCStringRuntimeArg && ArgIsNeverCString &&
          Arg->getValueKind() == VK_LValue &&
          BuiltinString::isLValueDirectHelper(FDecl->getName(),
                                              InsideStringRuntime);
      if (PassNeverCStringLValueDirect) {
        ArgE = DefaultFunctionArrayLvalueConversion(Arg, /*Diagnose=*/false);
      } else {
        InitializedEntity Entity =
            Param ? InitializedEntity::InitializeParameter(Context, Param,
                                                           ProtoArgType)
                  : InitializedEntity::InitializeParameter(Context,
                                                           ProtoArgType, false);
        ArgE = PerformCopyInitialization(Entity, SourceLocation(), Arg);
      }
      if (ArgE.isInvalid())
        return true;

      Arg = ArgE.getAs<Expr>();
    } else {
      return true;
    }

    // Check for array bounds violations for each argument to the call. This
    // check only triggers warnings when the argument isn't a more complex Expr
    // with its own checking, such as a BinaryOperator.
    CheckArrayAccess(Arg);

    // Check for violations of C99 static array rules (C99 6.7.5.3p7).
    CheckStaticArrayArgument(CallLoc, Param, Arg);

    AllArgs.push_back(Arg);
  }

  // If this is a variadic call, handle args passed through "...".
  if (CallType != VariadicDoesNotApply) {
    // NeverC string variadic args: insert an implicit
    // `__neverc_string_retain` wrapper for every lvalue NeverC `string`
    // argument the caller hands to a runtime helper through the `...`
    // tail.  C's `DefaultVariadicArgumentPromotion` cannot do this on
    // its own (the standard exposes no per-arg conversion callback for
    // variadic), so without the wrapper the helper's by-value consume
    // contract would double-free with the caller's scope cleanup.
    // Mirrors the non-variadic copy-init path's PerformCopyInitialization
    // which yields the same retain copy through Sema's value-init logic.
    // Only fires when the called helper is a NeverC string runtime
    // function (`Sema::isNeverCStringRuntimeFD`); plain C variadic
    // calls (printf, ...) are untouched.
    // Argument promotion for variadic arguments (C99 6.5.2.2p7).
    for (Expr *A : Args.slice(ArgIx)) {
      Expr *ArgExpr = A;
      if (IsNeverCStringRuntimeFn &&
          this->isNeverCStringType(ArgExpr->getType()) &&
          ArgExpr->getValueKind() == VK_LValue) {
        Expr *RetainArgs[] = {ArgExpr};
        ExprResult Retained = buildNeverCStringRuntimeCall(
            *this, /*Scope=*/nullptr, ArgExpr->getExprLoc(),
            BuiltinStringNames::RetainFunctionName, RetainArgs,
            ArgExpr->getExprLoc());
        if (Retained.isInvalid()) {
          Invalid = true;
        } else {
          ArgExpr = Retained.get();
        }
      }
      ExprResult Arg = DefaultVariadicArgumentPromotion(ArgExpr);
      Invalid |= Arg.isInvalid();
      AllArgs.push_back(Arg.get());
    }

    for (Expr *A : Args.slice(ArgIx))
      CheckArrayAccess(A);
  }
  return Invalid;
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnStaticArrayParam(Sema &S, ParmVarDecl *PVD) {
  TypeLoc TL = PVD->getTypeSourceInfo()->getTypeLoc();
  if (DecayedTypeLoc DTL = TL.getAs<DecayedTypeLoc>())
    TL = DTL.getOriginalLoc();
  if (ArrayTypeLoc ATL = TL.getAs<ArrayTypeLoc>())
    S.Diag(PVD->getLocation(), diag::note_callee_static_array)
        << ATL.getLocalSourceRange();
}
} // namespace

void Sema::CheckStaticArrayArgument(SourceLocation CallLoc, ParmVarDecl *Param,
                                    const Expr *ArgExpr) {
  if (!Param)
    return;

  QualType OrigTy = Param->getOriginalType();

  const ArrayType *AT = Context.getAsArrayType(OrigTy);
  if (!AT || AT->getSizeModifier() != ArraySizeModifier::Static)
    return;

  if (ArgExpr->isNullPointerConstant(Context, Expr::NPC_NeverValueDependent)) {
    Diag(CallLoc, diag::warn_null_arg) << ArgExpr->getSourceRange();
    warnStaticArrayParam(*this, Param);
    return;
  }

  const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(AT);
  if (!CAT)
    return;

  const ConstantArrayType *ArgCAT =
      Context.getAsConstantArrayType(ArgExpr->IgnoreParenCasts()->getType());
  if (!ArgCAT)
    return;

  if (getTreeContext().hasSameUnqualifiedType(CAT->getElementType(),
                                              ArgCAT->getElementType())) {
    if (ArgCAT->getSize().ult(CAT->getSize())) {
      Diag(CallLoc, diag::warn_static_array_too_small)
          << ArgExpr->getSourceRange()
          << (unsigned)ArgCAT->getSize().getZExtValue()
          << (unsigned)CAT->getSize().getZExtValue() << 0;
      warnStaticArrayParam(*this, Param);
    }
    return;
  }

  std::optional<CharUnits> ArgSize =
      getTreeContext().getTypeSizeInCharsIfKnown(ArgCAT);
  std::optional<CharUnits> ParmSize =
      getTreeContext().getTypeSizeInCharsIfKnown(CAT);
  if (ArgSize && ParmSize && *ArgSize < *ParmSize) {
    Diag(CallLoc, diag::warn_static_array_too_small)
        << ArgExpr->getSourceRange() << (unsigned)ArgSize->getQuantity()
        << (unsigned)ParmSize->getQuantity() << 1;
    warnStaticArrayParam(*this, Param);
  }
}

static bool isStrippablePlaceholderArg(QualType type) {
  // Placeholders are never sugared.
  const BuiltinType *placeholder = dyn_cast<BuiltinType>(type);
  if (!placeholder)
    return false;

  switch (placeholder->getKind()) {
    // Ignore all the non-placeholder types.
#define SVE_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
#define PLACEHOLDER_TYPE(ID, SINGLETON_ID)
#define BUILTIN_TYPE(ID, SINGLETON_ID) case BuiltinType::ID:
#include "neverc/Tree/Type/BuiltinTypes.def"
    return false;

  // We cannot lower out overload sets; they might validly be resolved
  // by the call machinery.
  case BuiltinType::Overload:
    return false;

  case BuiltinType::PseudoObject:
  case BuiltinType::BuiltinFn:
  case BuiltinType::IncompleteMatrixIdx:
    return true;
  }
  llvm_unreachable("bad builtin type kind");
}

namespace neverc {
bool resolveArgPlaceholders(Sema &S, MultiExprArg args) {
  // Apply this processing to all the arguments at once instead of
  // dying at the first failure.
  bool hasInvalid = false;
  for (size_t i = 0, e = args.size(); i != e; i++) {
    if (isStrippablePlaceholderArg(args[i]->getType())) {
      ExprResult result = S.CheckPlaceholderExpr(args[i]);
      if (result.isInvalid())
        hasInvalid = true;
      else
        args[i] = result.get();
    }
  }
  return hasInvalid;
}
} // namespace neverc

namespace neverc {
FunctionDecl *rewriteBuiltinFunctionDecl(Sema *Sema, TreeContext &Context,
                                         FunctionDecl *FDecl,
                                         MultiExprArg ArgExprs) {

  QualType DeclType = FDecl->getType();
  const FunctionProtoType *FT = dyn_cast<FunctionProtoType>(DeclType);

  if (!Context.BuiltinInfo.hasPtrArgsOrResult(FDecl->getBuiltinID()) || !FT ||
      ArgExprs.size() < FT->getNumParams())
    return nullptr;

  bool NeedsNewDecl = false;
  unsigned i = 0;
  llvm::SmallVector<QualType, 8> OverloadParams;

  for (QualType ParamType : FT->param_types()) {

    // Convert array arguments to pointer to simplify type lookup.
    ExprResult ArgRes =
        Sema->DefaultFunctionArrayLvalueConversion(ArgExprs[i++]);
    if (ArgRes.isInvalid())
      return nullptr;
    Expr *Arg = ArgRes.get();
    QualType ArgType = Arg->getType();
    if (!ParamType->isPointerType() || ParamType.hasAddressSpace() ||
        !ArgType->isPointerType() ||
        !ArgType->getPointeeType().hasAddressSpace() ||
        isPtrSizeAddressSpace(ArgType->getPointeeType().getAddressSpace())) {
      OverloadParams.push_back(ParamType);
      continue;
    }

    QualType PointeeType = ParamType->getPointeeType();
    if (PointeeType.hasAddressSpace())
      continue;

    NeedsNewDecl = true;
    LangAS AS = ArgType->getPointeeType().getAddressSpace();

    PointeeType = Context.getAddrSpaceQualType(PointeeType, AS);
    OverloadParams.push_back(Context.getPointerType(PointeeType));
  }

  if (!NeedsNewDecl)
    return nullptr;

  FunctionProtoType::ExtProtoInfo EPI;
  EPI.Variadic = FT->isVariadic();
  QualType OverloadTy =
      Context.getFunctionType(FT->getReturnType(), OverloadParams, EPI);
  DeclContext *Parent = FDecl->getParent();
  FunctionDecl *OverloadDecl = FunctionDecl::Create(
      Context, Parent, FDecl->getLocation(), FDecl->getLocation(),
      FDecl->getIdentifier(), OverloadTy,
      /*TInfo=*/nullptr, SC_Extern, Sema->getCurFPFeatures().isFPConstrained(),
      false,
      /*hasPrototype=*/true);
  llvm::SmallVector<ParmVarDecl *, 16> Params;
  FT = cast<FunctionProtoType>(OverloadTy);
  for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i) {
    QualType ParamType = FT->getParamType(i);
    ParmVarDecl *Parm =
        ParmVarDecl::Create(Context, OverloadDecl, SourceLocation(),
                            SourceLocation(), nullptr, ParamType,
                            /*TInfo=*/nullptr, SC_None, nullptr);
    Parm->setScopeInfo(0, i);
    Params.push_back(Parm);
  }
  OverloadDecl->setParams(Params);
  Sema->mergeDeclAttributes(OverloadDecl, FDecl);
  return OverloadDecl;
}

void validateDirectCallTarget(Sema &S, const Expr *Fn, FunctionDecl *Callee,
                              MultiExprArg ArgExprs) {
  // `Callee` (when called with ArgExprs) may be ill-formed. enable_if (and
  // similar attributes) really don't like it when functions are called with an
  // invalid number of args.
  if (S.TooManyArguments(Callee->getNumParams(), ArgExprs.size()) &&
      !Callee->isVariadic())
    return;
  if (Callee->getMinRequiredArguments() > ArgExprs.size())
    return;
}
} // namespace neverc

