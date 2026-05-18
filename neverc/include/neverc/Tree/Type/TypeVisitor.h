#ifndef NEVERC_AST_TYPEVISITOR_H
#define NEVERC_AST_TYPEVISITOR_H

#include "neverc/Tree/Type/Type.h"

namespace neverc {

#define DISPATCH(CLASS)                                                        \
  return static_cast<ImplClass *>(this)->Visit##CLASS(                         \
      static_cast<const CLASS *>(T))

template <typename ImplClass, typename RetTy = void> class TypeVisitor {
public:
  RetTy Visit(const Type *T) {
    // Top switch stmt: dispatch to VisitFooType for each FooType.
    switch (T->getTypeClass()) {
#define ABSTRACT_TYPE(CLASS, PARENT)
#define TYPE(CLASS, PARENT)                                                    \
  case Type::CLASS:                                                            \
    DISPATCH(CLASS##Type);
#include "neverc/Tree/TypeNodes.td.h"
    }
    llvm_unreachable("Unknown type class!");
  }

  // If the implementation does not override Visit* for a type class, fall back
  // on the superclass.
#define TYPE(CLASS, PARENT)                                                    \
  RetTy Visit##CLASS##Type(const CLASS##Type *T) { DISPATCH(PARENT); }
#include "neverc/Tree/TypeNodes.td.h"

  RetTy VisitType(const Type *) { return RetTy(); }
};

#undef DISPATCH

} // end namespace neverc

#endif
