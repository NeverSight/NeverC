#ifndef NEVERC_TREE_TYPELOCVISITOR_H
#define NEVERC_TREE_TYPELOCVISITOR_H

#include "neverc/Tree/Type/TypeLoc.h"
#include "llvm/Support/ErrorHandling.h"

namespace neverc {

#define DISPATCH(CLASSNAME)                                                    \
  return static_cast<ImplClass *>(this)->Visit##CLASSNAME(                     \
      TyLoc.castAs<CLASSNAME>())

template <typename ImplClass, typename RetTy = void> class TypeLocVisitor {
public:
  RetTy Visit(TypeLoc TyLoc) {
    switch (TyLoc.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  case TypeLoc::CLASS:                                                         \
    DISPATCH(CLASS##TypeLoc);
#include "neverc/Tree/Type/TypeLocNodes.def"
    }
    llvm_unreachable("unexpected type loc class!");
  }

  RetTy Visit(UnqualTypeLoc TyLoc) {
    switch (TyLoc.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  case TypeLoc::CLASS:                                                         \
    DISPATCH(CLASS##TypeLoc);
#include "neverc/Tree/Type/TypeLocNodes.def"
    }
    llvm_unreachable("unexpected type loc class!");
  }

#define TYPELOC(CLASS, PARENT)                                                 \
  RetTy Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc) { DISPATCH(PARENT); }
#include "neverc/Tree/Type/TypeLocNodes.def"

  RetTy VisitTypeLoc(TypeLoc TyLoc) { return RetTy(); }
};

#undef DISPATCH

} // end namespace neverc

#endif // NEVERC_TREE_TYPELOCVISITOR_H
