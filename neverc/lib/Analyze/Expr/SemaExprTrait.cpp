//===- SemaExprTrait.cpp - sizeof/alignof/paren & alignas -----------------===//
//
// Extracted from SemaExprLiteral.cpp (mechanical move, no logic change).
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
// Parentheses, unary traits & alignas
// ===----------------------------------------------------------------------===

ExprResult Sema::OnParenExpr(SourceLocation L, SourceLocation R, Expr *E) {
  assert(E && "OnParenExpr() missing expr");
  QualType ExprTy = E->getType();
  if (getLangOpts().ProtectParens && CurFPFeatures.getAllowFPReassociate() &&
      !E->isLValue() && ExprTy->hasFloatingRepresentation())
    return FormBuiltinCallExpr(R, Builtin::BI__arithmetic_fence, E);
  return new (Context) ParenExpr(L, R, E);
}

namespace {
bool checkVectorElementsTraitOperandType(Sema &S, QualType T,
                                         SourceLocation Loc,
                                         SourceRange ArgRange) {
  // builtin_vectorelements supports both fixed-sized and scalable vectors.
  if (!T->isVectorType() && !T->isSizelessVectorType())
    return S.Diag(Loc, diag::err_builtin_non_vector_type)
           << ""
           << "__builtin_vectorelements" << T << ArgRange;

  return false;
}
} // namespace

namespace {
bool checkExtensionTraitOperandType(Sema &S, QualType T, SourceLocation Loc,
                                    SourceRange ArgRange,
                                    UnaryExprOrTypeTrait TraitKind) {
  // sizeof/alignof on function or void type is an extension.
  if (T->isFunctionType() &&
      (TraitKind == UETT_SizeOf || TraitKind == UETT_AlignOf ||
       TraitKind == UETT_PreferredAlignOf)) {
    // sizeof(function)/alignof(function) is allowed as an extension.
    S.Diag(Loc, diag::ext_sizeof_alignof_function_type)
        << getTraitSpelling(TraitKind) << ArgRange;
    return false;
  }

  if (T->isVoidType()) {
    S.Diag(Loc, diag::ext_sizeof_alignof_void_type)
        << getTraitSpelling(TraitKind) << ArgRange;
    return false;
  }

  return true;
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnSizeofOnArrayDecay(Sema &S, SourceLocation Loc, QualType T,
                            const Expr *E) {
  // Don't warn if the operation changed the type.
  if (T != E->getType())
    return;

  // Now look for array decays.
  const auto *ICE = dyn_cast<ImplicitCastExpr>(E);
  if (!ICE || ICE->getCastKind() != CK_ArrayToPointerDecay)
    return;

  S.Diag(Loc, diag::warn_sizeof_array_decay)
      << ICE->getSourceRange() << ICE->getType()
      << ICE->getSubExpr()->getType();
}
} // namespace

bool Sema::CheckUnaryExprOrTypeTraitOperand(Expr *E,
                                            UnaryExprOrTypeTrait ExprKind) {
  QualType ExprTy = E->getType();

  bool IsUnevaluatedOperand =
      (ExprKind == UETT_SizeOf || ExprKind == UETT_AlignOf ||
       ExprKind == UETT_PreferredAlignOf);

  // The operand for sizeof and alignof is in an unevaluated expression context,
  // so side effects could result in unintended consequences.
  if (IsUnevaluatedOperand && !E->getType()->isVariableArrayType() &&
      E->HasSideEffects(Context, false))
    Diag(E->getExprLoc(), diag::warn_side_effects_unevaluated_context);

  if (ExprKind == UETT_VectorElements)
    return checkVectorElementsTraitOperandType(*this, ExprTy, E->getExprLoc(),
                                               E->getSourceRange());

  // Explicitly list some types as extensions.
  if (!checkExtensionTraitOperandType(*this, ExprTy, E->getExprLoc(),
                                      E->getSourceRange(), ExprKind))
    return false;

  // 'alignof' applied to an expression only requires the base element type of
  // the expression to be complete. 'sizeof' requires the expression's type to
  // be complete (and will attempt to complete it if it's an array of unknown
  // bound).
  if (ExprKind == UETT_AlignOf || ExprKind == UETT_PreferredAlignOf) {
    if (RequireCompleteSizedType(
            E->getExprLoc(), Context.getBaseElementType(E->getType()),
            diag::err_sizeof_alignof_incomplete_or_sizeless_type,
            getTraitSpelling(ExprKind), E->getSourceRange()))
      return true;
  } else {
    if (RequireCompleteSizedExprType(
            E, diag::err_sizeof_alignof_incomplete_or_sizeless_type,
            getTraitSpelling(ExprKind), E->getSourceRange()))
      return true;
  }

  ExprTy = E->getType();

  if (ExprTy->isFunctionType()) {
    Diag(E->getExprLoc(), diag::err_sizeof_alignof_function_type)
        << getTraitSpelling(ExprKind) << E->getSourceRange();
    return true;
  }

  if (ExprKind == UETT_SizeOf) {
    if (const auto *DeclRef = dyn_cast<DeclRefExpr>(E->IgnoreParens())) {
      if (const auto *PVD = dyn_cast<ParmVarDecl>(DeclRef->getFoundDecl())) {
        QualType OType = PVD->getOriginalType();
        QualType Type = PVD->getType();
        if (Type->isPointerType() && OType->isArrayType()) {
          Diag(E->getExprLoc(), diag::warn_sizeof_array_param) << Type << OType;
          Diag(PVD->getLocation(), diag::note_declared_at);
        }
      }
    }

    // Warn on "sizeof(array op x)" and "sizeof(x op array)", where the array
    // decays into a pointer and returns an unintended result. This is most
    // likely a typo for "sizeof(array) op x".
    if (const auto *BO = dyn_cast<BinaryOperator>(E->IgnoreParens())) {
      warnSizeofOnArrayDecay(*this, BO->getOperatorLoc(), BO->getType(),
                             BO->getLHS());
      warnSizeofOnArrayDecay(*this, BO->getOperatorLoc(), BO->getType(),
                             BO->getRHS());
    }
  }

  return false;
}

namespace {
bool checkAlignOfExpr(Sema &S, Expr *E, UnaryExprOrTypeTrait ExprKind) {
  if (E->getObjectKind() == OK_BitField) {
    S.Diag(E->getExprLoc(), diag::err_sizeof_alignof_typeof_bitfield)
        << 1 << E->getSourceRange();
    return true;
  }

  ValueDecl *D = nullptr;
  Expr *Inner = E->IgnoreParens();
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Inner)) {
    D = DRE->getDecl();
  } else if (MemberExpr *ME = dyn_cast<MemberExpr>(Inner)) {
    D = ME->getMemberDecl();
  }

  // If it's a field, require the containing struct to have a
  // complete definition so that we can compute the layout.
  //
  // Member can appear outside a member expression (unevaluated contexts,
  // trailing return type, etc.).
  //
  // For the record, since __alignof__ on expressions is a GCC
  // extension, GCC seems to permit this but always gives the
  // nonsensical answer 0.
  //
  // We don't really need the layout here --- we could instead just
  // directly check for all the appropriate alignment-lowing
  // attributes --- but that would require duplicating a lot of
  // logic that just isn't worth duplicating for such a marginal
  // use-case.
  if (FieldDecl *FD = dyn_cast_or_null<FieldDecl>(D)) {
    // Fast path this check, since we at least know the record has a
    // definition if we can find a member of it.
    if (!FD->getParent()->isCompleteDefinition()) {
      S.Diag(E->getExprLoc(), diag::err_alignof_member_of_incomplete_type)
          << E->getSourceRange();
      return true;
    }

    // Otherwise the field must have a complete type (or be a flexible array
    // member, which we explicitly want to white-list anyway), which makes the
    // following checks trivial.
    return false;
  }

  return S.CheckUnaryExprOrTypeTraitOperand(E, ExprKind);
}
} // namespace

bool Sema::CheckUnaryExprOrTypeTraitOperand(QualType ExprType,
                                            SourceLocation OpLoc,
                                            SourceRange ExprRange,
                                            UnaryExprOrTypeTrait ExprKind,
                                            llvm::StringRef KWName) {
  // alignof/_Alignof on an array: alignment of the element type
  // (C11 6.5.3.4/3).
  if (ExprKind == UETT_AlignOf || ExprKind == UETT_PreferredAlignOf)
    ExprType = Context.getBaseElementType(ExprType);

  if (ExprKind == UETT_VectorElements)
    return checkVectorElementsTraitOperandType(*this, ExprType, OpLoc,
                                               ExprRange);

  // Explicitly list some types as extensions.
  if (!checkExtensionTraitOperandType(*this, ExprType, OpLoc, ExprRange,
                                      ExprKind))
    return false;

  if (RequireCompleteSizedType(
          OpLoc, ExprType, diag::err_sizeof_alignof_incomplete_or_sizeless_type,
          KWName, ExprRange))
    return true;

  if (ExprType->isFunctionType()) {
    Diag(OpLoc, diag::err_sizeof_alignof_function_type) << KWName << ExprRange;
    return true;
  }

  return false;
}

ExprResult Sema::CreateUnaryExprOrTypeTraitExpr(TypeSourceInfo *TInfo,
                                                SourceLocation OpLoc,
                                                UnaryExprOrTypeTrait ExprKind,
                                                SourceRange R) {
  if (!TInfo)
    return ExprError();

  QualType T = TInfo->getType();

  if (CheckUnaryExprOrTypeTraitOperand(T, OpLoc, R, ExprKind,
                                       getTraitSpelling(ExprKind)))
    return ExprError();

  // Adds overload of TransformToPotentiallyEvaluated for TypeSourceInfo to
  // properly deal with VLAs in nested calls of sizeof and typeof.
  if (isUnevaluatedContext() && ExprKind == UETT_SizeOf &&
      TInfo->getType()->isVariablyModifiedType())
    TInfo = TransformToPotentiallyEvaluated(TInfo);

  // C99 6.5.3.4p4: the type (an unsigned integer type) is size_t.
  return new (Context) UnaryExprOrTypeTraitExpr(
      ExprKind, TInfo, Context.getSizeType(), OpLoc, R.getEnd());
}

ExprResult Sema::CreateUnaryExprOrTypeTraitExpr(Expr *E, SourceLocation OpLoc,
                                                UnaryExprOrTypeTrait ExprKind) {
  ExprResult PE = CheckPlaceholderExpr(E);
  if (PE.isInvalid())
    return ExprError();

  E = PE.get();

  // Verify that the operand is valid.
  bool isInvalid = false;
  if (ExprKind == UETT_AlignOf || ExprKind == UETT_PreferredAlignOf) {
    isInvalid = checkAlignOfExpr(*this, E, ExprKind);
  } else if (E->refersToBitField()) {
    Diag(E->getExprLoc(), diag::err_sizeof_alignof_typeof_bitfield) << 0;
    isInvalid = true;
  } else if (ExprKind == UETT_VectorElements) {
    isInvalid = CheckUnaryExprOrTypeTraitOperand(E, UETT_VectorElements);
  } else {
    isInvalid = CheckUnaryExprOrTypeTraitOperand(E, UETT_SizeOf);
  }

  if (isInvalid)
    return ExprError();

  if (ExprKind == UETT_SizeOf && E->getType()->isVariableArrayType()) {
    PE = TransformToPotentiallyEvaluated(E);
    if (PE.isInvalid())
      return ExprError();
    E = PE.get();
  }

  // Result type is size_t.
  return new (Context) UnaryExprOrTypeTraitExpr(
      ExprKind, E, Context.getSizeType(), OpLoc, E->getSourceRange().getEnd());
}

ExprResult Sema::OnUnaryExprOrTypeTraitExpr(SourceLocation OpLoc,
                                            UnaryExprOrTypeTrait ExprKind,
                                            bool IsType, void *TyOrEx,
                                            SourceRange ArgRange) {
  // If error parsing type, ignore.
  if (!TyOrEx)
    return ExprError();

  if (IsType) {
    TypeSourceInfo *TInfo;
    (void)GetTypeFromParser(ParsedType::getFromOpaquePtr(TyOrEx), &TInfo);
    return CreateUnaryExprOrTypeTraitExpr(TInfo, OpLoc, ExprKind, ArgRange);
  }

  Expr *ArgEx = (Expr *)TyOrEx;
  ExprResult Result = CreateUnaryExprOrTypeTraitExpr(ArgEx, OpLoc, ExprKind);
  return Result;
}

bool Sema::CheckAlignasTypeArgument(llvm::StringRef KWName,
                                    TypeSourceInfo *TInfo, SourceLocation OpLoc,
                                    SourceRange R) {
  if (!TInfo)
    return true;
  return CheckUnaryExprOrTypeTraitOperand(TInfo->getType(), OpLoc, R,
                                          UETT_AlignOf, KWName);
}

bool Sema::OnAlignasTypeArgument(llvm::StringRef KWName, ParsedType Ty,
                                 SourceLocation OpLoc, SourceRange R) {
  TypeSourceInfo *TInfo;
  (void)GetTypeFromParser(ParsedType::getFromOpaquePtr(Ty.getAsOpaquePtr()),
                          &TInfo);
  return CheckAlignasTypeArgument(KWName, TInfo, OpLoc, R);
}
