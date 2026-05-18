#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"

using namespace neverc;

using Cl = Expr::Classification;

// ===----------------------------------------------------------------------===
// Internal helper forward declarations
// ===----------------------------------------------------------------------===

namespace {

LLVM_ATTRIBUTE_ALWAYS_INLINE
Cl::Kinds classifyInternal(TreeContext &Ctx, const Expr *E);
LLVM_ATTRIBUTE_ALWAYS_INLINE
Cl::Kinds classifyDecl(const Decl *D);
LLVM_ATTRIBUTE_ALWAYS_INLINE
Cl::Kinds classifyUnnamed(TreeContext &Ctx, QualType T);
LLVM_ATTRIBUTE_ALWAYS_INLINE
Cl::Kinds classifyMemberExpr(TreeContext &Ctx, const MemberExpr *E);
Cl::ModifiableType isModifiable(TreeContext &Ctx, const Expr *E, Cl::Kinds Kind,
                                SourceLocation &Loc);

} // namespace

// ===----------------------------------------------------------------------===
// Public entry point
// ===----------------------------------------------------------------------===

__attribute__((hot)) Cl Expr::ClassifyImpl(TreeContext &Ctx,
                                           SourceLocation *Loc) const {
  Cl::Kinds kind = classifyInternal(Ctx, this);

  if (LLVM_UNLIKELY(TR->isFunctionType() || TR == Ctx.OverloadTy))
    kind = Cl::CL_Function;
  else if (LLVM_UNLIKELY(TR->isVoidType() && !TR.hasQualifiers()))
    kind = (kind == Cl::CL_LValue ? Cl::CL_AddressableVoid : Cl::CL_Void);

  assert((kind == Cl::CL_LValue ? isLValue() : isPRValue()) &&
         "value category mismatch");

  Cl::ModifiableType modifiable = Cl::CM_Untested;
  if (LLVM_UNLIKELY(Loc != nullptr))
    modifiable = isModifiable(Ctx, this, kind, *Loc);
  return Classification(kind, modifiable);
}

// ===----------------------------------------------------------------------===
// Classification implementation details
// ===----------------------------------------------------------------------===

namespace {

LLVM_ATTRIBUTE_ALWAYS_INLINE
Cl::Kinds classifyTemporary(QualType T) {
  if (LLVM_UNLIKELY(T->isRecordType()))
    return Cl::CL_RecordTemporary;
  if (LLVM_UNLIKELY(T->isArrayType()))
    return Cl::CL_ArrayTemporary;
  return Cl::CL_PRValue;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
Cl::Kinds classifyExprValueKind(ExprValueKind Kind) {
  return LLVM_LIKELY(Kind == VK_PRValue) ? Cl::CL_PRValue : Cl::CL_LValue;
}

Cl::Kinds classifyInternal(TreeContext &Ctx, const Expr *E) {
  switch (E->getStmtClass()) {
  case Stmt::NoStmtClass:
#define ABSTRACT_STMT(Kind)
#define STMT(Kind, Base) case Expr::Kind##Class:
#define EXPR(Kind, Base)
#include "neverc/Tree/StmtNodes.td.h"
    llvm_unreachable("cannot classify a statement");

    // First come the expressions that are always lvalues, unconditionally.
    // String literals (C 6.5.2.5) and __func__ are lvalues.
  case Expr::StringLiteralClass:
  case Expr::PredefinedExprClass:
    // Uncorrected typos get classified as lvalues.
  case Expr::TypoExprClass:
    return Cl::CL_LValue;

    // Compound literals are lvalues in C; some modes treat them as temporaries.
  case Expr::CompoundLiteralExprClass:
    return !E->isLValue() ? classifyTemporary(E->getType()) : Cl::CL_LValue;

    // Expressions that are prvalues.
  case Expr::UnaryExprOrTypeTraitExprClass:
  case Expr::NullPtrLiteralExprClass:
  case Expr::ImaginaryLiteralClass:
  case Expr::OffsetOfExprClass:
  case Expr::ShuffleVectorExprClass:
  case Expr::ConvertVectorExprClass:
  case Expr::IntegerLiteralClass:
  case Expr::FixedPointLiteralClass:
  case Expr::CharacterLiteralClass:
  case Expr::AddrLabelExprClass:
  case Expr::ImplicitValueInitExprClass:
  case Expr::FloatingLiteralClass:
  case Expr::ParenListExprClass:
  case Expr::AtomicExprClass:
  case Expr::ArrayInitLoopExprClass:
  case Expr::ArrayInitIndexExprClass:
  case Expr::NoInitExprClass:
  case Expr::DesignatedInitUpdateExprClass:
  case Expr::SourceLocExprClass:
    return Cl::CL_PRValue;

  case Expr::ConstantExprClass:
    return classifyInternal(Ctx, cast<ConstantExpr>(E)->getSubExpr());

    // Array subscript: lvalue of element type.
    // Vector subscripting follows the base.
  case Expr::ArraySubscriptExprClass:
    if (cast<ArraySubscriptExpr>(E)->getBase()->getType()->isVectorType())
      return classifyInternal(Ctx, cast<ArraySubscriptExpr>(E)->getBase());
    return Cl::CL_LValue;

  // Subscripting matrix types behaves like member accesses.
  case Expr::MatrixSubscriptExprClass:
    return classifyInternal(Ctx, cast<MatrixSubscriptExpr>(E)->getBase());

    // Decl ref: lvalue for objects/functions with object-like storage, else
    // prvalue (see classifyDecl).
  case Expr::DeclRefExprClass:
    return classifyDecl(cast<DeclRefExpr>(E)->getDecl());

    // Member access is complex.
  case Expr::MemberExprClass:
    return classifyMemberExpr(Ctx, cast<MemberExpr>(E));

  case Expr::UnaryOperatorClass:
    switch (cast<UnaryOperator>(E)->getOpcode()) {
      // Unary *: lvalue referring to the pointed-to object or function.
    case UO_Deref:
      return Cl::CL_LValue;

      // GNU extensions, simply look through them.
    case UO_Extension:
      return classifyInternal(Ctx, cast<UnaryOperator>(E)->getSubExpr());

    // Treat _Real and _Imag basically as if they were member
    // expressions:  l-value only if the operand is a true l-value.
    case UO_Real:
    case UO_Imag: {
      const Expr *Op = cast<UnaryOperator>(E)->getSubExpr()->IgnoreParens();
      Cl::Kinds K = classifyInternal(Ctx, Op);
      if (K != Cl::CL_LValue)
        return K;

      return Cl::CL_LValue;
    }

      // Pre-increment/decrement: prvalues under C rules.
    case UO_PreInc:
    case UO_PreDec:
      return Cl::CL_PRValue;

    default:
      return Cl::CL_PRValue;
    }

  case Expr::RecoveryExprClass:
  case Expr::OpaqueValueExprClass:
    return classifyExprValueKind(E->getValueKind());

  case Expr::PseudoObjectExprClass:
    return classifyExprValueKind(cast<PseudoObjectExpr>(E)->getValueKind());

  case Expr::ImplicitCastExprClass:
    return classifyExprValueKind(E->getValueKind());

    // Parentheses do not change value category.
  case Expr::ParenExprClass:
    return classifyInternal(Ctx, cast<ParenExpr>(E)->getSubExpr());

    // _Generic inherits the value category of its result expression.
  case Expr::GenericSelectionExprClass:
    if (cast<GenericSelectionExpr>(E)->isResultDependent())
      return Cl::CL_PRValue;
    return classifyInternal(Ctx,
                            cast<GenericSelectionExpr>(E)->getResultExpr());

  case Expr::BinaryOperatorClass:
  case Expr::CompoundAssignOperatorClass:
    return Cl::CL_PRValue;

  case Expr::CallExprClass:
    return classifyUnnamed(Ctx, cast<CallExpr>(E)->getCallReturnType(Ctx));

    // __builtin_choose_expr is equivalent to the chosen expression.
  case Expr::ChooseExprClass:
    return classifyInternal(Ctx, cast<ChooseExpr>(E)->getChosenSubExpr());

    // Extended vector element access is an lvalue unless there are duplicates
    // in the shuffle expression.
  case Expr::ExtVectorElementExprClass:
    if (cast<ExtVectorElementExpr>(E)->containsDuplicateElements())
      return Cl::CL_DuplicateVectorComponents;
    if (cast<ExtVectorElementExpr>(E)->isArrow())
      return Cl::CL_LValue;
    return classifyInternal(Ctx, cast<ExtVectorElementExpr>(E)->getBase());

    // Cleanups guard.
  case Expr::ExprWithCleanupsClass:
    return classifyInternal(Ctx, cast<ExprWithCleanups>(E)->getSubExpr());

    // Casts depend completely on the target type. All casts work the same.
  case Expr::CStyleCastExprClass:
    return Cl::CL_PRValue;

  case Expr::BinaryConditionalOperatorClass:
    return Cl::CL_PRValue;

  case Expr::ConditionalOperatorClass:
    return Cl::CL_PRValue;

  case Expr::VAArgExprClass:
    return classifyUnnamed(Ctx, E->getType());

  case Expr::DesignatedInitExprClass:
    return classifyInternal(Ctx, cast<DesignatedInitExpr>(E)->getInit());

  case Expr::StmtExprClass: {
    const CompoundStmt *S = cast<StmtExpr>(E)->getSubStmt();
    if (const auto *LastExpr = dyn_cast_or_null<Expr>(S->body_back()))
      return classifyUnnamed(Ctx, LastExpr->getType());
    return Cl::CL_PRValue;
  }

  case Expr::InitListExprClass: {
    const auto *ILE = cast<InitListExpr>(E);
    // An init list can be an lvalue if it is bound to a reference and
    // contains only one element. In that case, we look at that element
    // for an exact classification. Init list creation takes care of the
    // value kind for us, so we only need to fine-tune.
    if (E->isPRValue())
      return classifyExprValueKind(E->getValueKind());
    assert(ILE->getNumInits() == 1 &&
           "Only 1-element init lists can be lvalues.");
    return classifyInternal(Ctx, ILE->getInit(0));
  }
  }

  llvm_unreachable("unhandled expression kind in classification");
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
Cl::Kinds classifyDecl(const Decl *D) {
  Decl::Kind K = D->getKind();
  switch (K) {
  case Decl::Var:
  case Decl::ParmVar:
  case Decl::ImplicitParam:
  case Decl::Field:
  case Decl::IndirectField:
    return Cl::CL_LValue;
  default:
    if (LLVM_UNLIKELY(VarDecl::classofKind(K) || FieldDecl::classofKind(K)))
      return Cl::CL_LValue;
    return Cl::CL_PRValue;
  }
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
Cl::Kinds classifyUnnamed(TreeContext &, QualType) { return Cl::CL_PRValue; }

LLVM_ATTRIBUTE_ALWAYS_INLINE
Cl::Kinds classifyMemberExpr(TreeContext &Ctx, const MemberExpr *E) {
  if (E->isArrow())
    return Cl::CL_LValue;
  return classifyInternal(Ctx, E->getBase()->IgnoreParens());
}

__attribute__((hot)) Cl::ModifiableType isModifiable(TreeContext &Ctx,
                                                     const Expr *E,
                                                     Cl::Kinds Kind,
                                                     SourceLocation &Loc) {
  if (LLVM_UNLIKELY(Kind == Cl::CL_PRValue)) {
    if (const auto *CE = dyn_cast<ExplicitCastExpr>(E->IgnoreParens())) {
      if (CE->getSubExpr()->IgnoreParenImpCasts()->isLValue()) {
        Loc = CE->getExprLoc();
        return Cl::CM_LValueCast;
      }
    }
  }
  if (LLVM_LIKELY(Kind != Cl::CL_LValue))
    return Cl::CM_RValue;

  CanQualType CT = Ctx.getCanonicalType(E->getType());
  if (LLVM_UNLIKELY(CT.isConstQualified()))
    return Cl::CM_ConstQualified;
  if (LLVM_UNLIKELY(CT->isArrayType()))
    return Cl::CM_ArrayType;
  if (LLVM_UNLIKELY(CT->isIncompleteType()))
    return Cl::CM_IncompleteType;

  if (const RecordType *R = CT->getAs<RecordType>())
    if (R->hasConstFields())
      return Cl::CM_ConstQualifiedField;

  return Cl::CM_Modifiable;
}

} // namespace

// ===----------------------------------------------------------------------===
// LValue & modifiability queries
// ===----------------------------------------------------------------------===

Expr::LValueClassification Expr::ClassifyLValue(TreeContext &Ctx) const {
  Classification VC = Classify(Ctx);
  switch (VC.getKind()) {
  case Cl::CL_LValue:
    return LV_Valid;
  case Cl::CL_Function:
    return LV_NotObjectType;
  case Cl::CL_Void:
    return LV_InvalidExpression;
  case Cl::CL_AddressableVoid:
    return LV_IncompleteVoidType;
  case Cl::CL_DuplicateVectorComponents:
    return LV_DuplicateVectorComponents;
  case Cl::CL_RecordTemporary:
    return LV_RecordTemporary;
  case Cl::CL_ArrayTemporary:
    return LV_ArrayTemporary;
  case Cl::CL_PRValue:
    return LV_InvalidExpression;
  }
  llvm_unreachable("Unhandled kind");
}

Expr::isModifiableLvalueResult
Expr::isModifiableLvalue(TreeContext &Ctx, SourceLocation *Loc) const {
  SourceLocation dummy;
  Classification VC = ClassifyModifiable(Ctx, Loc ? *Loc : dummy);
  switch (VC.getKind()) {
  case Cl::CL_LValue:
    break;
  case Cl::CL_Function:
    return MLV_NotObjectType;
  case Cl::CL_Void:
    return MLV_InvalidExpression;
  case Cl::CL_AddressableVoid:
    return MLV_IncompleteVoidType;
  case Cl::CL_DuplicateVectorComponents:
    return MLV_DuplicateVectorComponents;
  case Cl::CL_RecordTemporary:
    return MLV_RecordTemporary;
  case Cl::CL_ArrayTemporary:
    return MLV_ArrayTemporary;
  case Cl::CL_PRValue:
    return VC.getModifiable() == Cl::CM_LValueCast ? MLV_LValueCast
                                                   : MLV_InvalidExpression;
  }
  assert(VC.getKind() == Cl::CL_LValue && "Unhandled kind");
  switch (VC.getModifiable()) {
  case Cl::CM_Untested:
    llvm_unreachable("Did not test modifiability");
  case Cl::CM_Modifiable:
    return MLV_Valid;
  case Cl::CM_RValue:
    llvm_unreachable("CM_RValue and CL_LValue don't match");
  case Cl::CM_LValueCast:
    llvm_unreachable("CM_LValueCast and CL_LValue don't match");
  case Cl::CM_ConstQualified:
    return MLV_ConstQualified;
  case Cl::CM_ConstQualifiedField:
    return MLV_ConstQualifiedField;
  case Cl::CM_ArrayType:
    return MLV_ArrayType;
  case Cl::CM_IncompleteType:
    return MLV_IncompleteType;
  }
  llvm_unreachable("Unhandled modifiable type");
}
