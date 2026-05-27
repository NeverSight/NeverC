#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Core/Mangle.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "neverc/Tree/Decl/GlobalDecl.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/DependenceFlags.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "neverc/Tree/Type/TypeLoc.h"
#include "llvm/ADT/APFixedPoint.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <optional>

using namespace neverc;

enum FloatingRank {
  BFloat16Rank,
  Float16Rank,
  HalfRank,
  FloatRank,
  DoubleRank,
  LongDoubleRank,
  Float128Rank
};

// ===----------------------------------------------------------------------===
// Type equivalence, compatibility & array types
// ===----------------------------------------------------------------------===

const ArrayType *TreeContext::getAsArrayType(QualType T) const {
  if (!T.hasLocalQualifiers()) {
    if (const auto *AT = dyn_cast<ArrayType>(T))
      return AT;
  }

  if (!isa<ArrayType>(T.getCanonicalType()))
    return nullptr;

  // Array type qualifiers apply to the element type, not the array itself.
  // Propagate qualifiers through sugar (typedefs etc.) to the element type.

  SplitQualType split = T.getSplitDesugaredType();
  Qualifiers qs = split.Quals;

  // If we have a simple case, just return now.
  const auto *ATy = dyn_cast<ArrayType>(split.Ty);
  if (!ATy || qs.empty())
    return ATy;

  // Otherwise, we have an array and we have qualifiers on it.  Push the
  // qualifiers into the array element type and return a new array type.
  QualType NewEltTy = getQualifiedType(ATy->getElementType(), qs);

  if (const auto *CAT = dyn_cast<ConstantArrayType>(ATy))
    return cast<ArrayType>(getConstantArrayType(
        NewEltTy, CAT->getSize(), CAT->getSizeExpr(), CAT->getSizeModifier(),
        CAT->getIndexTypeCVRQualifiers()));
  if (const auto *IAT = dyn_cast<IncompleteArrayType>(ATy))
    return cast<ArrayType>(getIncompleteArrayType(
        NewEltTy, IAT->getSizeModifier(), IAT->getIndexTypeCVRQualifiers()));

  const auto *VAT = cast<VariableArrayType>(ATy);
  return cast<ArrayType>(getVariableArrayType(
      NewEltTy, VAT->getSizeExpr(), VAT->getSizeModifier(),
      VAT->getIndexTypeCVRQualifiers(), VAT->getBracketsRange()));
}

QualType TreeContext::getAdjustedParameterType(QualType T) const {
  if (T->isArrayType() || T->isFunctionType())
    return getDecayedType(T);
  return T;
}

QualType TreeContext::getArrayDecayedType(QualType Ty) const {
  // Get the element type with 'getAsArrayType' so that we don't lose any
  // typedefs in the element type of the array.  This also handles propagation
  // of type qualifiers from the array type into the element type if present
  // (C99 6.7.3p8).
  const ArrayType *PrettyArrayType = getAsArrayType(Ty);
  assert(PrettyArrayType && "Not an array type!");

  QualType PtrTy = getPointerType(PrettyArrayType->getElementType());

  // int x[restrict 4] ->  int *restrict
  QualType Result =
      getQualifiedType(PtrTy, PrettyArrayType->getIndexTypeQualifiers());

  // int x[_Nullable] -> int * _Nullable
  if (auto Nullability = Ty->getNullability()) {
    Result = const_cast<TreeContext *>(this)->getAttributedType(
        AttributedType::getNullabilityAttrKind(*Nullability), Result, Result);
  }
  return Result;
}

QualType TreeContext::getBaseElementType(const ArrayType *array) const {
  return getBaseElementType(array->getElementType());
}

QualType TreeContext::getBaseElementType(QualType type) const {
  Qualifiers qs;
  while (true) {
    SplitQualType split = type.getSplitDesugaredType();
    const ArrayType *array = split.Ty->getAsArrayTypeUnsafe();
    if (!array)
      break;

    type = array->getElementType();
    qs.addConsistentQualifiers(split.Quals);
  }

  return getQualifiedType(type, qs);
}

uint64_t
TreeContext::getConstantArrayElementCount(const ConstantArrayType *CA) const {
  uint64_t ElementCount = 1;
  do {
    ElementCount *= CA->getSize().getZExtValue();
    CA = dyn_cast_or_null<ConstantArrayType>(
        CA->getElementType()->getAsArrayTypeUnsafe());
  } while (CA);
  return ElementCount;
}

uint64_t
TreeContext::getArrayInitLoopExprElementCount(const ArrayInitLoopExpr *) const {
  return 0;
}

namespace {
FloatingRank rankFloatingType(QualType T) {
  if (const auto *CT = T->getAs<ComplexType>())
    return rankFloatingType(CT->getElementType());

  switch (T->castAs<BuiltinType>()->getKind()) {
  default:
    llvm_unreachable("rankFloatingType(): not a floating type");
  case BuiltinType::Float16:
    return Float16Rank;
  case BuiltinType::Half:
    return HalfRank;
  case BuiltinType::Float:
    return FloatRank;
  case BuiltinType::Double:
    return DoubleRank;
  case BuiltinType::LongDouble:
    return LongDoubleRank;
  case BuiltinType::Float128:
    return Float128Rank;
  case BuiltinType::BFloat16:
    return BFloat16Rank;
  }
}
} // namespace

int TreeContext::getFloatingTypeOrder(QualType LHS, QualType RHS) const {
  FloatingRank LHSR = rankFloatingType(LHS);
  FloatingRank RHSR = rankFloatingType(RHS);

  if (LHSR == RHSR)
    return 0;
  if (LHSR > RHSR)
    return 1;
  return -1;
}

int TreeContext::getFloatingTypeSemanticOrder(QualType LHS,
                                              QualType RHS) const {
  if (&getFloatTypeSemantics(LHS) == &getFloatTypeSemantics(RHS))
    return 0;
  return getFloatingTypeOrder(LHS, RHS);
}

unsigned TreeContext::getIntegerRank(const Type *T) const {
  assert(T->isCanonicalUnqualified() && "T should be canonicalized");

  // Results in this 'losing' to any type of the same size, but winning if
  // larger.
  if (const auto *EIT = dyn_cast<BitIntType>(T))
    return 0 + (EIT->getNumBits() << 3);

  switch (cast<BuiltinType>(T)->getKind()) {
  default:
    llvm_unreachable("getIntegerRank(): not a built-in integer");
  case BuiltinType::Bool:
    return 1 + (getIntWidth(BoolTy) << 3);
  case BuiltinType::Char_S:
  case BuiltinType::Char_U:
  case BuiltinType::SChar:
  case BuiltinType::UChar:
    return 2 + (getIntWidth(CharTy) << 3);
  case BuiltinType::Short:
  case BuiltinType::UShort:
    return 3 + (getIntWidth(ShortTy) << 3);
  case BuiltinType::Int:
  case BuiltinType::UInt:
    return 4 + (getIntWidth(IntTy) << 3);
  case BuiltinType::Long:
  case BuiltinType::ULong:
    return 5 + (getIntWidth(LongTy) << 3);
  case BuiltinType::LongLong:
  case BuiltinType::ULongLong:
    return 6 + (getIntWidth(LongLongTy) << 3);
  case BuiltinType::Int128:
  case BuiltinType::UInt128:
    return 7 + (getIntWidth(Int128Ty) << 3);

  // char8_t, char16_t, char32_t, wchar_t rank equals their underlying type.
  case BuiltinType::Char8:
    return getIntegerRank(UnsignedCharTy.getTypePtr());
  case BuiltinType::Char16:
    return getIntegerRank(
        getFromTargetType(Target->getChar16Type()).getTypePtr());
  case BuiltinType::Char32:
    return getIntegerRank(
        getFromTargetType(Target->getChar32Type()).getTypePtr());
  case BuiltinType::WChar_S:
  case BuiltinType::WChar_U:
    return getIntegerRank(
        getFromTargetType(Target->getWCharType()).getTypePtr());
  }
}

QualType TreeContext::isPromotableBitField(Expr *E) const {
  if (E->isTypeDependent())
    return {};

  FieldDecl *Field = E->getSourceBitField();
  if (!Field)
    return {};

  QualType FT = Field->getType();

  uint64_t BitWidth = Field->getBitWidthValue(*this);
  uint64_t IntSize = getTypeSize(IntTy);
  // Integral bit-field promotion (C11 6.3.1.1/2). Wider than `int` keeps the
  // base type without integral promotion.
  //
  if (BitWidth < IntSize) {
    if (getLangOpts().MicrosoftExt || getLangOpts().MSVCCompat)
      return {};
    return IntTy;
  }

  if (BitWidth == IntSize)
    return FT->isSignedIntegerType() ? IntTy : UnsignedIntTy;

  // Bit-fields wider than int are not subject to promotions, and therefore act
  // like the base type. GCC has some weird bugs in this area that we
  // deliberately do not follow (GCC follows a pre-standard resolution to
  // C's DR315 which treats bit-width as being part of the type, and this leaks
  // into their semantics in some cases).
  return {};
}

QualType TreeContext::getPromotedIntegerType(QualType Promotable) const {
  assert(!Promotable.isNull());
  assert(isPromotableIntegerType(Promotable));
  if (const auto *ET = Promotable->getAs<EnumType>())
    return ET->getDecl()->getPromotionType();

  if (const auto *BT = Promotable->getAs<BuiltinType>()) {
    // `char8_t` / `char16_t` / `char32_t` / `wchar_t`: promote to the smallest
    // of int/unsigned/long/... that fits all values.
    if (BT->getKind() == BuiltinType::WChar_S ||
        BT->getKind() == BuiltinType::WChar_U ||
        BT->getKind() == BuiltinType::Char8 ||
        BT->getKind() == BuiltinType::Char16 ||
        BT->getKind() == BuiltinType::Char32) {
      bool FromIsSigned = BT->getKind() == BuiltinType::WChar_S;
      uint64_t FromSize = getTypeSize(BT);
      QualType PromoteTypes[] = {IntTy,      UnsignedIntTy,
                                 LongTy,     UnsignedLongTy,
                                 LongLongTy, UnsignedLongLongTy};
      for (const auto &PT : PromoteTypes) {
        uint64_t ToSize = getTypeSize(PT);
        if (FromSize < ToSize ||
            (FromSize == ToSize && FromIsSigned == PT->isSignedIntegerType()))
          return PT;
      }
      llvm_unreachable("char type should fit into long long");
    }
  }

  // At this point, we should have a signed or unsigned integer type.
  if (Promotable->isSignedIntegerType())
    return IntTy;
  uint64_t PromotableSize = getIntWidth(Promotable);
  uint64_t IntSize = getIntWidth(IntTy);
  assert(Promotable->isUnsignedIntegerType() && PromotableSize <= IntSize);
  return (PromotableSize != IntSize) ? IntTy : UnsignedIntTy;
}

namespace {
const Type *getEnumBaseType(const EnumType *ET) {
  if (ET->getDecl()->isComplete())
    return ET->getDecl()->getIntegerType().getTypePtr();
  return nullptr;
}
} // namespace

int TreeContext::getIntegerTypeOrder(QualType LHS, QualType RHS) const {
  const Type *LHSC = getCanonicalType(LHS).getTypePtr();
  const Type *RHSC = getCanonicalType(RHS).getTypePtr();

  // Unwrap enums to their underlying type.
  if (const auto *ET = dyn_cast<EnumType>(LHSC))
    LHSC = getEnumBaseType(ET);
  if (const auto *ET = dyn_cast<EnumType>(RHSC))
    RHSC = getEnumBaseType(ET);

  if (LHSC == RHSC)
    return 0;

  bool LHSUnsigned = LHSC->isUnsignedIntegerType();
  bool RHSUnsigned = RHSC->isUnsignedIntegerType();

  unsigned LHSRank = getIntegerRank(LHSC);
  unsigned RHSRank = getIntegerRank(RHSC);

  if (LHSUnsigned == RHSUnsigned) { // Both signed or both unsigned.
    if (LHSRank == RHSRank)
      return 0;
    return LHSRank > RHSRank ? 1 : -1;
  }

  // Otherwise, the LHS is signed and the RHS is unsigned or visa versa.
  if (LHSUnsigned) {
    // If the unsigned [LHS] type is larger, return it.
    if (LHSRank >= RHSRank)
      return 1;

    // If the signed type can represent all values of the unsigned type, it
    // wins.  Because we are dealing with 2's complement and types that are
    // powers of two larger than each other, this is always safe.
    return -1;
  }

  // If the unsigned [RHS] type is larger, return it.
  if (RHSRank >= LHSRank)
    return -1;

  // If the signed type can represent all values of the unsigned type, it
  // wins.  Because we are dealing with 2's complement and types that are
  // powers of two larger than each other, this is always safe.
  return 1;
}

TreeContext::InlineVariableDefinitionKind
TreeContext::getInlineVariableDefinitionKind(const VarDecl *VD) const {
  if (!VD->isInline())
    return InlineVariableDefinitionKind::None;

  return InlineVariableDefinitionKind::Weak;
}

namespace {
TypedefDecl *createCharPtrNamedVaListDecl(const TreeContext *Context,
                                          llvm::StringRef Name) {
  // typedef char* __builtin[_ms]_va_list;
  QualType T = Context->getPointerType(Context->CharTy);
  return Context->buildImplicitTypedef(T, Name);
}
} // namespace

namespace {
TypedefDecl *createMSVaListDecl(const TreeContext *Context) {
  return createCharPtrNamedVaListDecl(Context, "__builtin_ms_va_list");
}
} // namespace

namespace {
TypedefDecl *createCharPtrBuiltinVaListDecl(const TreeContext *Context) {
  return createCharPtrNamedVaListDecl(Context, "__builtin_va_list");
}
} // namespace

namespace {
TypedefDecl *createVoidPtrBuiltinVaListDecl(const TreeContext *Context) {
  // typedef void* __builtin_va_list;
  QualType T = Context->getPointerType(Context->VoidTy);
  return Context->buildImplicitTypedef(T, "__builtin_va_list");
}
} // namespace

namespace {
TypedefDecl *createAArch64ABIBuiltinVaListDecl(const TreeContext *Context) {
  // struct __va_list
  RecordDecl *VaListTagDecl = Context->buildImplicitRecord("__va_list");
  VaListTagDecl->startDefinition();

  const size_t NumFields = 5;
  QualType FieldTypes[NumFields];
  const char *FieldNames[NumFields];

  // void *__stack;
  FieldTypes[0] = Context->getPointerType(Context->VoidTy);
  FieldNames[0] = "__stack";

  // void *__gr_top;
  FieldTypes[1] = Context->getPointerType(Context->VoidTy);
  FieldNames[1] = "__gr_top";

  // void *__vr_top;
  FieldTypes[2] = Context->getPointerType(Context->VoidTy);
  FieldNames[2] = "__vr_top";

  // int __gr_offs;
  FieldTypes[3] = Context->IntTy;
  FieldNames[3] = "__gr_offs";

  // int __vr_offs;
  FieldTypes[4] = Context->IntTy;
  FieldNames[4] = "__vr_offs";

  for (unsigned i = 0; i < NumFields; ++i) {
    FieldDecl *Field = FieldDecl::Create(
        const_cast<TreeContext &>(*Context), VaListTagDecl, SourceLocation(),
        SourceLocation(), &Context->Idents.get(FieldNames[i]), FieldTypes[i],
        /*TInfo=*/nullptr, /*BitWidth=*/nullptr);
    Field->setAccess(AS_public);
    VaListTagDecl->addDecl(Field);
  }
  VaListTagDecl->completeDefinition();
  Context->VaListTagDecl = VaListTagDecl;
  QualType VaListTagType = Context->getRecordType(VaListTagDecl);

  // } __builtin_va_list;
  return Context->buildImplicitTypedef(VaListTagType, "__builtin_va_list");
}
} // namespace

namespace {
TypedefDecl *createX86_64ABIBuiltinVaListDecl(const TreeContext *Context) {
  // struct __va_list_tag {
  RecordDecl *VaListTagDecl;
  VaListTagDecl = Context->buildImplicitRecord("__va_list_tag");
  VaListTagDecl->startDefinition();

  const size_t NumFields = 4;
  QualType FieldTypes[NumFields];
  const char *FieldNames[NumFields];

  //   unsigned gp_offset;
  FieldTypes[0] = Context->UnsignedIntTy;
  FieldNames[0] = "gp_offset";

  //   unsigned fp_offset;
  FieldTypes[1] = Context->UnsignedIntTy;
  FieldNames[1] = "fp_offset";

  //   void* overflow_arg_area;
  FieldTypes[2] = Context->getPointerType(Context->VoidTy);
  FieldNames[2] = "overflow_arg_area";

  //   void* reg_save_area;
  FieldTypes[3] = Context->getPointerType(Context->VoidTy);
  FieldNames[3] = "reg_save_area";

  for (unsigned i = 0; i < NumFields; ++i) {
    FieldDecl *Field = FieldDecl::Create(
        const_cast<TreeContext &>(*Context), VaListTagDecl, SourceLocation(),
        SourceLocation(), &Context->Idents.get(FieldNames[i]), FieldTypes[i],
        /*TInfo=*/nullptr, /*BitWidth=*/nullptr);
    Field->setAccess(AS_public);
    VaListTagDecl->addDecl(Field);
  }
  VaListTagDecl->completeDefinition();
  Context->VaListTagDecl = VaListTagDecl;
  QualType VaListTagType = Context->getRecordType(VaListTagDecl);

  // };

  // typedef struct __va_list_tag __builtin_va_list[1];
  llvm::APInt Size(Context->getTypeSize(Context->getSizeType()), 1);
  QualType VaListTagArrayType = Context->getConstantArrayType(
      VaListTagType, Size, nullptr, ArraySizeModifier::Normal, 0);
  return Context->buildImplicitTypedef(VaListTagArrayType, "__builtin_va_list");
}
} // namespace

namespace {
TypedefDecl *createVaListDecl(const TreeContext *Context,
                              TargetInfo::BuiltinVaListKind Kind) {
  switch (Kind) {
  case TargetInfo::CharPtrBuiltinVaList:
    return createCharPtrBuiltinVaListDecl(Context);
  case TargetInfo::VoidPtrBuiltinVaList:
    return createVoidPtrBuiltinVaListDecl(Context);
  case TargetInfo::AArch64ABIBuiltinVaList:
    return createAArch64ABIBuiltinVaListDecl(Context);
  case TargetInfo::X86_64ABIBuiltinVaList:
    return createX86_64ABIBuiltinVaListDecl(Context);
  }

  llvm_unreachable("Unhandled __builtin_va_list type kind");
}
} // namespace

TypedefDecl *TreeContext::getBuiltinVaListDecl() const {
  if (!BuiltinVaListDecl) {
    BuiltinVaListDecl = createVaListDecl(this, Target->getBuiltinVaListKind());
    assert(BuiltinVaListDecl->isImplicit());
  }

  return BuiltinVaListDecl;
}

Decl *TreeContext::getVaListTagDecl() const {
  // Force the creation of VaListTagDecl by building the __builtin_va_list
  // declaration.
  if (!VaListTagDecl)
    (void)getBuiltinVaListDecl();

  return VaListTagDecl;
}

TypedefDecl *TreeContext::getBuiltinMSVaListDecl() const {
  if (!BuiltinMSVaListDecl)
    BuiltinMSVaListDecl = createMSVaListDecl(this);

  return BuiltinMSVaListDecl;
}

bool TreeContext::canBuiltinBeRedeclared(const FunctionDecl *FD) const {
  return BuiltinInfo.canBeRedeclared(FD->getBuiltinID());
}

CanQualType TreeContext::getFromTargetType(unsigned Type) const {
  switch (Type) {
  case TargetInfo::NoInt:
    return {};
  case TargetInfo::SignedChar:
    return SignedCharTy;
  case TargetInfo::UnsignedChar:
    return UnsignedCharTy;
  case TargetInfo::SignedShort:
    return ShortTy;
  case TargetInfo::UnsignedShort:
    return UnsignedShortTy;
  case TargetInfo::SignedInt:
    return IntTy;
  case TargetInfo::UnsignedInt:
    return UnsignedIntTy;
  case TargetInfo::SignedLong:
    return LongTy;
  case TargetInfo::UnsignedLong:
    return UnsignedLongTy;
  case TargetInfo::SignedLongLong:
    return LongLongTy;
  case TargetInfo::UnsignedLongLong:
    return UnsignedLongLongTy;
  }

  llvm_unreachable("Unhandled TargetInfo::IntType value");
}

namespace {
bool vectorTypesCompatible(const VectorType *LHS, const VectorType *RHS) {
  assert(LHS->isCanonicalUnqualified() && RHS->isCanonicalUnqualified());
  return LHS->getElementType() == RHS->getElementType() &&
         LHS->getNumElements() == RHS->getNumElements();
}
} // namespace

namespace {
bool matrixTypesCompatible(const ConstantMatrixType *LHS,
                           const ConstantMatrixType *RHS) {
  assert(LHS->isCanonicalUnqualified() && RHS->isCanonicalUnqualified());
  return LHS->getElementType() == RHS->getElementType() &&
         LHS->getNumRows() == RHS->getNumRows() &&
         LHS->getNumColumns() == RHS->getNumColumns();
}
} // namespace

bool TreeContext::areCompatibleVectorTypes(QualType FirstVec,
                                           QualType SecondVec) {
  assert(FirstVec->isVectorType() && "FirstVec should be a vector type");
  assert(SecondVec->isVectorType() && "SecondVec should be a vector type");

  if (hasSameUnqualifiedType(FirstVec, SecondVec))
    return true;

  // Treat Neon vector types as if they are the equivalent GCC vector types.
  const auto *First = FirstVec->castAs<VectorType>();
  const auto *Second = SecondVec->castAs<VectorType>();
  if (First->getNumElements() == Second->getNumElements() &&
      hasSameType(First->getElementType(), Second->getElementType()) &&
      First->getVectorKind() != VectorKind::SveFixedLengthData &&
      First->getVectorKind() != VectorKind::SveFixedLengthPredicate &&
      Second->getVectorKind() != VectorKind::SveFixedLengthData &&
      Second->getVectorKind() != VectorKind::SveFixedLengthPredicate)
    return true;

  return false;
}

namespace {
uint64_t getSVETypeSize(TreeContext &Context, const BuiltinType *Ty) {
  assert(Ty->isSveVLSBuiltinType() && "Invalid SVE Type");
  if (Ty->getKind() == BuiltinType::SveBool ||
      Ty->getKind() == BuiltinType::SveCount)
    return (Context.getLangOpts().VScaleMin * 128) / Context.getCharWidth();
  return Context.getLangOpts().VScaleMin * 128;
}
} // namespace

bool TreeContext::areCompatibleSveTypes(QualType FirstType,
                                        QualType SecondType) {
  assert(
      ((FirstType->isSVESizelessBuiltinType() && SecondType->isVectorType()) ||
       (FirstType->isVectorType() && SecondType->isSVESizelessBuiltinType())) &&
      "Expected SVE builtin type and vector type!");

  auto IsValidCast = [this](QualType FirstType, QualType SecondType) {
    if (const auto *BT = FirstType->getAs<BuiltinType>()) {
      if (const auto *VT = SecondType->getAs<VectorType>()) {
        // Predicates have the same representation as uint8 so we also have to
        // check the kind to make these types incompatible.
        if (VT->getVectorKind() == VectorKind::SveFixedLengthPredicate)
          return BT->getKind() == BuiltinType::SveBool;
        else if (VT->getVectorKind() == VectorKind::SveFixedLengthData)
          return VT->getElementType().getCanonicalType() ==
                 FirstType->getSveEltType(*this);
        else if (VT->getVectorKind() == VectorKind::Generic)
          return getTypeSize(SecondType) == getSVETypeSize(*this, BT) &&
                 hasSameType(VT->getElementType(),
                             getBuiltinVectorTypeInfo(BT).ElementType);
      }
    }
    return false;
  };

  return IsValidCast(FirstType, SecondType) ||
         IsValidCast(SecondType, FirstType);
}

bool TreeContext::areLaxCompatibleSveTypes(QualType FirstType,
                                           QualType SecondType) {
  assert(
      ((FirstType->isSVESizelessBuiltinType() && SecondType->isVectorType()) ||
       (FirstType->isVectorType() && SecondType->isSVESizelessBuiltinType())) &&
      "Expected SVE builtin type and vector type!");

  auto IsLaxCompatible = [this](QualType FirstType, QualType SecondType) {
    const auto *BT = FirstType->getAs<BuiltinType>();
    if (!BT)
      return false;

    const auto *VecTy = SecondType->getAs<VectorType>();
    if (VecTy && (VecTy->getVectorKind() == VectorKind::SveFixedLengthData ||
                  VecTy->getVectorKind() == VectorKind::Generic)) {
      const LangOptions::LaxVectorConversionKind LVCKind =
          getLangOpts().getLaxVectorConversions();

      // Can not convert between sve predicates and sve vectors because of
      // different size.
      if (BT->getKind() == BuiltinType::SveBool &&
          VecTy->getVectorKind() == VectorKind::SveFixedLengthData)
        return false;

      // If __ARM_FEATURE_SVE_BITS != N do not allow GNU vector lax conversion.
      // "Whenever __ARM_FEATURE_SVE_BITS==N, GNUT implicitly
      // converts to VLAT and VLAT implicitly converts to GNUT."
      // ACLE Spec Version 00bet6, 3.7.3.2. Behavior common to vectors and
      // predicates.
      if (VecTy->getVectorKind() == VectorKind::Generic &&
          getTypeSize(SecondType) != getSVETypeSize(*this, BT))
        return false;

      // If -flax-vector-conversions=all is specified, the types are
      // certainly compatible.
      if (LVCKind == LangOptions::LaxVectorConversionKind::All)
        return true;

      // If -flax-vector-conversions=integer is specified, the types are
      // compatible if the elements are integer types.
      if (LVCKind == LangOptions::LaxVectorConversionKind::Integer)
        return VecTy->getElementType().getCanonicalType()->isIntegerType() &&
               FirstType->getSveEltType(*this)->isIntegerType();
    }

    return false;
  };

  return IsLaxCompatible(FirstType, SecondType) ||
         IsLaxCompatible(SecondType, FirstType);
}

bool TreeContext::typesAreCompatible(QualType LHS, QualType RHS,
                                     bool CompareUnqualified) {
  return !mergeTypes(LHS, RHS, CompareUnqualified).isNull();
}

QualType TreeContext::mergeTransparentUnionType(QualType T, QualType SubType,
                                                bool Unqualified) {
  if (const RecordType *UT = T->getAsUnionType()) {
    RecordDecl *UD = UT->getDecl();
    if (UD->hasAttr<TransparentUnionAttr>()) {
      for (const auto *I : UD->fields()) {
        QualType ET = I->getType().getUnqualifiedType();
        QualType MT = mergeTypes(ET, SubType, Unqualified);
        if (!MT.isNull())
          return MT;
      }
    }
  }

  return {};
}

QualType TreeContext::mergeFunctionParameterTypes(QualType lhs, QualType rhs,
                                                  bool Unqualified) {
  // GNU extension: two types are compatible if they appear as a function
  // argument, one of the types is a transparent union type and the other
  // type is compatible with a union member
  QualType lmerge = mergeTransparentUnionType(lhs, rhs, Unqualified);
  if (!lmerge.isNull())
    return lmerge;

  QualType rmerge = mergeTransparentUnionType(rhs, lhs, Unqualified);
  if (!rmerge.isNull())
    return rmerge;

  return mergeTypes(lhs, rhs, Unqualified);
}

QualType TreeContext::mergeFunctionTypes(QualType lhs, QualType rhs,
                                         bool Unqualified,
                                         bool IsConditionalOperator) {
  const auto *lbase = lhs->castAs<FunctionType>();
  const auto *rbase = rhs->castAs<FunctionType>();
  const auto *lproto = dyn_cast<FunctionProtoType>(lbase);
  const auto *rproto = dyn_cast<FunctionProtoType>(rbase);
  bool allLTypes = true;
  bool allRTypes = true;

  QualType retType =
      mergeTypes(lbase->getReturnType(), rbase->getReturnType(), Unqualified);
  if (retType.isNull())
    return {};

  if (Unqualified)
    retType = retType.getUnqualifiedType();

  CanQualType LRetType = getCanonicalType(lbase->getReturnType());
  CanQualType RRetType = getCanonicalType(rbase->getReturnType());
  if (Unqualified) {
    LRetType = LRetType.getUnqualifiedType();
    RRetType = RRetType.getUnqualifiedType();
  }

  if (getCanonicalType(retType) != LRetType)
    allLTypes = false;
  if (getCanonicalType(retType) != RRetType)
    allRTypes = false;

  FunctionType::ExtInfo lbaseInfo = lbase->getExtInfo();
  FunctionType::ExtInfo rbaseInfo = rbase->getExtInfo();

  // Compatible functions must have compatible calling conventions
  if (lbaseInfo.getCC() != rbaseInfo.getCC())
    return {};

  // Regparm is part of the calling convention.
  if (lbaseInfo.getHasRegParm() != rbaseInfo.getHasRegParm())
    return {};
  if (lbaseInfo.getRegParm() != rbaseInfo.getRegParm())
    return {};

  if (lbaseInfo.getProducesResult() != rbaseInfo.getProducesResult())
    return {};
  if (lbaseInfo.getNoCallerSavedRegs() != rbaseInfo.getNoCallerSavedRegs())
    return {};
  if (lbaseInfo.getNoCfCheck() != rbaseInfo.getNoCfCheck())
    return {};

  // When merging declarations, it's common for supplemental information like
  // attributes to only be present in one of the declarations, and we generally
  // want type merging to preserve the union of information.  So a merged
  // function type should be noreturn if it was noreturn in *either* operand
  // type.
  //
  // But for the conditional operator, this is backwards.  The result of the
  // operator could be either operand, and its type should conservatively
  // reflect that.  So a function type in a composite type is noreturn only
  // if it's noreturn in *both* operand types.
  //
  // We use the simpler merge rule for `noreturn` instead of full subtype
  // lattice reasoning (rarely matters in practice).
  bool NoReturn = IsConditionalOperator
                      ? lbaseInfo.getNoReturn() && rbaseInfo.getNoReturn()
                      : lbaseInfo.getNoReturn() || rbaseInfo.getNoReturn();
  if (lbaseInfo.getNoReturn() != NoReturn)
    allLTypes = false;
  if (rbaseInfo.getNoReturn() != NoReturn)
    allRTypes = false;

  FunctionType::ExtInfo einfo = lbaseInfo.withNoReturn(NoReturn);

  if (lproto && rproto) { // two C99 style function prototypes
    assert(
        !lproto->hasExceptionSpec() && !rproto->hasExceptionSpec() &&
        "merging C composite types: exception specification not allowed here");
    // Compatible functions must have the same number of parameters
    if (lproto->getNumParams() != rproto->getNumParams())
      return {};

    // Variadic and non-variadic functions aren't compatible
    if (lproto->isVariadic() != rproto->isVariadic())
      return {};

    if (lproto->getMethodQuals() != rproto->getMethodQuals())
      return {};

    llvm::SmallVector<FunctionProtoType::ExtParameterInfo, 4> newParamInfos;
    bool canUseLeft, canUseRight;
    if (!mergeExtParameterInfo(lproto, rproto, canUseLeft, canUseRight,
                               newParamInfos))
      return {};

    if (!canUseLeft)
      allLTypes = false;
    if (!canUseRight)
      allRTypes = false;
    llvm::SmallVector<QualType, 10> types;
    for (unsigned i = 0, n = lproto->getNumParams(); i < n; i++) {
      QualType lParamType = lproto->getParamType(i).getUnqualifiedType();
      QualType rParamType = rproto->getParamType(i).getUnqualifiedType();
      QualType paramType =
          mergeFunctionParameterTypes(lParamType, rParamType, Unqualified);
      if (paramType.isNull())
        return {};

      if (Unqualified)
        paramType = paramType.getUnqualifiedType();

      types.push_back(paramType);
      if (Unqualified) {
        lParamType = lParamType.getUnqualifiedType();
        rParamType = rParamType.getUnqualifiedType();
      }

      if (getCanonicalType(paramType) != getCanonicalType(lParamType))
        allLTypes = false;
      if (getCanonicalType(paramType) != getCanonicalType(rParamType))
        allRTypes = false;
    }

    if (allLTypes)
      return lhs;
    if (allRTypes)
      return rhs;

    FunctionProtoType::ExtProtoInfo EPI = lproto->getExtProtoInfo();
    EPI.ExtInfo = einfo;
    EPI.ExtParameterInfos =
        newParamInfos.empty() ? nullptr : newParamInfos.data();
    return getFunctionType(retType, types, EPI);
  }

  if (lproto)
    allRTypes = false;
  if (rproto)
    allLTypes = false;

  const FunctionProtoType *proto = lproto ? lproto : rproto;
  if (proto) {
    assert(
        !proto->hasExceptionSpec() &&
        "merging C composite types: exception specification not allowed here");
    if (proto->isVariadic())
      return {};
    // Check that the types are compatible with the types that
    // would result from default argument promotions (C99 6.7.5.3p15).
    // The only types actually affected are promotable integer
    // types and floats, which would be passed as a different
    // type depending on whether the prototype is visible.
    for (unsigned i = 0, n = proto->getNumParams(); i < n; ++i) {
      QualType paramTy = proto->getParamType(i);

      // Look at the converted type of enum types, since that is the type used
      // to pass enum values.
      if (const auto *Enum = paramTy->getAs<EnumType>()) {
        paramTy = Enum->getDecl()->getIntegerType();
        if (paramTy.isNull())
          return {};
      }

      if (isPromotableIntegerType(paramTy) ||
          getCanonicalType(paramTy).getUnqualifiedType() == FloatTy)
        return {};
    }

    if (allLTypes)
      return lhs;
    if (allRTypes)
      return rhs;

    FunctionProtoType::ExtProtoInfo EPI = proto->getExtProtoInfo();
    EPI.ExtInfo = einfo;
    return getFunctionType(retType, proto->getParamTypes(), EPI);
  }

  if (allLTypes)
    return lhs;
  if (allRTypes)
    return rhs;
  return getFunctionNoProtoType(retType, einfo);
}

namespace {
QualType mergeEnumWithInteger(TreeContext &Context, const EnumType *ET,
                              QualType other, bool) {
  QualType underlyingType = ET->getDecl()->getIntegerType();
  if (underlyingType.isNull())
    return {};
  if (Context.hasSameType(underlyingType, other))
    return other;

  return {};
}
} // namespace

QualType TreeContext::mergeTypes(QualType LHS, QualType RHS, bool Unqualified,
                                 bool IsConditionalOperator) {

  if (Unqualified) {
    LHS = LHS.getUnqualifiedType();
    RHS = RHS.getUnqualifiedType();
  }

  QualType LHSCan = getCanonicalType(LHS), RHSCan = getCanonicalType(RHS);

  // If two types are identical, they are compatible.
  if (LHSCan == RHSCan)
    return LHS;

  // If the qualifiers are different, the types aren't compatible.
  Qualifiers LQuals = LHSCan.getLocalQualifiers();
  Qualifiers RQuals = RHSCan.getLocalQualifiers();
  if (LQuals != RQuals)
    return {};

  // Okay, qualifiers are equal.

  Type::TypeClass LHSClass = LHSCan->getTypeClass();
  Type::TypeClass RHSClass = RHSCan->getTypeClass();

  // We want to consider the two function types to be the same for these
  // comparisons, just force one to the other.
  if (LHSClass == Type::FunctionProto)
    LHSClass = Type::FunctionNoProto;
  if (RHSClass == Type::FunctionProto)
    RHSClass = Type::FunctionNoProto;

  // Same as above for arrays
  if (LHSClass == Type::VariableArray || LHSClass == Type::IncompleteArray)
    LHSClass = Type::ConstantArray;
  if (RHSClass == Type::VariableArray || RHSClass == Type::IncompleteArray)
    RHSClass = Type::ConstantArray;

  // Canonicalize ExtVector -> Vector.
  if (LHSClass == Type::ExtVector)
    LHSClass = Type::Vector;
  if (RHSClass == Type::ExtVector)
    RHSClass = Type::Vector;

  // If the canonical type classes don't match.
  if (LHSClass != RHSClass) {
    // Note that we only have special rules for turning block enum
    // returns into block int returns, not vice-versa.
    if (const auto *ETy = LHS->getAs<EnumType>()) {
      return mergeEnumWithInteger(*this, ETy, RHS, false);
    }
    if (const EnumType *ETy = RHS->getAs<EnumType>()) {
      return mergeEnumWithInteger(*this, ETy, LHS, false);
    }
    // Allow __auto_type to match anything; it merges to the type with more
    // information.
    if (const auto *AT = LHS->getAs<AutoType>()) {
      if (!AT->isDeduced() && AT->isGNUAutoType())
        return RHS;
    }
    if (const auto *AT = RHS->getAs<AutoType>()) {
      if (!AT->isDeduced() && AT->isGNUAutoType())
        return LHS;
    }
    return {};
  }

  // The canonical type classes match.
  switch (LHSClass) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#include "neverc/Tree/TypeNodes.td.h"
    llvm_unreachable("Non-canonical and dependent types shouldn't get here");

  case Type::IncompleteArray:
  case Type::VariableArray:
  case Type::FunctionProto:
  case Type::ExtVector:
    llvm_unreachable("Types are eliminated above");

  case Type::Pointer: {
    // Merge two pointer types, while trying to preserve typedef info
    QualType LHSPointee = LHS->castAs<PointerType>()->getPointeeType();
    QualType RHSPointee = RHS->castAs<PointerType>()->getPointeeType();
    if (Unqualified) {
      LHSPointee = LHSPointee.getUnqualifiedType();
      RHSPointee = RHSPointee.getUnqualifiedType();
    }
    QualType ResultType = mergeTypes(LHSPointee, RHSPointee, Unqualified);
    if (ResultType.isNull())
      return {};
    if (getCanonicalType(LHSPointee) == getCanonicalType(ResultType))
      return LHS;
    if (getCanonicalType(RHSPointee) == getCanonicalType(ResultType))
      return RHS;
    return getPointerType(ResultType);
  }
  case Type::Atomic: {
    // Merge two pointer types, while trying to preserve typedef info
    QualType LHSValue = LHS->castAs<AtomicType>()->getValueType();
    QualType RHSValue = RHS->castAs<AtomicType>()->getValueType();
    if (Unqualified) {
      LHSValue = LHSValue.getUnqualifiedType();
      RHSValue = RHSValue.getUnqualifiedType();
    }
    QualType ResultType = mergeTypes(LHSValue, RHSValue, Unqualified);
    if (ResultType.isNull())
      return {};
    if (getCanonicalType(LHSValue) == getCanonicalType(ResultType))
      return LHS;
    if (getCanonicalType(RHSValue) == getCanonicalType(ResultType))
      return RHS;
    return getAtomicType(ResultType);
  }
  case Type::ConstantArray: {
    const ConstantArrayType *LCAT = getAsConstantArrayType(LHS);
    const ConstantArrayType *RCAT = getAsConstantArrayType(RHS);
    if (LCAT && RCAT && RCAT->getSize() != LCAT->getSize())
      return {};

    QualType LHSElem = getAsArrayType(LHS)->getElementType();
    QualType RHSElem = getAsArrayType(RHS)->getElementType();
    if (Unqualified) {
      LHSElem = LHSElem.getUnqualifiedType();
      RHSElem = RHSElem.getUnqualifiedType();
    }

    QualType ResultType = mergeTypes(LHSElem, RHSElem, Unqualified);
    if (ResultType.isNull())
      return {};

    const VariableArrayType *LVAT = getAsVariableArrayType(LHS);
    const VariableArrayType *RVAT = getAsVariableArrayType(RHS);

    // If either side is a variable array, and both are complete, check whether
    // the current dimension is definite.
    if (LVAT || RVAT) {
      auto SizeFetch =
          [this](const VariableArrayType *VAT,
                 const ConstantArrayType *CAT) -> std::pair<bool, llvm::APInt> {
        if (VAT) {
          std::optional<llvm::APSInt> TheInt;
          Expr *E = VAT->getSizeExpr();
          if (E && (TheInt = E->getIntegerConstantExpr(*this)))
            return std::make_pair(true, *TheInt);
          return std::make_pair(false, llvm::APSInt());
        }
        if (CAT)
          return std::make_pair(true, CAT->getSize());
        return std::make_pair(false, llvm::APInt());
      };

      bool HaveLSize, HaveRSize;
      llvm::APInt LSize, RSize;
      std::tie(HaveLSize, LSize) = SizeFetch(LVAT, LCAT);
      std::tie(HaveRSize, RSize) = SizeFetch(RVAT, RCAT);
      if (HaveLSize && HaveRSize && !llvm::APInt::isSameValue(LSize, RSize))
        return {}; // Definite, but unequal, array dimension
    }

    if (LCAT && getCanonicalType(LHSElem) == getCanonicalType(ResultType))
      return LHS;
    if (RCAT && getCanonicalType(RHSElem) == getCanonicalType(ResultType))
      return RHS;
    if (LCAT)
      return getConstantArrayType(ResultType, LCAT->getSize(),
                                  LCAT->getSizeExpr(), ArraySizeModifier(), 0);
    if (RCAT)
      return getConstantArrayType(ResultType, RCAT->getSize(),
                                  RCAT->getSizeExpr(), ArraySizeModifier(), 0);
    if (LVAT && getCanonicalType(LHSElem) == getCanonicalType(ResultType))
      return LHS;
    if (RVAT && getCanonicalType(RHSElem) == getCanonicalType(ResultType))
      return RHS;
    if (LVAT) {
      return LHS;
    }
    if (RVAT) {
      return RHS;
    }
    if (getCanonicalType(LHSElem) == getCanonicalType(ResultType))
      return LHS;
    if (getCanonicalType(RHSElem) == getCanonicalType(ResultType))
      return RHS;
    return getIncompleteArrayType(ResultType, ArraySizeModifier(), 0);
  }
  case Type::FunctionNoProto:
    return mergeFunctionTypes(LHS, RHS, Unqualified, IsConditionalOperator);
  case Type::Record:
  case Type::Enum:
    return {};
  case Type::Builtin:
    // Only exactly equal builtin types are compatible, which is tested above.
    return {};
  case Type::Complex:
    // Distinct complex types are incompatible.
    return {};
  case Type::Vector:
    if (vectorTypesCompatible(LHSCan->castAs<VectorType>(),
                              RHSCan->castAs<VectorType>()))
      return LHS;
    return {};
  case Type::ConstantMatrix:
    if (matrixTypesCompatible(LHSCan->castAs<ConstantMatrixType>(),
                              RHSCan->castAs<ConstantMatrixType>()))
      return LHS;
    return {};
  case Type::BitInt: {
    // Merge two bit-precise int types, while trying to preserve typedef info.
    bool LHSUnsigned = LHS->castAs<BitIntType>()->isUnsigned();
    bool RHSUnsigned = RHS->castAs<BitIntType>()->isUnsigned();
    unsigned LHSBits = LHS->castAs<BitIntType>()->getNumBits();
    unsigned RHSBits = RHS->castAs<BitIntType>()->getNumBits();

    // Like unsigned/int, shouldn't have a type if they don't match.
    if (LHSUnsigned != RHSUnsigned)
      return {};

    if (LHSBits != RHSBits)
      return {};
    return LHS;
  }
  case Type::Auto:
    return {};
  }

  llvm_unreachable("Invalid Type::Class!");
}

bool TreeContext::mergeExtParameterInfo(
    const FunctionProtoType *FirstFnType, const FunctionProtoType *SecondFnType,
    bool &CanUseFirst, bool &CanUseSecond,
    llvm::SmallVectorImpl<FunctionProtoType::ExtParameterInfo> &NewParamInfos) {
  assert(NewParamInfos.empty() && "param info list not empty");
  CanUseFirst = CanUseSecond = true;
  bool FirstHasInfo = FirstFnType->hasExtParameterInfos();
  bool SecondHasInfo = SecondFnType->hasExtParameterInfos();

  // Fast path: if the first type doesn't have ext parameter infos,
  // we match if and only if the second type also doesn't have them.
  if (!FirstHasInfo && !SecondHasInfo)
    return true;

  bool NeedParamInfo = false;
  size_t E = FirstHasInfo ? FirstFnType->getExtParameterInfos().size()
                          : SecondFnType->getExtParameterInfos().size();

  for (size_t I = 0; I < E; ++I) {
    FunctionProtoType::ExtParameterInfo FirstParam, SecondParam;
    if (FirstHasInfo)
      FirstParam = FirstFnType->getExtParameterInfo(I);
    if (SecondHasInfo)
      SecondParam = SecondFnType->getExtParameterInfo(I);

    // Cannot merge unless everything except the noescape flag matches.
    if (FirstParam.withIsNoEscape(false) != SecondParam.withIsNoEscape(false))
      return false;

    bool FirstNoEscape = FirstParam.isNoEscape();
    bool SecondNoEscape = SecondParam.isNoEscape();
    bool IsNoEscape = FirstNoEscape && SecondNoEscape;
    NewParamInfos.push_back(FirstParam.withIsNoEscape(IsNoEscape));
    if (NewParamInfos.back().getOpaqueValue())
      NeedParamInfo = true;
    if (FirstNoEscape != IsNoEscape)
      CanUseFirst = false;
    if (SecondNoEscape != IsNoEscape)
      CanUseSecond = false;
  }

  if (!NeedParamInfo)
    NewParamInfos.clear();

  return true;
}

unsigned TreeContext::getIntWidth(QualType T) const {
  if (const auto *ET = T->getAs<EnumType>())
    T = ET->getDecl()->getIntegerType();
  if (T->isBooleanType())
    return 1;
  if (const auto *EIT = T->getAs<BitIntType>())
    return EIT->getNumBits();
  // For builtin types, just use the standard type sizing method
  return (unsigned)getTypeSize(T);
}

QualType TreeContext::getCorrespondingUnsignedType(QualType T) const {
  assert((T->hasIntegerRepresentation() || T->isEnumeralType() ||
          T->isFixedPointType()) &&
         "Unexpected type");

  // Turn <4 x signed int> -> <4 x unsigned int>
  if (const auto *VTy = T->getAs<VectorType>())
    return getVectorType(getCorrespondingUnsignedType(VTy->getElementType()),
                         VTy->getNumElements(), VTy->getVectorKind());

  // For _BitInt, return an unsigned _BitInt with same width.
  if (const auto *EITy = T->getAs<BitIntType>())
    return getBitIntType(/*Unsigned=*/true, EITy->getNumBits());

  // For enums, get the underlying integer type of the enum, and let the general
  // integer type signchanging code handle it.
  if (const auto *ETy = T->getAs<EnumType>())
    T = ETy->getDecl()->getIntegerType();

  switch (T->castAs<BuiltinType>()->getKind()) {
  case BuiltinType::Char_U:
    // Plain `char` is mapped to `unsigned char` even if it's already unsigned
  case BuiltinType::Char_S:
  case BuiltinType::SChar:
  case BuiltinType::Char8:
    return UnsignedCharTy;
  case BuiltinType::Short:
    return UnsignedShortTy;
  case BuiltinType::Int:
    return UnsignedIntTy;
  case BuiltinType::Long:
    return UnsignedLongTy;
  case BuiltinType::LongLong:
    return UnsignedLongLongTy;
  case BuiltinType::Int128:
    return UnsignedInt128Ty;
  // wchar_t is special. It is either signed or not, but when it's signed,
  // there's no matching "unsigned wchar_t". Therefore we return the unsigned
  // version of its underlying type instead.
  case BuiltinType::WChar_S:
    return getUnsignedWCharType();

  case BuiltinType::ShortAccum:
    return UnsignedShortAccumTy;
  case BuiltinType::Accum:
    return UnsignedAccumTy;
  case BuiltinType::LongAccum:
    return UnsignedLongAccumTy;
  case BuiltinType::SatShortAccum:
    return SatUnsignedShortAccumTy;
  case BuiltinType::SatAccum:
    return SatUnsignedAccumTy;
  case BuiltinType::SatLongAccum:
    return SatUnsignedLongAccumTy;
  case BuiltinType::ShortFract:
    return UnsignedShortFractTy;
  case BuiltinType::Fract:
    return UnsignedFractTy;
  case BuiltinType::LongFract:
    return UnsignedLongFractTy;
  case BuiltinType::SatShortFract:
    return SatUnsignedShortFractTy;
  case BuiltinType::SatFract:
    return SatUnsignedFractTy;
  case BuiltinType::SatLongFract:
    return SatUnsignedLongFractTy;
  default:
    assert((T->hasUnsignedIntegerRepresentation() ||
            T->isUnsignedFixedPointType()) &&
           "Unexpected signed integer or fixed point type");
    return T;
  }
}

QualType TreeContext::getCorrespondingSignedType(QualType T) const {
  assert((T->hasIntegerRepresentation() || T->isEnumeralType() ||
          T->isFixedPointType()) &&
         "Unexpected type");

  // Turn <4 x unsigned int> -> <4 x signed int>
  if (const auto *VTy = T->getAs<VectorType>())
    return getVectorType(getCorrespondingSignedType(VTy->getElementType()),
                         VTy->getNumElements(), VTy->getVectorKind());

  // For _BitInt, return a signed _BitInt with same width.
  if (const auto *EITy = T->getAs<BitIntType>())
    return getBitIntType(/*Unsigned=*/false, EITy->getNumBits());

  // For enums, get the underlying integer type of the enum, and let the general
  // integer type signchanging code handle it.
  if (const auto *ETy = T->getAs<EnumType>())
    T = ETy->getDecl()->getIntegerType();

  switch (T->castAs<BuiltinType>()->getKind()) {
  case BuiltinType::Char_S:
    // Plain `char` is mapped to `signed char` even if it's already signed
  case BuiltinType::Char_U:
  case BuiltinType::UChar:
  case BuiltinType::Char8:
    return SignedCharTy;
  case BuiltinType::UShort:
    return ShortTy;
  case BuiltinType::UInt:
    return IntTy;
  case BuiltinType::ULong:
    return LongTy;
  case BuiltinType::ULongLong:
    return LongLongTy;
  case BuiltinType::UInt128:
    return Int128Ty;
  // wchar_t is special. It is either unsigned or not, but when it's unsigned,
  // there's no matching "signed wchar_t". Therefore we return the signed
  // version of its underlying type instead.
  case BuiltinType::WChar_U:
    return getSignedWCharType();

  case BuiltinType::UShortAccum:
    return ShortAccumTy;
  case BuiltinType::UAccum:
    return AccumTy;
  case BuiltinType::ULongAccum:
    return LongAccumTy;
  case BuiltinType::SatUShortAccum:
    return SatShortAccumTy;
  case BuiltinType::SatUAccum:
    return SatAccumTy;
  case BuiltinType::SatULongAccum:
    return SatLongAccumTy;
  case BuiltinType::UShortFract:
    return ShortFractTy;
  case BuiltinType::UFract:
    return FractTy;
  case BuiltinType::ULongFract:
    return LongFractTy;
  case BuiltinType::SatUShortFract:
    return SatShortFractTy;
  case BuiltinType::SatUFract:
    return SatFractTy;
  case BuiltinType::SatULongFract:
    return SatLongFractTy;
  case BuiltinType::Bool: //[MSVC Compatibility]
    return BoolTy;
  default:
    assert(
        (T->hasSignedIntegerRepresentation() || T->isSignedFixedPointType()) &&
        "Unexpected signed integer or fixed point type");
    return T;
  }
}

TreeMutationListener::~TreeMutationListener() = default;

namespace {
QualType decodeTypeFromStr(const char *&Str, const TreeContext &Context,
                           TreeContext::GetBuiltinTypeError &Error,
                           bool &RequiresICE, bool AllowTypeModifiers) {
  // Modifiers.
  int HowLong = 0;
  bool Signed = false, Unsigned = false;
  RequiresICE = false;

  // Read the prefixed modifiers first.
  bool Done = false;
#ifndef NDEBUG
  bool IsSpecial = false;
#endif
  while (!Done) {
    switch (*Str++) {
    default:
      Done = true;
      --Str;
      break;
    case 'I':
      RequiresICE = true;
      break;
    case 'S':
      assert(!Unsigned && "Can't use both 'S' and 'U' modifiers!");
      assert(!Signed && "Can't use 'S' modifier multiple times!");
      Signed = true;
      break;
    case 'U':
      assert(!Signed && "Can't use both 'S' and 'U' modifiers!");
      assert(!Unsigned && "Can't use 'U' modifier multiple times!");
      Unsigned = true;
      break;
    case 'L':
      assert(!IsSpecial && "Can't use 'L' with 'W', 'N', 'Z' or 'O' modifiers");
      assert(HowLong <= 2 && "Can't have LLLL modifier");
      ++HowLong;
      break;
    case 'N':
      // 'N' behaves like 'L' for all non LP64 targets and 'int' otherwise.
      assert(!IsSpecial && "Can't use two 'N', 'W', 'Z' or 'O' modifiers!");
      assert(HowLong == 0 && "Can't use both 'L' and 'N' modifiers!");
#ifndef NDEBUG
      IsSpecial = true;
#endif
      if (Context.getTargetInfo().getLongWidth() == 32)
        ++HowLong;
      break;
    case 'Q':
      // long or long long
      if (Context.getTargetInfo().PointerWidth == 32)
        HowLong = 1;
      else if (Context.getTargetInfo().PointerWidth == 64)
        HowLong = 2;
      break;
    case 'W':
      // This modifier represents int64 type.
      assert(!IsSpecial && "Can't use two 'N', 'W', 'Z' or 'O' modifiers!");
      assert(HowLong == 0 && "Can't use both 'L' and 'W' modifiers!");
#ifndef NDEBUG
      IsSpecial = true;
#endif
      switch (Context.getTargetInfo().getInt64Type()) {
      default:
        llvm_unreachable("Unexpected integer type");
      case TargetInfo::SignedLong:
        HowLong = 1;
        break;
      case TargetInfo::SignedLongLong:
        HowLong = 2;
        break;
      }
      break;
    case 'Z':
      // This modifier represents int32 type.
      assert(!IsSpecial && "Can't use two 'N', 'W', 'Z' or 'O' modifiers!");
      assert(HowLong == 0 && "Can't use both 'L' and 'Z' modifiers!");
#ifndef NDEBUG
      IsSpecial = true;
#endif
      switch (Context.getTargetInfo().getIntTypeByWidth(32, true)) {
      default:
        llvm_unreachable("Unexpected integer type");
      case TargetInfo::SignedInt:
        HowLong = 0;
        break;
      case TargetInfo::SignedLong:
        HowLong = 1;
        break;
      case TargetInfo::SignedLongLong:
        HowLong = 2;
        break;
      }
      break;
    case 'O':
      assert(!IsSpecial && "Can't use two 'N', 'W', 'Z' or 'O' modifiers!");
      assert(HowLong == 0 && "Can't use both 'L' and 'O' modifiers!");
#ifndef NDEBUG
      IsSpecial = true;
#endif
      HowLong = 2;
      break;
    }
  }

  QualType Type;

  // Read the base type.
  switch (*Str++) {
  default:
    llvm_unreachable("Unknown builtin type letter!");
  case 'x':
    assert(HowLong == 0 && !Signed && !Unsigned &&
           "Bad modifiers used with 'x'!");
    Type = Context.Float16Ty;
    break;
  case 'y':
    assert(HowLong == 0 && !Signed && !Unsigned &&
           "Bad modifiers used with 'y'!");
    Type = Context.BFloat16Ty;
    break;
  case 'v':
    assert(HowLong == 0 && !Signed && !Unsigned &&
           "Bad modifiers used with 'v'!");
    Type = Context.VoidTy;
    break;
  case 'h':
    assert(HowLong == 0 && !Signed && !Unsigned &&
           "Bad modifiers used with 'h'!");
    Type = Context.HalfTy;
    break;
  case 'f':
    assert(HowLong == 0 && !Signed && !Unsigned &&
           "Bad modifiers used with 'f'!");
    Type = Context.FloatTy;
    break;
  case 'd':
    assert(HowLong < 3 && !Signed && !Unsigned &&
           "Bad modifiers used with 'd'!");
    if (HowLong == 1)
      Type = Context.LongDoubleTy;
    else if (HowLong == 2)
      Type = Context.Float128Ty;
    else
      Type = Context.DoubleTy;
    break;
  case 's':
    assert(HowLong == 0 && "Bad modifiers used with 's'!");
    if (Unsigned)
      Type = Context.UnsignedShortTy;
    else
      Type = Context.ShortTy;
    break;
  case 'i':
    if (HowLong == 3)
      Type = Unsigned ? Context.UnsignedInt128Ty : Context.Int128Ty;
    else if (HowLong == 2)
      Type = Unsigned ? Context.UnsignedLongLongTy : Context.LongLongTy;
    else if (HowLong == 1)
      Type = Unsigned ? Context.UnsignedLongTy : Context.LongTy;
    else
      Type = Unsigned ? Context.UnsignedIntTy : Context.IntTy;
    break;
  case 'c':
    assert(HowLong == 0 && "Bad modifiers used with 'c'!");
    if (Signed)
      Type = Context.SignedCharTy;
    else if (Unsigned)
      Type = Context.UnsignedCharTy;
    else
      Type = Context.CharTy;
    break;
  case 'b': // boolean
    assert(HowLong == 0 && !Signed && !Unsigned && "Bad modifiers for 'b'!");
    Type = Context.BoolTy;
    break;
  case 'z': // size_t.
    assert(HowLong == 0 && !Signed && !Unsigned && "Bad modifiers for 'z'!");
    Type = Context.getSizeType();
    break;
  case 'w': // wchar_t.
    assert(HowLong == 0 && !Signed && !Unsigned && "Bad modifiers for 'w'!");
    Type = Context.getWideCharType();
    break;
  case 'G':
    Type = Context.VoidPtrTy;
    break;
  case 'H':
    Type = Context.VoidPtrTy;
    break;
  case 'M':
    Type = Context.VoidPtrTy;
    break;
  case 'a':
    Type = Context.getBuiltinVaListType();
    assert(!Type.isNull() && "builtin va list type not initialized!");
    break;
  case 'A':
    // This is a "reference" to a va_list; however, what exactly
    // this means depends on how va_list is defined. There are two
    // different kinds of va_list: ones passed by value, and ones
    // passed by reference.  An example of a by-value va_list is
    // x86, where va_list is a char*. An example of by-ref va_list
    // is x86-64, where va_list is a __va_list_tag[1]. For x86,
    // we want this argument to be a char*&; for x86-64, we want
    // it to be a __va_list_tag*.
    Type = Context.getBuiltinVaListType();
    assert(!Type.isNull() && "builtin va list type not initialized!");
    if (Type->isArrayType())
      Type = Context.getArrayDecayedType(Type);
    break;
  case 'q': {
    char *End;
    unsigned NumElements = strtoul(Str, &End, 10);
    assert(End != Str && "Missing vector size");
    Str = End;

    QualType ElementType =
        decodeTypeFromStr(Str, Context, Error, RequiresICE, false);
    assert(!RequiresICE && "Can't require vector ICE");

    Type = Context.getScalableVectorType(ElementType, NumElements);
    break;
  }
  case 'Q': {
    switch (*Str++) {
    case 'a': {
      Type = Context.SveCountTy;
      break;
    }
    default:
      llvm_unreachable("Unexpected target builtin type");
    }
    break;
  }
  case 'V': {
    char *End;
    unsigned NumElements = strtoul(Str, &End, 10);
    assert(End != Str && "Missing vector size");
    Str = End;

    QualType ElementType =
        decodeTypeFromStr(Str, Context, Error, RequiresICE, false);
    assert(!RequiresICE && "Can't require vector ICE");

    Type = Context.getVectorType(ElementType, NumElements, VectorKind::Generic);
    break;
  }
  case 'E': {
    char *End;

    unsigned NumElements = strtoul(Str, &End, 10);
    assert(End != Str && "Missing vector size");

    Str = End;

    QualType ElementType =
        decodeTypeFromStr(Str, Context, Error, RequiresICE, false);
    Type = Context.getExtVectorType(ElementType, NumElements);
    break;
  }
  case 'X': {
    QualType ElementType =
        decodeTypeFromStr(Str, Context, Error, RequiresICE, false);
    assert(!RequiresICE && "Can't require complex ICE");
    Type = Context.getComplexType(ElementType);
    break;
  }
  case 'Y':
    Type = Context.getPointerDiffType();
    break;
  case 'P':
    Type = Context.getFILEType();
    if (Type.isNull()) {
      Error = TreeContext::GE_Missing_stdio;
      return {};
    }
    break;
  case 'J':
    if (Signed)
      Type = Context.getsigjmp_bufType();
    else
      Type = Context.getjmp_bufType();

    if (Type.isNull()) {
      Error = TreeContext::GE_Missing_setjmp;
      return {};
    }
    break;
  case 'K':
    assert(HowLong == 0 && !Signed && !Unsigned && "Bad modifiers for 'K'!");
    Type = Context.getucontext_tType();

    if (Type.isNull()) {
      Error = TreeContext::GE_Missing_ucontext;
      return {};
    }
    break;
  case 'p':
    Type = Context.getProcessIDType();
    break;
  }

  // If there are modifiers and if we're allowed to parse them, go for it.
  Done = !AllowTypeModifiers;
  while (!Done) {
    switch (*Str++) {
    default:
      Done = true;
      --Str;
      break;
    case '*':
    case '&': {
      // Both pointers and references can have their pointee types
      // qualified with an address space.
      char *End;
      unsigned AddrSpace = strtoul(Str, &End, 10);
      if (End != Str) {
        // Note AddrSpace == 0 is not the same as an unspecified address space.
        Type = Context.getAddrSpaceQualType(
            Type, Context.getLangASForBuiltinAddressSpace(AddrSpace));
        Str = End;
      }
      Type = Context.getPointerType(Type);
      break;
    }
    case 'C':
      Type = Type.withConst();
      break;
    case 'D':
      Type = Context.getVolatileType(Type);
      break;
    case 'R':
      Type = Type.withRestrict();
      break;
    }
  }

  assert((!RequiresICE || Type->isIntegralOrEnumerationType()) &&
         "Integer constant 'I' type must be an integer");

  return Type;
}
} // namespace

QualType TreeContext::GetBuiltinType(unsigned Id, GetBuiltinTypeError &Error,
                                     unsigned *IntegerConstantArgs) const {
  const char *TypeStr = BuiltinInfo.getTypeString(Id);
  if (TypeStr[0] == '\0') {
    Error = GE_Missing_type;
    return {};
  }

  llvm::SmallVector<QualType, 8> ArgTypes;

  bool RequiresICE = false;
  Error = GE_None;
  QualType ResType =
      decodeTypeFromStr(TypeStr, *this, Error, RequiresICE, true);
  if (Error != GE_None)
    return {};

  assert(!RequiresICE && "Result of intrinsic cannot be required to be an ICE");

  while (TypeStr[0] && TypeStr[0] != '.') {
    QualType Ty = decodeTypeFromStr(TypeStr, *this, Error, RequiresICE, true);
    if (Error != GE_None)
      return {};

    // If this argument is required to be an IntegerConstantExpression and the
    // caller cares, fill in the bitmask we return.
    if (RequiresICE && IntegerConstantArgs)
      *IntegerConstantArgs |= 1 << ArgTypes.size();

    // Do array -> pointer decay.  The builtin should use the decayed type.
    if (Ty->isArrayType())
      Ty = getArrayDecayedType(Ty);

    ArgTypes.push_back(Ty);
  }

  assert((TypeStr[0] != '.' || TypeStr[1] == 0) &&
         "'.' should only occur at end of builtin type list!");

  bool Variadic = (TypeStr[0] == '.');

  FunctionType::ExtInfo EI(
      getDefaultCallingConvention(Variadic, /*IsBuiltin=*/true));
  if (BuiltinInfo.isNoReturn(Id))
    EI = EI.withNoReturn(true);

  // We really shouldn't be making a no-proto type here.
  if (ArgTypes.empty() && Variadic && !getLangOpts().requiresStrictPrototypes())
    return getFunctionNoProtoType(ResType, EI);

  FunctionProtoType::ExtProtoInfo EPI;
  EPI.ExtInfo = EI;
  EPI.Variadic = Variadic;

  return getFunctionType(ResType, ArgTypes, EPI);
}

namespace {
GVALinkage basicGVALinkageForFunction(const TreeContext &Context,
                                      const FunctionDecl *FD) {
  if (!FD->isExternallyVisible())
    return GVA_Internal;

  GVALinkage External = GVA_StrongExternal;

  if (!FD->isInlined())
    return External;

  if (!FD->hasAttr<DLLExportAttr>() || FD->hasAttr<GNUInlineAttr>()) {

    // GNU or C99 inline semantics. Determine whether this symbol should be
    // externally visible.
    if (FD->isInlineDefinitionExternallyVisible())
      return External;

    // C99 inline semantics, where the symbol is not externally visible.
    return GVA_AvailableExternally;
  }

  // Functions specified with extern and inline in -fms-compatibility mode
  // forcibly get emitted.  While the body of the function cannot be later
  // replaced, the function definition cannot be discarded.
  if (FD->isMSExternInline())
    return GVA_StrongODR;

  return GVA_DiscardableODR;
}
} // namespace

namespace {
GVALinkage adjustGVALinkageForAttributes(const TreeContext &Context,
                                         const Decl *D, GVALinkage L) {
  // See http://msdn.microsoft.com/en-us/library/xa0d9ste.aspx
  // dllexport/dllimport on inline functions.
  if (D->hasAttr<DLLImportAttr>()) {
    if (L == GVA_DiscardableODR || L == GVA_StrongODR)
      return GVA_AvailableExternally;
  } else if (D->hasAttr<DLLExportAttr>()) {
    if (L == GVA_DiscardableODR)
      return GVA_StrongODR;
  }
  return L;
}
} // namespace

namespace {
GVALinkage adjustGVALinkageForExternalDefinitionKind(const TreeContext &Ctx,
                                                     const Decl *D,
                                                     GVALinkage L) {
  return L;
}
} // namespace

GVALinkage TreeContext::GetGVALinkageForFunction(const FunctionDecl *FD) const {
  return adjustGVALinkageForExternalDefinitionKind(
      *this, FD,
      adjustGVALinkageForAttributes(*this, FD,
                                    basicGVALinkageForFunction(*this, FD)));
}

namespace {
GVALinkage basicGVALinkageForVariable(const TreeContext &Context,
                                      const VarDecl *VD) {
  if (!VD->isExternallyVisible())
    return GVA_Internal;

  if (VD->isStaticLocal()) {
    const DeclContext *LexicalContext = VD->getParentFunctionOrMethod();
    while (LexicalContext && !isa<FunctionDecl>(LexicalContext))
      LexicalContext = LexicalContext->getLexicalParent();

    if (!LexicalContext)
      return GVA_DiscardableODR;

    // Otherwise, let the static local variable inherit its linkage from the
    // nearest enclosing function.
    auto StaticLocalLinkage =
        Context.GetGVALinkageForFunction(cast<FunctionDecl>(LexicalContext));

    // Itanium ABI 5.2.2: "Each COMDAT group [for a static local variable] must
    // be emitted in any object with references to the symbol for the object it
    // contains, whether inline or out-of-line."
    // Similar behavior is observed with MSVC. An alternative ABI could use
    // StrongODR/AvailableExternally to match the function, but none are
    // known/supported currently.
    if (StaticLocalLinkage == GVA_StrongODR ||
        StaticLocalLinkage == GVA_AvailableExternally)
      return GVA_DiscardableODR;
    return StaticLocalLinkage;
  }

  if (Context.getInlineVariableDefinitionKind(VD) ==
      TreeContext::InlineVariableDefinitionKind::Weak)
    return GVA_DiscardableODR;

  return GVA_StrongExternal;
}
} // namespace

GVALinkage TreeContext::GetGVALinkageForVariable(const VarDecl *VD) const {
  return adjustGVALinkageForExternalDefinitionKind(
      *this, VD,
      adjustGVALinkageForAttributes(*this, VD,
                                    basicGVALinkageForVariable(*this, VD)));
}

bool TreeContext::DeclMustBeEmitted(const Decl *D) {
  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    if (!VD->isFileVarDecl())
      return false;
    // Global named register variables (GNU extension) are never emitted.
    if (VD->getStorageClass() == SC_Register)
      return false;
  } else if (isa<FunctionDecl>(D)) {
    // Fall through to common emission logic below.
  } else if (isa<PragmaCommentDecl>(D))
    return true;
  else if (isa<PragmaDetectMismatchDecl>(D))
    return true;
  else
    return false;

  // Weak references don't produce any output by themselves.
  if (D->hasAttr<WeakRefAttr>())
    return false;

  // Aliases and used decls are required.
  if (D->hasAttr<AliasAttr>() || D->hasAttr<UsedAttr>())
    return true;

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    // Forward declarations aren't required.
    if (!FD->doesThisDeclarationHaveABody())
      return FD->doesDeclarationForceExternallyVisibleDefinition();

    // __attribute__((constructor)) / __attribute__((destructor)) are required.
    if (FD->hasAttr<ConstructorAttr>() || FD->hasAttr<DestructorAttr>())
      return true;

    GVALinkage Linkage = GetGVALinkageForFunction(FD);

    // `static` / `inline` / `always_inline` / `extern inline` can be deferred
    // when linkage is discardable.
    return !isDiscardableGVALinkage(Linkage);
  }

  const auto *VD = cast<VarDecl>(D);
  assert(VD->isFileVarDecl() && "Expected file scoped var");

  // If the decl is marked as `declare target to`, it should be emitted for the
  // host and for the device.
  if (VD->isThisDeclarationADefinition() == VarDecl::DeclarationOnly)
    return false;

  // Variables that can be needed in other TUs are required.
  auto Linkage = GetGVALinkageForVariable(VD);
  if (!isDiscardableGVALinkage(Linkage))
    return true;

  // We never need to emit a variable that is available in another TU.
  if (Linkage == GVA_AvailableExternally)
    return false;

  // Variables that have destruction with side-effects are required.
  if (VD->needsDestruction(*this))
    return true;

  // Variables that have initialization with side-effects are required.
  if (VD->getInit() && VD->getInit()->HasSideEffects(*this) &&
      !VD->evaluateValue())
    return true;

  return false;
}

void TreeContext::forEachMultiversionedFunctionVersion(
    const FunctionDecl *FD,
    llvm::function_ref<void(FunctionDecl *)> Pred) const {
  assert(FD->isMultiVersion() && "Only valid for multiversioned functions");
  llvm::SmallDenseSet<const FunctionDecl *, 4> SeenDecls;
  FD = FD->getMostRecentDecl();
  for (auto *CurDecl :
       FD->getDeclContext()->getRedeclContext()->lookup(FD->getDeclName())) {
    FunctionDecl *CurFD = CurDecl->getAsFunction()->getMostRecentDecl();
    if (CurFD && hasSameType(CurFD->getType(), FD->getType()) &&
        !SeenDecls.contains(CurFD)) {
      SeenDecls.insert(CurFD);
      Pred(CurFD);
    }
  }
}

CallingConv TreeContext::getDefaultCallingConvention(bool IsVariadic,
                                                     bool IsBuiltin) const {
  // Builtins ignore user-specified default calling convention and remain the
  // Target's default calling convention.
  if (!IsBuiltin) {
    switch (LangOpts.getDefaultCallingConv()) {
    case LangOptions::DCC_None:
      break;
    case LangOptions::DCC_CDecl:
      return CC_C;
    case LangOptions::DCC_FastCall:
      if (getTargetInfo().hasFeature("sse2") && !IsVariadic)
        return CC_X86FastCall;
      break;
    case LangOptions::DCC_StdCall:
      if (!IsVariadic)
        return CC_X86StdCall;
      break;
    case LangOptions::DCC_VectorCall:
      // __vectorcall cannot be applied to variadic functions.
      if (!IsVariadic)
        return CC_X86VectorCall;
      break;
    case LangOptions::DCC_RegCall:
      // __regcall cannot be applied to variadic functions.
      if (!IsVariadic)
        return CC_X86RegCall;
      break;
    }
  }
  return Target->getDefaultCallingConv();
}

MangleContext *TreeContext::createMangleContext() {
  return ItaniumMangleContext::create(*this, getDiagnostics());
}

QualType TreeContext::getIntTypeForBitwidth(unsigned DestWidth,
                                            unsigned Signed) const {
  TargetInfo::IntType Ty = getTargetInfo().getIntTypeByWidth(DestWidth, Signed);
  CanQualType QualTy = getFromTargetType(Ty);
  if (!QualTy && DestWidth == 128)
    return Signed ? Int128Ty : UnsignedInt128Ty;
  return QualTy;
}

QualType TreeContext::getRealTypeForBitwidth(unsigned DestWidth,
                                             FloatModeKind ExplicitType) const {
  FloatModeKind Ty =
      getTargetInfo().getRealTypeByWidth(DestWidth, ExplicitType);
  switch (Ty) {
  case FloatModeKind::Half:
    return HalfTy;
  case FloatModeKind::Float:
    return FloatTy;
  case FloatModeKind::Double:
    return DoubleTy;
  case FloatModeKind::LongDouble:
    return LongDoubleTy;
  case FloatModeKind::Float128:
    return Float128Ty;
  case FloatModeKind::NoFloat:
    return {};
  }

  llvm_unreachable("Unhandled TargetInfo::RealType value");
}

void TreeContext::setManglingNumber(const NamedDecl *ND, unsigned Number) {
  if (Number > 1)
    MangleNumbers[ND] = Number;
}

unsigned TreeContext::getManglingNumber(const NamedDecl *ND) const {
  auto I = MangleNumbers.find(ND);
  unsigned Res = I != MangleNumbers.end() ? I->second : 1;
  return Res > 1 ? Res : 1;
}

void TreeContext::setStaticLocalNumber(const VarDecl *VD, unsigned Number) {
  if (Number > 1)
    StaticLocalNumbers[VD] = Number;
}

void TreeContext::setParameterIndex(const ParmVarDecl *D, unsigned int index) {
  ParamIndices[D] = index;
}

unsigned TreeContext::getParameterIndex(const ParmVarDecl *D) const {
  ParameterIndexTable::const_iterator I = ParamIndices.find(D);
  assert(I != ParamIndices.end() &&
         "ParmIndices lacks entry set by ParmVarDecl");
  return I->second;
}

QualType TreeContext::getStringLiteralArrayType(QualType EltTy,
                                                unsigned Length) const {
  if (getLangOpts().ConstStrings)
    EltTy = EltTy.withConst();

  EltTy = adjustStringLiteralBaseType(EltTy);

  // Get an array type for the string, according to C99 6.4.5. This includes
  // the null terminator character.
  return getConstantArrayType(EltTy, llvm::APInt(32, Length + 1), nullptr,
                              ArraySizeModifier::Normal, /*IndexTypeQuals*/ 0);
}

StringLiteral *
TreeContext::getPredefinedStringLiteralFromCache(llvm::StringRef Key) const {
  StringLiteral *&Result = StringLiteralCache[Key];
  if (!Result)
    Result = StringLiteral::Create(
        *this, Key, StringLiteralKind::Ordinary,
        getStringLiteralArrayType(CharTy, Key.size()), SourceLocation());
  return Result;
}

bool TreeContext::AtomicUsesUnsupportedLibcall(const AtomicExpr *E) const {
  const llvm::Triple &T = getTargetInfo().getTriple();
  if (!T.isOSDarwin())
    return false;

  if (!(T.isiOS() && T.isOSVersionLT(7)) &&
      !(T.isMacOSX() && T.isOSVersionLT(10, 9)))
    return false;

  QualType AtomicTy = E->getPtr()->getType()->getPointeeType();
  CharUnits sizeChars = getTypeSizeInChars(AtomicTy);
  uint64_t Size = sizeChars.getQuantity();
  CharUnits alignChars = getTypeAlignInChars(AtomicTy);
  unsigned Align = alignChars.getQuantity();
  unsigned MaxInlineWidthInBits = getTargetInfo().getMaxAtomicInlineWidth();
  return (Size != Align || toBits(sizeChars) > MaxInlineWidthInBits);
}

uint64_t TreeContext::getTargetNullPointerValue(QualType QT) const {
  LangAS AS;
  if (QT->getUnqualifiedDesugaredType()->isNullPtrType())
    AS = LangAS::Default;
  else
    AS = QT->getPointeeType().getAddressSpace();

  return getTargetInfo().getNullPointerValue(AS);
}

unsigned TreeContext::getTargetAddressSpace(LangAS AS) const {
  return getTargetInfo().getTargetAddressSpace(AS);
}

bool TreeContext::hasSameExpr(const Expr *X, const Expr *Y) const {
  if (X == Y)
    return true;
  if (!X || !Y)
    return false;
  llvm::FoldingSetNodeID IDX, IDY;
  X->Profile(IDX, *this, /*Canonical=*/true);
  Y->Profile(IDY, *this, /*Canonical=*/true);
  return IDX == IDY;
}

// The getCommon* helpers return, for given 'same' X and Y entities given as
// inputs, another entity which is also the 'same' as the inputs, but which
// is closer to the canonical form of the inputs, each according to a given
// criteria.
// The getCommon*Checked variants are 'null inputs not-allowed' equivalents of
// the regular ones.

namespace {
Decl *getCommonDecl(Decl *X, Decl *Y) {
  if (!declaresSameEntity(X, Y))
    return nullptr;
  for (const Decl *DX : X->redecls()) {
    // If we reach Y before reaching the first decl, that means X is older.
    if (DX == Y)
      return X;
    // If we reach the first decl, then Y is older.
    if (DX->isFirstDecl())
      return Y;
  }
  llvm_unreachable("Corrupt redecls chain");
}
} // namespace

namespace {
template <class T, std::enable_if_t<std::is_base_of_v<Decl, T>, bool> = true>
T *getCommonDecl(T *X, T *Y) {
  return cast_or_null<T>(
      getCommonDecl(const_cast<Decl *>(cast_or_null<Decl>(X)),
                    const_cast<Decl *>(cast_or_null<Decl>(Y))));
}
} // namespace

namespace {
template <class T, std::enable_if_t<std::is_base_of_v<Decl, T>, bool> = true>
T *getCommonDeclChecked(T *X, T *Y) {
  return cast<T>(getCommonDecl(const_cast<Decl *>(cast<Decl>(X)),
                               const_cast<Decl *>(cast<Decl>(Y))));
}
} // namespace

namespace {
auto getCommonTypes(TreeContext &Ctx, llvm::ArrayRef<QualType> Xs,
                    llvm::ArrayRef<QualType> Ys, bool Unqualified = false) {
  assert(Xs.size() == Ys.size());
  llvm::SmallVector<QualType, 8> Rs(Xs.size());
  for (size_t I = 0; I < Rs.size(); ++I)
    Rs[I] = Ctx.getCommonSugaredType(Xs[I], Ys[I], Unqualified);
  return Rs;
}
} // namespace

namespace {
template <class T> SourceLocation getCommonAttrLoc(const T *X, const T *Y) {
  return X->getAttributeLoc() == Y->getAttributeLoc() ? X->getAttributeLoc()
                                                      : SourceLocation();
}
} // namespace

namespace {
template <class T>
ElaboratedTypeKeyword getCommonTypeKeyword(const T *X, const T *Y) {
  return X->getKeyword() == Y->getKeyword() ? X->getKeyword()
                                            : ElaboratedTypeKeyword::None;
}
} // namespace

namespace {
template <class T> void *getCommonNNS(TreeContext &, const T *, const T *) {
  return nullptr;
}
} // namespace

namespace {
template <class T>
QualType getCommonElementType(TreeContext &Ctx, const T *X, const T *Y) {
  return Ctx.getCommonSugaredType(X->getElementType(), Y->getElementType());
}
} // namespace

namespace {
template <class T>
QualType getCommonArrayElementType(TreeContext &Ctx, const T *X, Qualifiers &QX,
                                   const T *Y, Qualifiers &QY) {
  QualType EX = X->getElementType(), EY = Y->getElementType();
  QualType R = Ctx.getCommonSugaredType(EX, EY,
                                        /*Unqualified=*/true);
  Qualifiers RQ = R.getQualifiers();
  QX += EX.getQualifiers() - RQ;
  QY += EY.getQualifiers() - RQ;
  return R;
}
} // namespace

namespace {
template <class T>
QualType getCommonPointeeType(TreeContext &Ctx, const T *X, const T *Y) {
  return Ctx.getCommonSugaredType(X->getPointeeType(), Y->getPointeeType());
}
} // namespace

template <class T>
static auto *getCommonSizeExpr(TreeContext &Ctx, T *X, T *Y) {
  assert(Ctx.hasSameExpr(X->getSizeExpr(), Y->getSizeExpr()));
  return X->getSizeExpr();
}

namespace {
auto getCommonSizeModifier(const ArrayType *X, const ArrayType *Y) {
  assert(X->getSizeModifier() == Y->getSizeModifier());
  return X->getSizeModifier();
}
} // namespace

namespace {
auto getCommonIndexTypeCVRQualifiers(const ArrayType *X, const ArrayType *Y) {
  assert(X->getIndexTypeCVRQualifiers() == Y->getIndexTypeCVRQualifiers());
  return X->getIndexTypeCVRQualifiers();
}
} // namespace

FunctionProtoType::ExceptionSpecInfo
TreeContext::mergeExceptionSpecs(FunctionProtoType::ExceptionSpecInfo ESI1,
                                 FunctionProtoType::ExceptionSpecInfo ESI2) {
  // C only has EST_None and EST_NoThrow; any None wins, otherwise both NoThrow.
  if (ESI1.Type == EST_None)
    return ESI1;
  return ESI2;
}

namespace {
QualType getCommonNonSugarTypeNode(TreeContext &Ctx, const Type *X,
                                   Qualifiers &QX, const Type *Y,
                                   Qualifiers &QY) {
  Type::TypeClass TC = X->getTypeClass();
  assert(TC == Y->getTypeClass());
  switch (TC) {
#define UNEXPECTED_TYPE(Class, Kind)                                           \
  case Type::Class:                                                            \
    llvm_unreachable("Unexpected " Kind ": " #Class);

#define NON_CANONICAL_TYPE(Class, Base) UNEXPECTED_TYPE(Class, "non-canonical")
#define TYPE(Class, Base)
#include "neverc/Tree/TypeNodes.td.h"

#define SUGAR_FREE_TYPE(Class) UNEXPECTED_TYPE(Class, "sugar-free")
    SUGAR_FREE_TYPE(Builtin)
    SUGAR_FREE_TYPE(Enum)
    SUGAR_FREE_TYPE(BitInt)
    SUGAR_FREE_TYPE(Record)
#undef SUGAR_FREE_TYPE
#define NON_UNIQUE_TYPE(Class) UNEXPECTED_TYPE(Class, "non-unique")
    NON_UNIQUE_TYPE(TypeOfExpr)
    NON_UNIQUE_TYPE(VariableArray)
#undef NON_UNIQUE_TYPE

    UNEXPECTED_TYPE(TypeOf, "sugar")

#undef UNEXPECTED_TYPE

  case Type::Auto: {
    const auto *AX = cast<AutoType>(X);
    assert(AX->getDeducedType().isNull());
    assert(cast<AutoType>(Y)->getDeducedType().isNull());
    return Ctx.getAutoType(QualType(), AX->getKeyword(), false);
  }
  case Type::IncompleteArray: {
    const auto *AX = cast<IncompleteArrayType>(X),
               *AY = cast<IncompleteArrayType>(Y);
    return Ctx.getIncompleteArrayType(
        getCommonArrayElementType(Ctx, AX, QX, AY, QY),
        getCommonSizeModifier(AX, AY), getCommonIndexTypeCVRQualifiers(AX, AY));
  }
  case Type::ConstantArray: {
    const auto *AX = cast<ConstantArrayType>(X),
               *AY = cast<ConstantArrayType>(Y);
    assert(AX->getSize() == AY->getSize());
    const Expr *SizeExpr = Ctx.hasSameExpr(AX->getSizeExpr(), AY->getSizeExpr())
                               ? AX->getSizeExpr()
                               : nullptr;
    return Ctx.getConstantArrayType(
        getCommonArrayElementType(Ctx, AX, QX, AY, QY), AX->getSize(), SizeExpr,
        getCommonSizeModifier(AX, AY), getCommonIndexTypeCVRQualifiers(AX, AY));
  }
  case Type::Atomic: {
    const auto *AX = cast<AtomicType>(X), *AY = cast<AtomicType>(Y);
    return Ctx.getAtomicType(
        Ctx.getCommonSugaredType(AX->getValueType(), AY->getValueType()));
  }
  case Type::Complex: {
    const auto *CX = cast<ComplexType>(X), *CY = cast<ComplexType>(Y);
    return Ctx.getComplexType(getCommonArrayElementType(Ctx, CX, QX, CY, QY));
  }
  case Type::Pointer: {
    const auto *PX = cast<PointerType>(X), *PY = cast<PointerType>(Y);
    return Ctx.getPointerType(getCommonPointeeType(Ctx, PX, PY));
  }
  case Type::FunctionNoProto: {
    const auto *FX = cast<FunctionNoProtoType>(X),
               *FY = cast<FunctionNoProtoType>(Y);
    assert(FX->getExtInfo() == FY->getExtInfo());
    return Ctx.getFunctionNoProtoType(
        Ctx.getCommonSugaredType(FX->getReturnType(), FY->getReturnType()),
        FX->getExtInfo());
  }
  case Type::FunctionProto: {
    const auto *FX = cast<FunctionProtoType>(X),
               *FY = cast<FunctionProtoType>(Y);
    FunctionProtoType::ExtProtoInfo EPIX = FX->getExtProtoInfo(),
                                    EPIY = FY->getExtProtoInfo();
    assert(EPIX.ExtInfo == EPIY.ExtInfo);
    assert(EPIX.ExtParameterInfos == EPIY.ExtParameterInfos);
    assert(EPIX.TypeQuals == EPIY.TypeQuals);
    assert(EPIX.Variadic == EPIY.Variadic);

    QualType R =
        Ctx.getCommonSugaredType(FX->getReturnType(), FY->getReturnType());
    auto P = getCommonTypes(Ctx, FX->param_types(), FY->param_types(),
                            /*Unqualified=*/true);

    EPIX.ExceptionSpec =
        Ctx.mergeExceptionSpecs(EPIX.ExceptionSpec, EPIY.ExceptionSpec);
    return Ctx.getFunctionType(R, P, EPIX);
  }
  case Type::ConstantMatrix: {
    const auto *MX = cast<ConstantMatrixType>(X),
               *MY = cast<ConstantMatrixType>(Y);
    assert(MX->getNumRows() == MY->getNumRows());
    assert(MX->getNumColumns() == MY->getNumColumns());
    return Ctx.getConstantMatrixType(getCommonElementType(Ctx, MX, MY),
                                     MX->getNumRows(), MX->getNumColumns());
  }
  case Type::Vector: {
    const auto *VX = cast<VectorType>(X), *VY = cast<VectorType>(Y);
    assert(VX->getNumElements() == VY->getNumElements());
    assert(VX->getVectorKind() == VY->getVectorKind());
    return Ctx.getVectorType(getCommonElementType(Ctx, VX, VY),
                             VX->getNumElements(), VX->getVectorKind());
  }
  case Type::ExtVector: {
    const auto *VX = cast<ExtVectorType>(X), *VY = cast<ExtVectorType>(Y);
    assert(VX->getNumElements() == VY->getNumElements());
    return Ctx.getExtVectorType(getCommonElementType(Ctx, VX, VY),
                                VX->getNumElements());
  }
  }
  llvm_unreachable("Unknown Type Class");
}
} // namespace

namespace {
QualType getCommonSugarTypeNode(TreeContext &Ctx, const Type *X, const Type *Y,
                                SplitQualType Underlying) {
  Type::TypeClass TC = X->getTypeClass();
  if (TC != Y->getTypeClass())
    return QualType();
  switch (TC) {
#define UNEXPECTED_TYPE(Class, Kind)                                           \
  case Type::Class:                                                            \
    llvm_unreachable("Unexpected " Kind ": " #Class);
#define TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) UNEXPECTED_TYPE(Class, "dependent")
#include "neverc/Tree/TypeNodes.td.h"

#define CANONICAL_TYPE(Class) UNEXPECTED_TYPE(Class, "canonical")
    CANONICAL_TYPE(Atomic)
    CANONICAL_TYPE(BitInt)
    CANONICAL_TYPE(Builtin)
    CANONICAL_TYPE(Complex)
    CANONICAL_TYPE(ConstantArray)
    CANONICAL_TYPE(ConstantMatrix)
    CANONICAL_TYPE(Enum)
    CANONICAL_TYPE(ExtVector)
    CANONICAL_TYPE(FunctionNoProto)
    CANONICAL_TYPE(FunctionProto)
    CANONICAL_TYPE(IncompleteArray)
    CANONICAL_TYPE(Pointer)
    CANONICAL_TYPE(Record)
    CANONICAL_TYPE(VariableArray)
    CANONICAL_TYPE(Vector)
#undef CANONICAL_TYPE

#undef UNEXPECTED_TYPE

  case Type::Adjusted: {
    const auto *AX = cast<AdjustedType>(X), *AY = cast<AdjustedType>(Y);
    QualType OX = AX->getOriginalType(), OY = AY->getOriginalType();
    if (!Ctx.hasSameType(OX, OY))
      return QualType();
    return Ctx.getAdjustedType(Ctx.getCommonSugaredType(OX, OY),
                               Ctx.getQualifiedType(Underlying));
  }
  case Type::Decayed: {
    const auto *DX = cast<DecayedType>(X), *DY = cast<DecayedType>(Y);
    QualType OX = DX->getOriginalType(), OY = DY->getOriginalType();
    if (!Ctx.hasSameType(OX, OY))
      return QualType();
    return Ctx.getDecayedType(Ctx.getCommonSugaredType(OX, OY),
                              Ctx.getQualifiedType(Underlying));
  }
  case Type::Attributed: {
    const auto *AX = cast<AttributedType>(X), *AY = cast<AttributedType>(Y);
    AttributedType::Kind Kind = AX->getAttrKind();
    if (Kind != AY->getAttrKind())
      return QualType();
    QualType MX = AX->getModifiedType(), MY = AY->getModifiedType();
    if (!Ctx.hasSameType(MX, MY))
      return QualType();
    return Ctx.getAttributedType(Kind, Ctx.getCommonSugaredType(MX, MY),
                                 Ctx.getQualifiedType(Underlying));
  }
  case Type::BTFTagAttributed: {
    const auto *BX = cast<BTFTagAttributedType>(X);
    const BTFTypeTagAttr *AX = BX->getAttr();
    // The attribute is not uniqued, so just compare the tag.
    if (AX->getBTFTypeTag() !=
        cast<BTFTagAttributedType>(Y)->getAttr()->getBTFTypeTag())
      return QualType();
    return Ctx.getBTFTagAttributedType(AX, Ctx.getQualifiedType(Underlying));
  }
  case Type::Auto: {
    const auto *AX = cast<AutoType>(X), *AY = cast<AutoType>(Y);

    AutoTypeKeyword KW = AX->getKeyword();
    if (KW != AY->getKeyword())
      return QualType();

    return Ctx.getAutoType(Ctx.getQualifiedType(Underlying), AX->getKeyword(),
                           /*IsDependent=*/false);
  }
  case Type::Elaborated: {
    const auto *EX = cast<ElaboratedType>(X), *EY = cast<ElaboratedType>(Y);
    return Ctx.getElaboratedType(
        ::getCommonTypeKeyword(EX, EY), Ctx.getQualifiedType(Underlying),
        ::getCommonDecl(EX->getOwnedTagDecl(), EY->getOwnedTagDecl()));
  }
  case Type::MacroQualified: {
    const auto *MX = cast<MacroQualifiedType>(X),
               *MY = cast<MacroQualifiedType>(Y);
    const IdentifierInfo *IX = MX->getMacroIdentifier();
    if (IX != MY->getMacroIdentifier())
      return QualType();
    return Ctx.getMacroQualifiedType(Ctx.getQualifiedType(Underlying), IX);
  }
  case Type::Paren:
    return Ctx.getParenType(Ctx.getQualifiedType(Underlying));

  case Type::Typedef: {
    const auto *TX = cast<TypedefType>(X), *TY = cast<TypedefType>(Y);
    const TypedefNameDecl *CD = ::getCommonDecl(TX->getDecl(), TY->getDecl());
    if (!CD)
      return QualType();
    return Ctx.getTypedefType(CD, Ctx.getQualifiedType(Underlying));
  }
  case Type::TypeOf: {
    // The common sugar between two typeof expressions, where one is
    // potentially a typeof_unqual and the other is not, we unify to the
    // qualified type as that retains the most information along with the type.
    // We only return a typeof_unqual type when both types are unqual types.
    TypeOfKind Kind = TypeOfKind::Qualified;
    if (cast<TypeOfType>(X)->getKind() == cast<TypeOfType>(Y)->getKind() &&
        cast<TypeOfType>(X)->getKind() == TypeOfKind::Unqualified)
      Kind = TypeOfKind::Unqualified;
    return Ctx.getTypeOfType(Ctx.getQualifiedType(Underlying), Kind);
  }
  case Type::TypeOfExpr:
    return QualType();
  }
  llvm_unreachable("Unhandled Type Class");
}
} // namespace

namespace {
auto unwrapSugar(SplitQualType &T, Qualifiers &QTotal) {
  llvm::SmallVector<SplitQualType, 8> R;
  while (true) {
    QTotal.addConsistentQualifiers(T.Quals);
    QualType NT = T.Ty->getLocallyUnqualifiedSingleStepDesugaredType();
    if (NT == QualType(T.Ty, 0))
      break;
    R.push_back(T);
    T = NT.split();
  }
  return R;
}
} // namespace

QualType TreeContext::getCommonSugaredType(QualType X, QualType Y,
                                           bool Unqualified) {
  assert(Unqualified ? hasSameUnqualifiedType(X, Y) : hasSameType(X, Y));
  if (X == Y)
    return X;
  if (!Unqualified) {
    if (X.isCanonical())
      return X;
    if (Y.isCanonical())
      return Y;
  }

  SplitQualType SX = X.split(), SY = Y.split();
  Qualifiers QX, QY;
  // Desugar SX and SY, setting the sugar and qualifiers aside into Xs and Ys,
  // until we reach their underlying "canonical nodes". Note these are not
  // necessarily canonical types, as they may still have sugared properties.
  // QX and QY will store the sum of all qualifiers in Xs and Ys respectively.
  auto Xs = ::unwrapSugar(SX, QX), Ys = ::unwrapSugar(SY, QY);
  if (SX.Ty != SY.Ty) {
    // The canonical nodes differ. Build a common canonical node out of the two,
    // unifying their sugar. This may recurse back here.
    SX.Ty =
        ::getCommonNonSugarTypeNode(*this, SX.Ty, QX, SY.Ty, QY).getTypePtr();
  } else {
    // The canonical nodes were identical: We may have desugared too much.
    // Add any common sugar back in.
    while (!Xs.empty() && !Ys.empty() && Xs.back().Ty == Ys.back().Ty) {
      QX -= SX.Quals;
      QY -= SY.Quals;
      SX = Xs.pop_back_val();
      SY = Ys.pop_back_val();
    }
  }
  if (Unqualified)
    QX = Qualifiers::removeCommonQualifiers(QX, QY);
  else
    assert(QX == QY);

  // Even though the remaining sugar nodes in Xs and Ys differ, some may be
  // related. Walk up these nodes, unifying them and adding the result.
  while (!Xs.empty() && !Ys.empty()) {
    auto Underlying = SplitQualType(
        SX.Ty, Qualifiers::removeCommonQualifiers(SX.Quals, SY.Quals));
    SX = Xs.pop_back_val();
    SY = Ys.pop_back_val();
    SX.Ty = ::getCommonSugarTypeNode(*this, SX.Ty, SY.Ty, Underlying)
                .getTypePtrOrNull();
    // Stop at the first pair which is unrelated.
    if (!SX.Ty) {
      SX.Ty = Underlying.Ty;
      break;
    }
    QX -= Underlying.Quals;
  };

  // Add back the missing accumulated qualifiers, which were stripped off
  // with the sugar nodes we could not unify.
  QualType R = getQualifiedType(SX.Ty, QX);
  assert(Unqualified ? hasSameUnqualifiedType(R, X) : hasSameType(R, X));
  return R;
}

QualType TreeContext::getCorrespondingSaturatedType(QualType Ty) const {
  assert(Ty->isFixedPointType());

  if (Ty->isSaturatedFixedPointType())
    return Ty;

  switch (Ty->castAs<BuiltinType>()->getKind()) {
  default:
    llvm_unreachable("Not a fixed point type!");
  case BuiltinType::ShortAccum:
    return SatShortAccumTy;
  case BuiltinType::Accum:
    return SatAccumTy;
  case BuiltinType::LongAccum:
    return SatLongAccumTy;
  case BuiltinType::UShortAccum:
    return SatUnsignedShortAccumTy;
  case BuiltinType::UAccum:
    return SatUnsignedAccumTy;
  case BuiltinType::ULongAccum:
    return SatUnsignedLongAccumTy;
  case BuiltinType::ShortFract:
    return SatShortFractTy;
  case BuiltinType::Fract:
    return SatFractTy;
  case BuiltinType::LongFract:
    return SatLongFractTy;
  case BuiltinType::UShortFract:
    return SatUnsignedShortFractTy;
  case BuiltinType::UFract:
    return SatUnsignedFractTy;
  case BuiltinType::ULongFract:
    return SatUnsignedLongFractTy;
  }
}

LangAS TreeContext::getLangASForBuiltinAddressSpace(unsigned AS) const {
  return getLangASFromTargetAS(AS);
}

unsigned char TreeContext::getFixedPointScale(QualType Ty) const {
  assert(Ty->isFixedPointType());

  const TargetInfo &Target = getTargetInfo();
  switch (Ty->castAs<BuiltinType>()->getKind()) {
  default:
    llvm_unreachable("Not a fixed point type!");
  case BuiltinType::ShortAccum:
  case BuiltinType::SatShortAccum:
    return Target.getShortAccumScale();
  case BuiltinType::Accum:
  case BuiltinType::SatAccum:
    return Target.getAccumScale();
  case BuiltinType::LongAccum:
  case BuiltinType::SatLongAccum:
    return Target.getLongAccumScale();
  case BuiltinType::UShortAccum:
  case BuiltinType::SatUShortAccum:
    return Target.getUnsignedShortAccumScale();
  case BuiltinType::UAccum:
  case BuiltinType::SatUAccum:
    return Target.getUnsignedAccumScale();
  case BuiltinType::ULongAccum:
  case BuiltinType::SatULongAccum:
    return Target.getUnsignedLongAccumScale();
  case BuiltinType::ShortFract:
  case BuiltinType::SatShortFract:
    return Target.getShortFractScale();
  case BuiltinType::Fract:
  case BuiltinType::SatFract:
    return Target.getFractScale();
  case BuiltinType::LongFract:
  case BuiltinType::SatLongFract:
    return Target.getLongFractScale();
  case BuiltinType::UShortFract:
  case BuiltinType::SatUShortFract:
    return Target.getUnsignedShortFractScale();
  case BuiltinType::UFract:
  case BuiltinType::SatUFract:
    return Target.getUnsignedFractScale();
  case BuiltinType::ULongFract:
  case BuiltinType::SatULongFract:
    return Target.getUnsignedLongFractScale();
  }
}

llvm::FixedPointSemantics
TreeContext::getFixedPointSemantics(QualType Ty) const {
  assert((Ty->isFixedPointType() || Ty->isIntegerType()) &&
         "Can only get the fixed point semantics for a "
         "fixed point or integer type.");
  if (Ty->isIntegerType())
    return llvm::FixedPointSemantics::GetIntegerSemantics(
        getIntWidth(Ty), Ty->isSignedIntegerType());

  bool isSigned = Ty->isSignedFixedPointType();
  return llvm::FixedPointSemantics(
      static_cast<unsigned>(getTypeSize(Ty)), getFixedPointScale(Ty), isSigned,
      Ty->isSaturatedFixedPointType(),
      !isSigned && getTargetInfo().doUnsignedFixedPointTypesHavePadding());
}

llvm::APFixedPoint TreeContext::getFixedPointMax(QualType Ty) const {
  assert(Ty->isFixedPointType());
  return llvm::APFixedPoint::getMax(getFixedPointSemantics(Ty));
}

llvm::APFixedPoint TreeContext::getFixedPointMin(QualType Ty) const {
  assert(Ty->isFixedPointType());
  return llvm::APFixedPoint::getMin(getFixedPointSemantics(Ty));
}

QualType TreeContext::getCorrespondingSignedFixedPointType(QualType Ty) const {
  assert(Ty->isUnsignedFixedPointType() &&
         "Expected unsigned fixed point type");

  switch (Ty->castAs<BuiltinType>()->getKind()) {
  case BuiltinType::UShortAccum:
    return ShortAccumTy;
  case BuiltinType::UAccum:
    return AccumTy;
  case BuiltinType::ULongAccum:
    return LongAccumTy;
  case BuiltinType::SatUShortAccum:
    return SatShortAccumTy;
  case BuiltinType::SatUAccum:
    return SatAccumTy;
  case BuiltinType::SatULongAccum:
    return SatLongAccumTy;
  case BuiltinType::UShortFract:
    return ShortFractTy;
  case BuiltinType::UFract:
    return FractTy;
  case BuiltinType::ULongFract:
    return LongFractTy;
  case BuiltinType::SatUShortFract:
    return SatShortFractTy;
  case BuiltinType::SatUFract:
    return SatFractTy;
  case BuiltinType::SatULongFract:
    return SatLongFractTy;
  default:
    llvm_unreachable("Unexpected unsigned fixed point type");
  }
}

std::vector<std::string> TreeContext::filterFunctionTargetVersionAttrs(
    const TargetVersionAttr *TV) const {
  assert(TV != nullptr);
  llvm::SmallVector<llvm::StringRef, 8> Feats;
  std::vector<std::string> ResFeats;
  TV->getFeatures(Feats);
  for (auto &Feature : Feats)
    if (Target->validateCpuSupports(Feature.str()))
      // Use '?' to mark features that came from TargetVersion.
      ResFeats.push_back("?" + Feature.str());
  return ResFeats;
}

ParsedTargetAttr
TreeContext::filterFunctionTargetAttrs(const TargetAttr *TD) const {
  assert(TD != nullptr);
  ParsedTargetAttr ParsedAttr = Target->parseTargetAttr(TD->getFeaturesStr());

  llvm::erase_if(ParsedAttr.Features, [&](const std::string &Feat) {
    return !Target->isValidFeatureName(llvm::StringRef{Feat}.substr(1));
  });
  return ParsedAttr;
}

void TreeContext::getFunctionFeatureMap(llvm::StringMap<bool> &FeatureMap,
                                        const FunctionDecl *FD) const {
  if (FD)
    getFunctionFeatureMap(FeatureMap, GlobalDecl().getWithDecl(FD));
  else
    Target->initFeatureMap(FeatureMap, getDiagnostics(),
                           Target->getTargetOpts().CPU,
                           Target->getTargetOpts().Features);
}

// Fills in the supplied string map with the set of target features for the
// passed in function.
void TreeContext::getFunctionFeatureMap(llvm::StringMap<bool> &FeatureMap,
                                        GlobalDecl GD) const {
  llvm::StringRef TargetCPU = Target->getTargetOpts().CPU;
  const FunctionDecl *FD = GD.getDecl()->getAsFunction();
  if (const auto *TD = FD->getAttr<TargetAttr>()) {
    ParsedTargetAttr ParsedAttr = filterFunctionTargetAttrs(TD);

    // Make a copy of the features as passed on the command line into the
    // beginning of the additional features from the function to override.
    ParsedAttr.Features.insert(
        ParsedAttr.Features.begin(),
        Target->getTargetOpts().FeaturesAsWritten.begin(),
        Target->getTargetOpts().FeaturesAsWritten.end());

    if (ParsedAttr.CPU != "" && Target->isValidCPUName(ParsedAttr.CPU))
      TargetCPU = ParsedAttr.CPU;

    // Now populate the feature map, first with the TargetCPU which is either
    // the default or a new one from the target attribute string. Then we'll use
    // the passed in features (FeaturesAsWritten) along with the new ones from
    // the attribute.
    Target->initFeatureMap(FeatureMap, getDiagnostics(), TargetCPU,
                           ParsedAttr.Features);
  } else if (const auto *SD = FD->getAttr<CPUSpecificAttr>()) {
    llvm::SmallVector<llvm::StringRef, 32> FeaturesTmp;
    Target->getCPUSpecificCPUDispatchFeatures(
        SD->getCPUName(GD.getMultiVersionIndex())->getName(), FeaturesTmp);
    std::vector<std::string> Features(FeaturesTmp.begin(), FeaturesTmp.end());
    Features.insert(Features.begin(),
                    Target->getTargetOpts().FeaturesAsWritten.begin(),
                    Target->getTargetOpts().FeaturesAsWritten.end());
    Target->initFeatureMap(FeatureMap, getDiagnostics(), TargetCPU, Features);
  } else if (const auto *TC = FD->getAttr<TargetClonesAttr>()) {
    std::vector<std::string> Features;
    llvm::StringRef VersionStr = TC->getFeatureStr(GD.getMultiVersionIndex());
    if (Target->getTriple().isAArch64()) {
      // TargetClones for AArch64
      if (VersionStr != "default") {
        llvm::SmallVector<llvm::StringRef, 1> VersionFeatures;
        VersionStr.split(VersionFeatures, "+");
        for (auto &VFeature : VersionFeatures) {
          VFeature = VFeature.trim();
          // Use '?' to mark features that came from AArch64 TargetClones.
          Features.push_back((llvm::StringRef{"?"} + VFeature).str());
        }
      }
      Features.insert(Features.begin(),
                      Target->getTargetOpts().FeaturesAsWritten.begin(),
                      Target->getTargetOpts().FeaturesAsWritten.end());
    } else {
      if (VersionStr.starts_with("arch="))
        TargetCPU = VersionStr.drop_front(sizeof("arch=") - 1);
      else if (VersionStr != "default")
        Features.push_back((llvm::StringRef{"+"} + VersionStr).str());
    }
    Target->initFeatureMap(FeatureMap, getDiagnostics(), TargetCPU, Features);
  } else if (const auto *TV = FD->getAttr<TargetVersionAttr>()) {
    std::vector<std::string> Feats = filterFunctionTargetVersionAttrs(TV);
    Feats.insert(Feats.begin(),
                 Target->getTargetOpts().FeaturesAsWritten.begin(),
                 Target->getTargetOpts().FeaturesAsWritten.end());
    Target->initFeatureMap(FeatureMap, getDiagnostics(), TargetCPU, Feats);
  } else {
    FeatureMap = Target->getTargetOpts().FeatureMap;
  }
}

const StreamingDiagnostic &
neverc::operator<<(const StreamingDiagnostic &DB,
                   const TreeContext::SectionInfo &Section) {
  if (Section.Decl)
    return DB << Section.Decl;
  return DB << "a prior #pragma section";
}
