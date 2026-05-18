#ifndef NEVERC_AST_LOCINFOTYPE_H
#define NEVERC_AST_LOCINFOTYPE_H

#include "neverc/Tree/Type/Type.h"

namespace neverc {

class TypeSourceInfo;

class LocInfoType : public Type {
  enum {
    // The last number that can fit in Type's TC.
    // Avoids conflict with an existing Type class.
    LocInfo = Type::TypeLast + 1
  };

  TypeSourceInfo *DeclInfo;

  LocInfoType(QualType ty, TypeSourceInfo *TInfo)
      : Type((TypeClass)LocInfo, ty, ty->getDependence()), DeclInfo(TInfo) {
    assert(getTypeClass() == (TypeClass)LocInfo && "LocInfo didn't fit in TC?");
  }
  friend class Sema;

public:
  QualType getType() const { return getCanonicalTypeInternal(); }
  TypeSourceInfo *getTypeSourceInfo() const { return DeclInfo; }

  void getAsStringInternal(std::string &Str,
                           const PrintingPolicy &Policy) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == (TypeClass)LocInfo;
  }
};

} // end namespace neverc

#endif // NEVERC_AST_LOCINFOTYPE_H
