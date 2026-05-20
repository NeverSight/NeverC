#ifndef NEVERC_TREE_RECURSIVEASTVISITOR_H
#define NEVERC_TREE_RECURSIVEASTVISITOR_H

#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "neverc/Tree/Type/Type.h"
#include "neverc/Tree/Type/TypeLoc.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cstddef>
#include <type_traits>

namespace neverc {

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

// A helper macro to implement short-circuiting when recursing.  It invokes
// CALL_EXPR on the derived visitor so users can override the Traverse*/Visit*
// hook named in CALL_EXPR.
#define TRY_TO(CALL_EXPR)                                                      \
  do {                                                                         \
    if (!getDerived().CALL_EXPR)                                               \
      return false;                                                            \
  } while (false)

namespace detail {

template <typename T, typename U>
struct has_same_member_pointer_type : std::false_type {};
template <typename T, typename U, typename R, typename... P>
struct has_same_member_pointer_type<R (T::*)(P...), R (U::*)(P...)>
    : std::true_type {};

template <typename FirstMethodPtrTy, typename SecondMethodPtrTy>
LLVM_ATTRIBUTE_ALWAYS_INLINE LLVM_ATTRIBUTE_NODEBUG auto
isSameMethod([[maybe_unused]] FirstMethodPtrTy FirstMethodPtr,
             [[maybe_unused]] SecondMethodPtrTy SecondMethodPtr) -> bool {
  if constexpr (has_same_member_pointer_type<FirstMethodPtrTy,
                                             SecondMethodPtrTy>::value)
    return FirstMethodPtr == SecondMethodPtr;
  return false;
}

} // end namespace detail

template <typename Derived> class RecursiveTreeVisitor {
public:
  typedef llvm::SmallVectorImpl<llvm::PointerIntPair<Stmt *, 1, bool>>
      DataRecursionQueue;

  Derived &getDerived() { return *static_cast<Derived *>(this); }

  bool shouldWalkTypesOfTypeLocs() const { return true; }

  bool shouldVisitImplicitCode() const { return false; }

  bool shouldTraversePostOrder() const { return false; }

  bool TraverseAST(TreeContext &AST) {
    // Currently just an alias for TraverseDecl(TUDecl), but kept in case
    // we change the implementation again.
    return getDerived().TraverseDecl(AST.getTranslationUnitDecl());
  }

  bool TraverseStmt(Stmt *S, DataRecursionQueue *Queue = nullptr);

  bool dataTraverseStmtPre(Stmt *S) { return true; }

  bool dataTraverseStmtPost(Stmt *S) { return true; }

  bool TraverseType(QualType T);

  bool TraverseTypeLoc(TypeLoc TL);

  bool TraverseAttr(Attr *At);

  bool TraverseDecl(Decl *D);

  bool TraverseDeclarationNameInfo(DeclarationNameInfo NameInfo);

  bool TraverseSynOrSemInitListExpr(InitListExpr *S,
                                    DataRecursionQueue *Queue = nullptr);

  // ---- Methods on Attrs ----

  // Visit an attribute.
  bool VisitAttr(Attr *A) { return true; }

// Declare Traverse* and empty Visit* for all Attr classes.
#define ATTR_VISITOR_DECLS_ONLY
#include "neverc/Tree/AttrVisitor.td.h"
#undef ATTR_VISITOR_DECLS_ONLY

  // ---- Methods on Stmts ----

  Stmt::child_range getStmtChildren(Stmt *S) { return S->children(); }

private:
  // Traverse the given statement. If the most-derived traverse function takes a
  // data recursion queue, pass it on; otherwise, discard it. Note that the
  // first branch of this conditional must compile whether or not the derived
  // class can take a queue, so if we're taking the second arm, make the first
  // arm call our function rather than the derived class version.
#define TRAVERSE_STMT_BASE(NAME, CLASS, VAR, QUEUE)                            \
  (::neverc::detail::has_same_member_pointer_type<                             \
       decltype(&RecursiveTreeVisitor::Traverse##NAME),                        \
       decltype(&Derived::Traverse##NAME)>::value                              \
       ? static_cast<std::conditional_t<                                       \
             ::neverc::detail::has_same_member_pointer_type<                   \
                 decltype(&RecursiveTreeVisitor::Traverse##NAME),              \
                 decltype(&Derived::Traverse##NAME)>::value,                   \
             Derived &, RecursiveTreeVisitor &>>(*this)                        \
             .Traverse##NAME(static_cast<CLASS *>(VAR), QUEUE)                 \
       : getDerived().Traverse##NAME(static_cast<CLASS *>(VAR)))

// Try to traverse the given statement, or enqueue it if we're performing data
// recursion in the middle of traversing another statement. Can only be called
// from within a DEF_TRAVERSE_STMT body or similar context.
#define TRY_TO_TRAVERSE_OR_ENQUEUE_STMT(S)                                     \
  do {                                                                         \
    if (!TRAVERSE_STMT_BASE(Stmt, Stmt, S, Queue))                             \
      return false;                                                            \
  } while (false)

public:
// Declare Traverse*() for all concrete Stmt classes.
#define ABSTRACT_STMT(STMT)
#define STMT(CLASS, PARENT)                                                    \
  bool Traverse##CLASS(CLASS *S, DataRecursionQueue *Queue = nullptr);
#include "neverc/Tree/StmtNodes.td.h"
  // The above header #undefs ABSTRACT_STMT and STMT upon exit.

  // Define WalkUpFrom*() and empty Visit*() for all Stmt classes.
  bool WalkUpFromStmt(Stmt *S) { return getDerived().VisitStmt(S); }
  bool VisitStmt(Stmt *S) { return true; }
#define STMT(CLASS, PARENT)                                                    \
  bool WalkUpFrom##CLASS(CLASS *S) {                                           \
    TRY_TO(WalkUpFrom##PARENT(S));                                             \
    TRY_TO(Visit##CLASS(S));                                                   \
    return true;                                                               \
  }                                                                            \
  bool Visit##CLASS(CLASS *S) { return true; }
#include "neverc/Tree/StmtNodes.td.h"

// ---- Methods on Types ----

// Declare Traverse*() for all concrete Type classes.
#define ABSTRACT_TYPE(CLASS, BASE)
#define TYPE(CLASS, BASE) bool Traverse##CLASS##Type(CLASS##Type *T);
#include "neverc/Tree/TypeNodes.td.h"
  // The above header #undefs ABSTRACT_TYPE and TYPE upon exit.

  // Define WalkUpFrom*() and empty Visit*() for all Type classes.
  bool WalkUpFromType(Type *T) { return getDerived().VisitType(T); }
  bool VisitType(Type *T) { return true; }
#define TYPE(CLASS, BASE)                                                      \
  bool WalkUpFrom##CLASS##Type(CLASS##Type *T) {                               \
    TRY_TO(WalkUpFrom##BASE(T));                                               \
    TRY_TO(Visit##CLASS##Type(T));                                             \
    return true;                                                               \
  }                                                                            \
  bool Visit##CLASS##Type(CLASS##Type *T) { return true; }
#include "neverc/Tree/TypeNodes.td.h"

// ---- Methods on TypeLocs ----

// Declare Traverse*() for all concrete TypeLoc classes.
#define ABSTRACT_TYPELOC(CLASS, BASE)
#define TYPELOC(CLASS, BASE) bool Traverse##CLASS##TypeLoc(CLASS##TypeLoc TL);
#include "neverc/Tree/Type/TypeLocNodes.def"
  // The above header #undefs ABSTRACT_TYPELOC and TYPELOC upon exit.

  // Define WalkUpFrom*() and empty Visit*() for all TypeLoc classes.
  bool WalkUpFromTypeLoc(TypeLoc TL) { return getDerived().VisitTypeLoc(TL); }
  bool VisitTypeLoc(TypeLoc TL) { return true; }

  // QualifiedTypeLoc and UnqualTypeLoc are not declared in
  // TypeNodes.td.h and thus need to be handled specially.
  bool WalkUpFromQualifiedTypeLoc(QualifiedTypeLoc TL) {
    return getDerived().VisitUnqualTypeLoc(TL.getUnqualifiedLoc());
  }
  bool VisitQualifiedTypeLoc(QualifiedTypeLoc TL) { return true; }
  bool WalkUpFromUnqualTypeLoc(UnqualTypeLoc TL) {
    return getDerived().VisitUnqualTypeLoc(TL.getUnqualifiedLoc());
  }
  bool VisitUnqualTypeLoc(UnqualTypeLoc TL) { return true; }

// Note that BASE includes trailing 'Type' which CLASS doesn't.
#define TYPE(CLASS, BASE)                                                      \
  bool WalkUpFrom##CLASS##TypeLoc(CLASS##TypeLoc TL) {                         \
    TRY_TO(WalkUpFrom##BASE##Loc(TL));                                         \
    TRY_TO(Visit##CLASS##TypeLoc(TL));                                         \
    return true;                                                               \
  }                                                                            \
  bool Visit##CLASS##TypeLoc(CLASS##TypeLoc TL) { return true; }
#include "neverc/Tree/TypeNodes.td.h"

// ---- Methods on Decls ----

// Declare Traverse*() for all concrete Decl classes.
#define ABSTRACT_DECL(DECL)
#define DECL(CLASS, BASE) bool Traverse##CLASS##Decl(CLASS##Decl *D);
#include "neverc/Tree/DeclNodes.td.h"
  // The above header #undefs ABSTRACT_DECL and DECL upon exit.

  // Define WalkUpFrom*() and empty Visit*() for all Decl classes.
  bool WalkUpFromDecl(Decl *D) { return getDerived().VisitDecl(D); }
  bool VisitDecl(Decl *D) { return true; }
#define DECL(CLASS, BASE)                                                      \
  bool WalkUpFrom##CLASS##Decl(CLASS##Decl *D) {                               \
    TRY_TO(WalkUpFrom##BASE(D));                                               \
    TRY_TO(Visit##CLASS##Decl(D));                                             \
    return true;                                                               \
  }                                                                            \
  bool Visit##CLASS##Decl(CLASS##Decl *D) { return true; }
#include "neverc/Tree/DeclNodes.td.h"

  bool canIgnoreChildDeclWhileTraversingDeclContext(const Decl *Child);

  bool dataTraverseNode(Stmt *S, DataRecursionQueue *Queue);

private:
  bool TraverseArrayTypeLocHelper(ArrayTypeLoc TL);
  bool TraverseRecordHelper(RecordDecl *D);
  bool TraverseDeclaratorHelper(DeclaratorDecl *D);
  bool TraverseDeclContextHelper(DeclContext *DC);
  bool TraverseFunctionHelper(FunctionDecl *D);
  bool TraverseVarHelper(VarDecl *D);

  bool PostVisitStmt(Stmt *S);
};

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::dataTraverseNode(
    Stmt *S, DataRecursionQueue *Queue) {
  // Top switch stmt: dispatch to TraverseFooStmt for each concrete FooStmt.
  switch (S->getStmtClass()) {
  case Stmt::NoStmtClass:
    break;
#define ABSTRACT_STMT(STMT)
#define STMT(CLASS, PARENT)                                                    \
  case Stmt::CLASS##Class:                                                     \
    return TRAVERSE_STMT_BASE(CLASS, CLASS, S, Queue);
#include "neverc/Tree/StmtNodes.td.h"
  }

  return true;
}

#undef DISPATCH_STMT

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::PostVisitStmt(Stmt *S) {
  // In pre-order traversal mode, each Traverse##STMT method is responsible for
  // calling WalkUpFrom. Therefore, if the user overrides Traverse##STMT and
  // does not call the default implementation, the WalkUpFrom callback is not
  // called. Post-order traversal mode should provide the same behavior
  // regarding method overrides.
  //
  // In post-order traversal mode the Traverse##STMT method, when it receives a
  // DataRecursionQueue, can't call WalkUpFrom after traversing children because
  // it only enqueues the children and does not traverse them. TraverseStmt
  // traverses the enqueued children, and we call WalkUpFrom here.
  //
  // However, to make pre-order and post-order modes identical with regards to
  // whether they call WalkUpFrom at all, we call WalkUpFrom if and only if the
  // user did not override the Traverse##STMT method. We implement the override
  // check with isSameMethod calls below.

  switch (S->getStmtClass()) {
  case Stmt::NoStmtClass:
    break;
#define ABSTRACT_STMT(STMT)
#define STMT(CLASS, PARENT)                                                    \
  case Stmt::CLASS##Class:                                                     \
    if (::neverc::detail::isSameMethod(&RecursiveTreeVisitor::Traverse##CLASS, \
                                       &Derived::Traverse##CLASS)) {           \
      TRY_TO(WalkUpFrom##CLASS(static_cast<CLASS *>(S)));                      \
    }                                                                          \
    break;
#define INITLISTEXPR(CLASS, PARENT)                                            \
  case Stmt::CLASS##Class:                                                     \
    if (::neverc::detail::isSameMethod(&RecursiveTreeVisitor::Traverse##CLASS, \
                                       &Derived::Traverse##CLASS)) {           \
      auto ILE = static_cast<CLASS *>(S);                                      \
      if (auto Syn = ILE->isSemanticForm() ? ILE->getSyntacticForm() : ILE)    \
        TRY_TO(WalkUpFrom##CLASS(Syn));                                        \
      if (auto Sem = ILE->isSemanticForm() ? ILE : ILE->getSemanticForm())     \
        TRY_TO(WalkUpFrom##CLASS(Sem));                                        \
    }                                                                          \
    break;
#include "neverc/Tree/StmtNodes.td.h"
  }

  return true;
}

#undef DISPATCH_STMT

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseStmt(Stmt *S,
                                                 DataRecursionQueue *Queue) {
  if (!S)
    return true;

  if (Queue) {
    Queue->push_back({S, false});
    return true;
  }

  llvm::SmallVector<llvm::PointerIntPair<Stmt *, 1, bool>, 8> LocalQueue;
  LocalQueue.push_back({S, false});

  while (!LocalQueue.empty()) {
    auto &CurrSAndVisited = LocalQueue.back();
    Stmt *CurrS = CurrSAndVisited.getPointer();
    bool Visited = CurrSAndVisited.getInt();
    if (Visited) {
      LocalQueue.pop_back();
      TRY_TO(dataTraverseStmtPost(CurrS));
      if (getDerived().shouldTraversePostOrder()) {
        TRY_TO(PostVisitStmt(CurrS));
      }
      continue;
    }

    if (getDerived().dataTraverseStmtPre(CurrS)) {
      CurrSAndVisited.setInt(true);
      size_t N = LocalQueue.size();
      TRY_TO(dataTraverseNode(CurrS, &LocalQueue));
      // Process new children in the order they were added.
      std::reverse(LocalQueue.begin() + N, LocalQueue.end());
    } else {
      LocalQueue.pop_back();
    }
  }

  return true;
}

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseType(QualType T) {
  if (T.isNull())
    return true;

  switch (T->getTypeClass()) {
#define ABSTRACT_TYPE(CLASS, BASE)
#define TYPE(CLASS, BASE)                                                      \
  case Type::CLASS:                                                            \
    return getDerived().Traverse##CLASS##Type(                                 \
        static_cast<CLASS##Type *>(const_cast<Type *>(T.getTypePtr())));
#include "neverc/Tree/TypeNodes.td.h"
  }

  return true;
}

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseTypeLoc(TypeLoc TL) {
  if (TL.isNull())
    return true;

  switch (TL.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, BASE)
#define TYPELOC(CLASS, BASE)                                                   \
  case TypeLoc::CLASS:                                                         \
    return getDerived().Traverse##CLASS##TypeLoc(TL.castAs<CLASS##TypeLoc>());
#include "neverc/Tree/Type/TypeLocNodes.def"
  }

  return true;
}

// Define the Traverse*Attr(Attr* A) methods
#define VISITORCLASS RecursiveTreeVisitor
#include "neverc/Tree/AttrVisitor.td.h"
#undef VISITORCLASS

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseDecl(Decl *D) {
  if (!D)
    return true;

  // As a syntax visitor, by default we want to ignore declarations for
  // implicit declarations (ones not typed explicitly by the user).
  if (!getDerived().shouldVisitImplicitCode() && D->isImplicit())
    return true;

  switch (D->getKind()) {
#define ABSTRACT_DECL(DECL)
#define DECL(CLASS, BASE)                                                      \
  case Decl::CLASS:                                                            \
    if (!getDerived().Traverse##CLASS##Decl(static_cast<CLASS##Decl *>(D)))    \
      return false;                                                            \
    break;
#include "neverc/Tree/DeclNodes.td.h"
  }
  return true;
}

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseDeclarationNameInfo(
    DeclarationNameInfo) {
  return true;
}

// ----------------- Type traversal -----------------

// This macro makes available a variable T, the passed-in type.
#define DEF_TRAVERSE_TYPE(TYPE, CODE)                                          \
  template <typename Derived>                                                  \
  bool RecursiveTreeVisitor<Derived>::Traverse##TYPE(TYPE *T) {                \
    if (!getDerived().shouldTraversePostOrder())                               \
      TRY_TO(WalkUpFrom##TYPE(T));                                             \
    {                                                                          \
      CODE;                                                                    \
    }                                                                          \
    if (getDerived().shouldTraversePostOrder())                                \
      TRY_TO(WalkUpFrom##TYPE(T));                                             \
    return true;                                                               \
  }

DEF_TRAVERSE_TYPE(BuiltinType, {})

DEF_TRAVERSE_TYPE(ComplexType, { TRY_TO(TraverseType(T->getElementType())); })

DEF_TRAVERSE_TYPE(PointerType, { TRY_TO(TraverseType(T->getPointeeType())); })

DEF_TRAVERSE_TYPE(AdjustedType, { TRY_TO(TraverseType(T->getOriginalType())); })

DEF_TRAVERSE_TYPE(DecayedType, { TRY_TO(TraverseType(T->getOriginalType())); })

DEF_TRAVERSE_TYPE(ConstantArrayType, {
  TRY_TO(TraverseType(T->getElementType()));
  if (T->getSizeExpr())
    TRY_TO(TraverseStmt(const_cast<Expr *>(T->getSizeExpr())));
})

DEF_TRAVERSE_TYPE(IncompleteArrayType,
                  { TRY_TO(TraverseType(T->getElementType())); })

DEF_TRAVERSE_TYPE(VariableArrayType, {
  TRY_TO(TraverseType(T->getElementType()));
  TRY_TO(TraverseStmt(T->getSizeExpr()));
})

DEF_TRAVERSE_TYPE(VectorType, { TRY_TO(TraverseType(T->getElementType())); })

DEF_TRAVERSE_TYPE(ExtVectorType, { TRY_TO(TraverseType(T->getElementType())); })

DEF_TRAVERSE_TYPE(ConstantMatrixType,
                  { TRY_TO(TraverseType(T->getElementType())); })

DEF_TRAVERSE_TYPE(FunctionNoProtoType,
                  { TRY_TO(TraverseType(T->getReturnType())); })

DEF_TRAVERSE_TYPE(FunctionProtoType, {
  TRY_TO(TraverseType(T->getReturnType()));

  for (const auto &A : T->param_types()) {
    TRY_TO(TraverseType(A));
  }
})

DEF_TRAVERSE_TYPE(TypedefType, {})

DEF_TRAVERSE_TYPE(TypeOfExprType,
                  { TRY_TO(TraverseStmt(T->getUnderlyingExpr())); })

DEF_TRAVERSE_TYPE(TypeOfType, { TRY_TO(TraverseType(T->getUnmodifiedType())); })

DEF_TRAVERSE_TYPE(AutoType, { TRY_TO(TraverseType(T->getDeducedType())); })
DEF_TRAVERSE_TYPE(RecordType, {})
DEF_TRAVERSE_TYPE(EnumType, {})

DEF_TRAVERSE_TYPE(AttributedType,
                  { TRY_TO(TraverseType(T->getModifiedType())); })

DEF_TRAVERSE_TYPE(BTFTagAttributedType,
                  { TRY_TO(TraverseType(T->getWrappedType())); })

DEF_TRAVERSE_TYPE(ParenType, { TRY_TO(TraverseType(T->getInnerType())); })

DEF_TRAVERSE_TYPE(MacroQualifiedType,
                  { TRY_TO(TraverseType(T->getUnderlyingType())); })

DEF_TRAVERSE_TYPE(ElaboratedType, { TRY_TO(TraverseType(T->getNamedType())); })

DEF_TRAVERSE_TYPE(AtomicType, { TRY_TO(TraverseType(T->getValueType())); })

DEF_TRAVERSE_TYPE(BitIntType, {})
#undef DEF_TRAVERSE_TYPE

// ----------------- TypeLoc traversal -----------------

// This macro makes available a variable TL, the passed-in TypeLoc.
// If requested, it calls WalkUpFrom* for the Type in the given TypeLoc,
// in addition to WalkUpFrom* for the TypeLoc itself, such that existing
// clients that override the WalkUpFrom*Type() and/or Visit*Type() methods
// continue to work.
#define DEF_TRAVERSE_TYPELOC(TYPE, CODE)                                       \
  template <typename Derived>                                                  \
  bool RecursiveTreeVisitor<Derived>::Traverse##TYPE##Loc(TYPE##Loc TL) {      \
    if (!getDerived().shouldTraversePostOrder()) {                             \
      TRY_TO(WalkUpFrom##TYPE##Loc(TL));                                       \
      if (getDerived().shouldWalkTypesOfTypeLocs())                            \
        TRY_TO(WalkUpFrom##TYPE(const_cast<TYPE *>(TL.getTypePtr())));         \
    }                                                                          \
    {                                                                          \
      CODE;                                                                    \
    }                                                                          \
    if (getDerived().shouldTraversePostOrder()) {                              \
      TRY_TO(WalkUpFrom##TYPE##Loc(TL));                                       \
      if (getDerived().shouldWalkTypesOfTypeLocs())                            \
        TRY_TO(WalkUpFrom##TYPE(const_cast<TYPE *>(TL.getTypePtr())));         \
    }                                                                          \
    return true;                                                               \
  }

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseQualifiedTypeLoc(
    QualifiedTypeLoc TL) {
  // Move this over to the 'main' typeloc tree.  Note that this is a
  // move -- we pretend that we were really looking at the unqualified
  // typeloc all along -- rather than a recursion, so we don't follow
  // the normal CRTP plan of going through
  // getDerived().TraverseTypeLoc.  If we did, we'd be traversing
  // twice for the same type (once as a QualifiedTypeLoc version of
  // the type, once as an UnqualifiedTypeLoc version of the type),
  // which in effect means we'd call VisitTypeLoc twice with the
  // 'same' type.  This solves that problem, at the cost of never
  // seeing the qualified version of the type (unless the client
  // subclasses TraverseQualifiedTypeLoc themselves).  It's not a
  // perfect solution.  A perfect solution probably requires making
  // QualifiedTypeLoc a wrapper around TypeLoc -- like QualType is a
  // wrapper around Type* -- rather than being its own class in the
  // type hierarchy.
  return TraverseTypeLoc(TL.getUnqualifiedLoc());
}

DEF_TRAVERSE_TYPELOC(BuiltinType, {})

DEF_TRAVERSE_TYPELOC(ComplexType, {
  TRY_TO(TraverseType(TL.getTypePtr()->getElementType()));
})

DEF_TRAVERSE_TYPELOC(PointerType,
                     { TRY_TO(TraverseTypeLoc(TL.getPointeeLoc())); })

// We traverse this in the type case as well, but how is it not reached through
// the pointee type?
DEF_TRAVERSE_TYPELOC(AdjustedType,
                     { TRY_TO(TraverseTypeLoc(TL.getOriginalLoc())); })

DEF_TRAVERSE_TYPELOC(DecayedType,
                     { TRY_TO(TraverseTypeLoc(TL.getOriginalLoc())); })

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseArrayTypeLocHelper(
    ArrayTypeLoc TL) {
  // This isn't available for ArrayType, but is for the ArrayTypeLoc.
  TRY_TO(TraverseStmt(TL.getSizeExpr()));
  return true;
}

DEF_TRAVERSE_TYPELOC(ConstantArrayType, {
  TRY_TO(TraverseTypeLoc(TL.getElementLoc()));
  TRY_TO(TraverseArrayTypeLocHelper(TL));
})

DEF_TRAVERSE_TYPELOC(IncompleteArrayType, {
  TRY_TO(TraverseTypeLoc(TL.getElementLoc()));
  TRY_TO(TraverseArrayTypeLocHelper(TL));
})

DEF_TRAVERSE_TYPELOC(VariableArrayType, {
  TRY_TO(TraverseTypeLoc(TL.getElementLoc()));
  TRY_TO(TraverseArrayTypeLocHelper(TL));
})

DEF_TRAVERSE_TYPELOC(VectorType, {
  TRY_TO(TraverseType(TL.getTypePtr()->getElementType()));
})

DEF_TRAVERSE_TYPELOC(ExtVectorType, {
  TRY_TO(TraverseType(TL.getTypePtr()->getElementType()));
})

DEF_TRAVERSE_TYPELOC(ConstantMatrixType, {
  TRY_TO(TraverseStmt(TL.getAttrRowOperand()));
  TRY_TO(TraverseStmt(TL.getAttrColumnOperand()));
  TRY_TO(TraverseType(TL.getTypePtr()->getElementType()));
})

DEF_TRAVERSE_TYPELOC(FunctionNoProtoType,
                     { TRY_TO(TraverseTypeLoc(TL.getReturnLoc())); })

DEF_TRAVERSE_TYPELOC(FunctionProtoType, {
  TRY_TO(TraverseTypeLoc(TL.getReturnLoc()));

  const FunctionProtoType *T = TL.getTypePtr();

  for (unsigned I = 0, E = TL.getNumParams(); I != E; ++I) {
    if (TL.getParam(I)) {
      TRY_TO(TraverseDecl(TL.getParam(I)));
    } else if (I < T->getNumParams()) {
      TRY_TO(TraverseType(T->getParamType(I)));
    }
  }
})

DEF_TRAVERSE_TYPELOC(TypedefType, {})

DEF_TRAVERSE_TYPELOC(TypeOfExprType, {
  TRY_TO(TraverseStmt(TL.getTypePtr()->getUnderlyingExpr()));
})

DEF_TRAVERSE_TYPELOC(TypeOfType, {
  TRY_TO(TraverseTypeLoc(TL.getUnmodifiedTInfo()->getTypeLoc()));
})

DEF_TRAVERSE_TYPELOC(AutoType, {
  TRY_TO(TraverseType(TL.getTypePtr()->getDeducedType()));
})

DEF_TRAVERSE_TYPELOC(RecordType, {})
DEF_TRAVERSE_TYPELOC(EnumType, {})

DEF_TRAVERSE_TYPELOC(ParenType, { TRY_TO(TraverseTypeLoc(TL.getInnerLoc())); })

DEF_TRAVERSE_TYPELOC(MacroQualifiedType,
                     { TRY_TO(TraverseTypeLoc(TL.getInnerLoc())); })

DEF_TRAVERSE_TYPELOC(AttributedType,
                     { TRY_TO(TraverseTypeLoc(TL.getModifiedLoc())); })

DEF_TRAVERSE_TYPELOC(BTFTagAttributedType,
                     { TRY_TO(TraverseTypeLoc(TL.getWrappedLoc())); })

DEF_TRAVERSE_TYPELOC(ElaboratedType,
                     { TRY_TO(TraverseTypeLoc(TL.getNamedTypeLoc())); })

DEF_TRAVERSE_TYPELOC(AtomicType, { TRY_TO(TraverseTypeLoc(TL.getValueLoc())); })

DEF_TRAVERSE_TYPELOC(BitIntType, {})
#undef DEF_TRAVERSE_TYPELOC

// ----------------- Decl traversal -----------------
//
// For a Decl, we automate (in the DEF_TRAVERSE_DECL macro) traversing
// the children that come from the DeclContext associated with it.
// Therefore each Traverse* only needs to worry about children other
// than those.

template <typename Derived>
bool RecursiveTreeVisitor<
    Derived>::canIgnoreChildDeclWhileTraversingDeclContext(const Decl *Child) {
  return false;
}

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseDeclContextHelper(DeclContext *DC) {
  if (!DC)
    return true;

  for (auto *Child : DC->decls()) {
    if (!canIgnoreChildDeclWhileTraversingDeclContext(Child))
      TRY_TO(TraverseDecl(Child));
  }

  return true;
}

// This macro makes available a variable D, the passed-in decl.
#define DEF_TRAVERSE_DECL(DECL, CODE)                                          \
  template <typename Derived>                                                  \
  bool RecursiveTreeVisitor<Derived>::Traverse##DECL(DECL *D) {                \
    bool ShouldVisitChildren = true;                                           \
    bool ReturnValue = true;                                                   \
    if (!getDerived().shouldTraversePostOrder())                               \
      TRY_TO(WalkUpFrom##DECL(D));                                             \
    {                                                                          \
      CODE;                                                                    \
    }                                                                          \
    if (ReturnValue && ShouldVisitChildren)                                    \
      TRY_TO(TraverseDeclContextHelper(dyn_cast<DeclContext>(D)));             \
    if (ReturnValue) {                                                         \
      /* Visit any attributes attached to this declaration. */                 \
      for (auto *I : D->attrs())                                               \
        TRY_TO(getDerived().TraverseAttr(I));                                  \
    }                                                                          \
    if (ReturnValue && getDerived().shouldTraversePostOrder())                 \
      TRY_TO(WalkUpFrom##DECL(D));                                             \
    return ReturnValue;                                                        \
  }

DEF_TRAVERSE_DECL(EmptyDecl, {})

DEF_TRAVERSE_DECL(FileScopeAsmDecl,
                  { TRY_TO(TraverseStmt(D->getAsmString())); })

DEF_TRAVERSE_DECL(StaticAssertDecl, {
  TRY_TO(TraverseStmt(D->getAssertExpr()));
  TRY_TO(TraverseStmt(D->getMessage()));
})

DEF_TRAVERSE_DECL(TranslationUnitDecl, {
  // Code in an unnamed namespace shows up automatically in
  // decls_begin()/decls_end().  Thus we don't need to recurse on
  // D->getAnonymousNamespace().

  // If the traversal scope is set, then consider them to be the children of
  // the TUDecl, rather than traversing (and loading?) all top-level decls.
  auto Scope = D->getTreeContext().getTraversalScope();
  bool HasLimitedScope =
      Scope.size() != 1 || !isa<TranslationUnitDecl>(Scope.front());
  if (HasLimitedScope) {
    ShouldVisitChildren = false; // we'll do that here instead
    for (auto *Child : Scope) {
      if (!canIgnoreChildDeclWhileTraversingDeclContext(Child))
        TRY_TO(TraverseDecl(Child));
    }
  }
})

DEF_TRAVERSE_DECL(PragmaCommentDecl, {})

DEF_TRAVERSE_DECL(PragmaDetectMismatchDecl, {})

DEF_TRAVERSE_DECL(ExternCContextDecl, {})

// LabelDecl: no traversable code.
DEF_TRAVERSE_DECL(LabelDecl, {})

DEF_TRAVERSE_DECL(TypedefDecl, {
  TRY_TO(TraverseTypeLoc(D->getTypeSourceInfo()->getTypeLoc()));
  // We shouldn't traverse D->getTypeForDecl(); it's a result of
  // declaring the typedef, not something that was written in the
  // source.
})

DEF_TRAVERSE_DECL(EnumDecl, {
  if (auto *TSI = D->getIntegerTypeSourceInfo())
    TRY_TO(TraverseTypeLoc(TSI->getTypeLoc()));
  // The enumerators are already traversed by
  // decls_begin()/decls_end().
})

// Helper methods for RecordDecl and its children.
template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseRecordHelper(RecordDecl *D) {
  // We shouldn't traverse D->getTypeForDecl(); it's a result of
  // declaring the type, not something that was written in the source.

  return true;
}

DEF_TRAVERSE_DECL(RecordDecl, { TRY_TO(TraverseRecordHelper(D)); })

DEF_TRAVERSE_DECL(EnumConstantDecl, { TRY_TO(TraverseStmt(D->getInitExpr())); })

DEF_TRAVERSE_DECL(IndirectFieldDecl, {})

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseDeclaratorHelper(
    DeclaratorDecl *D) {
  if (D->getTypeSourceInfo())
    TRY_TO(TraverseTypeLoc(D->getTypeSourceInfo()->getTypeLoc()));
  else
    TRY_TO(TraverseType(D->getType()));
  return true;
}

DEF_TRAVERSE_DECL(FieldDecl, {
  TRY_TO(TraverseDeclaratorHelper(D));
  if (D->isBitField())
    TRY_TO(TraverseStmt(D->getBitWidth()));
})

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseFunctionHelper(FunctionDecl *D) {
  TRY_TO(TraverseDeclarationNameInfo(D->getNameInfo()));

  // Visit the function type itself, which can be either
  // FunctionNoProtoType or FunctionProtoType, or a typedef.  This
  // also covers the return type and the function parameters,
  // including exception specifications.
  if (TypeSourceInfo *TSI = D->getTypeSourceInfo()) {
    TRY_TO(TraverseTypeLoc(TSI->getTypeLoc()));
  } else if (getDerived().shouldVisitImplicitCode()) {
    // Visit parameter variable declarations of the implicit function
    // if the traverser is visiting implicit code. Parameter variable
    // declarations do not have valid TypeSourceInfo, so to visit them
    // we need to traverse the declarations explicitly.
    for (ParmVarDecl *Parameter : D->parameters()) {
      TRY_TO(TraverseDecl(Parameter));
    }
  }

  bool VisitBody = D->isThisDeclarationADefinition();

  if (VisitBody) {
    TRY_TO(TraverseStmt(D->getBody()));
  }
  return true;
}

DEF_TRAVERSE_DECL(FunctionDecl, {
  // We skip decls_begin/decls_end, which are already covered by
  // TraverseFunctionHelper().
  ShouldVisitChildren = false;
  ReturnValue = TraverseFunctionHelper(D);
})

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseVarHelper(VarDecl *D) {
  TRY_TO(TraverseDeclaratorHelper(D));
  // Default params are taken care of when we traverse the ParmVarDecl.
  if (!isa<ParmVarDecl>(D))
    TRY_TO(TraverseStmt(D->getInit()));
  return true;
}

DEF_TRAVERSE_DECL(VarDecl, { TRY_TO(TraverseVarHelper(D)); })

DEF_TRAVERSE_DECL(ImplicitParamDecl, { TRY_TO(TraverseVarHelper(D)); })

DEF_TRAVERSE_DECL(ParmVarDecl, {
  TRY_TO(TraverseVarHelper(D));

  if (D->hasDefaultArg())
    TRY_TO(TraverseStmt(D->getDefaultArg()));
})

#undef DEF_TRAVERSE_DECL

// ----------------- Stmt traversal -----------------
//
// For stmts, we automate (in the DEF_TRAVERSE_STMT macro) iterating
// over the children defined in children() (every stmt defines these,
// though sometimes the range is empty).  Each individual Traverse*
// method only needs to worry about children other than those.

// This macro makes available a variable S, the passed-in stmt.
#define DEF_TRAVERSE_STMT(STMT, CODE)                                          \
  template <typename Derived>                                                  \
  bool RecursiveTreeVisitor<Derived>::Traverse##STMT(                          \
      STMT *S, DataRecursionQueue *Queue) {                                    \
    bool ShouldVisitChildren = true;                                           \
    bool ReturnValue = true;                                                   \
    if (!getDerived().shouldTraversePostOrder())                               \
      TRY_TO(WalkUpFrom##STMT(S));                                             \
    {                                                                          \
      CODE;                                                                    \
    }                                                                          \
    if (ShouldVisitChildren) {                                                 \
      for (Stmt *SubStmt : getDerived().getStmtChildren(S)) {                  \
        TRY_TO_TRAVERSE_OR_ENQUEUE_STMT(SubStmt);                              \
      }                                                                        \
    }                                                                          \
    /* Call WalkUpFrom if TRY_TO_TRAVERSE_OR_ENQUEUE_STMT has traversed the    \
     * children already. If TRY_TO_TRAVERSE_OR_ENQUEUE_STMT only enqueued the  \
     * children, PostVisitStmt will call WalkUpFrom after we are done visiting \
     * children. */                                                            \
    if (!Queue && ReturnValue && getDerived().shouldTraversePostOrder()) {     \
      TRY_TO(WalkUpFrom##STMT(S));                                             \
    }                                                                          \
    return ReturnValue;                                                        \
  }

DEF_TRAVERSE_STMT(GCCAsmStmt, {
  TRY_TO_TRAVERSE_OR_ENQUEUE_STMT(S->getAsmString());
  for (unsigned I = 0, E = S->getNumInputs(); I < E; ++I) {
    TRY_TO_TRAVERSE_OR_ENQUEUE_STMT(S->getInputConstraintLiteral(I));
  }
  for (unsigned I = 0, E = S->getNumOutputs(); I < E; ++I) {
    TRY_TO_TRAVERSE_OR_ENQUEUE_STMT(S->getOutputConstraintLiteral(I));
  }
  for (unsigned I = 0, E = S->getNumClobbers(); I < E; ++I) {
    TRY_TO_TRAVERSE_OR_ENQUEUE_STMT(S->getClobberStringLiteral(I));
  }
  // children() iterates over inputExpr and outputExpr.
})

DEF_TRAVERSE_STMT(MSAsmStmt, {})

DEF_TRAVERSE_STMT(DeclStmt, {
  for (auto *I : S->decls()) {
    TRY_TO(TraverseDecl(I));
  }
  // Suppress the default iteration over children() by
  // returning.  Here's why: A DeclStmt looks like 'type var [=
  // initializer]'.  The decls above already traverse over the
  // initializers, so we don't have to do it again (which
  // children() would do).
  ShouldVisitChildren = false;
})

// These non-expr stmts (most of them), do not need any action except
// iterating over the children.
DEF_TRAVERSE_STMT(BreakStmt, {})
DEF_TRAVERSE_STMT(CaseStmt, {})
DEF_TRAVERSE_STMT(CompoundStmt, {})
DEF_TRAVERSE_STMT(ContinueStmt, {})
DEF_TRAVERSE_STMT(DefaultStmt, {})
DEF_TRAVERSE_STMT(DoStmt, {})
DEF_TRAVERSE_STMT(ForStmt, {})
DEF_TRAVERSE_STMT(GotoStmt, {})
DEF_TRAVERSE_STMT(IfStmt, {})
DEF_TRAVERSE_STMT(IndirectGotoStmt, {})
DEF_TRAVERSE_STMT(LabelStmt, {})
DEF_TRAVERSE_STMT(AttributedStmt, {})
DEF_TRAVERSE_STMT(NullStmt, {})

DEF_TRAVERSE_STMT(ReturnStmt, {})
DEF_TRAVERSE_STMT(SwitchStmt, {})
DEF_TRAVERSE_STMT(WhileStmt, {})

DEF_TRAVERSE_STMT(ConstantExpr, {})

DEF_TRAVERSE_STMT(DeclRefExpr,
                  { TRY_TO(TraverseDeclarationNameInfo(S->getNameInfo())); })

DEF_TRAVERSE_STMT(MemberExpr, {
  TRY_TO(TraverseDeclarationNameInfo(S->getMemberNameInfo()));
})

DEF_TRAVERSE_STMT(ImplicitCastExpr, {
                                        // We don't traverse the cast type, as
                                        // it's not written in the source code.
                                    })

DEF_TRAVERSE_STMT(CStyleCastExpr, {
  TRY_TO(TraverseTypeLoc(S->getTypeInfoAsWritten()->getTypeLoc()));
})

template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseSynOrSemInitListExpr(
    InitListExpr *S, DataRecursionQueue *Queue) {
  if (S) {
    // Skip this if we traverse postorder. We will visit it later
    // in PostVisitStmt.
    if (!getDerived().shouldTraversePostOrder())
      TRY_TO(WalkUpFromInitListExpr(S));

    // All we need are the default actions.
    for (Stmt *SubStmt : S->children()) {
      TRY_TO_TRAVERSE_OR_ENQUEUE_STMT(SubStmt);
    }

    if (!Queue && getDerived().shouldTraversePostOrder())
      TRY_TO(WalkUpFromInitListExpr(S));
  }
  return true;
}

// If shouldVisitImplicitCode() returns false, this method traverses only the
// syntactic form of InitListExpr.
// If shouldVisitImplicitCode() return true, this method is called once for
// each pair of syntactic and semantic InitListExpr, and it traverses the
// subtrees defined by the two forms. This may cause some of the children to be
// visited twice, if they appear both in the syntactic and the semantic form.
//
// There is no guarantee about which form \p S takes when this method is called.
template <typename Derived>
bool RecursiveTreeVisitor<Derived>::TraverseInitListExpr(
    InitListExpr *S, DataRecursionQueue *Queue) {
  if (S->isSemanticForm() && S->isSyntacticForm()) {
    // `S` does not have alternative forms, traverse only once.
    TRY_TO(TraverseSynOrSemInitListExpr(S, Queue));
    return true;
  }
  TRY_TO(TraverseSynOrSemInitListExpr(
      S->isSemanticForm() ? S->getSyntacticForm() : S, Queue));
  if (getDerived().shouldVisitImplicitCode()) {
    // Only visit the semantic form if the clients are interested in implicit
    // compiler-generated.
    TRY_TO(TraverseSynOrSemInitListExpr(
        S->isSemanticForm() ? S : S->getSemanticForm(), Queue));
  }
  return true;
}

// GenericSelectionExpr is a special case because the types and expressions
// are interleaved.  We also need to watch out for null types (default
// generic associations).
DEF_TRAVERSE_STMT(GenericSelectionExpr, {
  if (S->isExprPredicate())
    TRY_TO(TraverseStmt(S->getControllingExpr()));
  else
    TRY_TO(TraverseTypeLoc(S->getControllingType()->getTypeLoc()));

  for (const GenericSelectionExpr::Association Assoc : S->associations()) {
    if (TypeSourceInfo *TSI = Assoc.getTypeSourceInfo())
      TRY_TO(TraverseTypeLoc(TSI->getTypeLoc()));
    TRY_TO_TRAVERSE_OR_ENQUEUE_STMT(Assoc.getAssociationExpr());
  }
  ShouldVisitChildren = false;
})

// PseudoObjectExpr is a special case because of the weirdness with
// syntactic expressions and opaque values.
DEF_TRAVERSE_STMT(PseudoObjectExpr, {
  TRY_TO_TRAVERSE_OR_ENQUEUE_STMT(S->getSyntacticForm());
  for (PseudoObjectExpr::semantics_iterator i = S->semantics_begin(),
                                            e = S->semantics_end();
       i != e; ++i) {
    Expr *sub = *i;
    if (OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(sub))
      sub = OVE->getSourceExpr();
    TRY_TO_TRAVERSE_OR_ENQUEUE_STMT(sub);
  }
  ShouldVisitChildren = false;
})

DEF_TRAVERSE_STMT(OffsetOfExpr, {
  // The child-iterator will pick up the expression representing
  // the field.
  TRY_TO(TraverseTypeLoc(S->getTypeSourceInfo()->getTypeLoc()));
})

DEF_TRAVERSE_STMT(UnaryExprOrTypeTraitExpr, {
  // The child-iterator will pick up the arg if it's an expression,
  // but not if it's a type.
  if (S->isArgumentType())
    TRY_TO(TraverseTypeLoc(S->getArgumentTypeInfo()->getTypeLoc()));
})

DEF_TRAVERSE_STMT(VAArgExpr, {
  // The child-iterator will pick up the expression argument.
  TRY_TO(TraverseTypeLoc(S->getWrittenTypeInfo()->getTypeLoc()));
})

// These expressions all might take explicit template arguments.
// We traverse those if so.
DEF_TRAVERSE_STMT(CallExpr, {})
// These exprs (most of them), do not need any action except iterating
// over the children.
DEF_TRAVERSE_STMT(AddrLabelExpr, {})
DEF_TRAVERSE_STMT(ArraySubscriptExpr, {})
DEF_TRAVERSE_STMT(MatrixSubscriptExpr, {})

DEF_TRAVERSE_STMT(ChooseExpr, {})
DEF_TRAVERSE_STMT(CompoundLiteralExpr, {
  TRY_TO(TraverseTypeLoc(S->getTypeSourceInfo()->getTypeLoc()));
})
DEF_TRAVERSE_STMT(ExprWithCleanups, {})

DEF_TRAVERSE_STMT(NullPtrLiteralExpr, {})
DEF_TRAVERSE_STMT(DesignatedInitExpr, {})
DEF_TRAVERSE_STMT(DesignatedInitUpdateExpr, {})
DEF_TRAVERSE_STMT(ExtVectorElementExpr, {})
DEF_TRAVERSE_STMT(ImplicitValueInitExpr, {})
DEF_TRAVERSE_STMT(NoInitExpr, {})
DEF_TRAVERSE_STMT(ArrayInitLoopExpr, {
  if (OpaqueValueExpr *OVE = S->getCommonExpr())
    TRY_TO_TRAVERSE_OR_ENQUEUE_STMT(OVE->getSourceExpr());
})
DEF_TRAVERSE_STMT(ArrayInitIndexExpr, {})

DEF_TRAVERSE_STMT(ParenExpr, {})
DEF_TRAVERSE_STMT(ParenListExpr, {})
DEF_TRAVERSE_STMT(PredefinedExpr, {})
DEF_TRAVERSE_STMT(ShuffleVectorExpr, {})
DEF_TRAVERSE_STMT(ConvertVectorExpr, {})
DEF_TRAVERSE_STMT(StmtExpr, {})
DEF_TRAVERSE_STMT(SourceLocExpr, {})

DEF_TRAVERSE_STMT(SEHTryStmt, {})
DEF_TRAVERSE_STMT(SEHExceptStmt, {})
DEF_TRAVERSE_STMT(SEHFinallyStmt, {})
DEF_TRAVERSE_STMT(SEHLeaveStmt, {})
DEF_TRAVERSE_STMT(OpaqueValueExpr, {})
DEF_TRAVERSE_STMT(TypoExpr, {})
DEF_TRAVERSE_STMT(RecoveryExpr, {})

// These operators (all of them) do not need any action except
// iterating over the children.
DEF_TRAVERSE_STMT(BinaryConditionalOperator, {})
DEF_TRAVERSE_STMT(ConditionalOperator, {})
DEF_TRAVERSE_STMT(UnaryOperator, {})
DEF_TRAVERSE_STMT(BinaryOperator, {})
DEF_TRAVERSE_STMT(CompoundAssignOperator, {})
DEF_TRAVERSE_STMT(AtomicExpr, {})

// These literals (all of them) do not need any action.
DEF_TRAVERSE_STMT(IntegerLiteral, {})
DEF_TRAVERSE_STMT(FixedPointLiteral, {})
DEF_TRAVERSE_STMT(CharacterLiteral, {})
DEF_TRAVERSE_STMT(FloatingLiteral, {})
DEF_TRAVERSE_STMT(ImaginaryLiteral, {})
DEF_TRAVERSE_STMT(StringLiteral, {})

#undef DEF_TRAVERSE_STMT
#undef TRAVERSE_STMT
#undef TRAVERSE_STMT_BASE

#undef TRY_TO

} // end namespace neverc

#endif // NEVERC_TREE_RECURSIVEASTVISITOR_H
