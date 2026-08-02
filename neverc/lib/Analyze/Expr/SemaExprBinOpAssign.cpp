//===- SemaExprBinOpAssign.cpp - Assignment & comma operators -------------===//
//
//===----------------------------------------------------------------------===//

#include "Expr/SemaExprUtils.h"
#include "Expr/TreeTransform.h"
#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaFixItUtils.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Analyze/SemaPluginHooks.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

using namespace neverc;
using namespace sema;

namespace {
bool areSameIdentityFieldExpr(const Expr *LHSExpr, const Expr *RHSExpr) {
  LHSExpr = LHSExpr->IgnoreParenImpCasts();
  RHSExpr = RHSExpr->IgnoreParenImpCasts();
  if (LHSExpr->getStmtClass() != RHSExpr->getStmtClass())
    return false;

  if (const auto *LHSDeclRef = dyn_cast<DeclRefExpr>(LHSExpr)) {
    const auto *RHSDeclRef = cast<DeclRefExpr>(RHSExpr);
    return LHSDeclRef->getDecl()->getCanonicalDecl() ==
           RHSDeclRef->getDecl()->getCanonicalDecl();
  }

  if (const auto *LHSMember = dyn_cast<MemberExpr>(LHSExpr)) {
    const auto *RHSMember = cast<MemberExpr>(RHSExpr);
    return LHSMember->getMemberDecl()->getCanonicalDecl() ==
               RHSMember->getMemberDecl()->getCanonicalDecl() &&
           areSameIdentityFieldExpr(LHSMember->getBase(),
                                    RHSMember->getBase());
  }

  return false;
}

void checkIdentityFieldAssignment(Expr *LHSExpr, Expr *RHSExpr,
                                  SourceLocation Loc, Sema &Sema) {
  if (Sema.isUnevaluatedContext())
    return;
  if (Loc.isInvalid() || Loc.isMacroID())
    return;
  if (LHSExpr->getExprLoc().isMacroID() || RHSExpr->getExprLoc().isMacroID())
    return;

  MemberExpr *ML = dyn_cast<MemberExpr>(LHSExpr->IgnoreParenImpCasts());
  MemberExpr *MR = dyn_cast<MemberExpr>(RHSExpr->IgnoreParenImpCasts());
  if (ML && MR) {
    const ValueDecl *LHSDecl =
        cast<ValueDecl>(ML->getMemberDecl()->getCanonicalDecl());
    const ValueDecl *RHSDecl =
        cast<ValueDecl>(MR->getMemberDecl()->getCanonicalDecl());
    if (LHSDecl != RHSDecl)
      return;
    if (LHSDecl->getType().isVolatileQualified())
      return;
    if (!areSameIdentityFieldExpr(ML, MR))
      return;

    Sema.Diag(Loc, diag::warn_identity_field_assign);
  }
}
} // namespace

// C99 6.5.16.1
// ===----------------------------------------------------------------------===
// Assignment & comma operators
// ===----------------------------------------------------------------------===

QualType Sema::CheckAssignmentOperands(Expr *LHSExpr, ExprResult &RHS,
                                       SourceLocation Loc,
                                       QualType CompoundType,
                                       BinaryOperatorKind Opc) {
  assert(!LHSExpr->hasPlaceholderType(BuiltinType::PseudoObject));

  // Verify that LHS is a modifiable lvalue, and emit error if not.
  if (checkForModifiableLvalue(LHSExpr, Loc, *this))
    return QualType();

  QualType LHSType = LHSExpr->getType();

  if (LLVM_LIKELY(CompoundType.isNull())) {
    QualType RHSType = RHS.get()->getType();
    if (LLVM_LIKELY(LHSType == RHSType)) {
      if (const auto *BT = LHSType->getAs<BuiltinType>()) {
        unsigned K = BT->getKind();
        if (LLVM_LIKELY(K == BuiltinType::Int || K == BuiltinType::UInt ||
                        K == BuiltinType::Long || K == BuiltinType::ULong ||
                        K == BuiltinType::Short || K == BuiltinType::UShort ||
                        K == BuiltinType::Char_U || K == BuiltinType::Char_S ||
                        K == BuiltinType::SChar || K == BuiltinType::UChar ||
                        K == BuiltinType::LongLong ||
                        K == BuiltinType::ULongLong)) {
          RHS = DefaultFunctionArrayLvalueConversion(RHS.get());
          if (RHS.isInvalid())
            return QualType();
          return LHSType.getAtomicUnqualifiedType();
        }
      }
    }

    Expr *RHSCheck = RHS.get();

    if (LLVM_UNLIKELY(!Diags.getIgnoreAllWarnings()))
      checkIdentityFieldAssignment(LHSExpr, RHSCheck, Loc, *this);

    QualType LHSTy(LHSType);
    AssignConvertType ConvTy = CheckSingleAssignmentConstraints(LHSTy, RHS);
    if (RHS.isInvalid())
      return QualType();
    if (LLVM_UNLIKELY(!Diags.getIgnoreAllWarnings())) {
      if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(RHSCheck))
        RHSCheck = ICE->getSubExpr();
      if (UnaryOperator *UO = dyn_cast<UnaryOperator>(RHSCheck)) {
        if ((UO->getOpcode() == UO_Plus || UO->getOpcode() == UO_Minus) &&
            Loc.isFileID() && UO->getOperatorLoc().isFileID() &&
            Loc.getLocWithOffset(1) == UO->getOperatorLoc() &&
            Loc.getLocWithOffset(2) != UO->getSubExpr()->getBeginLoc() &&
            UO->getSubExpr()->getBeginLoc().isFileID()) {
          Diag(Loc, diag::warn_not_compound_assign)
              << (UO->getOpcode() == UO_Plus ? "+" : "-")
              << SourceRange(UO->getOperatorLoc(), UO->getOperatorLoc());
        }
      }
    }

    if (LLVM_UNLIKELY(DiagnoseAssignmentResult(ConvTy, Loc, LHSType, RHSType,
                                               RHS.get(), AA_Assigning)))
      return QualType();
  } else {
    QualType RHSType = CompoundType;
    AssignConvertType ConvTy =
        CheckAssignmentConstraints(Loc, LHSType, RHSType);
    if (LLVM_UNLIKELY(DiagnoseAssignmentResult(ConvTy, Loc, LHSType, RHSType,
                                               RHS.get(), AA_Assigning)))
      return QualType();
  }

  if (LLVM_UNLIKELY(
          (!isa<DeclRefExpr, MemberExpr, ArraySubscriptExpr>(LHSExpr))))
    warnNullPtrDeref(*this, LHSExpr);

  return LHSType.getAtomicUnqualifiedType();
}

// Scenarios to ignore if expression E is:
// 1. an explicit cast expression into void
// 2. a function call expression that returns void
namespace {
bool shouldIgnoreCommaOperand(const Expr *E, const TreeContext &Context) {
  E = E->IgnoreParens();

  if (const CastExpr *CE = dyn_cast<CastExpr>(E)) {
    if (CE->getCastKind() == CK_ToVoid) {
      return true;
    }
  }

  if (const auto *CE = dyn_cast<CallExpr>(E))
    return CE->getCallReturnType(Context)->isVoidType();
  return false;
}
} // namespace

// Look for instances where it is likely the comma operator is confused with
// another operator.  There is an explicit list of acceptable expressions for
// the left hand side of the comma operator, otherwise emit a warning.
void Sema::DiagnoseCommaOperator(const Expr *LHS, SourceLocation Loc) {
  // No warnings in macros
  if (Loc.isMacroID())
    return;

  // Scope isn't fine-grained enough to explicitly list the specific cases, so
  // instead, skip more than needed, then call back into here with the
  // CommaVisitor in SemaStmt.cpp.
  // The listed locations are the initialization and increment portions
  // of a for loop.  The additional checks are on the condition of
  // if statements, do/while loops, and for loops.
  // Differences in scope flags for C89 mode requires the extra logic.
  const unsigned ForIncrementFlags =
      getLangOpts().C99
          ? Scope::ControlScope | Scope::ContinueScope | Scope::BreakScope
          : Scope::ContinueScope | Scope::BreakScope;
  const unsigned ForInitFlags = Scope::ControlScope | Scope::DeclScope;
  const unsigned ScopeFlags = getCurScope()->getFlags();
  if ((ScopeFlags & ForIncrementFlags) == ForIncrementFlags ||
      (ScopeFlags & ForInitFlags) == ForInitFlags)
    return;

  // If there are multiple comma operators used together, get the RHS of the
  // of the comma operator as the LHS.
  while (const BinaryOperator *BO = dyn_cast<BinaryOperator>(LHS)) {
    if (BO->getOpcode() != BO_Comma)
      break;
    LHS = BO->getRHS();
  }

  // Only allow some expressions on LHS to not warn.
  if (shouldIgnoreCommaOperand(LHS, Context))
    return;

  Diag(Loc, diag::warn_comma_operator);
  Diag(LHS->getBeginLoc(), diag::note_cast_to_void)
      << LHS->getSourceRange()
      << FixItHint::CreateInsertion(LHS->getBeginLoc(), "(void)(")
      << FixItHint::CreateInsertion(PP.getLocForEndOfToken(LHS->getEndLoc()),
                                    ")");
}

// C99 6.5.17
namespace {
QualType checkCommaOperands(Sema &S, ExprResult &LHS, ExprResult &RHS,
                            SourceLocation Loc) {
  LHS = S.CheckPlaceholderExpr(LHS.get());
  RHS = S.CheckPlaceholderExpr(RHS.get());
  if (LHS.isInvalid() || RHS.isInvalid())
    return QualType();

  // Comma: discard the LHS value, then complete the RHS type for checks.
  LHS = S.IgnoredValueConversions(LHS.get());
  if (LHS.isInvalid())
    return QualType();
#ifndef _WIN32
  S.DiagnoseUnusedExprResult(LHS.get(), diag::warn_unused_comma_left_operand);
#endif
  RHS = S.DefaultFunctionArrayLvalueConversion(RHS.get());
  if (RHS.isInvalid())
    return QualType();
  if (!RHS.get()->getType()->isVoidType())
    S.RequireCompleteType(Loc, RHS.get()->getType(), diag::err_incomplete_type);

  if (!S.getDiagnostics().isIgnored(diag::warn_comma_operator, Loc))
    S.DiagnoseCommaOperator(LHS.get(), Loc);

  return RHS.get()->getType();
}
} // namespace


namespace {
ValueDecl *getPrimaryDecl(Expr *E) {
  switch (E->getStmtClass()) {
  case Stmt::DeclRefExprClass:
    return cast<DeclRefExpr>(E)->getDecl();
  case Stmt::MemberExprClass:
    // If this is an arrow operator, the address is an offset from
    // the base's value, so the object the base refers to is
    // irrelevant.
    if (cast<MemberExpr>(E)->isArrow())
      return nullptr;
    // Otherwise, the expression refers to a part of the base
    return getPrimaryDecl(cast<MemberExpr>(E)->getBase());
  case Stmt::ArraySubscriptExprClass: {
    Expr *Base = cast<ArraySubscriptExpr>(E)->getBase();
    if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Base)) {
      if (ICE->getSubExpr()->getType()->isArrayType())
        return getPrimaryDecl(ICE->getSubExpr());
    }
    return nullptr;
  }
  case Stmt::UnaryOperatorClass: {
    UnaryOperator *UO = cast<UnaryOperator>(E);

    switch (UO->getOpcode()) {
    case UO_Real:
    case UO_Imag:
    case UO_Extension:
      return getPrimaryDecl(UO->getSubExpr());
    default:
      return nullptr;
    }
  }
  case Stmt::ParenExprClass:
    return getPrimaryDecl(cast<ParenExpr>(E)->getSubExpr());
  case Stmt::ImplicitCastExprClass:
    // If the result of an implicit cast is an l-value, we care about
    // the sub-expression; otherwise, the result here doesn't matter.
    return getPrimaryDecl(cast<ImplicitCastExpr>(E)->getSubExpr());
  default:
    return nullptr;
  }
}
} // namespace

namespace {
enum {
  AO_Bit_Field = 0,
  AO_Vector_Element = 1,
  AO_Property_Expansion = 2,
  AO_Register_Variable = 3,
  AO_Matrix_Element = 4,
  AO_No_Error = 5
};
}
namespace {
LLVM_ATTRIBUTE_NOINLINE
void warnAddressOfInvalidType(Sema &S, SourceLocation Loc, Expr *E,
                              unsigned Type) {
  S.Diag(Loc, diag::err_typecheck_address_of) << Type << E->getSourceRange();
}
} // namespace

QualType Sema::CheckAddressOfOperand(ExprResult &OrigOp, SourceLocation OpLoc) {
  if (OrigOp.get()->getType()->getAsPlaceholderType()) {
    OrigOp = CheckPlaceholderExpr(OrigOp.get());
    if (OrigOp.isInvalid())
      return QualType();
  }

  if (OrigOp.get()->isTypeDependent())
    return Context.DependentTy;

  assert(!OrigOp.get()->hasPlaceholderType());

  // Make sure to ignore parentheses in subsequent checks
  Expr *op = OrigOp.get()->IgnoreParens();

  if (getLangOpts().C99) {
    // Implement C99-only parts of addressof rules.
    if (UnaryOperator *uOp = dyn_cast<UnaryOperator>(op)) {
      if (uOp->getOpcode() == UO_Deref)
        // Per C99 6.5.3.2, the address of a deref always returns a valid result
        // (assuming the deref expression is valid).
        return uOp->getSubExpr()->getType();
    }
    // Technically, there should be a check for array subscript
    // expressions here, but the result of one is always an lvalue anyway.
  }
  ValueDecl *dcl = getPrimaryDecl(op);

  if (auto *FD = dyn_cast_or_null<FunctionDecl>(dcl))
    if (!checkAddressOfFunctionIsAvailable(FD, /*Complain=*/true,
                                           op->getBeginLoc()))
      return QualType();

  Expr::LValueClassification lval = op->ClassifyLValue(Context);
  unsigned AddressOfError = AO_No_Error;

  if (lval != Expr::LV_Valid && lval != Expr::LV_IncompleteVoidType) {
    // C99 6.5.3.2p1
    // The operand must be either an l-value or a function designator
    if (!op->getType()->isFunctionType()) {
      Diag(OpLoc, diag::err_typecheck_invalid_lvalue_addrof)
          << op->getType() << op->getSourceRange();
      return QualType();
    }

  } else if (op->getObjectKind() == OK_BitField) { // C99 6.5.3.2p1
    // The operand cannot be a bit-field
    AddressOfError = AO_Bit_Field;
  } else if (op->getObjectKind() == OK_VectorComponent) {
    // The operand cannot be an element of a vector
    AddressOfError = AO_Vector_Element;
  } else if (op->getObjectKind() == OK_MatrixComponent) {
    // The operand cannot be an element of a matrix.
    AddressOfError = AO_Matrix_Element;
  } else if (dcl) { // C99 6.5.3.2p1
    // We have an lvalue with a decl. Make sure the decl is not declared
    // with the register storage-class specifier.
    if (const VarDecl *vd = dyn_cast<VarDecl>(dcl)) {
      if (vd->getStorageClass() == SC_Register)
        AddressOfError = AO_Register_Variable;
    } else if (isa<FieldDecl>(dcl) || isa<IndirectFieldDecl>(dcl)) {
      // Okay: we can take the address of a field.
    } else if (!isa<FunctionDecl>(dcl))
      llvm_unreachable("Unknown/unexpected decl type");
  }

  if (AddressOfError != AO_No_Error) {
    warnAddressOfInvalidType(*this, OpLoc, op, AddressOfError);
    return QualType();
  }

  if (lval == Expr::LV_IncompleteVoidType) {
    // Taking the address of a void variable is technically illegal, but we
    // allow it in cases which are otherwise valid.
    // Example: "extern void x; void* y = &x;".
    Diag(OpLoc, diag::ext_typecheck_addrof_void) << op->getSourceRange();
  }

  CheckAddressOfPackedMember(op);

  return Context.getPointerType(op->getType());
}

namespace neverc {
void noteModifiableNonNullParam(Sema &S, const Expr *Exp) {
  const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Exp);
  if (!DRE)
    return;
  const Decl *D = DRE->getDecl();
  if (!D)
    return;
  const ParmVarDecl *Param = dyn_cast<ParmVarDecl>(D);
  if (!Param)
    return;
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(Param->getDeclContext()))
    if (!FD->hasAttr<NonNullAttr>() && !Param->hasAttr<NonNullAttr>())
      return;
  if (FunctionScopeInfo *FD = S.getCurFunction())
    FD->ModifiedNonNullParams.insert(Param);
}
} // namespace

namespace neverc {
QualType checkIndirectionOperand(Sema &S, Expr *Op, ExprValueKind &VK,
                                 SourceLocation OpLoc,
                                 bool IsAfterAmp) {
  if (Op->isTypeDependent())
    return S.Context.DependentTy;

  ExprResult ConvResult = S.UsualUnaryConversions(Op);
  if (ConvResult.isInvalid())
    return QualType();
  Op = ConvResult.get();
  QualType OpTy = Op->getType();
  QualType Result;

  if (const PointerType *PT = OpTy->getAs<PointerType>()) {
    Result = PT->getPointeeType();
  } else {
    ExprResult PR = S.CheckPlaceholderExpr(Op);
    if (PR.isInvalid())
      return QualType();
    if (PR.get() != Op)
      return checkIndirectionOperand(S, PR.get(), VK, OpLoc);
  }

  if (Result.isNull()) {
    S.Diag(OpLoc, diag::err_typecheck_indirection_requires_pointer)
        << OpTy << Op->getSourceRange();
    return QualType();
  }

  if (Result->isVoidType()) {
    if (!(S.getLangOpts().C99 && IsAfterAmp) && !S.isUnevaluatedContext())
      S.Diag(OpLoc, diag::ext_typecheck_indirection_through_void_pointer)
          << OpTy << Op->getSourceRange();
  }

  // Dereferences are usually l-values...
  VK = VK_LValue;

  if (Result.isCForbiddenLValueType())
    VK = VK_PRValue;

  return Result;
}
} // namespace

BinaryOperatorKind Sema::ConvertTokenKindToBinaryOpcode(tok::TokenKind Kind) {
  BinaryOperatorKind Opc;
  switch (Kind) {
  default:
    llvm_unreachable("Unknown binop!");
  case tok::star:
    Opc = BO_Mul;
    break;
  case tok::slash:
    Opc = BO_Div;
    break;
  case tok::percent:
    Opc = BO_Rem;
    break;
  case tok::plus:
    Opc = BO_Add;
    break;
  case tok::minus:
    Opc = BO_Sub;
    break;
  case tok::lessless:
    Opc = BO_Shl;
    break;
  case tok::greatergreater:
    Opc = BO_Shr;
    break;
  case tok::lessequal:
    Opc = BO_LE;
    break;
  case tok::less:
    Opc = BO_LT;
    break;
  case tok::greaterequal:
    Opc = BO_GE;
    break;
  case tok::greater:
    Opc = BO_GT;
    break;
  case tok::exclaimequal:
    Opc = BO_NE;
    break;
  case tok::equalequal:
    Opc = BO_EQ;
    break;
  case tok::amp:
    Opc = BO_And;
    break;
  case tok::caret:
    Opc = BO_Xor;
    break;
  case tok::pipe:
    Opc = BO_Or;
    break;
  case tok::ampamp:
    Opc = BO_LAnd;
    break;
  case tok::pipepipe:
    Opc = BO_LOr;
    break;
  case tok::equal:
    Opc = BO_Assign;
    break;
  case tok::starequal:
    Opc = BO_MulAssign;
    break;
  case tok::slashequal:
    Opc = BO_DivAssign;
    break;
  case tok::percentequal:
    Opc = BO_RemAssign;
    break;
  case tok::plusequal:
    Opc = BO_AddAssign;
    break;
  case tok::minusequal:
    Opc = BO_SubAssign;
    break;
  case tok::lesslessequal:
    Opc = BO_ShlAssign;
    break;
  case tok::greatergreaterequal:
    Opc = BO_ShrAssign;
    break;
  case tok::ampequal:
    Opc = BO_AndAssign;
    break;
  case tok::caretequal:
    Opc = BO_XorAssign;
    break;
  case tok::pipeequal:
    Opc = BO_OrAssign;
    break;
  case tok::comma:
    Opc = BO_Comma;
    break;
  }
  return Opc;
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseSelfAssignment(Sema &S, Expr *LHSExpr, Expr *RHSExpr,
                            SourceLocation OpLoc) {
  if (S.isUnevaluatedContext())
    return;
  if (OpLoc.isInvalid() || OpLoc.isMacroID())
    return;
  LHSExpr = LHSExpr->IgnoreParenImpCasts();
  RHSExpr = RHSExpr->IgnoreParenImpCasts();
  const DeclRefExpr *LHSDeclRef = dyn_cast<DeclRefExpr>(LHSExpr);
  const DeclRefExpr *RHSDeclRef = dyn_cast<DeclRefExpr>(RHSExpr);
  if (!LHSDeclRef || !RHSDeclRef || LHSDeclRef->getLocation().isMacroID() ||
      RHSDeclRef->getLocation().isMacroID())
    return;
  const ValueDecl *LHSDecl =
      cast<ValueDecl>(LHSDeclRef->getDecl()->getCanonicalDecl());
  const ValueDecl *RHSDecl =
      cast<ValueDecl>(RHSDeclRef->getDecl()->getCanonicalDecl());
  if (LHSDecl != RHSDecl)
    return;
  if (LHSDecl->getType().isVolatileQualified())
    return;
  return;

  S.Diag(OpLoc, diag::warn_self_assignment_builtin)
      << LHSDeclRef->getType() << LHSExpr->getSourceRange()
      << RHSExpr->getSourceRange() << 0;
}
} // namespace

// This helper function promotes a binary operator's operands (which are of a
// half vector type) to a vector of floats and then truncates the result to
// a vector of either half or short.
namespace {
ExprResult emitHalfVecBinOp(Sema &S, ExprResult LHS, ExprResult RHS,
                            BinaryOperatorKind Opc, QualType ResultTy,
                            ExprValueKind VK, ExprObjectKind OK,
                            bool IsCompAssign, SourceLocation OpLoc,
                            FPOptionsOverride FPFeatures) {
  auto &Context = S.getTreeContext();
  assert((isVector(ResultTy, Context.HalfTy) ||
          isVector(ResultTy, Context.ShortTy)) &&
         "Result must be a vector of half or short");
  assert(isVector(LHS.get()->getType(), Context.HalfTy) &&
         isVector(RHS.get()->getType(), Context.HalfTy) &&
         "both operands expected to be a half vector");

  RHS = castVectorElement(RHS.get(), Context.FloatTy, S);
  QualType BinOpResTy = RHS.get()->getType();

  // If Opc is a comparison, ResultType is a vector of shorts. In that case,
  // change BinOpResTy to a vector of ints.
  if (isVector(ResultTy, Context.ShortTy))
    BinOpResTy = S.GetSignedVectorType(BinOpResTy);

  if (IsCompAssign)
    return CompoundAssignOperator::Create(Context, LHS.get(), RHS.get(), Opc,
                                          ResultTy, VK, OK, OpLoc, FPFeatures,
                                          BinOpResTy, BinOpResTy);

  LHS = castVectorElement(LHS.get(), Context.FloatTy, S);
  auto *BO = BinaryOperator::Create(Context, LHS.get(), RHS.get(), Opc,
                                    BinOpResTy, VK, OK, OpLoc, FPFeatures);
  return castVectorElement(BO, ResultTy->castAs<VectorType>()->getElementType(),
                           S);
}
} // namespace

namespace neverc {
bool requiresHalfVecConversion(bool OpRequiresConversion, TreeContext &Ctx,
                               Expr *E0, Expr *E1) {
  if (!OpRequiresConversion || Ctx.getLangOpts().NativeHalfType ||
      Ctx.getTargetInfo().useFP16ConversionIntrinsics())
    return false;

  auto HasVectorOfHalfType = [&Ctx](Expr *E) {
    QualType Ty = E->IgnoreImplicit()->getType();

    if (const VectorType *VT = Ty->getAs<VectorType>()) {
      if (VT->getVectorKind() == VectorKind::Neon)
        return false;
      return VT->getElementType().getCanonicalType() == Ctx.HalfTy;
    }
    return false;
  };

  return HasVectorOfHalfType(E0) && (!E1 || HasVectorOfHalfType(E1));
}
} // namespace

NEVERC_HOT ExprResult Sema::CreateBuiltinBinOp(SourceLocation OpLoc,
                                                         BinaryOperatorKind Opc,
                                                         Expr *LHSExpr,
                                                         Expr *RHSExpr) {
  {
    bool FastPathOpc;
    if (LLVM_LIKELY(Diags.getIgnoreAllWarnings()))
      FastPathOpc = (Opc >= BO_Mul && Opc <= BO_Or) || Opc == BO_Assign;
    else
      FastPathOpc =
          (Opc == BO_Mul || Opc == BO_Add || Opc == BO_Sub || Opc == BO_And ||
           Opc == BO_Xor || Opc == BO_Or || Opc == BO_Assign);
    if (LLVM_LIKELY(FastPathOpc)) {
      QualType LTy = LHSExpr->getType(), RTy = RHSExpr->getType();
      if (LLVM_LIKELY(LTy == RTy)) {
        if (const auto *BT = LTy->getAs<BuiltinType>()) {
          unsigned K = BT->getKind();
          if (LLVM_LIKELY(K == BuiltinType::Int || K == BuiltinType::UInt ||
                          K == BuiltinType::Long || K == BuiltinType::ULong ||
                          K == BuiltinType::LongLong ||
                          K == BuiltinType::ULongLong)) {
            QualType ResultTy = LTy.getUnqualifiedType();
            FPOptionsOverride FPO = CurFPFeatureOverrides();
            if (LLVM_UNLIKELY(Opc == BO_Assign)) {
              if (LLVM_LIKELY(LHSExpr->isLValue() && !LTy.isConstQualified() &&
                              !LTy.isVolatileQualified())) {
                ExprResult RHS = RHSExpr;
                if (RHSExpr->isLValue())
                  RHS = ImplicitCastExpr::Create(Context, ResultTy,
                                                 CK_LValueToRValue, RHSExpr,
                                                 VK_PRValue, FPO);
                return BinaryOperator::Create(Context, LHSExpr, RHS.get(), Opc,
                                              ResultTy, VK_PRValue, OK_Ordinary,
                                              OpLoc, FPO);
              }
            } else {
              ExprResult LHS = LHSExpr, RHS = RHSExpr;
              if (LHSExpr->isLValue())
                LHS = ImplicitCastExpr::Create(Context, ResultTy,
                                               CK_LValueToRValue, LHSExpr,
                                               VK_PRValue, FPO);
              if (RHSExpr->isLValue())
                RHS = ImplicitCastExpr::Create(Context, ResultTy,
                                               CK_LValueToRValue, RHSExpr,
                                               VK_PRValue, FPO);
              QualType BinOpTy = BinaryOperator::isComparisonOp(Opc)
                                     ? Context.getLogicalOperationType()
                                     : ResultTy;
              return BinaryOperator::Create(Context, LHS.get(), RHS.get(), Opc,
                                            BinOpTy, VK_PRValue, OK_Ordinary,
                                            OpLoc, FPO);
            }
          }
        }
      }
    }
  }

  ExprResult LHS = LHSExpr, RHS = RHSExpr;
  QualType ResultTy; // Result type of the binary operator.
  // The following two variables are used for compound assignment operators
  QualType CompLHSTy;    // Type of LHS after promotions for computation
  QualType CompResultTy; // Type of computation result
  ExprValueKind VK = VK_PRValue;
  ExprObjectKind OK = OK_Ordinary;
  bool ConvertHalfVec = false;

  {
    QualType LTy = LHSExpr->getType(), RTy = RHSExpr->getType();
    if (LLVM_UNLIKELY(!LTy->isIntegerType() && !LTy->isPointerType() &&
                      !LTy->isRealFloatingType()))
      checkTypeSupport(LTy, OpLoc, /*ValueDecl*/ nullptr);
    if (LLVM_UNLIKELY(!RTy->isIntegerType() && !RTy->isPointerType() &&
                      !RTy->isRealFloatingType()))
      checkTypeSupport(RTy, OpLoc, /*ValueDecl*/ nullptr);
  }

  switch (Opc) {
  case BO_Assign:
    ResultTy = CheckAssignmentOperands(LHS.get(), RHS, OpLoc, QualType(), Opc);
    if (LLVM_UNLIKELY(!Diags.getIgnoreAllWarnings() && !ResultTy.isNull())) {
      diagnoseSelfAssignment(*this, LHS.get(), RHS.get(), OpLoc);
      noteModifiableNonNullParam(*this, LHS.get());
    }
    break;
  case BO_Mul:
  case BO_Div:
    ConvertHalfVec = true;
    ResultTy =
        CheckMultiplyDivideOperands(LHS, RHS, OpLoc, false, Opc == BO_Div);
    break;
  case BO_Rem:
    ResultTy = CheckRemainderOperands(LHS, RHS, OpLoc);
    break;
  case BO_Add:
    ConvertHalfVec = true;
    ResultTy = CheckAdditionOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_Sub:
    ConvertHalfVec = true;
    ResultTy = CheckSubtractionOperands(LHS, RHS, OpLoc);
    break;
  case BO_Shl:
  case BO_Shr:
    ResultTy = CheckShiftOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_LE:
  case BO_LT:
  case BO_GE:
  case BO_GT:
    ConvertHalfVec = true;
    ResultTy = CheckCompareOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_EQ:
  case BO_NE:
    ConvertHalfVec = true;
    ResultTy = CheckCompareOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_And:
  case BO_Xor:
  case BO_Or:
    ResultTy = CheckBitwiseOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_LAnd:
  case BO_LOr:
    ConvertHalfVec = true;
    ResultTy = CheckLogicalOperands(LHS, RHS, OpLoc, Opc);
    break;
  case BO_MulAssign:
  case BO_DivAssign:
    ConvertHalfVec = true;
    CompResultTy =
        CheckMultiplyDivideOperands(LHS, RHS, OpLoc, true, Opc == BO_DivAssign);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_RemAssign:
    CompResultTy = CheckRemainderOperands(LHS, RHS, OpLoc, true);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_AddAssign:
    ConvertHalfVec = true;
    CompResultTy = CheckAdditionOperands(LHS, RHS, OpLoc, Opc, &CompLHSTy);
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_SubAssign:
    ConvertHalfVec = true;
    CompResultTy = CheckSubtractionOperands(LHS, RHS, OpLoc, &CompLHSTy);
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_ShlAssign:
  case BO_ShrAssign:
    CompResultTy = CheckShiftOperands(LHS, RHS, OpLoc, Opc, true);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_AndAssign:
  case BO_OrAssign: // fallthrough
    if (LLVM_UNLIKELY(!Diags.getIgnoreAllWarnings()))
      diagnoseSelfAssignment(*this, LHS.get(), RHS.get(), OpLoc);
    [[fallthrough]];
  case BO_XorAssign:
    CompResultTy = CheckBitwiseOperands(LHS, RHS, OpLoc, Opc);
    CompLHSTy = CompResultTy;
    if (!CompResultTy.isNull() && !LHS.isInvalid() && !RHS.isInvalid())
      ResultTy =
          CheckAssignmentOperands(LHS.get(), RHS, OpLoc, CompResultTy, Opc);
    break;
  case BO_Comma:
    ResultTy = checkCommaOperands(*this, LHS, RHS, OpLoc);
    break;
  }
  if (ResultTy.isNull() || LHS.isInvalid() || RHS.isInvalid())
    return ExprError();

  // Some of the binary operations require promoting operands of half vector to
  // float vectors and truncating the result back to half vector. For now, we do
  // this only when HalfArgsAndReturn is set (that is, when the target is arm or
  // arm64).
  assert(
      (Opc == BO_Comma || isVector(RHS.get()->getType(), Context.HalfTy) ==
                              isVector(LHS.get()->getType(), Context.HalfTy)) &&
      "both sides are half vectors or neither sides are");
  ConvertHalfVec =
      requiresHalfVecConversion(ConvertHalfVec, Context, LHS.get(), RHS.get());

  {
    auto SC = LHS.get()->getStmtClass();
    if (LLVM_UNLIKELY(SC != Stmt::BinaryOperatorClass &&
                      SC != Stmt::CompoundAssignOperatorClass &&
                      SC != Stmt::IntegerLiteralClass &&
                      SC != Stmt::FloatingLiteralClass &&
                      SC != Stmt::CharacterLiteralClass &&
                      SC != Stmt::DeclRefExprClass &&
                      SC != Stmt::CallExprClass))
      CheckArrayAccess(LHS.get());
    SC = RHS.get()->getStmtClass();
    if (LLVM_UNLIKELY(SC != Stmt::BinaryOperatorClass &&
                      SC != Stmt::CompoundAssignOperatorClass &&
                      SC != Stmt::IntegerLiteralClass &&
                      SC != Stmt::FloatingLiteralClass &&
                      SC != Stmt::CharacterLiteralClass &&
                      SC != Stmt::DeclRefExprClass &&
                      SC != Stmt::CallExprClass))
      CheckArrayAccess(RHS.get());
  }

  // Opc is not a compound assignment if CompResultTy is null.
  if (CompResultTy.isNull()) {
    if (ConvertHalfVec)
      return emitHalfVecBinOp(*this, LHS, RHS, Opc, ResultTy, VK, OK, false,
                              OpLoc, CurFPFeatureOverrides());
    return BinaryOperator::Create(Context, LHS.get(), RHS.get(), Opc, ResultTy,
                                  VK, OK, OpLoc, CurFPFeatureOverrides());
  }

  // The LHS is not converted to the result type for fixed-point compound
  // assignment as the common type is computed on demand. Reset the CompLHSTy
  // to the LHS type we would have gotten after unary conversions.
  if (CompResultTy->isFixedPointType())
    CompLHSTy = UsualUnaryConversions(LHS.get()).get()->getType();

  if (ConvertHalfVec)
    return emitHalfVecBinOp(*this, LHS, RHS, Opc, ResultTy, VK, OK, true, OpLoc,
                            CurFPFeatureOverrides());

  return CompoundAssignOperator::Create(
      Context, LHS.get(), RHS.get(), Opc, ResultTy, VK, OK, OpLoc,
      CurFPFeatureOverrides(), CompLHSTy, CompResultTy);
}

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseBitwisePrecedence(Sema &Self, BinaryOperatorKind Opc,
                               SourceLocation OpLoc, Expr *LHSExpr,
                               Expr *RHSExpr) {
  BinaryOperator *LHSBO = dyn_cast<BinaryOperator>(LHSExpr);
  BinaryOperator *RHSBO = dyn_cast<BinaryOperator>(RHSExpr);

  // Check that one of the sides is a comparison operator and the other isn't.
  bool isLeftComp = LHSBO && LHSBO->isComparisonOp();
  bool isRightComp = RHSBO && RHSBO->isComparisonOp();
  if (isLeftComp == isRightComp)
    return;

  // Bitwise operations are sometimes used as eager logical ops.
  // Don't diagnose this.
  bool isLeftBitwise = LHSBO && LHSBO->isBitwiseOp();
  bool isRightBitwise = RHSBO && RHSBO->isBitwiseOp();
  if (isLeftBitwise || isRightBitwise)
    return;

  SourceRange DiagRange = isLeftComp
                              ? SourceRange(LHSExpr->getBeginLoc(), OpLoc)
                              : SourceRange(OpLoc, RHSExpr->getEndLoc());
  llvm::StringRef OpStr =
      isLeftComp ? LHSBO->getOpcodeStr() : RHSBO->getOpcodeStr();
  SourceRange ParensRange =
      isLeftComp
          ? SourceRange(LHSBO->getRHS()->getBeginLoc(), RHSExpr->getEndLoc())
          : SourceRange(LHSExpr->getBeginLoc(), RHSBO->getLHS()->getEndLoc());

  Self.Diag(OpLoc, diag::warn_precedence_bitwise_rel)
      << DiagRange << BinaryOperator::getOpcodeStr(Opc) << OpStr;
  suggestParentheses(Self, OpLoc,
                     Self.PDiag(diag::note_precedence_silence) << OpStr,
                     (isLeftComp ? LHSExpr : RHSExpr)->getSourceRange());
  suggestParentheses(Self, OpLoc,
                     Self.PDiag(diag::note_precedence_bitwise_first)
                         << BinaryOperator::getOpcodeStr(Opc),
                     ParensRange);
}
} // namespace

namespace {
void genDiagnosticForLogicalAndInLogicalOr(Sema &Self, SourceLocation OpLoc,
                                           BinaryOperator *Bop) {
  assert(Bop->getOpcode() == BO_LAnd);
  Self.Diag(Bop->getOperatorLoc(), diag::warn_logical_and_in_logical_or)
      << Bop->getSourceRange() << OpLoc;
  suggestParentheses(Self, Bop->getOperatorLoc(),
                     Self.PDiag(diag::note_precedence_silence)
                         << Bop->getOpcodeStr(),
                     Bop->getSourceRange());
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseLogicalAndInLogicalOrLHS(Sema &S, SourceLocation OpLoc,
                                      Expr *LHSExpr, Expr *RHSExpr) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(LHSExpr)) {
    if (Bop->getOpcode() == BO_LAnd) {
      // If it's "string_literal && a || b" don't warn since the precedence
      // doesn't matter.
      if (!isa<StringLiteral>(Bop->getLHS()->IgnoreParenImpCasts()))
        return genDiagnosticForLogicalAndInLogicalOr(S, OpLoc, Bop);
    } else if (Bop->getOpcode() == BO_LOr) {
      if (BinaryOperator *RBop = dyn_cast<BinaryOperator>(Bop->getRHS())) {
        // If it's "a || b && string_literal || c" we didn't warn earlier for
        // "a || b && string_literal", but warn now.
        if (RBop->getOpcode() == BO_LAnd &&
            isa<StringLiteral>(RBop->getRHS()->IgnoreParenImpCasts()))
          return genDiagnosticForLogicalAndInLogicalOr(S, OpLoc, RBop);
      }
    }
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseLogicalAndInLogicalOrRHS(Sema &S, SourceLocation OpLoc,
                                      Expr *LHSExpr, Expr *RHSExpr) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(RHSExpr)) {
    if (Bop->getOpcode() == BO_LAnd) {
      // If it's "a || b && string_literal" don't warn since the precedence
      // doesn't matter.
      if (!isa<StringLiteral>(Bop->getRHS()->IgnoreParenImpCasts()))
        return genDiagnosticForLogicalAndInLogicalOr(S, OpLoc, Bop);
    }
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseBitwiseOpInBitwiseOp(Sema &S, BinaryOperatorKind Opc,
                                  SourceLocation OpLoc, Expr *SubExpr) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(SubExpr)) {
    if (Bop->isBitwiseOp() && Bop->getOpcode() < Opc) {
      S.Diag(Bop->getOperatorLoc(), diag::warn_bitwise_op_in_bitwise_op)
          << Bop->getOpcodeStr() << BinaryOperator::getOpcodeStr(Opc)
          << Bop->getSourceRange() << OpLoc;
      suggestParentheses(S, Bop->getOperatorLoc(),
                         S.PDiag(diag::note_precedence_silence)
                             << Bop->getOpcodeStr(),
                         Bop->getSourceRange());
    }
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseAdditionInShift(Sema &S, SourceLocation OpLoc, Expr *SubExpr,
                             llvm::StringRef Shift) {
  if (BinaryOperator *Bop = dyn_cast<BinaryOperator>(SubExpr)) {
    if (Bop->getOpcode() == BO_Add || Bop->getOpcode() == BO_Sub) {
      llvm::StringRef Op = Bop->getOpcodeStr();
      S.Diag(Bop->getOperatorLoc(), diag::warn_addition_in_bitshift)
          << Bop->getSourceRange() << OpLoc << Shift << Op;
      suggestParentheses(S, Bop->getOperatorLoc(),
                         S.PDiag(diag::note_precedence_silence) << Op,
                         Bop->getSourceRange());
    }
  }
}
} // namespace

namespace {
LLVM_ATTRIBUTE_NOINLINE
void diagnoseBinOpPrecedence(Sema &Self, BinaryOperatorKind Opc,
                             SourceLocation OpLoc, Expr *LHSExpr,
                             Expr *RHSExpr) {
  // Diagnose "arg1 'bitwise' arg2 'eq' arg3".
  if (BinaryOperator::isBitwiseOp(Opc))
    diagnoseBitwisePrecedence(Self, Opc, OpLoc, LHSExpr, RHSExpr);

  // Diagnose "arg1 & arg2 | arg3"
  if ((Opc == BO_Or || Opc == BO_Xor) &&
      !OpLoc.isMacroID() /* Don't warn in macros. */) {
    diagnoseBitwiseOpInBitwiseOp(Self, Opc, OpLoc, LHSExpr);
    diagnoseBitwiseOpInBitwiseOp(Self, Opc, OpLoc, RHSExpr);
  }

  // Warn about arg1 || arg2 && arg3, as GCC 4.3+ does.
  // We don't warn for 'assert(a || b && "bad")' since this is safe.
  if (Opc == BO_LOr && !OpLoc.isMacroID() /* Don't warn in macros. */) {
    diagnoseLogicalAndInLogicalOrLHS(Self, OpLoc, LHSExpr, RHSExpr);
    diagnoseLogicalAndInLogicalOrRHS(Self, OpLoc, LHSExpr, RHSExpr);
  }

  if ((Opc == BO_Shl &&
       LHSExpr->getType()->isIntegralType(Self.getTreeContext())) ||
      Opc == BO_Shr) {
    llvm::StringRef Shift = BinaryOperator::getOpcodeStr(Opc);
    diagnoseAdditionInShift(Self, OpLoc, LHSExpr, Shift);
    diagnoseAdditionInShift(Self, OpLoc, RHSExpr, Shift);
  }
}
} // namespace

namespace {
ExprResult buildNeverCStringAssign(Sema &S, Scope *Sc, SourceLocation OpLoc,
                                   Expr *LHS, Expr *RHS) {
  if (checkForModifiableLvalue(LHS, OpLoc, S))
    return ExprError();

  ExprResult LHSAddr = S.FormUnaryOp(Sc, OpLoc, UO_AddrOf, LHS);
  if (LHSAddr.isInvalid())
    return ExprError();

  Expr *Args[] = {LHSAddr.get(), RHS};
  return buildNeverCStringRuntimeCall(
      S, Sc, OpLoc, BuiltinStringNames::AssignFunctionName, Args, OpLoc);
}
} // namespace

namespace {
Expr *promoteCharToNeverCString(Sema &S, SourceLocation OpLoc, Expr *E) {
  if (!E)
    return nullptr;
  QualType T = E->getType();
  if (!T->isIntegerType() || S.isNeverCStringType(T))
    return nullptr;
  // Reject `_Bool` so `s + true` is still a hard error -- the
  // canonical std::string-parity surface only accepts char-like
  // integers (the from_char helper truncates the value to a byte).
  if (T->isBooleanType())
    return nullptr;
  ExprResult Lvalue = S.DefaultLvalueConversion(E);
  if (Lvalue.isInvalid())
    return nullptr;
  // Truncate to `char` so the wrapped value matches the runtime
  // helper signature; mirrors the implicit conversion C performs
  // for `char x = some_int;` itself.
  ExprResult Truncated = Lvalue;
  if (Truncated.get()->getType() != S.Context.CharTy)
    Truncated =
        S.ImpCastExprToType(Truncated.get(), S.Context.CharTy, CK_IntegralCast);
  if (Truncated.isInvalid())
    return nullptr;
  Expr *Args[] = {Truncated.get()};
  ExprResult Wrapped = buildNeverCStringRuntimeCall(
      S, /*Scope=*/nullptr, OpLoc, BuiltinStringNames::FromCharFunctionName,
      Args, OpLoc);
  if (Wrapped.isInvalid())
    return nullptr;
  return Wrapped.get();
}
} // namespace

namespace {
std::optional<ExprResult> tryNeverCStringBinaryOpRewrite(Sema &S, Scope *Sc,
                                                         SourceLocation OpLoc,
                                                         BinaryOperatorKind Opc,
                                                         Expr *LHS, Expr *RHS) {
  // Fast reject: only assign / add-assign / add / comparison ops can be
  // NeverC string rewrites, and at least one operand must look like it
  // could be a record or string-literal type.  This keeps every other
  // binary op (shifts, bitwise, logical, ...) from paying the
  // isNeverCStringType type-class check.
  if (Opc != BO_Assign && Opc != BO_AddAssign && Opc != BO_Add &&
      !BinaryOperator::isComparisonOp(Opc))
    return std::nullopt;
  if (!couldBeNeverCStringOperand(LHS) && !couldBeNeverCStringOperand(RHS))
    return std::nullopt;

  // Plain assignment `s = t`.
  if (Opc == BO_Assign && S.isNeverCStringType(LHS->getType()) &&
      !S.isInsideNeverCStringRuntime())
    return buildNeverCStringAssign(S, Sc, OpLoc, LHS, RHS);

  // Compound `s += t`.
  if (Opc == BO_AddAssign && S.isNeverCStringType(LHS->getType())) {
    Expr *RHSExpr = RHS;
    if (!isNeverCStringOperand(S, RHS)) {
      Expr *Promoted = promoteCharToNeverCString(S, OpLoc, RHS);
      if (!Promoted)
        return std::nullopt;
      RHSExpr = Promoted;
    }
    ExprResult Joined = buildNeverCStringConcat(S, OpLoc, LHS, RHSExpr);
    if (Joined.isInvalid())
      return Joined;
    return buildNeverCStringAssign(S, Sc, OpLoc, LHS, Joined.get());
  }

  // Pure concat `s + t` and the char shapes `s + ch` / `ch + s`.
  if (Opc == BO_Add) {
    bool LHSIsString = isNeverCStringOperand(S, LHS);
    bool RHSIsString = isNeverCStringOperand(S, RHS);
    if (LHSIsString && RHSIsString)
      return buildNeverCStringConcat(S, OpLoc, LHS, RHS);
    if (LHSIsString && !RHSIsString) {
      if (Expr *Wrapped = promoteCharToNeverCString(S, OpLoc, RHS))
        return buildNeverCStringConcat(S, OpLoc, LHS, Wrapped);
    }
    if (RHSIsString && !LHSIsString) {
      if (Expr *Wrapped = promoteCharToNeverCString(S, OpLoc, LHS))
        return buildNeverCStringConcat(S, OpLoc, Wrapped, RHS);
    }
  }

  // Comparison operators.
  if (BinaryOperator::isComparisonOp(Opc) && isNeverCStringOperand(S, LHS) &&
      isNeverCStringOperand(S, RHS))
    return buildNeverCStringCompare(S, OpLoc, Opc, LHS, RHS);

  return std::nullopt;
}
} // namespace

// Binary Operators.  'Tok' is the token for the operator.
NEVERC_HOT ExprResult Sema::OnBinOp(Scope *S, SourceLocation TokLoc,
                                              tok::TokenKind Kind,
                                              Expr *LHSExpr, Expr *RHSExpr) {
  BinaryOperatorKind Opc = ConvertTokenKindToBinaryOpcode(Kind);
  assert(LHSExpr && "OnBinOp(): missing left expression");
  assert(RHSExpr && "OnBinOp(): missing right expression");
  if (SemaPluginHooks *Hooks = getPluginHooks()) {
    Expr *Replacement = nullptr;
    switch (Hooks->analyzeBinaryExpression(*this, TokLoc, Kind, LHSExpr,
                                           RHSExpr, Replacement)) {
    case SemaPluginOutcome::NotHandled:
      break;
    case SemaPluginOutcome::Handled:
      return Replacement ? ExprResult(Replacement) : ExprError();
    case SemaPluginOutcome::Error:
      return ExprError();
    }
  }

  // Only bitwise, logical-or and shift operators can trigger precedence
  // warnings.  Skip the noinline diagnoseBinOpPrecedence call for the
  // overwhelmingly common arithmetic / assignment / comparison operators.
  if (LLVM_UNLIKELY(BinaryOperator::isBitwiseOp(Opc) || Opc == BO_LOr ||
                    Opc == BO_Shl || Opc == BO_Shr))
    diagnoseBinOpPrecedence(*this, Opc, TokLoc, LHSExpr, RHSExpr);

  return FormBinOp(S, TokLoc, Opc, LHSExpr, RHSExpr);
}

NEVERC_HOT ExprResult Sema::FormBinOp(Scope *S, SourceLocation OpLoc,
                                                BinaryOperatorKind Opc,
                                                Expr *LHSExpr, Expr *RHSExpr) {
  const Type *LT = LHSExpr->getType().getTypePtrOrNull();
  const Type *RT = RHSExpr->getType().getTypePtrOrNull();

  if (LLVM_LIKELY(LT && RT && LT->isBuiltinType() && RT->isBuiltinType() &&
                  !LT->isPlaceholderType() && !RT->isPlaceholderType()))
    return CreateBuiltinBinOp(OpLoc, Opc, LHSExpr, RHSExpr);

  if (LLVM_UNLIKELY(LT && LT->getAsPlaceholderType())) {
    ExprResult LHS = CheckPlaceholderExpr(LHSExpr);
    if (LHS.isInvalid())
      return ExprError();
    LHSExpr = LHS.get();
  }

  if (LLVM_UNLIKELY(RT && RT->getAsPlaceholderType())) {
    const BuiltinType *pty = RT->getAsPlaceholderType();
    if (Opc == BO_Assign && pty->getKind() == BuiltinType::Overload) {
      return CreateBuiltinBinOp(OpLoc, Opc, LHSExpr, RHSExpr);
    }

    ExprResult resolvedRHS = CheckPlaceholderExpr(RHSExpr);
    if (!resolvedRHS.isUsable())
      return ExprError();
    RHSExpr = resolvedRHS.get();
  }

  if (auto NeverCRewrite = tryNeverCStringBinaryOpRewrite(*this, S, OpLoc, Opc,
                                                          LHSExpr, RHSExpr))
    return *NeverCRewrite;

  if (getLangOpts().RecoveryAST &&
      (LHSExpr->isTypeDependent() || RHSExpr->isTypeDependent())) {
    if (BinaryOperator::isCompoundAssignmentOp(Opc))
      return CompoundAssignOperator::Create(
          Context, LHSExpr, RHSExpr, Opc,
          LHSExpr->getType().getUnqualifiedType(), VK_PRValue, OK_Ordinary,
          OpLoc, CurFPFeatureOverrides());
    QualType ResultType;
    switch (Opc) {
    case BO_Assign:
      ResultType = LHSExpr->getType().getUnqualifiedType();
      break;
    case BO_LT:
    case BO_GT:
    case BO_LE:
    case BO_GE:
    case BO_EQ:
    case BO_NE:
    case BO_LAnd:
    case BO_LOr:
      ResultType = Context.IntTy;
      break;
    case BO_Comma:
      ResultType = RHSExpr->getType();
      break;
    default:
      ResultType = Context.DependentTy;
      break;
    }
    return BinaryOperator::Create(Context, LHSExpr, RHSExpr, Opc, ResultType,
                                  VK_PRValue, OK_Ordinary, OpLoc,
                                  CurFPFeatureOverrides());
  }

  return CreateBuiltinBinOp(OpLoc, Opc, LHSExpr, RHSExpr);
}
