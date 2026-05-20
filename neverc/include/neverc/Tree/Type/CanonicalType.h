#ifndef NEVERC_TREE_CANONICALTYPE_H
#define NEVERC_TREE_CANONICALTYPE_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <cassert>
#include <iterator>
#include <type_traits>

namespace neverc {

template <typename T> class CanProxy;
template <typename T> struct CanProxyAdaptor;
class EnumDecl;
class RecordDecl;
class TagDecl;

//----------------------------------------------------------------------------//
// Canonical, qualified type template
//----------------------------------------------------------------------------//

template <typename T = Type> class CanQual {
  QualType Stored;

public:
  CanQual() = default;

  template <typename U>
  CanQual(const CanQual<U> &Other,
          std::enable_if_t<std::is_base_of<T, U>::value, int> = 0);

  const T *getTypePtr() const { return cast<T>(Stored.getTypePtr()); }

  const T *getTypePtrOrNull() const {
    return cast_or_null<T>(Stored.getTypePtrOrNull());
  }

  operator QualType() const { return Stored; }

  explicit operator bool() const { return !isNull(); }

  bool isNull() const { return Stored.isNull(); }

  SplitQualType split() const { return Stored.split(); }

  template <typename U> CanProxy<U> getAs() const;

  template <typename U> CanProxy<U> castAs() const;

  CanProxy<T> operator->() const;

  Qualifiers getQualifiers() const { return Stored.getLocalQualifiers(); }

  unsigned getCVRQualifiers() const { return Stored.getLocalCVRQualifiers(); }

  bool hasQualifiers() const { return Stored.hasLocalQualifiers(); }

  bool isConstQualified() const { return Stored.isLocalConstQualified(); }

  bool isVolatileQualified() const { return Stored.isLocalVolatileQualified(); }

  bool isRestrictQualified() const { return Stored.isLocalRestrictQualified(); }

  bool isCanonicalAsParam() const { return Stored.isCanonicalAsParam(); }

  CanQual<T> getUnqualifiedType() const;

  QualType withConst() const { return Stored.withConst(); }

  bool isMoreQualifiedThan(CanQual<T> Other) const {
    return Stored.isMoreQualifiedThan(Other.Stored);
  }

  bool isAtLeastAsQualifiedAs(CanQual<T> Other) const {
    return Stored.isAtLeastAsQualifiedAs(Other.Stored);
  }

  void *getAsOpaquePtr() const { return Stored.getAsOpaquePtr(); }

  static CanQual<T> getFromOpaquePtr(void *Ptr);

  // (dynamic) type.
  static CanQual<T> CreateUnsafe(QualType Other);

  void dump() const { Stored.dump(); }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddPointer(getAsOpaquePtr());
  }
};

template <typename T, typename U>
inline bool operator==(CanQual<T> x, CanQual<U> y) {
  return x.getAsOpaquePtr() == y.getAsOpaquePtr();
}

template <typename T, typename U>
inline bool operator!=(CanQual<T> x, CanQual<U> y) {
  return x.getAsOpaquePtr() != y.getAsOpaquePtr();
}

using CanQualType = CanQual<Type>;

inline CanQualType Type::getCanonicalTypeUnqualified() const {
  return CanQualType::CreateUnsafe(getCanonicalTypeInternal());
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             CanQualType T) {
  DB << static_cast<QualType>(T);
  return DB;
}

//----------------------------------------------------------------------------//
// Internal proxy classes used by canonical types
//----------------------------------------------------------------------------//

#define NEVERC_CANPROXY_TYPE_ACCESSOR(Accessor)                                \
  CanQualType Accessor() const {                                               \
    return CanQualType::CreateUnsafe(this->getTypePtr()->Accessor());          \
  }

#define NEVERC_CANPROXY_SIMPLE_ACCESSOR(Type, Accessor)                        \
  Type Accessor() const { return this->getTypePtr()->Accessor(); }

template <typename T> class CanProxyBase {
protected:
  CanQual<T> Stored;

public:
  const T *getTypePtr() const { return Stored.getTypePtr(); }

  // context,e.g.,
  operator const T *() const { return this->Stored.getTypePtrOrNull(); }

  template <typename U> CanProxy<U> getAs() const {
    return this->Stored.template getAs<U>();
  }

  NEVERC_CANPROXY_SIMPLE_ACCESSOR(Type::TypeClass, getTypeClass)

  // Type predicates
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isObjectType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isIncompleteType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isSizelessType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isSizelessBuiltinType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isIncompleteOrObjectType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isVariablyModifiedType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isIntegerType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isEnumeralType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isBooleanType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isCharType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isWideCharType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isIntegralType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isIntegralOrEnumerationType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isRealFloatingType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isComplexType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isAnyComplexType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isFloatingType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isRealType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isArithmeticType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isVoidType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isDerivedType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isScalarType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isAggregateType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isAnyPointerType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isVoidPointerType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isFunctionPointerType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isStructureType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isUnionType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isComplexIntegerType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isNullPtrType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isDependentType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isArrayType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, hasPointerRepresentation)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, hasIntegerRepresentation)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, hasSignedIntegerRepresentation)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, hasUnsignedIntegerRepresentation)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, hasFloatingRepresentation)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isSignedIntegerType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isUnsignedIntegerType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isSignedIntegerOrEnumerationType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isUnsignedIntegerOrEnumerationType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isConstantSizeType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isSpecifierType)

  const CanProxyAdaptor<T> *operator->() const {
    return static_cast<const CanProxyAdaptor<T> *>(this);
  }
};

template <typename T> struct CanProxyAdaptor : CanProxyBase<T> {};

template <typename T> class CanProxy : public CanProxyAdaptor<T> {
public:
  CanProxy() = default;

  CanProxy(CanQual<T> Stored) { this->Stored = Stored; }

  operator CanQual<T>() const { return this->Stored; }
};

} // namespace neverc

namespace llvm {

template <typename T> struct simplify_type<::neverc::CanQual<T>> {
  using SimpleType = const T *;

  static SimpleType getSimplifiedValue(::neverc::CanQual<T> Val) {
    return Val.getTypePtr();
  }
};

// Teach SmallPtrSet that CanQual<T> is "basically a pointer".
template <typename T> struct PointerLikeTypeTraits<neverc::CanQual<T>> {
  static void *getAsVoidPointer(neverc::CanQual<T> P) {
    return P.getAsOpaquePtr();
  }

  static neverc::CanQual<T> getFromVoidPointer(void *P) {
    return neverc::CanQual<T>::getFromOpaquePtr(P);
  }

  // qualifier information is encoded in the low bits.
  static constexpr int NumLowBitsAvailable = 0;
};

} // namespace llvm

namespace neverc {

//----------------------------------------------------------------------------//
// Canonical proxy adaptors for canonical type nodes.
//----------------------------------------------------------------------------//

template <typename InputIterator>
struct CanTypeIterator
    : llvm::iterator_adaptor_base<
          CanTypeIterator<InputIterator>, InputIterator,
          typename std::iterator_traits<InputIterator>::iterator_category,
          CanQualType,
          typename std::iterator_traits<InputIterator>::difference_type,
          CanProxy<Type>, CanQualType> {
  CanTypeIterator() = default;
  explicit CanTypeIterator(InputIterator Iter)
      : CanTypeIterator::iterator_adaptor_base(std::move(Iter)) {}

  CanQualType operator*() const { return CanQualType::CreateUnsafe(*this->I); }
  CanProxy<Type> operator->() const;
};

template <>
struct CanProxyAdaptor<ComplexType> : public CanProxyBase<ComplexType> {
  NEVERC_CANPROXY_TYPE_ACCESSOR(getElementType)
};

template <>
struct CanProxyAdaptor<PointerType> : public CanProxyBase<PointerType> {
  NEVERC_CANPROXY_TYPE_ACCESSOR(getPointeeType)
};

// CanProxyAdaptors for arrays are intentionally unimplemented because
// they are not safe.
template <> struct CanProxyAdaptor<ArrayType>;
template <> struct CanProxyAdaptor<ConstantArrayType>;
template <> struct CanProxyAdaptor<IncompleteArrayType>;
template <> struct CanProxyAdaptor<VariableArrayType>;

template <>
struct CanProxyAdaptor<VectorType> : public CanProxyBase<VectorType> {
  NEVERC_CANPROXY_TYPE_ACCESSOR(getElementType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(unsigned, getNumElements)
};

template <>
struct CanProxyAdaptor<ExtVectorType> : public CanProxyBase<ExtVectorType> {
  NEVERC_CANPROXY_TYPE_ACCESSOR(getElementType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(unsigned, getNumElements)
};

template <>
struct CanProxyAdaptor<FunctionType> : public CanProxyBase<FunctionType> {
  NEVERC_CANPROXY_TYPE_ACCESSOR(getReturnType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(FunctionType::ExtInfo, getExtInfo)
};

template <>
struct CanProxyAdaptor<FunctionNoProtoType>
    : public CanProxyBase<FunctionNoProtoType> {
  NEVERC_CANPROXY_TYPE_ACCESSOR(getReturnType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(FunctionType::ExtInfo, getExtInfo)
};

template <>
struct CanProxyAdaptor<FunctionProtoType>
    : public CanProxyBase<FunctionProtoType> {
  NEVERC_CANPROXY_TYPE_ACCESSOR(getReturnType)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(FunctionType::ExtInfo, getExtInfo)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(unsigned, getNumParams)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, hasExtParameterInfos)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(
      llvm::ArrayRef<FunctionProtoType::ExtParameterInfo>, getExtParameterInfos)

  CanQualType getParamType(unsigned i) const {
    return CanQualType::CreateUnsafe(this->getTypePtr()->getParamType(i));
  }

  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isVariadic)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(Qualifiers, getMethodQuals)

  using param_type_iterator =
      CanTypeIterator<FunctionProtoType::param_type_iterator>;

  param_type_iterator param_type_begin() const {
    return param_type_iterator(this->getTypePtr()->param_type_begin());
  }

  param_type_iterator param_type_end() const {
    return param_type_iterator(this->getTypePtr()->param_type_end());
  }

  // Note: canonical function types never have exception specifications
};

template <>
struct CanProxyAdaptor<TypeOfType> : public CanProxyBase<TypeOfType> {
  NEVERC_CANPROXY_TYPE_ACCESSOR(getUnmodifiedType)
};

template <> struct CanProxyAdaptor<TagType> : public CanProxyBase<TagType> {
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(TagDecl *, getDecl)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isBeingDefined)
};

template <>
struct CanProxyAdaptor<RecordType> : public CanProxyBase<RecordType> {
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(RecordDecl *, getDecl)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isBeingDefined)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, hasConstFields)
};

template <> struct CanProxyAdaptor<EnumType> : public CanProxyBase<EnumType> {
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(EnumDecl *, getDecl)
  NEVERC_CANPROXY_SIMPLE_ACCESSOR(bool, isBeingDefined)
};

//----------------------------------------------------------------------------//
// Method and function definitions
//----------------------------------------------------------------------------//
template <typename T> inline CanQual<T> CanQual<T>::getUnqualifiedType() const {
  return CanQual<T>::CreateUnsafe(Stored.getLocalUnqualifiedType());
}

template <typename T> CanQual<T> CanQual<T>::getFromOpaquePtr(void *Ptr) {
  CanQual<T> Result;
  Result.Stored = QualType::getFromOpaquePtr(Ptr);
  assert((!Result || Result.Stored.getAsOpaquePtr() == (void *)-1 ||
          Result.Stored.isCanonical()) &&
         "Type is not canonical!");
  return Result;
}

template <typename T> CanQual<T> CanQual<T>::CreateUnsafe(QualType Other) {
  assert((Other.isNull() || Other.isCanonical()) && "Type is not canonical!");
  assert((Other.isNull() || isa<T>(Other.getTypePtr())) &&
         "Dynamic type does not meet the static type's requires");
  CanQual<T> Result;
  Result.Stored = Other;
  return Result;
}

template <typename T>
template <typename U>
CanProxy<U> CanQual<T>::getAs() const {
  static_assert(!TypeIsArrayType<T>::value,
                "ArrayType cannot be used with getAs!");

  if (Stored.isNull())
    return CanProxy<U>();

  if (isa<U>(Stored.getTypePtr()))
    return CanQual<U>::CreateUnsafe(Stored);

  return CanProxy<U>();
}

template <typename T>
template <typename U>
CanProxy<U> CanQual<T>::castAs() const {
  static_assert(!TypeIsArrayType<U>::value,
                "ArrayType cannot be used with castAs!");

  assert(!Stored.isNull() && isa<U>(Stored.getTypePtr()));
  return CanQual<U>::CreateUnsafe(Stored);
}

template <typename T> CanProxy<T> CanQual<T>::operator->() const {
  return CanProxy<T>(*this);
}

template <typename InputIterator>
CanProxy<Type> CanTypeIterator<InputIterator>::operator->() const {
  return CanProxy<Type>(*this);
}

} // namespace neverc

#endif // NEVERC_TREE_CANONICALTYPE_H
