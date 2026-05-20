#ifndef NEVERC_ANALYZE_OVERLOAD_H
#define NEVERC_ANALYZE_OVERLOAD_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace neverc {

enum ImplicitConversionKind {
  ICK_Identity = 0,

  ICK_Lvalue_To_Rvalue,

  ICK_Array_To_Pointer,

  ICK_Function_To_Pointer,

  ICK_Function_Conversion,

  ICK_Qualification,

  ICK_Integral_Promotion,

  ICK_Floating_Promotion,

  ICK_Complex_Promotion,

  ICK_Integral_Conversion,

  ICK_Floating_Conversion,

  ICK_Complex_Conversion,

  ICK_Floating_Integral,

  ICK_Pointer_Conversion,

  ICK_Boolean_Conversion,

  ICK_Compatible_Conversion,

  ICK_Vector_Conversion,

  ICK_Vector_Splat,

  ICK_Complex_Real,

  ICK_TransparentUnionConversion,

  ICK_Fixed_Point_Conversion,
};

class StandardConversionSequence {
public:
  ImplicitConversionKind First : 8;

  ImplicitConversionKind Second : 8;

  ImplicitConversionKind Third : 8;

  unsigned DeprecatedStringLiteralToCharPtr : 1;

  void *FromTypePtr;

  void *ToTypePtrs[3];

  void setFromType(QualType T) { FromTypePtr = T.getAsOpaquePtr(); }

  void setToType(unsigned Idx, QualType T) {
    assert(Idx < 3 && "To type index is out of range");
    ToTypePtrs[Idx] = T.getAsOpaquePtr();
  }

  void setAllToTypes(QualType T) {
    ToTypePtrs[0] = T.getAsOpaquePtr();
    ToTypePtrs[1] = ToTypePtrs[0];
    ToTypePtrs[2] = ToTypePtrs[0];
  }

  QualType getFromType() const {
    return QualType::getFromOpaquePtr(FromTypePtr);
  }

  QualType getToType(unsigned Idx) const {
    assert(Idx < 3 && "To type index is out of range");
    return QualType::getFromOpaquePtr(ToTypePtrs[Idx]);
  }

  void setAsIdentityConversion();

  bool isIdentityConversion() const {
    return Second == ICK_Identity && Third == ICK_Identity;
  }
};

struct BadConversionSequence {
  // This can be null, e.g. for implicit object arguments.
  Expr *FromExpr;

private:
  // The type we're converting from (an opaque QualType).
  void *FromTy;

  // The type we're converting to (an opaque QualType).
  void *ToTy;

public:
  void init(Expr *From, QualType To) {
    init(From->getType(), To);
    FromExpr = From;
  }

  void init(QualType From, QualType To) {
    FromExpr = nullptr;
    setFromType(From);
    setToType(To);
  }

  QualType getFromType() const { return QualType::getFromOpaquePtr(FromTy); }
  QualType getToType() const { return QualType::getFromOpaquePtr(ToTy); }

  void setFromExpr(Expr *E) {
    FromExpr = E;
    setFromType(E->getType());
  }

  void setFromType(QualType T) { FromTy = T.getAsOpaquePtr(); }
  void setToType(QualType T) { ToTy = T.getAsOpaquePtr(); }
};

class ImplicitConversionSequence {
public:
  enum Kind { StandardConversion = 0, BadConversion = 1 };

private:
  enum { Uninitialized = 2 };

  unsigned ConversionKind = Uninitialized;

  void setKind(Kind K) { ConversionKind = K; }

  void destruct() {}

public:
  union {
    StandardConversionSequence Standard;
    BadConversionSequence Bad;
  };

  ImplicitConversionSequence() { Standard.setAsIdentityConversion(); }

  ImplicitConversionSequence(const ImplicitConversionSequence &Other)
      : ConversionKind(Other.ConversionKind) {
    switch (ConversionKind) {
    case Uninitialized:
      break;
    case StandardConversion:
      Standard = Other.Standard;
      break;
    case BadConversion:
      Bad = Other.Bad;
      break;
    }
  }

  ImplicitConversionSequence &
  operator=(const ImplicitConversionSequence &Other) {
    destruct();
    new (this) ImplicitConversionSequence(Other);
    return *this;
  }

  ~ImplicitConversionSequence() { destruct(); }

  Kind getKind() const {
    assert(isInitialized() && "querying uninitialized conversion");
    return Kind(ConversionKind);
  }

  bool isBad() const { return getKind() == BadConversion; }

  bool isInitialized() const { return ConversionKind != Uninitialized; }

  void setBad(Expr *FromExpr, QualType ToType) {
    setKind(BadConversion);
    Bad.init(FromExpr, ToType);
  }

  void setBad(QualType FromType, QualType ToType) {
    setKind(BadConversion);
    Bad.init(FromType, ToType);
  }

  void setStandard() { setKind(StandardConversion); }

  void setAsIdentityConversion(QualType T) {
    setStandard();
    Standard.setAsIdentityConversion();
    Standard.setFromType(T);
    Standard.setAllToTypes(T);
  }

  static ImplicitConversionSequence getNullptrToBool(QualType SourceType,
                                                     QualType DestType,
                                                     bool NeedLValToRVal) {
    ImplicitConversionSequence ICS;
    ICS.setStandard();
    ICS.Standard.setAsIdentityConversion();
    ICS.Standard.setFromType(SourceType);
    if (NeedLValToRVal)
      ICS.Standard.First = ICK_Lvalue_To_Rvalue;
    ICS.Standard.setToType(0, SourceType);
    ICS.Standard.Second = ICK_Boolean_Conversion;
    ICS.Standard.setToType(1, DestType);
    ICS.Standard.setToType(2, DestType);
    return ICS;
  }
};

} // namespace neverc

#endif // NEVERC_ANALYZE_OVERLOAD_H
