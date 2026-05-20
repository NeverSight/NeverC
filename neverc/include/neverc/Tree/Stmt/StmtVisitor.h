#ifndef NEVERC_TREE_STMTVISITOR_H
#define NEVERC_TREE_STMTVISITOR_H

#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <utility>

namespace neverc {

using llvm::dyn_cast;

template <template <typename> class Ptr, typename ImplClass,
          typename RetTy = void, class... ParamTys>
class StmtVisitorBase {
public:
#define PTR(CLASS) typename Ptr<CLASS>::type
#define DISPATCH(NAME, CLASS)                                                  \
  return static_cast<ImplClass *>(this)->Visit##NAME(                          \
      static_cast<PTR(CLASS)>(S), std::forward<ParamTys>(P)...)

  RetTy Visit(PTR(Stmt) S, ParamTys... P) {
    // If we have a binary expr, dispatch to the subcode of the binop.  A smart
    // optimizer (e.g. LLVM) will fold this comparison into the switch stmt
    // below.
    if (PTR(BinaryOperator) BinOp = dyn_cast<BinaryOperator>(S)) {
      switch (BinOp->getOpcode()) {
      case BO_Mul:
        DISPATCH(BinMul, BinaryOperator);
      case BO_Div:
        DISPATCH(BinDiv, BinaryOperator);
      case BO_Rem:
        DISPATCH(BinRem, BinaryOperator);
      case BO_Add:
        DISPATCH(BinAdd, BinaryOperator);
      case BO_Sub:
        DISPATCH(BinSub, BinaryOperator);
      case BO_Shl:
        DISPATCH(BinShl, BinaryOperator);
      case BO_Shr:
        DISPATCH(BinShr, BinaryOperator);

      case BO_LT:
        DISPATCH(BinLT, BinaryOperator);
      case BO_GT:
        DISPATCH(BinGT, BinaryOperator);
      case BO_LE:
        DISPATCH(BinLE, BinaryOperator);
      case BO_GE:
        DISPATCH(BinGE, BinaryOperator);
      case BO_EQ:
        DISPATCH(BinEQ, BinaryOperator);
      case BO_NE:
        DISPATCH(BinNE, BinaryOperator);

      case BO_And:
        DISPATCH(BinAnd, BinaryOperator);
      case BO_Xor:
        DISPATCH(BinXor, BinaryOperator);
      case BO_Or:
        DISPATCH(BinOr, BinaryOperator);
      case BO_LAnd:
        DISPATCH(BinLAnd, BinaryOperator);
      case BO_LOr:
        DISPATCH(BinLOr, BinaryOperator);
      case BO_Assign:
        DISPATCH(BinAssign, BinaryOperator);
      case BO_MulAssign:
        DISPATCH(BinMulAssign, CompoundAssignOperator);
      case BO_DivAssign:
        DISPATCH(BinDivAssign, CompoundAssignOperator);
      case BO_RemAssign:
        DISPATCH(BinRemAssign, CompoundAssignOperator);
      case BO_AddAssign:
        DISPATCH(BinAddAssign, CompoundAssignOperator);
      case BO_SubAssign:
        DISPATCH(BinSubAssign, CompoundAssignOperator);
      case BO_ShlAssign:
        DISPATCH(BinShlAssign, CompoundAssignOperator);
      case BO_ShrAssign:
        DISPATCH(BinShrAssign, CompoundAssignOperator);
      case BO_AndAssign:
        DISPATCH(BinAndAssign, CompoundAssignOperator);
      case BO_OrAssign:
        DISPATCH(BinOrAssign, CompoundAssignOperator);
      case BO_XorAssign:
        DISPATCH(BinXorAssign, CompoundAssignOperator);
      case BO_Comma:
        DISPATCH(BinComma, BinaryOperator);
      }
    } else if (PTR(UnaryOperator) UnOp = dyn_cast<UnaryOperator>(S)) {
      switch (UnOp->getOpcode()) {
      case UO_PostInc:
        DISPATCH(UnaryPostInc, UnaryOperator);
      case UO_PostDec:
        DISPATCH(UnaryPostDec, UnaryOperator);
      case UO_PreInc:
        DISPATCH(UnaryPreInc, UnaryOperator);
      case UO_PreDec:
        DISPATCH(UnaryPreDec, UnaryOperator);
      case UO_AddrOf:
        DISPATCH(UnaryAddrOf, UnaryOperator);
      case UO_Deref:
        DISPATCH(UnaryDeref, UnaryOperator);
      case UO_Plus:
        DISPATCH(UnaryPlus, UnaryOperator);
      case UO_Minus:
        DISPATCH(UnaryMinus, UnaryOperator);
      case UO_Not:
        DISPATCH(UnaryNot, UnaryOperator);
      case UO_LNot:
        DISPATCH(UnaryLNot, UnaryOperator);
      case UO_Real:
        DISPATCH(UnaryReal, UnaryOperator);
      case UO_Imag:
        DISPATCH(UnaryImag, UnaryOperator);
      case UO_Extension:
        DISPATCH(UnaryExtension, UnaryOperator);
      }
    }

    // Top switch stmt: dispatch to VisitFooStmt for each FooStmt.
    switch (S->getStmtClass()) {
    default:
      llvm_unreachable("Unknown stmt kind!");
#define ABSTRACT_STMT(STMT)
#define STMT(CLASS, PARENT)                                                    \
  case Stmt::CLASS##Class:                                                     \
    DISPATCH(CLASS, CLASS);
#include "neverc/Tree/StmtNodes.td.h"
    }
  }

  // If the implementation does not override Visit* for a stmt kind, fall back
  // on VisitExpr or whatever else is the superclass.
#define STMT(CLASS, PARENT)                                                    \
  RetTy Visit##CLASS(PTR(CLASS) S, ParamTys... P) { DISPATCH(PARENT, PARENT); }
#include "neverc/Tree/StmtNodes.td.h"

  // If the implementation doesn't override Visit* for specific binary
  // operators, fall back on VisitBinaryOperator.
#define BINOP_FALLBACK(NAME)                                                   \
  RetTy VisitBin##NAME(PTR(BinaryOperator) S, ParamTys... P) {                 \
    DISPATCH(BinaryOperator, BinaryOperator);                                  \
  }
  BINOP_FALLBACK(Mul)
  BINOP_FALLBACK(Div)
  BINOP_FALLBACK(Rem)
  BINOP_FALLBACK(Add) BINOP_FALLBACK(Sub) BINOP_FALLBACK(Shl) BINOP_FALLBACK(
      Shr)

      BINOP_FALLBACK(LT) BINOP_FALLBACK(GT) BINOP_FALLBACK(LE) BINOP_FALLBACK(
          GE) BINOP_FALLBACK(EQ) BINOP_FALLBACK(NE)

          BINOP_FALLBACK(And) BINOP_FALLBACK(Xor) BINOP_FALLBACK(
              Or) BINOP_FALLBACK(LAnd) BINOP_FALLBACK(LOr)

              BINOP_FALLBACK(Assign) BINOP_FALLBACK(Comma)
#undef BINOP_FALLBACK

  // If the implementation doesn't override Visit* for compound assignment
  // operators, fall back on VisitCompoundAssignOperator.
#define CAO_FALLBACK(NAME)                                                     \
  RetTy VisitBin##NAME(PTR(CompoundAssignOperator) S, ParamTys... P) {         \
    DISPATCH(CompoundAssignOperator, CompoundAssignOperator);                  \
  }
                  CAO_FALLBACK(MulAssign) CAO_FALLBACK(DivAssign) CAO_FALLBACK(
                      RemAssign) CAO_FALLBACK(AddAssign) CAO_FALLBACK(SubAssign)
                      CAO_FALLBACK(ShlAssign) CAO_FALLBACK(ShrAssign)
                          CAO_FALLBACK(AndAssign) CAO_FALLBACK(
                              OrAssign) CAO_FALLBACK(XorAssign)
#undef CAO_FALLBACK

  // If the implementation doesn't override Visit* for specific unary operators,
  // fall back on VisitUnaryOperator.
#define UNARYOP_FALLBACK(NAME)                                                 \
  RetTy VisitUnary##NAME(PTR(UnaryOperator) S, ParamTys... P) {                \
    DISPATCH(UnaryOperator, UnaryOperator);                                    \
  }
                              UNARYOP_FALLBACK(PostInc) UNARYOP_FALLBACK(
                                  PostDec) UNARYOP_FALLBACK(PreInc)
                                  UNARYOP_FALLBACK(PreDec) UNARYOP_FALLBACK(
                                      AddrOf) UNARYOP_FALLBACK(Deref)

                                      UNARYOP_FALLBACK(Plus)
                                          UNARYOP_FALLBACK(Minus)
                                              UNARYOP_FALLBACK(Not)
                                                  UNARYOP_FALLBACK(LNot)
                                                      UNARYOP_FALLBACK(Real)
                                                          UNARYOP_FALLBACK(Imag)
                                                              UNARYOP_FALLBACK(
                                                                  Extension)
#undef UNARYOP_FALLBACK

      // Base case, ignore it. :)
      RetTy VisitStmt(PTR(Stmt) Node, ParamTys... P) {
    return RetTy();
  }

#undef PTR
#undef DISPATCH
};

template <typename ImplClass, typename RetTy = void, typename... ParamTys>
class StmtVisitor
    : public StmtVisitorBase<std::add_pointer, ImplClass, RetTy, ParamTys...> {
};

template <typename ImplClass, typename RetTy = void, typename... ParamTys>
class ConstStmtVisitor : public StmtVisitorBase<llvm::make_const_ptr, ImplClass,
                                                RetTy, ParamTys...> {};

} // namespace neverc

#endif // NEVERC_TREE_STMTVISITOR_H
