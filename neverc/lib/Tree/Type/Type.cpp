#include "neverc/Tree/Type/Type.h"
#include "Core/Linkage.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/DependenceFlags.h"
#include "neverc/Tree/Type/TypeVisitor.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <type_traits>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Qualifiers & QualType basics
// ===----------------------------------------------------------------------===

bool Qualifiers::isStrictSupersetOf(Qualifiers Other) const {
  return (*this != Other) &&
         (((Mask & CVRMask) | (Other.Mask & CVRMask)) == (Mask & CVRMask)) &&
         ((getAddressSpace() == Other.getAddressSpace()) ||
          (hasAddressSpace() && !Other.hasAddressSpace()));
}

const IdentifierInfo *QualType::getBaseTypeIdentifier() const {
  const Type *ty = getTypePtr();
  NamedDecl *ND = nullptr;
  if (ty->isPointerType())
    return ty->getPointeeType().getBaseTypeIdentifier();
  else if (ty->isRecordType())
    ND = ty->castAs<RecordType>()->getDecl();
  else if (ty->isEnumeralType())
    ND = ty->castAs<EnumType>()->getDecl();
  else if (ty->getTypeClass() == Type::Typedef)
    ND = ty->castAs<TypedefType>()->getDecl();
  else if (ty->isArrayType())
    return ty->castAsArrayTypeUnsafe()
        ->getElementType()
        .getBaseTypeIdentifier();

  if (ND)
    return ND->getIdentifier();
  return nullptr;
}

bool QualType::isConstant(QualType T, const TreeContext &Ctx) {
  if (T.isConstQualified())
    return true;

  if (const ArrayType *AT = Ctx.getAsArrayType(T))
    return AT->getElementType().isConstant(Ctx);

  return false;
}

std::optional<QualType::NonConstantStorageReason>
QualType::isNonConstantStorage(const TreeContext &Ctx, bool ExcludeCtor,
                               bool ExcludeDtor) {
  if (!isConstant(Ctx))
    return NonConstantStorageReason::NonConstType;
  return std::nullopt;
}

// Array dependence comes from the element type and, when present, the size
// expression (and dependent/VM array kinds).
// ===----------------------------------------------------------------------===
// Compound type constructors (Array, Matrix, Vector, BitInt)
// ===----------------------------------------------------------------------===

ArrayType::ArrayType(TypeClass tc, QualType et, QualType can,
                     ArraySizeModifier sm, unsigned tq, const Expr *sz)
    //
    //   template<int ...N> int arr[] = {N...};
    : Type(tc, can,
           et->getDependence() |
               (sz ? toTypeDependence(
                         turnValueToTypeDependence(sz->getDependence()))
                   : TypeDependence::None) |
               (tc == VariableArray ? TypeDependence::VariablyModified
                                    : TypeDependence::None)),
      ElementType(et) {
  ArrayTypeBits.IndexTypeQuals = tq;
  ArrayTypeBits.SizeModifier = llvm::to_underlying(sm);
}

unsigned
ConstantArrayType::getNumAddressingBits(const TreeContext &Context,
                                        QualType ElementType,
                                        const llvm::APInt &NumElements) {
  uint64_t ElementSize = Context.getTypeSizeInChars(ElementType).getQuantity();

  // Fast path the common cases so we can avoid the conservative computation
  // below, which in common cases allocates "large" APSInt values, which are
  // slow.

  // If the element size is a power of 2, we can directly compute the additional
  // number of addressing bits beyond those required for the element count.
  if (llvm::isPowerOf2_64(ElementSize)) {
    return NumElements.getActiveBits() + llvm::Log2_64(ElementSize);
  }

  // If both the element count and element size fit in 32-bits, we can do the
  // computation directly in 64-bits.
  if ((ElementSize >> 32) == 0 && NumElements.getBitWidth() <= 64 &&
      (NumElements.getZExtValue() >> 32) == 0) {
    uint64_t TotalSize = NumElements.getZExtValue() * ElementSize;
    return llvm::bit_width(TotalSize);
  }

  // Otherwise, use APSInt to handle arbitrary sized values.
  llvm::APSInt SizeExtended(NumElements, true);
  unsigned SizeTypeBits = Context.getTypeSize(Context.getSizeType());
  SizeExtended = SizeExtended.extend(
      std::max(SizeTypeBits, SizeExtended.getBitWidth()) * 2);

  llvm::APSInt TotalSize(llvm::APInt(SizeExtended.getBitWidth(), ElementSize));
  TotalSize *= SizeExtended;

  return TotalSize.getActiveBits();
}

unsigned
ConstantArrayType::getNumAddressingBits(const TreeContext &Context) const {
  return getNumAddressingBits(Context, getElementType(), getSize());
}

unsigned ConstantArrayType::getMaxSizeBits(const TreeContext &Context) {
  unsigned Bits = Context.getTypeSize(Context.getSizeType());

  // Limit the number of bits in size_t so that maximal bit size fits 64 bit
  // integer (see PR8256).  We can do this as currently there is no hardware
  // that supports full 64-bit virtual space.
  if (Bits > 61)
    Bits = 61;

  return Bits;
}

void ConstantArrayType::Profile(llvm::FoldingSetNodeID &ID,
                                const TreeContext &Context, QualType ET,
                                const llvm::APInt &ArraySize,
                                const Expr *SizeExpr, ArraySizeModifier SizeMod,
                                unsigned TypeQuals) {
  ID.AddPointer(ET.getAsOpaquePtr());
  ID.AddInteger(ArraySize.getZExtValue());
  ID.AddInteger(llvm::to_underlying(SizeMod));
  ID.AddInteger(TypeQuals);
  ID.AddBoolean(SizeExpr != nullptr);
  if (SizeExpr)
    SizeExpr->Profile(ID, Context, true);
}

MatrixType::MatrixType(TypeClass tc, QualType matrixType, QualType canonType,
                       const Expr *RowExpr, const Expr *ColumnExpr)
    : Type(tc, canonType,
           (RowExpr ? (matrixType->getDependence() | TypeDependence::Dependent |
                       TypeDependence::Instantiation |
                       (matrixType->isVariablyModifiedType()
                            ? TypeDependence::VariablyModified
                            : TypeDependence::None))
                    : matrixType->getDependence())),
      ElementType(matrixType) {}

ConstantMatrixType::ConstantMatrixType(QualType matrixType, unsigned nRows,
                                       unsigned nColumns, QualType canonType)
    : ConstantMatrixType(ConstantMatrix, matrixType, nRows, nColumns,
                         canonType) {}

ConstantMatrixType::ConstantMatrixType(TypeClass tc, QualType matrixType,
                                       unsigned nRows, unsigned nColumns,
                                       QualType canonType)
    : MatrixType(tc, matrixType, canonType), NumRows(nRows),
      NumColumns(nColumns) {}

VectorType::VectorType(QualType vecType, unsigned nElements, QualType canonType,
                       VectorKind vecKind)
    : VectorType(Vector, vecType, nElements, canonType, vecKind) {}

VectorType::VectorType(TypeClass tc, QualType vecType, unsigned nElements,
                       QualType canonType, VectorKind vecKind)
    : Type(tc, canonType, vecType->getDependence()), ElementType(vecType) {
  VectorTypeBits.VecKind = llvm::to_underlying(vecKind);
  VectorTypeBits.NumElements = nElements;
}

BitIntType::BitIntType(bool IsUnsigned, unsigned NumBits)
    : Type(BitInt, QualType{}, TypeDependence::None), IsUnsigned(IsUnsigned),
      NumBits(NumBits) {}

// ===----------------------------------------------------------------------===
// Type desugaring & canonicalization
// ===----------------------------------------------------------------------===

const Type *Type::getArrayElementTypeNoTypeQual() const {
  // If this is directly an array type, return it.
  if (const auto *ATy = dyn_cast<ArrayType>(this))
    return ATy->getElementType().getTypePtr();

  // If the canonical form of this type isn't the right kind, reject it.
  if (!isa<ArrayType>(CanonicalType))
    return nullptr;

  // If this is a typedef for an array type, strip the typedef off without
  // losing all typedef information.
  return cast<ArrayType>(getUnqualifiedDesugaredType())
      ->getElementType()
      .getTypePtr();
}

QualType QualType::getDesugaredType(QualType T, const TreeContext &Context) {
  SplitQualType split = getSplitDesugaredType(T);
  return Context.getQualifiedType(split.Ty, split.Quals);
}

QualType QualType::getSingleStepDesugaredTypeImpl(QualType type,
                                                  const TreeContext &Context) {
  SplitQualType split = type.split();
  QualType desugar = split.Ty->getLocallyUnqualifiedSingleStepDesugaredType();
  return Context.getQualifiedType(desugar, split.Quals);
}

// Check that no type class is polymorphic. LLVM style RTTI should be used
// instead. If absolutely needed an exception can still be added here by
// defining the appropriate macro (but please don't do this).
#define TYPE(CLASS, BASE)                                                      \
  static_assert(!std::is_polymorphic<CLASS##Type>::value,                      \
                #CLASS "Type should not be polymorphic!");
#include "neverc/Tree/TypeNodes.td.h"

// Check that no type class has a non-trival destructor. Types are
// allocated with the BumpPtrAllocator from TreeContext and therefore
// their destructor is not executed.
//
#define TYPE(CLASS, BASE)                                                      \
  static_assert(std::is_trivially_destructible<CLASS##Type>::value ||          \
                    std::is_same<CLASS##Type, ConstantArrayType>::value,       \
                #CLASS "Type should be trivially destructible!");
#include "neverc/Tree/TypeNodes.td.h"

QualType Type::getLocallyUnqualifiedSingleStepDesugaredType() const {
  switch (getTypeClass()) {
#define ABSTRACT_TYPE(Class, Parent)
#define TYPE(Class, Parent)                                                    \
  case Type::Class: {                                                          \
    const auto *ty = cast<Class##Type>(this);                                  \
    if (!ty->isSugared())                                                      \
      return QualType(ty, 0);                                                  \
    return ty->desugar();                                                      \
  }
#include "neverc/Tree/TypeNodes.td.h"
  }
  llvm_unreachable("bad type kind!");
}

SplitQualType QualType::getSplitDesugaredType(QualType T) {
  QualifierCollector Qs;

  QualType Cur = T;
  while (true) {
    const Type *CurTy = Qs.strip(Cur);
    switch (CurTy->getTypeClass()) {
#define ABSTRACT_TYPE(Class, Parent)
#define TYPE(Class, Parent)                                                    \
  case Type::Class: {                                                          \
    const auto *Ty = cast<Class##Type>(CurTy);                                 \
    if (!Ty->isSugared())                                                      \
      return SplitQualType(Ty, Qs);                                            \
    Cur = Ty->desugar();                                                       \
    break;                                                                     \
  }
#include "neverc/Tree/TypeNodes.td.h"
    }
  }
}

SplitQualType QualType::getSplitUnqualifiedTypeImpl(QualType type) {
  SplitQualType split = type.split();

  // All the qualifiers we've seen so far.
  Qualifiers quals = split.Quals;

  // The last type node we saw with any nodes inside it.
  const Type *lastTypeWithQuals = split.Ty;

  while (true) {
    QualType next;

    // Do a single-step desugar, aborting the loop if the type isn't
    // sugared.
    switch (split.Ty->getTypeClass()) {
#define ABSTRACT_TYPE(Class, Parent)
#define TYPE(Class, Parent)                                                    \
  case Type::Class: {                                                          \
    const auto *ty = cast<Class##Type>(split.Ty);                              \
    if (!ty->isSugared())                                                      \
      goto done;                                                               \
    next = ty->desugar();                                                      \
    break;                                                                     \
  }
#include "neverc/Tree/TypeNodes.td.h"
    }

    // Otherwise, split the underlying type.  If that yields qualifiers,
    // update the information.
    split = next.split();
    if (!split.Quals.empty()) {
      lastTypeWithQuals = split.Ty;
      quals.addConsistentQualifiers(split.Quals);
    }
  }

done:
  return SplitQualType(lastTypeWithQuals, quals);
}

QualType QualType::IgnoreParens(QualType T) {
  while (const auto *PT = T->getAs<ParenType>())
    T = PT->getInnerType();
  return T;
}

template <typename T> static const T *getAsSugar(const Type *Cur) {
  while (true) {
    if (const auto *Sugar = dyn_cast<T>(Cur))
      return Sugar;
    switch (Cur->getTypeClass()) {
#define ABSTRACT_TYPE(Class, Parent)
#define TYPE(Class, Parent)                                                    \
  case Type::Class: {                                                          \
    const auto *Ty = cast<Class##Type>(Cur);                                   \
    if (!Ty->isSugared())                                                      \
      return 0;                                                                \
    Cur = Ty->desugar().getTypePtr();                                          \
    break;                                                                     \
  }
#include "neverc/Tree/TypeNodes.td.h"
    }
  }
}

template <> const TypedefType *Type::getAs() const {
  return getAsSugar<TypedefType>(this);
}

template <> const AttributedType *Type::getAs() const {
  return getAsSugar<AttributedType>(this);
}

const Type *Type::getUnqualifiedDesugaredType() const {
  const Type *Cur = this;

  while (true) {
    switch (Cur->getTypeClass()) {
#define ABSTRACT_TYPE(Class, Parent)
#define TYPE(Class, Parent)                                                    \
  case Class: {                                                                \
    const auto *Ty = cast<Class##Type>(Cur);                                   \
    if (!Ty->isSugared())                                                      \
      return Cur;                                                              \
    Cur = Ty->desugar().getTypePtr();                                          \
    break;                                                                     \
  }
#include "neverc/Tree/TypeNodes.td.h"
    }
  }
}

// ===----------------------------------------------------------------------===
// Type predicates & accessors
// ===----------------------------------------------------------------------===

bool Type::isStructureType() const {
  if (const auto *RT = getAs<RecordType>())
    return RT->getDecl()->isStruct();
  return false;
}

bool Type::isVoidPointerType() const {
  if (const auto *PT = getAs<PointerType>())
    return PT->getPointeeType()->isVoidType();
  return false;
}

bool Type::isUnionType() const {
  if (const auto *RT = getAs<RecordType>())
    return RT->getDecl()->isUnion();
  return false;
}

bool Type::isComplexType() const {
  if (const auto *CT = dyn_cast<ComplexType>(CanonicalType))
    return CT->getElementType()->isFloatingType();
  return false;
}

bool Type::isComplexIntegerType() const {
  // Check for GCC complex integer extension.
  return getAsComplexIntegerType();
}

const ComplexType *Type::getAsComplexIntegerType() const {
  if (const auto *Complex = getAs<ComplexType>())
    if (Complex->getElementType()->isIntegerType())
      return Complex;
  return nullptr;
}

QualType Type::getPointeeType() const {
  if (const auto *PT = getAs<PointerType>())
    return PT->getPointeeType();
  if (const auto *DT = getAs<DecayedType>())
    return DT->getPointeeType();
  return {};
}

const RecordType *Type::getAsStructureType() const {
  // If this is directly a structure type, return it.
  if (const auto *RT = dyn_cast<RecordType>(this)) {
    if (RT->getDecl()->isStruct())
      return RT;
  }

  // If the canonical form of this type isn't the right kind, reject it.
  if (const auto *RT = dyn_cast<RecordType>(CanonicalType)) {
    if (!RT->getDecl()->isStruct())
      return nullptr;

    // If this is a typedef for a structure type, strip the typedef off without
    // losing all typedef information.
    return cast<RecordType>(getUnqualifiedDesugaredType());
  }
  return nullptr;
}

const RecordType *Type::getAsUnionType() const {
  // If this is directly a union type, return it.
  if (const auto *RT = dyn_cast<RecordType>(this)) {
    if (RT->getDecl()->isUnion())
      return RT;
  }

  // If the canonical form of this type isn't the right kind, reject it.
  if (const auto *RT = dyn_cast<RecordType>(CanonicalType)) {
    if (!RT->getDecl()->isUnion())
      return nullptr;

    // If this is a typedef for a union type, strip the typedef off without
    // losing all typedef information.
    return cast<RecordType>(getUnqualifiedDesugaredType());
  }

  return nullptr;
}

namespace {

template <typename Derived>
struct SimpleTransformVisitor : public TypeVisitor<Derived, QualType> {
  TreeContext &Ctx;

  QualType recurse(QualType type) {
    // Split out the qualifiers from the type.
    SplitQualType splitType = type.split();

    // Visit the type itself.
    QualType result = static_cast<Derived *>(this)->Visit(splitType.Ty);
    if (result.isNull())
      return result;

    // Reconstruct the transformed type by applying the local qualifiers
    // from the split type.
    return Ctx.getQualifiedType(result, splitType.Quals);
  }

public:
  explicit SimpleTransformVisitor(TreeContext &ctx) : Ctx(ctx) {}

  // None of the clients of this transformation can occur where
  // there are dependent types, so skip dependent types.
#define TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base)                                            \
  QualType Visit##Class##Type(const Class##Type *T) { return QualType(T, 0); }
#include "neverc/Tree/TypeNodes.td.h"

#define TRIVIAL_TYPE_CLASS(Class)                                              \
  QualType Visit##Class##Type(const Class##Type *T) { return QualType(T, 0); }
#define SUGARED_TYPE_CLASS(Class)                                              \
  QualType Visit##Class##Type(const Class##Type *T) {                          \
    if (!T->isSugared())                                                       \
      return QualType(T, 0);                                                   \
    QualType desugaredType = recurse(T->desugar());                            \
    if (desugaredType.isNull())                                                \
      return {};                                                               \
    if (desugaredType.getAsOpaquePtr() == T->desugar().getAsOpaquePtr())       \
      return QualType(T, 0);                                                   \
    return desugaredType;                                                      \
  }

  TRIVIAL_TYPE_CLASS(Builtin)

  QualType VisitComplexType(const ComplexType *T) {
    QualType elementType = recurse(T->getElementType());
    if (elementType.isNull())
      return {};

    if (elementType.getAsOpaquePtr() == T->getElementType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getComplexType(elementType);
  }

  QualType VisitPointerType(const PointerType *T) {
    QualType pointeeType = recurse(T->getPointeeType());
    if (pointeeType.isNull())
      return {};

    if (pointeeType.getAsOpaquePtr() == T->getPointeeType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getPointerType(pointeeType);
  }

  QualType VisitConstantArrayType(const ConstantArrayType *T) {
    QualType elementType = recurse(T->getElementType());
    if (elementType.isNull())
      return {};

    if (elementType.getAsOpaquePtr() == T->getElementType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getConstantArrayType(elementType, T->getSize(), T->getSizeExpr(),
                                    T->getSizeModifier(),
                                    T->getIndexTypeCVRQualifiers());
  }

  QualType VisitVariableArrayType(const VariableArrayType *T) {
    QualType elementType = recurse(T->getElementType());
    if (elementType.isNull())
      return {};

    if (elementType.getAsOpaquePtr() == T->getElementType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getVariableArrayType(
        elementType, T->getSizeExpr(), T->getSizeModifier(),
        T->getIndexTypeCVRQualifiers(), T->getBracketsRange());
  }

  QualType VisitIncompleteArrayType(const IncompleteArrayType *T) {
    QualType elementType = recurse(T->getElementType());
    if (elementType.isNull())
      return {};

    if (elementType.getAsOpaquePtr() == T->getElementType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getIncompleteArrayType(elementType, T->getSizeModifier(),
                                      T->getIndexTypeCVRQualifiers());
  }

  QualType VisitVectorType(const VectorType *T) {
    QualType elementType = recurse(T->getElementType());
    if (elementType.isNull())
      return {};

    if (elementType.getAsOpaquePtr() == T->getElementType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getVectorType(elementType, T->getNumElements(),
                             T->getVectorKind());
  }

  QualType VisitExtVectorType(const ExtVectorType *T) {
    QualType elementType = recurse(T->getElementType());
    if (elementType.isNull())
      return {};

    if (elementType.getAsOpaquePtr() == T->getElementType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getExtVectorType(elementType, T->getNumElements());
  }

  QualType VisitConstantMatrixType(const ConstantMatrixType *T) {
    QualType elementType = recurse(T->getElementType());
    if (elementType.isNull())
      return {};
    if (elementType.getAsOpaquePtr() == T->getElementType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getConstantMatrixType(elementType, T->getNumRows(),
                                     T->getNumColumns());
  }

  QualType VisitFunctionNoProtoType(const FunctionNoProtoType *T) {
    QualType returnType = recurse(T->getReturnType());
    if (returnType.isNull())
      return {};

    if (returnType.getAsOpaquePtr() == T->getReturnType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getFunctionNoProtoType(returnType, T->getExtInfo());
  }

  QualType VisitFunctionProtoType(const FunctionProtoType *T) {
    QualType returnType = recurse(T->getReturnType());
    if (returnType.isNull())
      return {};

    // Transform parameter types.
    llvm::SmallVector<QualType, 4> paramTypes;
    bool paramChanged = false;
    for (auto paramType : T->getParamTypes()) {
      QualType newParamType = recurse(paramType);
      if (newParamType.isNull())
        return {};

      if (newParamType.getAsOpaquePtr() != paramType.getAsOpaquePtr())
        paramChanged = true;

      paramTypes.push_back(newParamType);
    }

    FunctionProtoType::ExtProtoInfo info = T->getExtProtoInfo();

    if (returnType.getAsOpaquePtr() == T->getReturnType().getAsOpaquePtr() &&
        !paramChanged)
      return QualType(T, 0);

    return Ctx.getFunctionType(returnType, paramTypes, info);
  }

  QualType VisitParenType(const ParenType *T) {
    QualType innerType = recurse(T->getInnerType());
    if (innerType.isNull())
      return {};

    if (innerType.getAsOpaquePtr() == T->getInnerType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getParenType(innerType);
  }

  SUGARED_TYPE_CLASS(Typedef)
  SUGARED_TYPE_CLASS(MacroQualified)

  QualType VisitAdjustedType(const AdjustedType *T) {
    QualType originalType = recurse(T->getOriginalType());
    if (originalType.isNull())
      return {};

    QualType adjustedType = recurse(T->getAdjustedType());
    if (adjustedType.isNull())
      return {};

    if (originalType.getAsOpaquePtr() ==
            T->getOriginalType().getAsOpaquePtr() &&
        adjustedType.getAsOpaquePtr() == T->getAdjustedType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getAdjustedType(originalType, adjustedType);
  }

  QualType VisitDecayedType(const DecayedType *T) {
    QualType originalType = recurse(T->getOriginalType());
    if (originalType.isNull())
      return {};

    if (originalType.getAsOpaquePtr() == T->getOriginalType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getDecayedType(originalType);
  }

  SUGARED_TYPE_CLASS(TypeOfExpr)
  SUGARED_TYPE_CLASS(TypeOf)
  TRIVIAL_TYPE_CLASS(Record)
  TRIVIAL_TYPE_CLASS(Enum)

  SUGARED_TYPE_CLASS(Elaborated)

  QualType VisitAttributedType(const AttributedType *T) {
    QualType modifiedType = recurse(T->getModifiedType());
    if (modifiedType.isNull())
      return {};

    QualType equivalentType = recurse(T->getEquivalentType());
    if (equivalentType.isNull())
      return {};

    if (modifiedType.getAsOpaquePtr() ==
            T->getModifiedType().getAsOpaquePtr() &&
        equivalentType.getAsOpaquePtr() ==
            T->getEquivalentType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getAttributedType(T->getAttrKind(), modifiedType,
                                 equivalentType);
  }

  QualType VisitAutoType(const AutoType *T) {
    if (!T->isDeduced())
      return QualType(T, 0);

    QualType deducedType = recurse(T->getDeducedType());
    if (deducedType.isNull())
      return {};

    if (deducedType.getAsOpaquePtr() == T->getDeducedType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getAutoType(deducedType, T->getKeyword(), T->isDependentType());
  }

  QualType VisitAtomicType(const AtomicType *T) {
    QualType valueType = recurse(T->getValueType());
    if (valueType.isNull())
      return {};

    if (valueType.getAsOpaquePtr() == T->getValueType().getAsOpaquePtr())
      return QualType(T, 0);

    return Ctx.getAtomicType(valueType);
  }

#undef TRIVIAL_TYPE_CLASS
#undef SUGARED_TYPE_CLASS
};

} // namespace

bool QualType::UseExcessPrecision(const TreeContext &Ctx) {
  const BuiltinType *BT = getTypePtr()->getAs<BuiltinType>();
  if (!BT) {
    const VectorType *VT = getTypePtr()->getAs<VectorType>();
    if (VT) {
      QualType ElementType = VT->getElementType();
      return ElementType.UseExcessPrecision(Ctx);
    }
  } else {
    switch (BT->getKind()) {
    case BuiltinType::Kind::Float16: {
      const TargetInfo &TI = Ctx.getTargetInfo();
      if (TI.hasFloat16Type() && !TI.hasLegalHalfType() &&
          Ctx.getLangOpts().getFloat16ExcessPrecision() !=
              Ctx.getLangOpts().ExcessPrecisionKind::FPP_None)
        return true;
      break;
    }
    case BuiltinType::Kind::BFloat16: {
      const TargetInfo &TI = Ctx.getTargetInfo();
      if (TI.hasBFloat16Type() && !TI.hasFullBFloat16Type() &&
          Ctx.getLangOpts().getBFloat16ExcessPrecision() !=
              Ctx.getLangOpts().ExcessPrecisionKind::FPP_None)
        return true;
      break;
    }
    default:
      return false;
    }
  }
  return false;
}

QualType QualType::getAtomicUnqualifiedType() const {
  if (const auto AT = getTypePtr()->getAs<AtomicType>())
    return AT->getValueType().getUnqualifiedType();
  return getUnqualifiedType();
}

RecordDecl *Type::getAsRecordDecl() const {
  return dyn_cast_or_null<RecordDecl>(getAsTagDecl());
}

TagDecl *Type::getAsTagDecl() const {
  if (const auto *TT = getAs<TagType>())
    return TT->getDecl();

  return nullptr;
}

bool Type::hasAttr(attr::Kind AK) const {
  const Type *Cur = this;
  while (const auto *AT = Cur->getAs<AttributedType>()) {
    if (AT->getAttrKind() == AK)
      return true;
    Cur = AT->getEquivalentType().getTypePtr();
  }
  return false;
}

namespace {

class GetContainedDeducedTypeVisitor
    : public TypeVisitor<GetContainedDeducedTypeVisitor, Type *> {
public:
  GetContainedDeducedTypeVisitor(bool = false) {}

  using TypeVisitor<GetContainedDeducedTypeVisitor, Type *>::Visit;

  Type *Visit(QualType T) {
    if (T.isNull())
      return nullptr;
    return Visit(T.getTypePtr());
  }

  // The deduced type itself.
  Type *VisitDeducedType(const DeducedType *AT) {
    return const_cast<DeducedType *>(AT);
  }

  Type *VisitElaboratedType(const ElaboratedType *T) {
    return Visit(T->getNamedType());
  }

  Type *VisitPointerType(const PointerType *T) {
    return Visit(T->getPointeeType());
  }

  Type *VisitArrayType(const ArrayType *T) {
    return Visit(T->getElementType());
  }

  Type *VisitVectorType(const VectorType *T) {
    return Visit(T->getElementType());
  }

  Type *VisitConstantMatrixType(const ConstantMatrixType *T) {
    return Visit(T->getElementType());
  }

  Type *VisitFunctionProtoType(const FunctionProtoType *T) {
    return VisitFunctionType(T);
  }

  Type *VisitFunctionType(const FunctionType *T) {
    return Visit(T->getReturnType());
  }

  Type *VisitParenType(const ParenType *T) { return Visit(T->getInnerType()); }

  Type *VisitAttributedType(const AttributedType *T) {
    return Visit(T->getModifiedType());
  }

  Type *VisitMacroQualifiedType(const MacroQualifiedType *T) {
    return Visit(T->getUnderlyingType());
  }

  Type *VisitAdjustedType(const AdjustedType *T) {
    return Visit(T->getOriginalType());
  }
};

} // namespace

DeducedType *Type::getContainedDeducedType() const {
  return cast_or_null<DeducedType>(
      GetContainedDeducedTypeVisitor().Visit(this));
}

bool Type::hasIntegerRepresentation() const {
  if (const auto *VT = dyn_cast<VectorType>(CanonicalType))
    return VT->getElementType()->isIntegerType();
  if (CanonicalType->isSveVLSBuiltinType()) {
    const auto *VT = cast<BuiltinType>(CanonicalType);
    return VT->getKind() == BuiltinType::SveBool ||
           (VT->getKind() >= BuiltinType::SveInt8 &&
            VT->getKind() <= BuiltinType::SveUint64);
  }
  return isIntegerType();
}

bool Type::isIntegralType(const TreeContext &Ctx) const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() >= BuiltinType::Bool &&
           BT->getKind() <= BuiltinType::Int128;

  if (const auto *ET = dyn_cast<EnumType>(CanonicalType))
    return ET->getDecl()->isComplete();

  return isBitIntType();
}

bool Type::isIntegralOrUnscopedEnumerationType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() >= BuiltinType::Bool &&
           BT->getKind() <= BuiltinType::Int128;

  if (isBitIntType())
    return true;

  return isUnscopedEnumerationType();
}

bool Type::isUnscopedEnumerationType() const {
  return isa<EnumType>(CanonicalType);
}

namespace {
enum CharClassBits : uint8_t {
  CCB_None = 0,
  CCB_Char = 1 << 0,
  CCB_WChar = 1 << 1,
  CCB_Char8 = 1 << 2,
  CCB_Char16 = 1 << 3,
  CCB_Char32 = 1 << 4,
  CCB_AnyChar = CCB_Char | CCB_WChar | CCB_Char8 | CCB_Char16 | CCB_Char32,
};

constexpr uint8_t CharClassTable[] = {
    [BuiltinType::Char_U] = CCB_Char | CCB_AnyChar,
    [BuiltinType::UChar] = CCB_Char | CCB_AnyChar,
    [BuiltinType::Char_S] = CCB_Char | CCB_AnyChar,
    [BuiltinType::SChar] = CCB_Char | CCB_AnyChar,
    [BuiltinType::WChar_S] = CCB_WChar | CCB_AnyChar,
    [BuiltinType::WChar_U] = CCB_WChar | CCB_AnyChar,
    [BuiltinType::Char8] = CCB_Char8 | CCB_AnyChar,
    [BuiltinType::Char16] = CCB_Char16 | CCB_AnyChar,
    [BuiltinType::Char32] = CCB_Char32 | CCB_AnyChar,
};
constexpr unsigned CharClassTableSize =
    sizeof(CharClassTable) / sizeof(CharClassTable[0]);

LLVM_ATTRIBUTE_ALWAYS_INLINE
uint8_t getCharClass(QualType CT) {
  if (const auto *BT = dyn_cast<BuiltinType>(CT)) {
    auto K = static_cast<unsigned>(BT->getKind());
    return LLVM_LIKELY(K < CharClassTableSize) ? CharClassTable[K] : 0;
  }
  return 0;
}
} // namespace

bool Type::isCharType() const { return getCharClass(CanonicalType) & CCB_Char; }

bool Type::isWideCharType() const {
  return getCharClass(CanonicalType) & CCB_WChar;
}

bool Type::isChar8Type() const {
  return getCharClass(CanonicalType) & CCB_Char8;
}

bool Type::isChar16Type() const {
  return getCharClass(CanonicalType) & CCB_Char16;
}

bool Type::isChar32Type() const {
  return getCharClass(CanonicalType) & CCB_Char32;
}

bool Type::isAnyCharacterType() const {
  return getCharClass(CanonicalType) & CCB_AnyChar;
}

bool Type::isSignedIntegerType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType)) {
    return BT->getKind() >= BuiltinType::Char_S &&
           BT->getKind() <= BuiltinType::Int128;
  }

  if (const EnumType *ET = dyn_cast<EnumType>(CanonicalType)) {
    if (ET->getDecl()->isComplete())
      return ET->getDecl()->getIntegerType()->isSignedIntegerType();
  }

  if (const auto *IT = dyn_cast<BitIntType>(CanonicalType))
    return IT->isSigned();

  return false;
}

bool Type::isSignedIntegerOrEnumerationType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType)) {
    return BT->getKind() >= BuiltinType::Char_S &&
           BT->getKind() <= BuiltinType::Int128;
  }

  if (const auto *ET = dyn_cast<EnumType>(CanonicalType)) {
    if (ET->getDecl()->isComplete())
      return ET->getDecl()->getIntegerType()->isSignedIntegerType();
  }

  if (const auto *IT = dyn_cast<BitIntType>(CanonicalType))
    return IT->isSigned();

  return false;
}

bool Type::hasSignedIntegerRepresentation() const {
  if (const auto *VT = dyn_cast<VectorType>(CanonicalType))
    return VT->getElementType()->isSignedIntegerOrEnumerationType();
  else
    return isSignedIntegerOrEnumerationType();
}

bool Type::isUnsignedIntegerType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType)) {
    return BT->getKind() >= BuiltinType::Bool &&
           BT->getKind() <= BuiltinType::UInt128;
  }

  if (const auto *ET = dyn_cast<EnumType>(CanonicalType)) {
    if (ET->getDecl()->isComplete())
      return ET->getDecl()->getIntegerType()->isUnsignedIntegerType();
  }

  if (const auto *IT = dyn_cast<BitIntType>(CanonicalType))
    return IT->isUnsigned();

  return false;
}

bool Type::isUnsignedIntegerOrEnumerationType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType)) {
    return BT->getKind() >= BuiltinType::Bool &&
           BT->getKind() <= BuiltinType::UInt128;
  }

  if (const auto *ET = dyn_cast<EnumType>(CanonicalType)) {
    if (ET->getDecl()->isComplete())
      return ET->getDecl()->getIntegerType()->isUnsignedIntegerType();
  }

  if (const auto *IT = dyn_cast<BitIntType>(CanonicalType))
    return IT->isUnsigned();

  return false;
}

bool Type::hasUnsignedIntegerRepresentation() const {
  if (const auto *VT = dyn_cast<VectorType>(CanonicalType))
    return VT->getElementType()->isUnsignedIntegerOrEnumerationType();
  if (const auto *VT = dyn_cast<MatrixType>(CanonicalType))
    return VT->getElementType()->isUnsignedIntegerOrEnumerationType();
  if (CanonicalType->isSveVLSBuiltinType()) {
    const auto *VT = cast<BuiltinType>(CanonicalType);
    return VT->getKind() >= BuiltinType::SveUint8 &&
           VT->getKind() <= BuiltinType::SveUint64;
  }
  return isUnsignedIntegerOrEnumerationType();
}

bool Type::isFloatingType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() >= BuiltinType::Half &&
           BT->getKind() <= BuiltinType::Float128;
  if (const auto *CT = dyn_cast<ComplexType>(CanonicalType))
    return CT->getElementType()->isFloatingType();
  return false;
}

bool Type::hasFloatingRepresentation() const {
  if (const auto *VT = dyn_cast<VectorType>(CanonicalType))
    return VT->getElementType()->isFloatingType();
  if (const auto *MT = dyn_cast<MatrixType>(CanonicalType))
    return MT->getElementType()->isFloatingType();
  return isFloatingType();
}

bool Type::isRealFloatingType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->isFloatingPoint();
  return false;
}

bool Type::isRealType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() >= BuiltinType::Bool &&
           BT->getKind() <= BuiltinType::Float128;
  if (const auto *ET = dyn_cast<EnumType>(CanonicalType))
    return ET->getDecl()->isComplete();
  return isBitIntType();
}

bool Type::isArithmeticType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() >= BuiltinType::Bool &&
           BT->getKind() <= BuiltinType::Float128;
  if (const auto *ET = dyn_cast<EnumType>(CanonicalType))
    // GCC allows forward declaration of enum types (forbid by C99 6.7.2.3p2).
    return ET->getDecl()->isComplete();
  return isa<ComplexType>(CanonicalType) || isBitIntType();
}

Type::ScalarTypeKind Type::getScalarTypeKind() const {
  assert(isScalarType());

  const Type *T = CanonicalType.getTypePtr();
  if (const auto *BT = dyn_cast<BuiltinType>(T)) {
    if (BT->getKind() == BuiltinType::Bool)
      return STK_Bool;
    if (BT->getKind() == BuiltinType::NullPtr)
      return STK_CPointer;
    if (BT->isInteger())
      return STK_Integral;
    if (BT->isFloatingPoint())
      return STK_Floating;
    if (BT->isFixedPointType())
      return STK_FixedPoint;
    llvm_unreachable("unknown scalar builtin type");
  } else if (isa<PointerType>(T)) {
    return STK_CPointer;
  } else if (isa<EnumType>(T)) {
    assert(cast<EnumType>(T)->getDecl()->isComplete());
    return STK_Integral;
  } else if (const auto *CT = dyn_cast<ComplexType>(T)) {
    if (CT->getElementType()->isRealFloatingType())
      return STK_FloatingComplex;
    return STK_IntegralComplex;
  } else if (isBitIntType()) {
    return STK_Integral;
  }

  llvm_unreachable("unknown scalar type");
}

bool Type::isAggregateType() const {
  if (isa<RecordType>(CanonicalType))
    return true;
  return isa<ArrayType>(CanonicalType);
}

bool Type::isConstantSizeType() const {
  assert(!isIncompleteType() && "This doesn't make sense for incomplete types");
  assert(!isDependentType() && "This doesn't make sense for dependent types");
  // The VAT must have a size, as it is known to be complete.
  return !isa<VariableArrayType>(CanonicalType);
}

bool Type::isIncompleteType(NamedDecl **Def) const {
  if (Def)
    *Def = nullptr;

  switch (CanonicalType->getTypeClass()) {
  default:
    return false;
  case Builtin:
    // Void is the only incomplete builtin type.  Per C99 6.2.5p19, it can never
    // be completed.
    return isVoidType();
  case Enum: {
    EnumDecl *EnumD = cast<EnumType>(CanonicalType)->getDecl();
    if (Def)
      *Def = EnumD;
    return !EnumD->isComplete();
  }
  case Record: {
    // A tagged type (struct/union/enum/class) is incomplete if the decl is a
    // forward declaration, but not a full definition (C99 6.2.5p22).
    RecordDecl *Rec = cast<RecordType>(CanonicalType)->getDecl();
    if (Def)
      *Def = Rec;
    return !Rec->isCompleteDefinition();
  }
  case ConstantArray:
  case VariableArray:
    // An array is incomplete if its element type is incomplete.
    // We don't handle dependent-sized arrays (dependent types are never treated
    // as incomplete).
    return cast<ArrayType>(CanonicalType)
        ->getElementType()
        ->isIncompleteType(Def);
  case IncompleteArray:
    // An array of unknown size is an incomplete type (C99 6.2.5p22).
    return true;
  }
  return false;
}

bool Type::isSizelessBuiltinType() const {
  if (isSizelessVectorType())
    return true;

  if (const BuiltinType *BT = getAs<BuiltinType>()) {
    switch (BT->getKind()) {
      return true;
    default:
      return false;
    }
  }
  return false;
}

bool Type::isSizelessType() const { return isSizelessBuiltinType(); }

bool Type::isSizelessVectorType() const { return isSVESizelessBuiltinType(); }

bool Type::isSVESizelessBuiltinType() const {
  if (const BuiltinType *BT = getAs<BuiltinType>()) {
    switch (BT->getKind()) {
      // SVE Types
#define SVE_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
      return true;
    default:
      return false;
    }
  }
  return false;
}

bool Type::isSveVLSBuiltinType() const {
  if (const BuiltinType *BT = getAs<BuiltinType>()) {
    switch (BT->getKind()) {
    case BuiltinType::SveInt8:
    case BuiltinType::SveInt16:
    case BuiltinType::SveInt32:
    case BuiltinType::SveInt64:
    case BuiltinType::SveUint8:
    case BuiltinType::SveUint16:
    case BuiltinType::SveUint32:
    case BuiltinType::SveUint64:
    case BuiltinType::SveFloat16:
    case BuiltinType::SveFloat32:
    case BuiltinType::SveFloat64:
    case BuiltinType::SveBFloat16:
    case BuiltinType::SveBool:
    case BuiltinType::SveBoolx2:
    case BuiltinType::SveBoolx4:
      return true;
    default:
      return false;
    }
  }
  return false;
}

QualType Type::getSveEltType(const TreeContext &Ctx) const {
  assert(isSveVLSBuiltinType() && "unsupported type!");

  const BuiltinType *BTy = castAs<BuiltinType>();
  if (BTy->getKind() == BuiltinType::SveBool)
    // Represent predicates as i8 rather than i1 to avoid any layout issues.
    // The type is bitcasted to a scalable predicate type when casting between
    // scalable and fixed-length vectors.
    return Ctx.UnsignedCharTy;
  else
    return Ctx.getBuiltinVectorTypeInfo(BTy).ElementType;
}

bool QualType::isPODType(const TreeContext &Context) const {
  if (isNull())
    return false;

  if ((*this)->isIncompleteArrayType())
    return Context.getBaseElementType(*this).isPODType(Context);

  if ((*this)->isIncompleteType())
    return false;

  QualType CanonicalType = getTypePtr()->CanonicalType;
  switch (CanonicalType->getTypeClass()) {
  default:
    return false;
  case Type::VariableArray:
  case Type::ConstantArray:
    return Context.getBaseElementType(*this).isPODType(Context);

  case Type::Builtin:
  case Type::Complex:
  case Type::Pointer:
  case Type::Vector:
  case Type::ExtVector:
  case Type::BitInt:
    return true;

  case Type::Enum:
    return true;

  case Type::Record:
    return true;
  }
}

bool QualType::isTriviallyCopyableType(const TreeContext &Context) const {
  if ((*this)->isArrayType())
    return Context.getBaseElementType(*this).isTriviallyCopyableType(Context);

  // Trivially copyable: scalars, trivially-copyable records, arrays thereof.

  QualType CanonicalType = getCanonicalType();
  if (CanonicalType->isSizelessBuiltinType())
    return true;

  // Return false for incomplete types after skipping any incomplete array types
  // which are expressly allowed by the standard and thus our API.
  if (CanonicalType->isIncompleteType())
    return false;

  // As an extension, NeverC treats vector types as Scalar types.
  if (CanonicalType->isScalarType() || CanonicalType->isVectorType())
    return true;
  if (CanonicalType->getAs<RecordType>())
    return true;

  return false;
}

bool QualType::hasNonTrivialToPrimitiveDefaultInitializeCUnion(
    const RecordDecl *RD) {
  return RD->hasNonTrivialToPrimitiveDefaultInitializeCUnion();
}

bool QualType::hasNonTrivialToPrimitiveDestructCUnion(const RecordDecl *RD) {
  return RD->hasNonTrivialToPrimitiveDestructCUnion();
}

bool QualType::hasNonTrivialToPrimitiveCopyCUnion(const RecordDecl *RD) {
  return RD->hasNonTrivialToPrimitiveCopyCUnion();
}

QualType::PrimitiveDefaultInitializeKind
QualType::isNonTrivialToPrimitiveDefaultInitialize() const {
  if (const auto *RT =
          getTypePtr()->getBaseElementTypeUnsafe()->getAs<RecordType>())
    if (RT->getDecl()->isNonTrivialToPrimitiveDefaultInitialize())
      return PDIK_Struct;

  return PDIK_Trivial;
}

QualType::PrimitiveCopyKind QualType::isNonTrivialToPrimitiveCopy() const {
  if (const auto *RT =
          getTypePtr()->getBaseElementTypeUnsafe()->getAs<RecordType>())
    if (RT->getDecl()->isNonTrivialToPrimitiveCopy())
      return PCK_Struct;

  return getQualifiers().hasVolatile() ? PCK_VolatileTrivial : PCK_Trivial;
}

QualType::PrimitiveCopyKind
QualType::isNonTrivialToPrimitiveDestructiveMove() const {
  return isNonTrivialToPrimitiveCopy();
}

bool Type::isLiteralType(const TreeContext &Ctx) const {
  (void)Ctx;

  // Literal types exclude VLAs; arrays of literal element type qualify.
  if (isVariableArrayType())
    return false;
  const Type *BaseTy = getBaseElementTypeUnsafe();
  assert(BaseTy && "NULL element type");

  // Return false for incomplete types after skipping any incomplete array
  // types; those are expressly allowed by the standard and thus our API.
  if (BaseTy->isIncompleteType())
    return false;

  // Scalars are literal; NeverC also treats vector and complex types as
  // literal.
  if (BaseTy->isScalarType() || BaseTy->isVectorType() ||
      BaseTy->isAnyComplexType())
    return true;
  if (BaseTy->getAs<RecordType>())
    return true;

  // We treat _Atomic T as a literal type if T is a literal type.
  if (const auto *AT = BaseTy->getAs<AtomicType>())
    return AT->getValueType()->isLiteralType(Ctx);

  // If this type hasn't been deduced yet, then conservatively assume that
  // it'll work out to be a literal type.
  if (isa<AutoType>(BaseTy->getCanonicalTypeInternal()))
    return true;

  return false;
}

bool Type::isStandardLayoutType() const {
  // Standard-layout: scalars, standard-layout records, arrays thereof.
  const Type *BaseTy = getBaseElementTypeUnsafe();
  assert(BaseTy && "NULL element type");

  // Return false for incomplete types after skipping any incomplete array
  // types which are expressly allowed by the standard and thus our API.
  if (BaseTy->isIncompleteType())
    return false;

  // As an extension, NeverC treats vector types as Scalar types.
  if (BaseTy->isScalarType() || BaseTy->isVectorType())
    return true;
  if (BaseTy->getAs<RecordType>())
    return true;

  // No other types can match.
  return false;
}

bool Type::isSpecifierType() const {
  // Note that this intentionally does not use the canonical type.
  switch (getTypeClass()) {
  case Builtin:
  case Record:
  case Enum:
  case Typedef:
  case Complex:
  case TypeOfExpr:
  case TypeOf:
  case Elaborated:
    return true;
  default:
    return false;
  }
}

ElaboratedTypeKeyword
TypeWithKeyword::getKeywordForTypeSpec(unsigned TypeSpec) {
  switch (TypeSpec) {
  default:
    return ElaboratedTypeKeyword::None;
  case TST_struct:
    return ElaboratedTypeKeyword::Struct;
  case TST_union:
    return ElaboratedTypeKeyword::Union;
  case TST_enum:
    return ElaboratedTypeKeyword::Enum;
  }
}

TagTypeKind TypeWithKeyword::getTagTypeKindForTypeSpec(unsigned TypeSpec) {
  switch (TypeSpec) {
  case TST_struct:
    return TagTypeKind::Struct;
  case TST_union:
    return TagTypeKind::Union;
  case TST_enum:
    return TagTypeKind::Enum;
  }

  llvm_unreachable("Type specifier is not a tag type kind.");
}

ElaboratedTypeKeyword
TypeWithKeyword::getKeywordForTagTypeKind(TagTypeKind Kind) {
  switch (Kind) {
  case TagTypeKind::Struct:
    return ElaboratedTypeKeyword::Struct;
  case TagTypeKind::Union:
    return ElaboratedTypeKeyword::Union;
  case TagTypeKind::Enum:
    return ElaboratedTypeKeyword::Enum;
  }
  llvm_unreachable("Unknown tag type kind.");
}

TagTypeKind
TypeWithKeyword::getTagTypeKindForKeyword(ElaboratedTypeKeyword Keyword) {
  switch (Keyword) {
  case ElaboratedTypeKeyword::Struct:
    return TagTypeKind::Struct;
  case ElaboratedTypeKeyword::Union:
    return TagTypeKind::Union;
  case ElaboratedTypeKeyword::Enum:
    return TagTypeKind::Enum;
  case ElaboratedTypeKeyword::None:
    llvm_unreachable("Elaborated type keyword is not a tag type kind.");
  }
  llvm_unreachable("Unknown elaborated type keyword.");
}

bool TypeWithKeyword::KeywordIsTagTypeKind(ElaboratedTypeKeyword Keyword) {
  switch (Keyword) {
  case ElaboratedTypeKeyword::None:
    return false;
  case ElaboratedTypeKeyword::Struct:
  case ElaboratedTypeKeyword::Union:
  case ElaboratedTypeKeyword::Enum:
    return true;
  }
  llvm_unreachable("Unknown elaborated type keyword.");
}

llvm::StringRef TypeWithKeyword::getKeywordName(ElaboratedTypeKeyword Keyword) {
  switch (Keyword) {
  case ElaboratedTypeKeyword::None:
    return {};
  case ElaboratedTypeKeyword::Struct:
    return tok::getKeywordSpelling(tok::kw_struct);
  case ElaboratedTypeKeyword::Union:
    return tok::getKeywordSpelling(tok::kw_union);
  case ElaboratedTypeKeyword::Enum:
    return tok::getKeywordSpelling(tok::kw_enum);
  }

  llvm_unreachable("Unknown elaborated type keyword.");
}

const char *Type::getTypeClassName() const {
  switch (TypeBits.TC) {
#define ABSTRACT_TYPE(Derived, Base)
#define TYPE(Derived, Base)                                                    \
  case Derived:                                                                \
    return #Derived;
#include "neverc/Tree/TypeNodes.td.h"
  }

  llvm_unreachable("Invalid type class.");
}

llvm::StringRef BuiltinType::getName(const PrintingPolicy &Policy) const {
  switch (getKind()) {
  case Void:
    return tok::getKeywordSpelling(tok::kw_void);
  case Bool:
    return Policy.Bool ? tok::getKeywordSpelling(tok::kw_bool)
                       : tok::getKeywordSpelling(tok::kw__Bool);
  case Char_S:
    return tok::getKeywordSpelling(tok::kw_char);
  case Char_U:
    return tok::getKeywordSpelling(tok::kw_char);
  case SChar:
    return "signed char";
  case Short:
    return tok::getKeywordSpelling(tok::kw_short);
  case Int:
    return tok::getKeywordSpelling(tok::kw_int);
  case Long:
    return tok::getKeywordSpelling(tok::kw_long);
  case LongLong:
    return "long long";
  case Int128:
    return tok::getKeywordSpelling(tok::kw___int128);
  case UChar:
    return "unsigned char";
  case UShort:
    return "unsigned short";
  case UInt:
    return "unsigned int";
  case ULong:
    return "unsigned long";
  case ULongLong:
    return "unsigned long long";
  case UInt128:
    return "unsigned __int128";
  case Half:
    return "__fp16";
  case BFloat16:
    return tok::getKeywordSpelling(tok::kw___bf16);
  case Float:
    return tok::getKeywordSpelling(tok::kw_float);
  case Double:
    return tok::getKeywordSpelling(tok::kw_double);
  case LongDouble:
    return "long double";
  case ShortAccum:
    return "short _Accum";
  case Accum:
    return tok::getKeywordSpelling(tok::kw__Accum);
  case LongAccum:
    return "long _Accum";
  case UShortAccum:
    return "unsigned short _Accum";
  case UAccum:
    return "unsigned _Accum";
  case ULongAccum:
    return "unsigned long _Accum";
  case BuiltinType::ShortFract:
    return "short _Fract";
  case BuiltinType::Fract:
    return tok::getKeywordSpelling(tok::kw__Fract);
  case BuiltinType::LongFract:
    return "long _Fract";
  case BuiltinType::UShortFract:
    return "unsigned short _Fract";
  case BuiltinType::UFract:
    return "unsigned _Fract";
  case BuiltinType::ULongFract:
    return "unsigned long _Fract";
  case BuiltinType::SatShortAccum:
    return "_Sat short _Accum";
  case BuiltinType::SatAccum:
    return "_Sat _Accum";
  case BuiltinType::SatLongAccum:
    return "_Sat long _Accum";
  case BuiltinType::SatUShortAccum:
    return "_Sat unsigned short _Accum";
  case BuiltinType::SatUAccum:
    return "_Sat unsigned _Accum";
  case BuiltinType::SatULongAccum:
    return "_Sat unsigned long _Accum";
  case BuiltinType::SatShortFract:
    return "_Sat short _Fract";
  case BuiltinType::SatFract:
    return "_Sat _Fract";
  case BuiltinType::SatLongFract:
    return "_Sat long _Fract";
  case BuiltinType::SatUShortFract:
    return "_Sat unsigned short _Fract";
  case BuiltinType::SatUFract:
    return "_Sat unsigned _Fract";
  case BuiltinType::SatULongFract:
    return "_Sat unsigned long _Fract";
  case Float16:
    return tok::getKeywordSpelling(tok::kw__Float16);
  case Float128:
    return tok::getKeywordSpelling(tok::kw___float128);
  case WChar_S:
  case WChar_U:
    return Policy.MSWChar ? "__wchar_t"
                          : tok::getKeywordSpelling(tok::kw_wchar_t);
  case Char8:
    return tok::getKeywordSpelling(tok::kw_char8_t);
  case Char16:
    return "char16_t";
  case Char32:
    return "char32_t";
  case NullPtr:
    return "nullptr_t";
  case Overload:
    return "<overloaded function type>";
  case PseudoObject:
    return "<pseudo-object type>";
  case Dependent:
    return "<dependent type>";
  case BuiltinFn:
    return "<builtin fn type>";
  case IncompleteMatrixIdx:
    return "<incomplete matrix index type>";
#define SVE_TYPE(Name, Id, SingletonId)                                        \
  case Id:                                                                     \
    return Name;
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
  }

  llvm_unreachable("Invalid builtin type.");
}

QualType QualType::getNonLValueExprType(const TreeContext &Context) const {
  // Non-class prvalues use cv-unqualified types here; see also C99 6.3.2.1p2.
  return getUnqualifiedType();
}

llvm::StringRef FunctionType::getNameForCallConv(CallingConv CC) {
  switch (CC) {
  case CC_C:
    return "cdecl";
  case CC_X86StdCall:
    return "stdcall";
  case CC_X86FastCall:
    return "fastcall";
  case CC_X86VectorCall:
    return "vectorcall";
  case CC_Win64:
    return "ms_abi";
  case CC_X86_64SysV:
    return "sysv_abi";
  case CC_X86RegCall:
    return "regcall";
  case CC_AArch64VectorCall:
    return "aarch64_vector_pcs";
  case CC_AArch64SVEPCS:
    return "aarch64_sve_pcs";

  case CC_PreserveMost:
    return "preserve_most";
  case CC_PreserveAll:
    return "preserve_all";
  }

  llvm_unreachable("Invalid calling convention.");
}

FunctionProtoType::FunctionProtoType(QualType result,
                                     llvm::ArrayRef<QualType> params,
                                     QualType canonical,
                                     const ExtProtoInfo &epi)
    : FunctionType(FunctionProto, result, canonical, result->getDependence(),
                   epi.ExtInfo) {
  FunctionTypeBits.FastTypeQuals = epi.TypeQuals.getFastQualifiers();
  FunctionTypeBits.NumParams = params.size();
  assert(getNumParams() == params.size() && "NumParams overflow!");
  FunctionTypeBits.ExceptionSpecType = epi.ExceptionSpec.Type;
  FunctionTypeBits.HasExtParameterInfos = !!epi.ExtParameterInfos;
  FunctionTypeBits.Variadic = epi.Variadic;

  if (epi.requiresFunctionProtoTypeExtraBitfields()) {
    FunctionTypeBits.HasExtraBitfields = true;
    auto &ExtraBits = *getTrailingObjects<FunctionTypeExtraBitfields>();
    ExtraBits = FunctionTypeExtraBitfields();
  } else {
    FunctionTypeBits.HasExtraBitfields = false;
  }

  // Fill in the trailing argument array.
  auto *argSlot = getTrailingObjects<QualType>();
  for (unsigned i = 0; i != getNumParams(); ++i) {
    addDependence(params[i]->getDependence() &
                  ~TypeDependence::VariablyModified);
    argSlot[i] = params[i];
  }

  // Propagate the SME ACLE attributes.
  if (epi.AArch64SMEAttributes != SME_NormalFunction) {
    auto &ExtraBits = *getTrailingObjects<FunctionTypeExtraBitfields>();
    assert(epi.AArch64SMEAttributes <= SME_AttributeMask &&
           "Not enough bits to encode SME attributes");
    ExtraBits.AArch64SMEAttributes = epi.AArch64SMEAttributes;
  }

  if (getCanonicalTypeInternal()->isDependentType())
    addDependence(TypeDependence::DependentInstantiation);

  // Fill in the extra parameter info if present.
  if (epi.ExtParameterInfos) {
    auto *extParamInfos = getTrailingObjects<ExtParameterInfo>();
    for (unsigned i = 0; i != getNumParams(); ++i)
      extParamInfos[i] = epi.ExtParameterInfos[i];
  }

  if (epi.TypeQuals.hasNonFastQualifiers()) {
    FunctionTypeBits.HasExtQuals = 1;
    *getTrailingObjects<Qualifiers>() = epi.TypeQuals;
  } else {
    FunctionTypeBits.HasExtQuals = 0;
  }

  // Fill in the Ellipsis location info if present.
  if (epi.Variadic) {
    auto &EllipsisLoc = *getTrailingObjects<SourceLocation>();
    EllipsisLoc = epi.EllipsisLoc;
  }
}

void FunctionProtoType::Profile(llvm::FoldingSetNodeID &ID, QualType Result,
                                const QualType *ArgTys, unsigned NumParams,
                                const ExtProtoInfo &epi,
                                const TreeContext &Context, bool Canonical) {
  // We have to be careful not to get ambiguous profile encodings.
  // Note that valid type pointers are never ambiguous with anything else.
  //
  // The encoding grammar begins:
  //      type type* bool int bool
  // If that final bool is true, then there is a section for the EH spec:
  //      bool type*
  // This is followed by an optional "consumed argument" section of the
  // same length as the first type sequence:
  //      bool*
  // This is followed by the ext info:
  //      int
  // Finally we have a trailing return type flag (bool)
  // combined with AArch64 SME Attributes, to save space:
  //      int
  //
  // There is no ambiguity between the consumed arguments and an empty EH
  // spec because of the leading 'bool' which unambiguously indicates
  // whether the following bool is the EH spec or part of the arguments.

  ID.AddPointer(Result.getAsOpaquePtr());
  for (unsigned i = 0; i != NumParams; ++i)
    ID.AddPointer(ArgTys[i].getAsOpaquePtr());
  // This method is relatively performance sensitive, so as a performance
  // shortcut, use one AddInteger call instead of four for the next four
  // fields.
  ID.AddInteger(unsigned(epi.Variadic) + (epi.ExceptionSpec.Type << 1));
  ID.Add(epi.TypeQuals);
  if (epi.ExtParameterInfos) {
    for (unsigned i = 0; i != NumParams; ++i)
      ID.AddInteger(epi.ExtParameterInfos[i].getOpaqueValue());
  }

  epi.ExtInfo.Profile(ID);
  ID.AddInteger(epi.AArch64SMEAttributes);
}

void FunctionProtoType::Profile(llvm::FoldingSetNodeID &ID,
                                const TreeContext &Ctx) {
  Profile(ID, getReturnType(), param_type_begin(), getNumParams(),
          getExtProtoInfo(), Ctx, isCanonicalUnqualified());
}

TypedefType::TypedefType(TypeClass tc, const TypedefNameDecl *D,
                         QualType Underlying, QualType can)
    : Type(tc, can, toSemanticDependence(can->getDependence())),
      Decl(const_cast<TypedefNameDecl *>(D)) {
  assert(!isa<TypedefType>(can) && "Invalid canonical type");
  TypedefBits.hasTypeDifferentFromDecl = !Underlying.isNull();
  if (!typeMatchesDecl())
    *getTrailingObjects<QualType>() = Underlying;
}

QualType TypedefType::desugar() const {
  return typeMatchesDecl() ? Decl->getUnderlyingType()
                           : *getTrailingObjects<QualType>();
}

QualType MacroQualifiedType::desugar() const { return getUnderlyingType(); }

QualType MacroQualifiedType::getModifiedType() const {
  // Step over MacroQualifiedTypes from the same macro to find the type
  // ultimately qualified by the macro qualifier.
  QualType Inner = cast<AttributedType>(getUnderlyingType())->getModifiedType();
  while (auto *InnerMQT = dyn_cast<MacroQualifiedType>(Inner)) {
    if (InnerMQT->getMacroIdentifier() != getMacroIdentifier())
      break;
    Inner = InnerMQT->getModifiedType();
  }
  return Inner;
}

TypeOfExprType::TypeOfExprType(Expr *E, TypeOfKind Kind, QualType Can)
    : Type(TypeOfExpr,
           // We have to protect against 'Can' being invalid through its
           // default argument.
           Kind == TypeOfKind::Unqualified && !Can.isNull()
               ? Can.getAtomicUnqualifiedType()
               : Can,
           toTypeDependence(E->getDependence()) |
               (E->getType()->getDependence() &
                TypeDependence::VariablyModified)),
      TOExpr(E) {
  TypeOfBits.IsUnqual = Kind == TypeOfKind::Unqualified;
}

bool TypeOfExprType::isSugared() const { return !TOExpr->isTypeDependent(); }

QualType TypeOfExprType::desugar() const {
  if (isSugared()) {
    QualType QT = getUnderlyingExpr()->getType();
    return TypeOfBits.IsUnqual ? QT.getAtomicUnqualifiedType() : QT;
  }
  return QualType(this, 0);
}

void DependentTypeOfExprType::Profile(llvm::FoldingSetNodeID &ID,
                                      const TreeContext &Context, Expr *E,
                                      bool IsUnqual) {
  E->Profile(ID, Context, true);
  ID.AddBoolean(IsUnqual);
}

TagType::TagType(TypeClass TC, const TagDecl *D, QualType can)
    : Type(TC, can, TypeDependence::None), decl(const_cast<TagDecl *>(D)) {}

namespace {
TagDecl *getInterestingTagDecl(TagDecl *decl) {
  for (auto *I : decl->redecls()) {
    if (I->isCompleteDefinition() || I->isBeingDefined())
      return I;
  }
  // If there's no definition (not even in progress), return what we have.
  return decl;
}
} // namespace

TagDecl *TagType::getDecl() const { return getInterestingTagDecl(decl); }

bool TagType::isBeingDefined() const { return getDecl()->isBeingDefined(); }

bool RecordType::hasConstFields() const {
  std::vector<const RecordType *> RecordTypeList;
  RecordTypeList.push_back(this);
  unsigned NextToCheckIndex = 0;

  while (RecordTypeList.size() > NextToCheckIndex) {
    for (FieldDecl *FD :
         RecordTypeList[NextToCheckIndex]->getDecl()->fields()) {
      QualType FieldTy = FD->getType();
      if (FieldTy.isConstQualified())
        return true;
      FieldTy = FieldTy.getCanonicalType();
      if (const auto *FieldRecTy = FieldTy->getAs<RecordType>()) {
        if (!llvm::is_contained(RecordTypeList, FieldRecTy))
          RecordTypeList.push_back(FieldRecTy);
      }
    }
    ++NextToCheckIndex;
  }
  return false;
}

bool AttributedType::isQualifier() const {
  switch (getAttrKind()) {
  // These are type qualifiers in the traditional C sense: they annotate
  // something about a specific value/variable of a type.  (They aren't
  // always part of the canonical type, though.)
  case attr::TypeNonNull:
  case attr::TypeNullable:
  case attr::TypeNullUnspecified:
  case attr::AddressSpace:
    return true;

  // All other type attributes aren't qualifiers; they rewrite the modified
  // type to be a semantically different type.
  default:
    return false;
  }
}

bool AttributedType::isMSTypeSpec() const {
  switch (getAttrKind()) {
  default:
    return false;
  case attr::Ptr32:
  case attr::Ptr64:
  case attr::SPtr:
  case attr::UPtr:
    return true;
  }
  llvm_unreachable("invalid attr kind");
}

bool AttributedType::isCallingConv() const {
  switch (getAttrKind()) {
  default:
    return false;
  case attr::CDecl:
  case attr::FastCall:
  case attr::StdCall:
  case attr::RegCall:
  case attr::VectorCall:
  case attr::AArch64VectorPcs:
  case attr::AArch64SVEPcs:
  case attr::MSABI:
  case attr::SysVABI:
  case attr::PreserveMost:
  case attr::PreserveAll:
    return true;
  }
  llvm_unreachable("invalid attr kind");
}

QualType QualifierCollector::apply(const TreeContext &Context,
                                   QualType QT) const {
  if (!hasNonFastQualifiers())
    return QT.withFastQualifiers(getFastQualifiers());

  return Context.getQualifiedType(QT, *this);
}

QualType QualifierCollector::apply(const TreeContext &Context,
                                   const Type *T) const {
  if (!hasNonFastQualifiers())
    return QualType(T, getFastQualifiers());

  return Context.getQualifiedType(T, *this);
}

namespace {

class CachedProperties {
  Linkage L;
  bool local;

public:
  CachedProperties(Linkage L, bool local) : L(L), local(local) {}

  Linkage getLinkage() const { return L; }
  bool hasLocalOrUnnamedType() const { return local; }

  friend CachedProperties merge(CachedProperties L, CachedProperties R) {
    Linkage MergedLinkage = minLinkage(L.L, R.L);
    return CachedProperties(MergedLinkage, L.hasLocalOrUnnamedType() ||
                                               R.hasLocalOrUnnamedType());
  }
};

} // namespace

namespace {
CachedProperties computeCachedProperties(const Type *T);
} // namespace

namespace neverc {

template <class Private> class TypePropertyCache {
public:
  static CachedProperties get(QualType T) { return get(T.getTypePtr()); }

  static CachedProperties get(const Type *T) {
    ensure(T);
    return CachedProperties(T->TypeBits.getLinkage(),
                            T->TypeBits.hasLocalOrUnnamedType());
  }

  static void ensure(const Type *T) {
    // If the cache is valid, we're okay.
    if (T->TypeBits.isCacheValid())
      return;

    // If this type is non-canonical, ask its canonical type for the
    // relevant information.
    if (!T->isCanonicalUnqualified()) {
      const Type *CT = T->getCanonicalTypeInternal().getTypePtr();
      ensure(CT);
      T->TypeBits.CacheValid = true;
      T->TypeBits.CachedLinkage = CT->TypeBits.CachedLinkage;
      T->TypeBits.CachedLocalOrUnnamed = CT->TypeBits.CachedLocalOrUnnamed;
      return;
    }

    CachedProperties Result = computeCachedProperties(T);
    T->TypeBits.CacheValid = true;
    T->TypeBits.CachedLinkage = llvm::to_underlying(Result.getLinkage());
    T->TypeBits.CachedLocalOrUnnamed = Result.hasLocalOrUnnamedType();
  }
};

} // namespace neverc

// Instantiate the friend template at a private class.  In a
// reasonable implementation, these symbols will be internal.
// It is terrible that this is the best way to accomplish this.
namespace {

class Private {};

} // namespace

using Cache = TypePropertyCache<Private>;

namespace {
CachedProperties computeCachedProperties(const Type *T) {
  switch (T->getTypeClass()) {
#define TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#include "neverc/Tree/TypeNodes.td.h"
    llvm_unreachable("didn't expect a non-canonical type here");

#define TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#include "neverc/Tree/TypeNodes.td.h"
    // Treat instantiation-dependent types as external.
    if (!T->isInstantiationDependentType())
      T->dump();
    assert(T->isInstantiationDependentType());
    return CachedProperties(Linkage::External, false);

  case Type::Auto:
    // Give non-deduced 'auto' types external linkage. We should only see them
    // here in error recovery.
    return CachedProperties(Linkage::External, false);

  case Type::BitInt:
  case Type::Builtin:
    // Built-in / fundamental types carry external linkage in this model.
    return CachedProperties(Linkage::External, false);

  case Type::Record:
  case Type::Enum: {
    const TagDecl *Tag = cast<TagType>(T)->getDecl();

    // Tag types use their declaration's linkage.
    Linkage L = Tag->getLinkageInternal();
    bool IsLocalOrUnnamed = Tag->getDeclContext()->isFunctionOrMethod() ||
                            !Tag->hasNameForLinkage();
    return CachedProperties(L, IsLocalOrUnnamed);
  }

    // Other compound types inherit linkage from their component types.
  case Type::Complex:
    return Cache::get(cast<ComplexType>(T)->getElementType());
  case Type::Pointer:
    return Cache::get(cast<PointerType>(T)->getPointeeType());

  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::VariableArray:
    return Cache::get(cast<ArrayType>(T)->getElementType());
  case Type::Vector:
  case Type::ExtVector:
    return Cache::get(cast<VectorType>(T)->getElementType());
  case Type::ConstantMatrix:
    return Cache::get(cast<ConstantMatrixType>(T)->getElementType());
  case Type::FunctionNoProto:
    return Cache::get(cast<FunctionType>(T)->getReturnType());
  case Type::FunctionProto: {
    const auto *FPT = cast<FunctionProtoType>(T);
    CachedProperties result = Cache::get(FPT->getReturnType());
    for (const auto &ai : FPT->param_types())
      result = merge(result, Cache::get(ai));
    return result;
  }
  case Type::Atomic:
    return Cache::get(cast<AtomicType>(T)->getValueType());
  }

  llvm_unreachable("unhandled type class");
}
} // namespace

Linkage Type::getLinkage() const {
  Cache::ensure(this);
  return TypeBits.getLinkage();
}

LinkageInfo LinkageComputer::computeTypeLinkageInfo(const Type *T) {
  switch (T->getTypeClass()) {
#define TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#include "neverc/Tree/TypeNodes.td.h"
    llvm_unreachable("didn't expect a non-canonical type here");

#define TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#include "neverc/Tree/TypeNodes.td.h"
    // Treat instantiation-dependent types as external.
    assert(T->isInstantiationDependentType());
    return LinkageInfo::external();

  case Type::BitInt:
  case Type::Builtin:
    return LinkageInfo::external();

  case Type::Auto:
    return LinkageInfo::external();

  case Type::Record:
  case Type::Enum:
    return getDeclLinkageAndVisibility(cast<TagType>(T)->getDecl());

  case Type::Complex:
    return computeTypeLinkageInfo(cast<ComplexType>(T)->getElementType());
  case Type::Pointer:
    return computeTypeLinkageInfo(cast<PointerType>(T)->getPointeeType());
  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::VariableArray:
    return computeTypeLinkageInfo(cast<ArrayType>(T)->getElementType());
  case Type::Vector:
  case Type::ExtVector:
    return computeTypeLinkageInfo(cast<VectorType>(T)->getElementType());
  case Type::ConstantMatrix:
    return computeTypeLinkageInfo(
        cast<ConstantMatrixType>(T)->getElementType());
  case Type::FunctionNoProto:
    return computeTypeLinkageInfo(cast<FunctionType>(T)->getReturnType());
  case Type::FunctionProto: {
    const auto *FPT = cast<FunctionProtoType>(T);
    LinkageInfo LV = computeTypeLinkageInfo(FPT->getReturnType());
    for (const auto &ai : FPT->param_types())
      LV.merge(computeTypeLinkageInfo(ai));
    return LV;
  }
  case Type::Atomic:
    return computeTypeLinkageInfo(cast<AtomicType>(T)->getValueType());
  }

  llvm_unreachable("unhandled type class");
}

bool Type::isLinkageValid() const {
  if (!TypeBits.isCacheValid())
    return true;

  Linkage L = LinkageComputer{}
                  .computeTypeLinkageInfo(getCanonicalTypeInternal())
                  .getLinkage();
  return L == TypeBits.getLinkage();
}

LinkageInfo LinkageComputer::getTypeLinkageAndVisibility(const Type *T) {
  if (!T->isCanonicalUnqualified())
    return computeTypeLinkageInfo(T->getCanonicalTypeInternal());

  LinkageInfo LV = computeTypeLinkageInfo(T);
  assert(LV.getLinkage() == T->getLinkage());
  return LV;
}

LinkageInfo Type::getLinkageAndVisibility() const {
  return LinkageComputer{}.getTypeLinkageAndVisibility(this);
}

std::optional<NullabilityKind> Type::getNullability() const {
  QualType Type(this, 0);
  while (const auto *AT = Type->getAs<AttributedType>()) {
    // Check whether this is an attributed type with nullability
    // information.
    if (auto Nullability = AT->getImmediateNullability())
      return Nullability;

    Type = AT->getEquivalentType();
  }
  return std::nullopt;
}

bool Type::canHaveNullability(bool ResultIfUnknown) const {
  QualType type = getCanonicalTypeInternal();

  switch (type->getTypeClass()) {
    // We'll only see canonical types here.
#define NON_CANONICAL_TYPE(Class, Parent)                                      \
  case Type::Class:                                                            \
    llvm_unreachable("non-canonical type");
#define TYPE(Class, Parent)
#include "neverc/Tree/TypeNodes.td.h"

  // Pointer types.
  case Type::Pointer:
  // Dependent types that could instantiate to pointer types.
  case Type::TypeOfExpr:
  case Type::TypeOf:
  case Type::Auto:
    return ResultIfUnknown;

  case Type::Builtin:
    switch (cast<BuiltinType>(type.getTypePtr())->getKind()) {
      // Signed, unsigned, and floating-point types cannot have nullability.
#define SIGNED_TYPE(Id, SingletonId) case BuiltinType::Id:
#define UNSIGNED_TYPE(Id, SingletonId) case BuiltinType::Id:
#define FLOATING_TYPE(Id, SingletonId) case BuiltinType::Id:
#define BUILTIN_TYPE(Id, SingletonId)
#include "neverc/Tree/Type/BuiltinTypes.def"
      return false;

    // Dependent types that could instantiate to a pointer type.
    case BuiltinType::Dependent:
    case BuiltinType::Overload:
    case BuiltinType::PseudoObject:
      return ResultIfUnknown;

    case BuiltinType::Void:
#define SVE_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
    case BuiltinType::BuiltinFn:
    case BuiltinType::NullPtr:
    case BuiltinType::IncompleteMatrixIdx:
      return false;
    }
    llvm_unreachable("unknown builtin type");

  // Non-pointer types.
  case Type::Complex:
  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::VariableArray:
  case Type::Vector:
  case Type::ExtVector:
  case Type::ConstantMatrix:
  case Type::FunctionProto:
  case Type::FunctionNoProto:
  case Type::Record:
  case Type::Enum:
  case Type::Atomic:
  case Type::BitInt:
    return false;
  }
  llvm_unreachable("bad type kind!");
}

std::optional<NullabilityKind> AttributedType::getImmediateNullability() const {
  if (getAttrKind() == attr::TypeNonNull)
    return NullabilityKind::NonNull;
  if (getAttrKind() == attr::TypeNullable)
    return NullabilityKind::Nullable;
  if (getAttrKind() == attr::TypeNullUnspecified)
    return NullabilityKind::Unspecified;
  return std::nullopt;
}

std::optional<NullabilityKind>
AttributedType::stripOuterNullability(QualType &T) {
  QualType AttrTy = T;
  if (auto MacroTy = dyn_cast<MacroQualifiedType>(T))
    AttrTy = MacroTy->getUnderlyingType();

  if (auto attributed = dyn_cast<AttributedType>(AttrTy)) {
    if (auto nullability = attributed->getImmediateNullability()) {
      T = attributed->getModifiedType();
      return nullability;
    }
  }

  return std::nullopt;
}

bool Type::hasSizedVLAType() const {
  if (!isVariablyModifiedType())
    return false;

  if (const auto *ptr = getAs<PointerType>())
    return ptr->getPointeeType()->hasSizedVLAType();
  if (const ArrayType *arr = getAsArrayTypeUnsafe()) {
    if (isa<VariableArrayType>(arr) &&
        cast<VariableArrayType>(arr)->getSizeExpr())
      return true;

    return arr->getElementType()->hasSizedVLAType();
  }

  return false;
}

QualType::DestructionKind QualType::isDestructedTypeImpl(QualType type) {
  if (const auto *RT = type->getBaseElementTypeUnsafe()->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    /// Check if this is a C struct that is non-trivial to destroy or an array
    /// that contains such a struct.
    if (RD->isNonTrivialToPrimitiveDestroy())
      return DK_nontrivial_c_struct;
  }

  return DK_none;
}

void neverc::FixedPointValueToString(llvm::SmallVectorImpl<char> &Str,
                                     llvm::APSInt Val, unsigned Scale) {
  llvm::FixedPointSemantics FXSema(Val.getBitWidth(), Scale, Val.isSigned(),
                                   /*IsSaturated=*/false,
                                   /*HasUnsignedPadding=*/false);
  llvm::APFixedPoint(Val, FXSema).toString(Str);
}

AutoType::AutoType(QualType DeducedAsType, AutoTypeKeyword Keyword,
                   TypeDependence ExtraDependence, QualType Canon)
    : DeducedType(Auto, DeducedAsType, ExtraDependence, Canon) {
  AutoTypeBits.Keyword = llvm::to_underlying(Keyword);
}

void AutoType::Profile(llvm::FoldingSetNodeID &ID, const TreeContext &Context,
                       QualType Deduced, AutoTypeKeyword Keyword,
                       bool IsDependent) {
  ID.AddPointer(Deduced.getAsOpaquePtr());
  ID.AddInteger((unsigned)Keyword);
  ID.AddBoolean(IsDependent);
}

void AutoType::Profile(llvm::FoldingSetNodeID &ID, const TreeContext &Context) {
  Profile(ID, Context, getDeducedType(), getKeyword(), isDependentType());
}
