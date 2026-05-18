#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/AttrIterator.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/Mangle.h"
#include "neverc/Tree/Core/TreeMutationListener.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Decl/DeclContextInternals.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "neverc/Tree/Type/DependenceFlags.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "neverc/Tree/Type/TypeLoc.h"
#include "neverc/Tree/Type/TypeOrdering.h"
#include "llvm/ADT/APFixedPoint.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <utility>

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
// Construction & initialization
// ===----------------------------------------------------------------------===

TreeContext::TreeContext(LangOptions &LOpts, SourceManager &SM,
                         IdentifierTable &idents, Builtin::Context &builtins)
    : ConstantArrayTypes(this_(), ConstantArrayTypesLog2InitSize),
      FunctionProtoTypes(this_(), FunctionProtoTypesLog2InitSize),
      DependentTypeOfExprTypes(this_()), AutoTypes(this_()), SourceMgr(SM),
      LangOpts(LOpts), PrintingPolicy{LOpts}, Idents(idents),
      BuiltinInfo(builtins), LastSDM(nullptr, 0) {
  addTranslationUnitDecl();
}

void TreeContext::cleanup() {
  // Release the DenseMaps associated with DeclContext objects.
  ReleaseDeclContextMaps();

  // Call all of the deallocation functions on all of their targets.
  for (auto &Pair : Deallocations)
    (Pair.first)(Pair.second);
  Deallocations.clear();

  // StructRecordLayout objects in StructRecordLayouts must always be destroyed
  // because they can contain DenseMaps.
  for (auto &[Key, Layout] : StructRecordLayouts) {
    if (auto *R = const_cast<StructRecordLayout *>(Layout))
      R->Destroy(*this);
  }
  StructRecordLayouts.clear();

  for (auto &[Key, Vec] : DeclAttrs)
    Vec->~AttrVec();
  DeclAttrs.clear();
}

TreeContext::~TreeContext() { cleanup(); }

void TreeContext::setTraversalScope(const std::vector<Decl *> &TopLevelDecls) {
  TraversalScope = TopLevelDecls;
}

void TreeContext::AddDeallocation(void (*Callback)(void *), void *Data) const {
  Deallocations.push_back({Callback, Data});
}

void TreeContext::PrintStats() const {
  llvm::errs() << "\n*** AST Context Stats:\n";
  const auto TypeCount = Types.size();
  llvm::errs() << "  " << TypeCount << " types total.\n";

  unsigned counts[] = {
#define TYPE(Name, Parent) 0,
#define ABSTRACT_TYPE(Name, Parent)
#include "neverc/Tree/TypeNodes.td.h"
      0 // Extra
  };

  const Type *const *TypeData = Types.data();
  for (size_t i = 0; i < TypeCount; ++i) {
    if (LLVM_LIKELY(i + 4 < TypeCount))
      __builtin_prefetch(TypeData[i + 4], 0, 1);
    counts[static_cast<unsigned>(TypeData[i]->getTypeClass())]++;
  }

  unsigned Idx = 0;
  uint64_t TotalBytes = 0;
#define TYPE(Name, Parent)                                                     \
  if (counts[Idx])                                                             \
    llvm::errs() << "    " << counts[Idx] << " " << #Name << " types, "        \
                 << sizeof(Name##Type) << " each "                             \
                 << "(" << counts[Idx] * sizeof(Name##Type) << " bytes)\n";    \
  TotalBytes += static_cast<uint64_t>(counts[Idx]) * sizeof(Name##Type);       \
  ++Idx;
#define ABSTRACT_TYPE(Name, Parent)
#include "neverc/Tree/TypeNodes.td.h"

  llvm::errs() << "Total bytes = " << TotalBytes << "\n";

  BumpAlloc.PrintStats();
}

ExternCContextDecl *TreeContext::getExternCContextDecl() const {
  if (!ExternCContext)
    ExternCContext =
        ExternCContextDecl::Create(*this, getTranslationUnitDecl());

  return ExternCContext;
}

RecordDecl *TreeContext::buildImplicitRecord(llvm::StringRef Name,
                                             RecordDecl::TagKind TK) const {
  SourceLocation Loc;
  RecordDecl *NewDecl = RecordDecl::Create(*this, TK, getTranslationUnitDecl(),
                                           Loc, Loc, &Idents.get(Name));
  NewDecl->setImplicit();
  NewDecl->addAttr(TypeVisibilityAttr::CreateImplicit(
      const_cast<TreeContext &>(*this), TypeVisibilityAttr::Default));
  return NewDecl;
}

TypedefDecl *TreeContext::buildImplicitTypedef(QualType T,
                                               llvm::StringRef Name) const {
  TypeSourceInfo *TInfo = getTrivialTypeSourceInfo(T);
  TypedefDecl *NewDecl = TypedefDecl::Create(
      const_cast<TreeContext &>(*this), getTranslationUnitDecl(),
      SourceLocation(), SourceLocation(), &Idents.get(Name), TInfo);
  NewDecl->setImplicit();
  return NewDecl;
}

TypedefDecl *TreeContext::getInt128Decl() const {
  if (!Int128Decl)
    Int128Decl = buildImplicitTypedef(Int128Ty, "__int128_t");
  return Int128Decl;
}

TypedefDecl *TreeContext::getUInt128Decl() const {
  if (!UInt128Decl)
    UInt128Decl = buildImplicitTypedef(UnsignedInt128Ty, "__uint128_t");
  return UInt128Decl;
}

// ===----------------------------------------------------------------------===
// Builtin type initialization
// ===----------------------------------------------------------------------===

void TreeContext::InitBuiltinType(CanQualType &R, BuiltinType::Kind K) {
  auto *Ty = new (*this, alignof(BuiltinType)) BuiltinType(K);
  R = CanQualType::CreateUnsafe(QualType(Ty, 0));
  Types.push_back(Ty);
}

void TreeContext::InitBuiltinTypes(const TargetInfo &Target) {
  assert((!this->Target || this->Target == &Target) &&
         "Incorrect target reinitialization");
  assert(VoidTy.isNull() && "Context reinitialized?");

  this->Target = &Target;

  AddrSpaceMapMangling = Target.useAddressSpaceMapMangling();

  InitBuiltinType(VoidTy, BuiltinType::Void);

  InitBuiltinType(BoolTy, BuiltinType::Bool);
  if (LangOpts.CharIsSigned)
    InitBuiltinType(CharTy, BuiltinType::Char_S);
  else
    InitBuiltinType(CharTy, BuiltinType::Char_U);

  // Signed integer types.
  InitBuiltinType(SignedCharTy, BuiltinType::SChar);
  InitBuiltinType(ShortTy, BuiltinType::Short);
  InitBuiltinType(IntTy, BuiltinType::Int);
  InitBuiltinType(LongTy, BuiltinType::Long);
  InitBuiltinType(LongLongTy, BuiltinType::LongLong);

  // Unsigned integer types.
  InitBuiltinType(UnsignedCharTy, BuiltinType::UChar);
  InitBuiltinType(UnsignedShortTy, BuiltinType::UShort);
  InitBuiltinType(UnsignedIntTy, BuiltinType::UInt);
  InitBuiltinType(UnsignedLongTy, BuiltinType::ULong);
  InitBuiltinType(UnsignedLongLongTy, BuiltinType::ULongLong);

  // Floating-point types.
  InitBuiltinType(FloatTy, BuiltinType::Float);
  InitBuiltinType(DoubleTy, BuiltinType::Double);
  InitBuiltinType(LongDoubleTy, BuiltinType::LongDouble);

  InitBuiltinType(Float128Ty, BuiltinType::Float128);

  InitBuiltinType(Float16Ty, BuiltinType::Float16);

  InitBuiltinType(ShortAccumTy, BuiltinType::ShortAccum);
  InitBuiltinType(AccumTy, BuiltinType::Accum);
  InitBuiltinType(LongAccumTy, BuiltinType::LongAccum);
  InitBuiltinType(UnsignedShortAccumTy, BuiltinType::UShortAccum);
  InitBuiltinType(UnsignedAccumTy, BuiltinType::UAccum);
  InitBuiltinType(UnsignedLongAccumTy, BuiltinType::ULongAccum);
  InitBuiltinType(ShortFractTy, BuiltinType::ShortFract);
  InitBuiltinType(FractTy, BuiltinType::Fract);
  InitBuiltinType(LongFractTy, BuiltinType::LongFract);
  InitBuiltinType(UnsignedShortFractTy, BuiltinType::UShortFract);
  InitBuiltinType(UnsignedFractTy, BuiltinType::UFract);
  InitBuiltinType(UnsignedLongFractTy, BuiltinType::ULongFract);
  InitBuiltinType(SatShortAccumTy, BuiltinType::SatShortAccum);
  InitBuiltinType(SatAccumTy, BuiltinType::SatAccum);
  InitBuiltinType(SatLongAccumTy, BuiltinType::SatLongAccum);
  InitBuiltinType(SatUnsignedShortAccumTy, BuiltinType::SatUShortAccum);
  InitBuiltinType(SatUnsignedAccumTy, BuiltinType::SatUAccum);
  InitBuiltinType(SatUnsignedLongAccumTy, BuiltinType::SatULongAccum);
  InitBuiltinType(SatShortFractTy, BuiltinType::SatShortFract);
  InitBuiltinType(SatFractTy, BuiltinType::SatFract);
  InitBuiltinType(SatLongFractTy, BuiltinType::SatLongFract);
  InitBuiltinType(SatUnsignedShortFractTy, BuiltinType::SatUShortFract);
  InitBuiltinType(SatUnsignedFractTy, BuiltinType::SatUFract);
  InitBuiltinType(SatUnsignedLongFractTy, BuiltinType::SatULongFract);

  InitBuiltinType(Int128Ty, BuiltinType::Int128);
  InitBuiltinType(UnsignedInt128Ty, BuiltinType::UInt128);

  if (TargetInfo::isTypeSigned(Target.getWCharType()))
    InitBuiltinType(WCharTy, BuiltinType::WChar_S);
  else
    InitBuiltinType(WCharTy, BuiltinType::WChar_U);
  WideCharTy = getFromTargetType(Target.getWCharType());

  WIntTy = getFromTargetType(Target.getWIntType());

  InitBuiltinType(Char8Ty, BuiltinType::Char8);

  Char16Ty = getFromTargetType(Target.getChar16Type());

  Char32Ty = getFromTargetType(Target.getChar32Type());

  InitBuiltinType(DependentTy, BuiltinType::Dependent);
  InitBuiltinType(OverloadTy, BuiltinType::Overload);
  InitBuiltinType(PseudoObjectTy, BuiltinType::PseudoObject);
  InitBuiltinType(BuiltinFnTy, BuiltinType::BuiltinFn);

  if (Target.hasAArch64SVETypes()) {
#define SVE_TYPE(Name, Id, SingletonId)                                        \
  InitBuiltinType(SingletonId, BuiltinType::Id);
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
  }

  VoidPtrTy = getPointerType(VoidTy);

  InitBuiltinType(NullPtrTy, BuiltinType::NullPtr);
  InitBuiltinType(HalfTy, BuiltinType::Half);
  InitBuiltinType(BFloat16Ty, BuiltinType::BFloat16);

  VaListTagDecl = nullptr;
}

DiagnosticsEngine &TreeContext::getDiagnostics() const {
  return SourceMgr.getDiagnostics();
}

AttrVec &TreeContext::getDeclAttrs(const Decl *D) {
  AttrVec *&Result = DeclAttrs[D];
  if (!Result) {
    void *Mem = Allocate(sizeof(AttrVec));
    Result = new (Mem) AttrVec;
  }

  return *Result;
}

void TreeContext::eraseDeclAttrs(const Decl *D) {
  llvm::DenseMap<const Decl *, AttrVec *>::iterator Pos = DeclAttrs.find(D);
  if (Pos != DeclAttrs.end()) {
    Pos->second->~AttrVec();
    DeclAttrs.erase(Pos);
  }
}

const llvm::fltSemantics &TreeContext::getFloatTypeSemantics(QualType T) const {
  switch (T->castAs<BuiltinType>()->getKind()) {
  default:
    llvm_unreachable("Not a floating point type!");
  case BuiltinType::BFloat16:
    return Target->getBFloat16Format();
  case BuiltinType::Float16:
    return Target->getHalfFormat();
  case BuiltinType::Half:
    return Target->getHalfFormat();
  case BuiltinType::Float:
    return Target->getFloatFormat();
  case BuiltinType::Double:
    return Target->getDoubleFormat();
  case BuiltinType::LongDouble:
    return Target->getLongDoubleFormat();
  case BuiltinType::Float128:
    return Target->getFloat128Format();
  }
}

CharUnits TreeContext::getDeclAlign(const Decl *D, bool ForAlignof) const {
  unsigned Align = Target->getCharWidth();

  const unsigned AlignFromAttr = D->getMaxAlignment();
  if (AlignFromAttr)
    Align = AlignFromAttr;

  bool UseAlignAttrOnly;
  if (const FieldDecl *FD = dyn_cast<FieldDecl>(D))
    UseAlignAttrOnly =
        FD->hasAttr<PackedAttr>() || FD->getParent()->hasAttr<PackedAttr>();
  else
    UseAlignAttrOnly = AlignFromAttr != 0;

  if (UseAlignAttrOnly) {
  } else if (const auto *VD = dyn_cast<ValueDecl>(D)) {
    QualType T = VD->getType();
    QualType BaseT = getBaseElementType(T);
    if (T->isFunctionType())
      Align = getTypeInfoImpl(T.getTypePtr()).Align;
    else if (!BaseT->isIncompleteType()) {
      if (const ArrayType *arrayType = getAsArrayType(T)) {
        unsigned MinWidth = Target->getLargeArrayMinWidth();
        if (!ForAlignof && MinWidth) {
          if (isa<VariableArrayType>(arrayType))
            Align = std::max(Align, Target->getLargeArrayAlign());
          else if (isa<ConstantArrayType>(arrayType) &&
                   MinWidth <= getTypeSize(cast<ConstantArrayType>(arrayType)))
            Align = std::max(Align, Target->getLargeArrayAlign());
        }
      }
      Align = std::max(Align, getPreferredTypeAlign(T.getTypePtr()));
      if (BaseT.getQualifiers().hasUnaligned())
        Align = Target->getCharWidth();
    }

    if (const auto *VD = dyn_cast<VarDecl>(D))
      if (VD->hasGlobalStorage() && !ForAlignof) {
        uint64_t TypeSize =
            !BaseT->isIncompleteType() ? getTypeSize(T.getTypePtr()) : 0;
        Align = std::max(Align, getTargetInfo().getMinGlobalAlign(TypeSize));
      }

    if (const auto *Field = dyn_cast<FieldDecl>(VD)) {
      const RecordDecl *Parent = Field->getParent();
      if (!Parent->isInvalidDecl()) {
        const StructRecordLayout &Layout = getStructRecordLayout(Parent);

        unsigned FieldAlign = toBits(Layout.getAlignment());

        uint64_t Offset = Layout.getFieldOffset(Field->getFieldIndex());
        if (Offset > 0) {
          uint64_t LowBitOfOffset = Offset & (~Offset + 1);
          if (LowBitOfOffset < FieldAlign)
            FieldAlign = static_cast<unsigned>(LowBitOfOffset);
        }

        Align = std::min(Align, FieldAlign);
      }
    }
  }

  const unsigned MaxAlignedAttr = getTargetInfo().getMaxAlignedAttribute();
  const auto *VD = dyn_cast<VarDecl>(D);
  if (MaxAlignedAttr && VD && VD->getStorageClass() == SC_Static)
    Align = std::min(Align, MaxAlignedAttr);

  return toCharUnitsFromBits(Align);
}

TypeInfoChars TreeContext::getTypeInfoDataSizeInChars(QualType T) const {
  return getTypeInfoInChars(T);
}

namespace {
TypeInfoChars computeConstArrayInfoInChars(const TreeContext &Context,
                                           const ConstantArrayType *CAT) {
  TypeInfoChars EltInfo = Context.getTypeInfoInChars(CAT->getElementType());
  uint64_t Size = CAT->getSize().getZExtValue();
  assert((Size == 0 || static_cast<uint64_t>(EltInfo.Width.getQuantity()) <=
                           (uint64_t)(-1) / Size) &&
         "Overflow in array type char size evaluation");
  uint64_t Width = EltInfo.Width.getQuantity() * Size;
  unsigned Align = EltInfo.Align.getQuantity();
  Width = llvm::alignTo(Width, Align);
  return TypeInfoChars(CharUnits::fromQuantity(Width),
                       CharUnits::fromQuantity(Align),
                       EltInfo.AlignRequirement);
}
} // namespace

TypeInfoChars TreeContext::getTypeInfoInChars(const Type *T) const {
  if (const auto *CAT = dyn_cast<ConstantArrayType>(T))
    return computeConstArrayInfoInChars(*this, CAT);
  TypeInfo Info = getTypeInfo(T);
  return TypeInfoChars(toCharUnitsFromBits(Info.Width),
                       toCharUnitsFromBits(Info.Align), Info.AlignRequirement);
}

TypeInfoChars TreeContext::getTypeInfoInChars(QualType T) const {
  return getTypeInfoInChars(T.getTypePtr());
}

// ===----------------------------------------------------------------------===
// Type queries — size, alignment & properties
// ===----------------------------------------------------------------------===

bool TreeContext::isPromotableIntegerType(QualType T) const {
  if (const auto *BT = T->getAs<BuiltinType>())
    switch (BT->getKind()) {
    case BuiltinType::Bool:
    case BuiltinType::Char_S:
    case BuiltinType::Char_U:
    case BuiltinType::SChar:
    case BuiltinType::UChar:
    case BuiltinType::Short:
    case BuiltinType::UShort:
    case BuiltinType::WChar_S:
    case BuiltinType::WChar_U:
    case BuiltinType::Char8:
    case BuiltinType::Char16:
    case BuiltinType::Char32:
      return true;
    default:
      return false;
    }

  // Enums promote like their underlying integer type.
  if (const auto *ET = T->getAs<EnumType>()) {
    if (ET->getDecl()->getPromotionType().isNull())
      return false;
    return true;
  }

  return false;
}

bool TreeContext::isAlignmentRequired(const Type *T) const {
  return getTypeInfo(T).AlignRequirement != AlignRequirementKind::None;
}

bool TreeContext::isAlignmentRequired(QualType T) const {
  return isAlignmentRequired(T.getTypePtr());
}

unsigned TreeContext::getTypeAlignIfKnown(QualType T) const {
  // An alignment on a typedef overrides anything else.
  if (const auto *TT = T->getAs<TypedefType>())
    if (unsigned Align = TT->getDecl()->getMaxAlignment())
      return Align;

  // If we have an (array of) complete type, we're done.
  T = getBaseElementType(T);
  if (!T->isIncompleteType())
    return getTypeAlign(T);

  // If we had an array type, its element type might be a typedef
  // type with an alignment attribute.
  if (const auto *TT = T->getAs<TypedefType>())
    if (unsigned Align = TT->getDecl()->getMaxAlignment())
      return Align;

  // Otherwise, see if the declaration of the type had an attribute.
  if (const auto *TT = T->getAs<TagType>())
    return TT->getDecl()->getMaxAlignment();

  return 0;
}

TypeInfo TreeContext::getTypeInfo(const Type *T) const {
  TypeInfoMap::iterator I = MemoizedTypeInfo.find(T);
  if (LLVM_LIKELY(I != MemoizedTypeInfo.end()))
    return I->second;

  // getTypeInfoImpl may recurse into getTypeInfo, invalidating iterators.
  TypeInfo TI = getTypeInfoImpl(T);
  MemoizedTypeInfo[T] = TI;
  return TI;
}

TypeInfo TreeContext::getTypeInfoImpl(const Type *T) const {
  uint64_t Width = 0;
  unsigned Align = 8;
  AlignRequirementKind AlignRequirement = AlignRequirementKind::None;
  LangAS AS = LangAS::Default;
  switch (T->getTypeClass()) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base)                       \
  case Type::Class:                                                            \
    assert(!T->isDependentType() && "should not see dependent types here");    \
    return getTypeInfo(cast<Class##Type>(T)->desugar().getTypePtr());
#include "neverc/Tree/TypeNodes.td.h"
    llvm_unreachable("Should not see dependent types");

  case Type::FunctionNoProto:
  case Type::FunctionProto:
    // GCC extension: alignof(function) = 32 bits
    Width = 0;
    Align = 32;
    break;

  case Type::IncompleteArray:
  case Type::VariableArray:
  case Type::ConstantArray: {
    // Model non-constant sized arrays as size zero, but track the alignment.
    uint64_t Size = 0;
    if (const auto *CAT = dyn_cast<ConstantArrayType>(T))
      Size = CAT->getSize().getZExtValue();

    TypeInfo EltInfo = getTypeInfo(cast<ArrayType>(T)->getElementType());
    assert((Size == 0 || EltInfo.Width <= (uint64_t)(-1) / Size) &&
           "Overflow in array type bit size evaluation");
    Width = EltInfo.Width * Size;
    Align = EltInfo.Align;
    AlignRequirement = EltInfo.AlignRequirement;
    Width = llvm::alignTo(Width, Align);
    break;
  }

  case Type::ExtVector:
  case Type::Vector: {
    const auto *VT = cast<VectorType>(T);
    TypeInfo EltInfo = getTypeInfo(VT->getElementType());
    Width = VT->isExtVectorBoolType() ? VT->getNumElements()
                                      : EltInfo.Width * VT->getNumElements();
    // Enforce at least byte size and alignment.
    Width = std::max<unsigned>(8, Width);
    Align = std::max<unsigned>(8, Width);

    // If the alignment is not a power of 2, round up to the next power of 2.
    // This happens for non-power-of-2 length vectors.
    if (Align & (Align - 1)) {
      Align = llvm::bit_ceil(Align);
      Width = llvm::alignTo(Width, Align);
    }
    // Adjust the alignment based on the target max.
    uint64_t TargetVectorAlign = Target->getMaxVectorAlign();
    if (TargetVectorAlign && TargetVectorAlign < Align)
      Align = TargetVectorAlign;
    if (VT->getVectorKind() == VectorKind::SveFixedLengthData)
      // Adjust the alignment for fixed-length SVE vectors. This is important
      // for non-power-of-2 vector lengths.
      Align = 128;
    else if (VT->getVectorKind() == VectorKind::SveFixedLengthPredicate)
      // Adjust the alignment for fixed-length SVE predicates.
      Align = 16;
    break;
  }

  case Type::ConstantMatrix: {
    const auto *MT = cast<ConstantMatrixType>(T);
    TypeInfo ElementInfo = getTypeInfo(MT->getElementType());
    // The internal layout of a matrix value is implementation defined.
    // Initially be ABI compatible with arrays with respect to alignment and
    // size.
    Width = ElementInfo.Width * MT->getNumRows() * MT->getNumColumns();
    Align = ElementInfo.Align;
    break;
  }

  case Type::Builtin:
    switch (cast<BuiltinType>(T)->getKind()) {
    default:
      llvm_unreachable("Unknown builtin type!");
    case BuiltinType::Void:
      // GCC extension: alignof(void) = 8 bits.
      Width = 0;
      Align = 8;
      break;
    case BuiltinType::Bool:
      Width = Target->getBoolWidth();
      Align = Target->getBoolAlign();
      break;
    case BuiltinType::Char_S:
    case BuiltinType::Char_U:
    case BuiltinType::UChar:
    case BuiltinType::SChar:
    case BuiltinType::Char8:
      Width = Target->getCharWidth();
      Align = Target->getCharAlign();
      break;
    case BuiltinType::WChar_S:
    case BuiltinType::WChar_U:
      Width = Target->getWCharWidth();
      Align = Target->getWCharAlign();
      break;
    case BuiltinType::Char16:
      Width = Target->getChar16Width();
      Align = Target->getChar16Align();
      break;
    case BuiltinType::Char32:
      Width = Target->getChar32Width();
      Align = Target->getChar32Align();
      break;
    case BuiltinType::UShort:
    case BuiltinType::Short:
      Width = Target->getShortWidth();
      Align = Target->getShortAlign();
      break;
    case BuiltinType::UInt:
    case BuiltinType::Int:
      Width = Target->getIntWidth();
      Align = Target->getIntAlign();
      break;
    case BuiltinType::ULong:
    case BuiltinType::Long:
      Width = Target->getLongWidth();
      Align = Target->getLongAlign();
      break;
    case BuiltinType::ULongLong:
    case BuiltinType::LongLong:
      Width = Target->getLongLongWidth();
      Align = Target->getLongLongAlign();
      break;
    case BuiltinType::Int128:
    case BuiltinType::UInt128:
      Width = 128;
      Align = Target->getInt128Align();
      break;
    case BuiltinType::ShortAccum:
    case BuiltinType::UShortAccum:
    case BuiltinType::SatShortAccum:
    case BuiltinType::SatUShortAccum:
      Width = Target->getShortAccumWidth();
      Align = Target->getShortAccumAlign();
      break;
    case BuiltinType::Accum:
    case BuiltinType::UAccum:
    case BuiltinType::SatAccum:
    case BuiltinType::SatUAccum:
      Width = Target->getAccumWidth();
      Align = Target->getAccumAlign();
      break;
    case BuiltinType::LongAccum:
    case BuiltinType::ULongAccum:
    case BuiltinType::SatLongAccum:
    case BuiltinType::SatULongAccum:
      Width = Target->getLongAccumWidth();
      Align = Target->getLongAccumAlign();
      break;
    case BuiltinType::ShortFract:
    case BuiltinType::UShortFract:
    case BuiltinType::SatShortFract:
    case BuiltinType::SatUShortFract:
      Width = Target->getShortFractWidth();
      Align = Target->getShortFractAlign();
      break;
    case BuiltinType::Fract:
    case BuiltinType::UFract:
    case BuiltinType::SatFract:
    case BuiltinType::SatUFract:
      Width = Target->getFractWidth();
      Align = Target->getFractAlign();
      break;
    case BuiltinType::LongFract:
    case BuiltinType::ULongFract:
    case BuiltinType::SatLongFract:
    case BuiltinType::SatULongFract:
      Width = Target->getLongFractWidth();
      Align = Target->getLongFractAlign();
      break;
    case BuiltinType::BFloat16:
      if (Target->hasBFloat16Type()) {
        Width = Target->getBFloat16Width();
        Align = Target->getBFloat16Align();
      }
      break;
    case BuiltinType::Float16:
    case BuiltinType::Half:
      Width = Target->getHalfWidth();
      Align = Target->getHalfAlign();
      break;
    case BuiltinType::Float:
      Width = Target->getFloatWidth();
      Align = Target->getFloatAlign();
      break;
    case BuiltinType::Double:
      Width = Target->getDoubleWidth();
      Align = Target->getDoubleAlign();
      break;
    case BuiltinType::LongDouble:
      Width = Target->getLongDoubleWidth();
      Align = Target->getLongDoubleAlign();
      break;
    case BuiltinType::Float128:
      Width = Target->getFloat128Width();
      Align = Target->getFloat128Align();
      break;
    case BuiltinType::NullPtr:
      // `nullptr_t` has the size/alignment of `void*`.
      Width = Target->getPointerWidth(LangAS::Default);
      Align = Target->getPointerAlign(LangAS::Default);
      break;
      // The SVE types are effectively target-specific.  The length of an
      // SVE_VECTOR_TYPE is only known at runtime, but it is always a multiple
      // of 128 bits.  There is one predicate bit for each vector byte, so the
      // length of an SVE_PREDICATE_TYPE is always a multiple of 16 bits.
      //
      // Because the length is only known at runtime, we use a dummy value
      // of 0 for the static length.  The alignment values are those defined
      // by the Procedure Call Standard for the Arm Architecture.
#define SVE_VECTOR_TYPE(Name, MangledName, Id, SingletonId, NumEls, ElBits,    \
                        IsSigned, IsFP, IsBF)                                  \
  case BuiltinType::Id:                                                        \
    Width = 0;                                                                 \
    Align = 128;                                                               \
    break;
#define SVE_PREDICATE_TYPE(Name, MangledName, Id, SingletonId, NumEls)         \
  case BuiltinType::Id:                                                        \
    Width = 0;                                                                 \
    Align = 16;                                                                \
    break;
#define SVE_OPAQUE_TYPE(Name, MangledName, Id, SingletonId)                    \
  case BuiltinType::Id:                                                        \
    Width = 0;                                                                 \
    Align = 16;                                                                \
    break;
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
    }
    break;
  case Type::Pointer:
    AS = cast<PointerType>(T)->getPointeeType().getAddressSpace();
    Width = Target->getPointerWidth(AS);
    Align = Target->getPointerAlign(AS);
    break;
  case Type::Complex: {
    // Complex types have the same alignment as their elements, but twice the
    // size.
    TypeInfo EltInfo = getTypeInfo(cast<ComplexType>(T)->getElementType());
    Width = EltInfo.Width * 2;
    Align = EltInfo.Align;
    break;
  }
  case Type::Adjusted:
  case Type::Decayed:
    return getTypeInfo(cast<AdjustedType>(T)->getAdjustedType().getTypePtr());
  case Type::BitInt: {
    const auto *EIT = cast<BitIntType>(T);
    Align = std::clamp<unsigned>(llvm::PowerOf2Ceil(EIT->getNumBits()),
                                 getCharWidth(), Target->getLongLongAlign());
    Width = llvm::alignTo(EIT->getNumBits(), Align);
    break;
  }
  case Type::Record:
  case Type::Enum: {
    const auto *TT = cast<TagType>(T);

    if (TT->getDecl()->isInvalidDecl()) {
      Width = 8;
      Align = 8;
      break;
    }

    if (const auto *ET = dyn_cast<EnumType>(TT)) {
      const EnumDecl *ED = ET->getDecl();
      TypeInfo Info =
          getTypeInfo(ED->getIntegerType()->getUnqualifiedDesugaredType());
      if (unsigned AttrAlign = ED->getMaxAlignment()) {
        Info.Align = AttrAlign;
        Info.AlignRequirement = AlignRequirementKind::RequiredByEnum;
      }
      return Info;
    }

    const auto *RT = cast<RecordType>(TT);
    const RecordDecl *RD = RT->getDecl();
    const StructRecordLayout &Layout = getStructRecordLayout(RD);
    Width = toBits(Layout.getSize());
    Align = toBits(Layout.getAlignment());
    AlignRequirement = RD->hasAttr<AlignedAttr>()
                           ? AlignRequirementKind::RequiredByRecord
                           : AlignRequirementKind::None;
    break;
  }

  case Type::Auto: {
    const auto *A = cast<DeducedType>(T);
    assert(!A->getDeducedType().isNull() &&
           "cannot request the size of an undeduced or dependent auto type");
    return getTypeInfo(A->getDeducedType().getTypePtr());
  }

  case Type::Paren:
    return getTypeInfo(cast<ParenType>(T)->getInnerType().getTypePtr());

  case Type::MacroQualified:
    return getTypeInfo(
        cast<MacroQualifiedType>(T)->getUnderlyingType().getTypePtr());

  case Type::Typedef: {
    const auto *TT = cast<TypedefType>(T);
    TypeInfo Info = getTypeInfo(TT->desugar().getTypePtr());
    // If the typedef has an aligned attribute on it, it overrides any computed
    // alignment we have.  This violates the GCC documentation (which says that
    // attribute(aligned) can only round up) but matches its implementation.
    if (unsigned AttrAlign = TT->getDecl()->getMaxAlignment()) {
      Align = AttrAlign;
      AlignRequirement = AlignRequirementKind::RequiredByTypedef;
    } else {
      Align = Info.Align;
      AlignRequirement = Info.AlignRequirement;
    }
    Width = Info.Width;
    break;
  }

  case Type::Elaborated:
    return getTypeInfo(cast<ElaboratedType>(T)->getNamedType().getTypePtr());

  case Type::Attributed:
    return getTypeInfo(
        cast<AttributedType>(T)->getEquivalentType().getTypePtr());

  case Type::BTFTagAttributed:
    return getTypeInfo(
        cast<BTFTagAttributedType>(T)->getWrappedType().getTypePtr());

  case Type::Atomic: {
    // Start with the base type information.
    TypeInfo Info = getTypeInfo(cast<AtomicType>(T)->getValueType());
    Width = Info.Width;
    Align = Info.Align;

    if (!Width) {
      // An otherwise zero-sized type should still generate an
      // atomic operation.
      Width = Target->getCharWidth();
      assert(Align);
    } else if (Width <= Target->getMaxAtomicPromoteWidth()) {
      // If the size of the type doesn't exceed the platform's max
      // atomic promotion width, make the size and alignment more
      // favorable to atomic operations:

      // Round the size up to a power of 2.
      Width = llvm::bit_ceil(Width);
      Align = static_cast<unsigned>(Width);
    }
  } break;
  }

  assert(llvm::isPowerOf2_32(Align) && "Alignment must be power of 2");
  return TypeInfo(Width, Align, AlignRequirement);
}

unsigned TreeContext::getTypeUnadjustedAlign(const Type *T) const {
  UnadjustedAlignMap::iterator I = MemoizedUnadjustedAlign.find(T);
  if (I != MemoizedUnadjustedAlign.end())
    return I->second;

  unsigned UnadjustedAlign;
  if (const auto *RT = T->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    const StructRecordLayout &Layout = getStructRecordLayout(RD);
    UnadjustedAlign = toBits(Layout.getUnadjustedAlignment());
  } else {
    UnadjustedAlign = getTypeAlign(T->getUnqualifiedDesugaredType());
  }

  MemoizedUnadjustedAlign[T] = UnadjustedAlign;
  return UnadjustedAlign;
}

CharUnits TreeContext::toCharUnitsFromBits(int64_t BitSize) const {
  return CharUnits::fromQuantity(BitSize / getCharWidth());
}

int64_t TreeContext::toBits(CharUnits CharSize) const {
  return CharSize.getQuantity() * getCharWidth();
}

CharUnits TreeContext::getTypeSizeInChars(QualType T) const {
  return getTypeInfoInChars(T).Width;
}
CharUnits TreeContext::getTypeSizeInChars(const Type *T) const {
  return getTypeInfoInChars(T).Width;
}

CharUnits TreeContext::getTypeAlignInChars(QualType T) const {
  return toCharUnitsFromBits(getTypeAlign(T));
}
CharUnits TreeContext::getTypeAlignInChars(const Type *T) const {
  return toCharUnitsFromBits(getTypeAlign(T));
}

CharUnits TreeContext::getTypeUnadjustedAlignInChars(QualType T) const {
  return toCharUnitsFromBits(getTypeUnadjustedAlign(T));
}
CharUnits TreeContext::getTypeUnadjustedAlignInChars(const Type *T) const {
  return toCharUnitsFromBits(getTypeUnadjustedAlign(T));
}

unsigned TreeContext::getPreferredTypeAlign(const Type *T) const {
  TypeInfo TI = getTypeInfo(T);
  unsigned ABIAlign = TI.Align;

  T = T->getBaseElementTypeUnsafe();

  if (const auto *RT = T->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();

    // When used as part of a typedef, or together with a 'packed' attribute,
    // the 'aligned' attribute can be used to decrease alignment. Note that the
    // 'packed' case is already taken into consideration when computing the
    // alignment, we only need to handle the typedef case here.
    if (TI.AlignRequirement == AlignRequirementKind::RequiredByTypedef ||
        RD->isInvalidDecl())
      return ABIAlign;

    unsigned RecordAlign =
        static_cast<unsigned>(toBits(getStructRecordLayout(RD).Alignment));
    assert(RecordAlign >= ABIAlign &&
           "Record alignment should be at least as large as ABIAlign.");
    return RecordAlign;
  }

  // Double and long long should be naturally aligned (despite requiring less
  // alignment) if possible.
  if (const auto *CT = T->getAs<ComplexType>())
    T = CT->getElementType().getTypePtr();
  if (const auto *ET = T->getAs<EnumType>())
    T = ET->getDecl()->getIntegerType().getTypePtr();
  if (T->isSpecificBuiltinType(BuiltinType::Double) ||
      T->isSpecificBuiltinType(BuiltinType::LongLong) ||
      T->isSpecificBuiltinType(BuiltinType::ULongLong))
    if (!TI.isAlignRequired())
      return std::max(ABIAlign, (unsigned)getTypeSize(T));

  return ABIAlign;
}

unsigned TreeContext::getTargetDefaultAlignForAttributeAligned() const {
  return getTargetInfo().getDefaultAlignForAttributeAligned();
}

unsigned TreeContext::getAlignOfGlobalVar(QualType T) const {
  uint64_t TypeSize = getTypeSize(T.getTypePtr());
  return std::max(getPreferredTypeAlign(T),
                  getTargetInfo().getMinGlobalAlign(TypeSize));
}

CharUnits TreeContext::getAlignOfGlobalVarInChars(QualType T) const {
  return toCharUnitsFromBits(getAlignOfGlobalVar(T));
}

namespace {
bool unionHasUniqueRepr(const TreeContext &Context, const RecordDecl *RD,
                        bool CheckIfTriviallyCopyable) {
  assert(RD->isUnion() && "Must be union type");
  CharUnits UnionSize = Context.getTypeSizeInChars(RD->getTypeForDecl());

  for (const auto *Field : RD->fields()) {
    if (!Context.hasUniqueObjectRepresentations(Field->getType(),
                                                CheckIfTriviallyCopyable))
      return false;
    CharUnits FieldSize = Context.getTypeSizeInChars(Field->getType());
    if (FieldSize != UnionSize)
      return false;
  }
  return !RD->field_empty();
}
} // namespace

namespace {
int64_t getSubobjectOffset(const FieldDecl *Field, const TreeContext &Context,
                           const neverc::StructRecordLayout & /*Layout*/) {
  return Context.getFieldOffset(Field);
}
} // namespace

namespace {
std::optional<int64_t>
structHasUniqueObjectRepresentations(const TreeContext &Context,
                                     const RecordDecl *RD,
                                     bool CheckIfTriviallyCopyable);
} // namespace

namespace {
std::optional<int64_t> getSubobjectSizeInBits(const FieldDecl *Field,
                                              const TreeContext &Context,
                                              bool CheckIfTriviallyCopyable) {
  if (Field->getType()->isRecordType()) {
    const RecordDecl *RD = Field->getType()->getAsRecordDecl();
    if (!RD->isUnion())
      return structHasUniqueObjectRepresentations(Context, RD,
                                                  CheckIfTriviallyCopyable);
  }

  // A _BitInt type may not be unique if it has padding bits
  // but if it is a bitfield the padding bits are not used.
  bool IsBitIntType = Field->getType()->isBitIntType();
  if (!IsBitIntType && !Context.hasUniqueObjectRepresentations(
                           Field->getType(), CheckIfTriviallyCopyable))
    return std::nullopt;

  int64_t FieldSizeInBits =
      Context.toBits(Context.getTypeSizeInChars(Field->getType()));
  if (Field->isBitField()) {
    // If we have explicit padding bits, they don't contribute bits
    // to the actual object representation, so return 0.
    if (Field->isUnnamedBitfield())
      return 0;

    int64_t BitfieldSize = Field->getBitWidthValue(Context);
    if (IsBitIntType) {
      if ((unsigned)BitfieldSize >
          cast<BitIntType>(Field->getType())->getNumBits())
        return std::nullopt;
    } else if (BitfieldSize > FieldSizeInBits) {
      return std::nullopt;
    }
    FieldSizeInBits = BitfieldSize;
  } else if (IsBitIntType && !Context.hasUniqueObjectRepresentations(
                                 Field->getType(), CheckIfTriviallyCopyable)) {
    return std::nullopt;
  }
  return FieldSizeInBits;
}
} // namespace

namespace {
template <typename RangeT>
std::optional<int64_t> structSubobjectsHaveUniqueObjectRepresentations(
    const RangeT &Subobjects, int64_t CurOffsetInBits,
    const TreeContext &Context, const neverc::StructRecordLayout &Layout,
    bool CheckIfTriviallyCopyable) {
  for (const auto *Subobject : Subobjects) {
    std::optional<int64_t> SizeInBits =
        getSubobjectSizeInBits(Subobject, Context, CheckIfTriviallyCopyable);
    if (!SizeInBits)
      return std::nullopt;
    if (*SizeInBits != 0) {
      int64_t Offset = getSubobjectOffset(Subobject, Context, Layout);
      if (Offset != CurOffsetInBits)
        return std::nullopt;
      CurOffsetInBits += *SizeInBits;
    }
  }
  return CurOffsetInBits;
}
} // namespace

namespace {
std::optional<int64_t>
structHasUniqueObjectRepresentations(const TreeContext &Context,
                                     const RecordDecl *RD,
                                     bool CheckIfTriviallyCopyable) {
  assert(!RD->isUnion() && "Must be struct type");
  const auto &Layout = Context.getStructRecordLayout(RD);

  int64_t CurOffsetInBits = 0;

  std::optional<int64_t> OffsetAfterFields =
      structSubobjectsHaveUniqueObjectRepresentations(
          RD->fields(), CurOffsetInBits, Context, Layout,
          CheckIfTriviallyCopyable);
  if (!OffsetAfterFields)
    return std::nullopt;
  CurOffsetInBits = *OffsetAfterFields;

  return CurOffsetInBits;
}
} // namespace

bool TreeContext::hasUniqueObjectRepresentations(
    QualType Ty, bool CheckIfTriviallyCopyable) const {
  // `__has_unique_object_representations`: trivially copyable and no distinct
  // values with distinct object representations (padding breaks uniqueness).
  assert(!Ty.isNull() && "Null QualType sent to unique object rep check");

  // Arrays are unique only if their element type is unique.
  if (Ty->isArrayType())
    return hasUniqueObjectRepresentations(getBaseElementType(Ty),
                                          CheckIfTriviallyCopyable);

  // (9.1) - T is trivially copyable...
  if (CheckIfTriviallyCopyable && !Ty.isTriviallyCopyableType(*this))
    return false;

  // All integrals and enums are unique.
  if (Ty->isIntegralOrEnumerationType()) {
    // Except _BitInt types that have padding bits.
    if (const auto *BIT = Ty->getAs<BitIntType>())
      return getTypeSize(BIT) == BIT->getNumBits();

    return true;
  }

  // All other pointers are unique.
  if (Ty->isPointerType())
    return true;

  if (Ty->isRecordType()) {
    const RecordDecl *Record = Ty->castAs<RecordType>()->getDecl();

    if (Record->isInvalidDecl())
      return false;

    if (Record->isUnion())
      return unionHasUniqueRepr(*this, Record, CheckIfTriviallyCopyable);

    std::optional<int64_t> StructSize = structHasUniqueObjectRepresentations(
        *this, Record, CheckIfTriviallyCopyable);

    return StructSize && *StructSize == static_cast<int64_t>(getTypeSize(Ty));
  }

  return false;
}

bool TreeContext::isSentinelNullExpr(const Expr *E) {
  if (!E)
    return false;

  // nullptr_t is always treated as null.
  if (E->getType()->isNullPtrType())
    return true;

  if (E->getType()->isAnyPointerType() &&
      E->IgnoreParenCasts()->isNullPointerConstant(
          *this, Expr::NPC_ValueDependentIsNull))
    return true;

  return false;
}

TypeSourceInfo *TreeContext::CreateTypeSourceInfo(QualType T,
                                                  unsigned DataSize) const {
  if (!DataSize)
    DataSize = TypeLoc::getFullDataSizeForType(T);
  else
    assert(DataSize == TypeLoc::getFullDataSizeForType(T) &&
           "incorrect data size provided to CreateTypeSourceInfo!");

  auto *TInfo = (TypeSourceInfo *)BumpAlloc.Allocate(
      sizeof(TypeSourceInfo) + DataSize, 8);
  new (TInfo) TypeSourceInfo(T, DataSize);
  return TInfo;
}

TypeSourceInfo *TreeContext::getTrivialTypeSourceInfo(QualType T,
                                                      SourceLocation L) const {
  TypeSourceInfo *DI = CreateTypeSourceInfo(T);
  DI->getTypeLoc().initialize(const_cast<TreeContext &>(*this), L);
  return DI;
}

// ===----------------------------------------------------------------------===
// QualType construction & canonicalization
// ===----------------------------------------------------------------------===

QualType TreeContext::getExtQualType(const Type *baseType,
                                     Qualifiers quals) const {
  unsigned fastQuals = quals.getFastQualifiers();
  quals.removeFastQualifiers();

  llvm::FoldingSetNodeID ID;
  ExtQuals::Profile(ID, baseType, quals);
  void *insertPos = nullptr;
  if (ExtQuals *eq = ExtQualNodes.FindNodeOrInsertPos(ID, insertPos)) {
    assert(eq->getQualifiers() == quals);
    return QualType(eq, fastQuals);
  }

  // If the base type is not canonical, make the appropriate canonical type.
  QualType canon;
  if (!baseType->isCanonicalUnqualified()) {
    SplitQualType canonSplit = baseType->getCanonicalTypeInternal().split();
    canonSplit.Quals.addConsistentQualifiers(quals);
    canon = getExtQualType(canonSplit.Ty, canonSplit.Quals);

    // Re-find the insert position.
    (void)ExtQualNodes.FindNodeOrInsertPos(ID, insertPos);
  }

  auto *eq = new (*this, alignof(ExtQuals)) ExtQuals(baseType, canon, quals);
  ExtQualNodes.InsertNode(eq, insertPos);
  return QualType(eq, fastQuals);
}

QualType TreeContext::getAddrSpaceQualType(QualType T,
                                           LangAS AddressSpace) const {
  QualType CanT = getCanonicalType(T);
  if (CanT.getAddressSpace() == AddressSpace)
    return T;

  // If we are composing extended qualifiers together, merge together
  // into one ExtQuals node.
  QualifierCollector Quals;
  const Type *TypeNode = Quals.strip(T);

  // If this type already has an address space specified, it cannot get
  // another one.
  assert(!Quals.hasAddressSpace() && "Type cannot be in multiple addr spaces!");
  Quals.addAddressSpace(AddressSpace);

  return getExtQualType(TypeNode, Quals);
}

QualType TreeContext::removeAddrSpaceQualType(QualType T) const {
  // If the type is not qualified with an address space, just return it
  // immediately.
  if (!T.hasAddressSpace())
    return T;

  // If we are composing extended qualifiers together, merge together
  // into one ExtQuals node.
  QualifierCollector Quals;
  const Type *TypeNode;

  while (T.hasAddressSpace()) {
    TypeNode = Quals.strip(T);

    // If the type no longer has an address space after stripping qualifiers,
    // jump out.
    if (!QualType(TypeNode, 0).hasAddressSpace())
      break;

    // There might be sugar in the way. Strip it and try again.
    T = T.getSingleStepDesugaredType(*this);
  }

  Quals.removeAddressSpace();

  // Removal of the address space can mean there are no longer any
  // non-fast qualifiers, so creating an ExtQualType isn't possible (asserts)
  // or required.
  if (Quals.hasNonFastQualifiers())
    return getExtQualType(TypeNode, Quals);
  else
    return QualType(TypeNode, Quals.getFastQualifiers());
}

QualType TreeContext::removePtrSizeAddrSpace(QualType T) const {
  if (const PointerType *Ptr = T->getAs<PointerType>()) {
    QualType Pointee = Ptr->getPointeeType();
    if (isPtrSizeAddressSpace(Pointee.getAddressSpace())) {
      return getPointerType(removeAddrSpaceQualType(Pointee));
    }
  }
  return T;
}

const FunctionType *
TreeContext::adjustFunctionType(const FunctionType *T,
                                FunctionType::ExtInfo Info) {
  if (T->getExtInfo() == Info)
    return T;

  QualType Result;
  if (const auto *FNPT = dyn_cast<FunctionNoProtoType>(T)) {
    Result = getFunctionNoProtoType(FNPT->getReturnType(), Info);
  } else {
    const auto *FPT = cast<FunctionProtoType>(T);
    FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
    EPI.ExtInfo = Info;
    Result = getFunctionType(FPT->getReturnType(), FPT->getParamTypes(), EPI);
  }

  return cast<FunctionType>(Result.getTypePtr());
}

QualType TreeContext::getFunctionTypeWithExceptionSpec(
    QualType Orig, const FunctionProtoType::ExceptionSpecInfo &ESI) const {
  // Might have some parens.
  if (const auto *PT = dyn_cast<ParenType>(Orig))
    return getParenType(
        getFunctionTypeWithExceptionSpec(PT->getInnerType(), ESI));

  // Might be wrapped in a macro qualified type.
  if (const auto *MQT = dyn_cast<MacroQualifiedType>(Orig))
    return getMacroQualifiedType(
        getFunctionTypeWithExceptionSpec(MQT->getUnderlyingType(), ESI),
        MQT->getMacroIdentifier());

  // Might have a calling-convention attribute.
  if (const auto *AT = dyn_cast<AttributedType>(Orig))
    return getAttributedType(
        AT->getAttrKind(),
        getFunctionTypeWithExceptionSpec(AT->getModifiedType(), ESI),
        getFunctionTypeWithExceptionSpec(AT->getEquivalentType(), ESI));

  // Anything else must be a function type. Rebuild it with the new exception
  // specification.
  const auto *Proto = Orig->castAs<FunctionProtoType>();
  return getFunctionType(Proto->getReturnType(), Proto->getParamTypes(),
                         Proto->getExtProtoInfo().withExceptionSpec(ESI));
}

QualType TreeContext::getFunctionTypeWithoutPtrSizes(QualType T) {
  if (const auto *Proto = T->getAs<FunctionProtoType>()) {
    QualType RetTy = removePtrSizeAddrSpace(Proto->getReturnType());
    llvm::SmallVector<QualType, 16> Args(Proto->param_types().size());
    for (unsigned i = 0, n = Args.size(); i != n; ++i)
      Args[i] = removePtrSizeAddrSpace(Proto->param_types()[i]);
    return getFunctionType(RetTy, Args, Proto->getExtProtoInfo());
  }

  if (const FunctionNoProtoType *Proto = T->getAs<FunctionNoProtoType>()) {
    QualType RetTy = removePtrSizeAddrSpace(Proto->getReturnType());
    return getFunctionNoProtoType(RetTy, Proto->getExtInfo());
  }

  return T;
}

bool TreeContext::hasSameFunctionTypeIgnoringPtrSizes(QualType T, QualType U) {
  return hasSameType(T, U) || hasSameType(getFunctionTypeWithoutPtrSizes(T),
                                          getFunctionTypeWithoutPtrSizes(U));
}

QualType TreeContext::getComplexType(QualType T) const {
  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  ComplexType::Profile(ID, T);

  void *InsertPos = nullptr;
  if (ComplexType *CT = ComplexTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(CT, 0);

  // If the pointee type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!T.isCanonical()) {
    Canonical = getComplexType(getCanonicalType(T));

    ComplexType *NewIP = ComplexTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!");
    (void)NewIP;
  }
  auto *New = new (*this, alignof(ComplexType)) ComplexType(T, Canonical);
  Types.push_back(New);
  ComplexTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

__attribute__((hot)) QualType TreeContext::getPointerType(QualType T) const {
  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  PointerType::Profile(ID, T);

  void *InsertPos = nullptr;
  if (PointerType *PT = PointerTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(PT, 0);

  // If the pointee type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!T.isCanonical()) {
    Canonical = getPointerType(getCanonicalType(T));

    PointerType *NewIP = PointerTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!");
    (void)NewIP;
  }
  auto *New = new (*this, alignof(PointerType)) PointerType(T, Canonical);
  Types.push_back(New);
  PointerTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

QualType TreeContext::getAdjustedType(QualType Orig, QualType New) const {
  llvm::FoldingSetNodeID ID;
  AdjustedType::Profile(ID, Orig, New);
  void *InsertPos = nullptr;
  AdjustedType *AT = AdjustedTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (AT)
    return QualType(AT, 0);

  QualType Canonical = getCanonicalType(New);

  AT = AdjustedTypes.FindNodeOrInsertPos(ID, InsertPos);
  assert(!AT && "Shouldn't be in the map!");

  AT = new (*this, alignof(AdjustedType))
      AdjustedType(Type::Adjusted, Orig, New, Canonical);
  Types.push_back(AT);
  AdjustedTypes.InsertNode(AT, InsertPos);
  return QualType(AT, 0);
}

QualType TreeContext::getDecayedType(QualType Orig, QualType Decayed) const {
  llvm::FoldingSetNodeID ID;
  AdjustedType::Profile(ID, Orig, Decayed);
  void *InsertPos = nullptr;
  AdjustedType *AT = AdjustedTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (AT)
    return QualType(AT, 0);

  QualType Canonical = getCanonicalType(Decayed);

  AT = AdjustedTypes.FindNodeOrInsertPos(ID, InsertPos);
  assert(!AT && "Shouldn't be in the map!");

  AT = new (*this, alignof(DecayedType)) DecayedType(Orig, Decayed, Canonical);
  Types.push_back(AT);
  AdjustedTypes.InsertNode(AT, InsertPos);
  return QualType(AT, 0);
}

QualType TreeContext::getDecayedType(QualType T) const {
  assert((T->isArrayType() || T->isFunctionType()) && "T does not decay");

  QualType Decayed;

  // Array parameters decay to qualified pointer; function params to pointer.
  if (T->isArrayType())
    Decayed = getArrayDecayedType(T);
  if (T->isFunctionType())
    Decayed = getPointerType(T);

  return getDecayedType(T, Decayed);
}

QualType TreeContext::getConstantArrayType(QualType EltTy,
                                           const llvm::APInt &ArySizeIn,
                                           const Expr *SizeExpr,
                                           ArraySizeModifier ASM,
                                           unsigned IndexTypeQuals) const {
  assert((EltTy->isDependentType() || EltTy->isIncompleteType() ||
          EltTy->isConstantSizeType()) &&
         "Constant array of VLAs is illegal!");

  SizeExpr = nullptr;

  // Convert the array size into a canonical width matching the pointer size for
  // the target.
  llvm::APInt ArySize(ArySizeIn);
  ArySize = ArySize.zextOrTrunc(Target->getMaxPointerWidth());

  llvm::FoldingSetNodeID ID;
  ConstantArrayType::Profile(ID, *this, EltTy, ArySize, SizeExpr, ASM,
                             IndexTypeQuals);

  void *InsertPos = nullptr;
  if (ConstantArrayType *ATP =
          ConstantArrayTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(ATP, 0);

  // If the element type isn't canonical or has qualifiers, or the array bound
  // is instantiation-dependent, this won't be a canonical type either, so fill
  // in the canonical type field.
  QualType Canon;
  if (!EltTy.isCanonical() || EltTy.hasLocalQualifiers() || SizeExpr) {
    SplitQualType canonSplit = getCanonicalType(EltTy).split();
    Canon = getConstantArrayType(QualType(canonSplit.Ty, 0), ArySize, nullptr,
                                 ASM, IndexTypeQuals);
    Canon = getQualifiedType(Canon, canonSplit.Quals);

    ConstantArrayType *NewIP =
        ConstantArrayTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!");
    (void)NewIP;
  }

  void *Mem = Allocate(
      ConstantArrayType::totalSizeToAlloc<const Expr *>(SizeExpr ? 1 : 0),
      alignof(ConstantArrayType));
  auto *New = new (Mem)
      ConstantArrayType(EltTy, Canon, ArySize, SizeExpr, ASM, IndexTypeQuals);
  ConstantArrayTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

QualType TreeContext::getVariableArrayDecayedType(QualType type) const {
  // Vastly most common case.
  if (!type->isVariablyModifiedType())
    return type;

  QualType result;

  SplitQualType split = type.getSplitDesugaredType();
  const Type *ty = split.Ty;
  switch (ty->getTypeClass()) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#include "neverc/Tree/TypeNodes.td.h"
    llvm_unreachable("didn't desugar past all non-canonical types?");

  // These types should never be variably-modified.
  case Type::Builtin:
  case Type::Complex:
  case Type::Vector:
  case Type::ExtVector:
  case Type::ConstantMatrix:
  case Type::Record:
  case Type::Enum:
  case Type::TypeOfExpr:
  case Type::TypeOf:
  case Type::Auto:
  case Type::BitInt:
    llvm_unreachable("type should never be variably-modified");

  // These types can be variably-modified but should never need to
  // further decay.
  case Type::FunctionNoProto:
  case Type::FunctionProto:
    return type;

  // These types can be variably-modified.  All these modifications
  // preserve structure except as noted by comments.
  case Type::Pointer:
    result = getPointerType(
        getVariableArrayDecayedType(cast<PointerType>(ty)->getPointeeType()));
    break;

  case Type::Atomic: {
    const auto *at = cast<AtomicType>(ty);
    result = getAtomicType(getVariableArrayDecayedType(at->getValueType()));
    break;
  }

  case Type::ConstantArray: {
    const auto *cat = cast<ConstantArrayType>(ty);
    result = getConstantArrayType(
        getVariableArrayDecayedType(cat->getElementType()), cat->getSize(),
        cat->getSizeExpr(), cat->getSizeModifier(),
        cat->getIndexTypeCVRQualifiers());
    break;
  }

  // Turn incomplete types into [*] types.
  case Type::IncompleteArray: {
    const auto *iat = cast<IncompleteArrayType>(ty);
    result =
        getVariableArrayType(getVariableArrayDecayedType(iat->getElementType()),
                             /*size*/ nullptr, ArraySizeModifier::Normal,
                             iat->getIndexTypeCVRQualifiers(), SourceRange());
    break;
  }

  // Turn VLA types into [*] types.
  case Type::VariableArray: {
    const auto *vat = cast<VariableArrayType>(ty);
    result = getVariableArrayType(
        getVariableArrayDecayedType(vat->getElementType()),
        /*size*/ nullptr, ArraySizeModifier::Star,
        vat->getIndexTypeCVRQualifiers(), vat->getBracketsRange());
    break;
  }
  }

  // Apply the top-level qualifiers from the original.
  return getQualifiedType(result, split.Quals);
}

QualType TreeContext::getVariableArrayType(QualType EltTy, Expr *NumElts,
                                           ArraySizeModifier ASM,
                                           unsigned IndexTypeQuals,
                                           SourceRange Brackets) const {
  // Since we don't unique expressions, it isn't possible to unique VLA's
  // that have an expression provided for their size.
  QualType Canon;

  // Be sure to pull qualifiers off the element type.
  if (!EltTy.isCanonical() || EltTy.hasLocalQualifiers()) {
    SplitQualType canonSplit = getCanonicalType(EltTy).split();
    Canon = getVariableArrayType(QualType(canonSplit.Ty, 0), NumElts, ASM,
                                 IndexTypeQuals, Brackets);
    Canon = getQualifiedType(Canon, canonSplit.Quals);
  }

  auto *New = new (*this, alignof(VariableArrayType))
      VariableArrayType(EltTy, Canon, NumElts, ASM, IndexTypeQuals, Brackets);

  VariableArrayTypes.push_back(New);
  Types.push_back(New);
  return QualType(New, 0);
}

QualType TreeContext::getIncompleteArrayType(QualType elementType,
                                             ArraySizeModifier ASM,
                                             unsigned elementTypeQuals) const {
  llvm::FoldingSetNodeID ID;
  IncompleteArrayType::Profile(ID, elementType, ASM, elementTypeQuals);

  void *insertPos = nullptr;
  if (IncompleteArrayType *iat =
          IncompleteArrayTypes.FindNodeOrInsertPos(ID, insertPos))
    return QualType(iat, 0);

  // If the element type isn't canonical, this won't be a canonical type
  // either, so fill in the canonical type field.  We also have to pull
  // qualifiers off the element type.
  QualType canon;

  if (!elementType.isCanonical() || elementType.hasLocalQualifiers()) {
    SplitQualType canonSplit = getCanonicalType(elementType).split();
    canon = getIncompleteArrayType(QualType(canonSplit.Ty, 0), ASM,
                                   elementTypeQuals);
    canon = getQualifiedType(canon, canonSplit.Quals);

    IncompleteArrayType *existing =
        IncompleteArrayTypes.FindNodeOrInsertPos(ID, insertPos);
    assert(!existing && "Shouldn't be in the map!");
    (void)existing;
  }

  auto *newType = new (*this, alignof(IncompleteArrayType))
      IncompleteArrayType(elementType, canon, ASM, elementTypeQuals);

  IncompleteArrayTypes.InsertNode(newType, insertPos);
  Types.push_back(newType);
  return QualType(newType, 0);
}

TreeContext::BuiltinVectorTypeInfo
TreeContext::getBuiltinVectorTypeInfo(const BuiltinType *Ty) const {
#define SVE_INT_ELTTY(BITS, ELTS, SIGNED, NUMVECTORS)                          \
  {getIntTypeForBitwidth(BITS, SIGNED), llvm::ElementCount::getScalable(ELTS), \
   NUMVECTORS};

#define SVE_ELTTY(ELTTY, ELTS, NUMVECTORS)                                     \
  {ELTTY, llvm::ElementCount::getScalable(ELTS), NUMVECTORS};

  switch (Ty->getKind()) {
  default:
    llvm_unreachable("Unsupported builtin vector type");
  case BuiltinType::SveInt8:
    return SVE_INT_ELTTY(8, 16, true, 1);
  case BuiltinType::SveUint8:
    return SVE_INT_ELTTY(8, 16, false, 1);
  case BuiltinType::SveInt8x2:
    return SVE_INT_ELTTY(8, 16, true, 2);
  case BuiltinType::SveUint8x2:
    return SVE_INT_ELTTY(8, 16, false, 2);
  case BuiltinType::SveInt8x3:
    return SVE_INT_ELTTY(8, 16, true, 3);
  case BuiltinType::SveUint8x3:
    return SVE_INT_ELTTY(8, 16, false, 3);
  case BuiltinType::SveInt8x4:
    return SVE_INT_ELTTY(8, 16, true, 4);
  case BuiltinType::SveUint8x4:
    return SVE_INT_ELTTY(8, 16, false, 4);
  case BuiltinType::SveInt16:
    return SVE_INT_ELTTY(16, 8, true, 1);
  case BuiltinType::SveUint16:
    return SVE_INT_ELTTY(16, 8, false, 1);
  case BuiltinType::SveInt16x2:
    return SVE_INT_ELTTY(16, 8, true, 2);
  case BuiltinType::SveUint16x2:
    return SVE_INT_ELTTY(16, 8, false, 2);
  case BuiltinType::SveInt16x3:
    return SVE_INT_ELTTY(16, 8, true, 3);
  case BuiltinType::SveUint16x3:
    return SVE_INT_ELTTY(16, 8, false, 3);
  case BuiltinType::SveInt16x4:
    return SVE_INT_ELTTY(16, 8, true, 4);
  case BuiltinType::SveUint16x4:
    return SVE_INT_ELTTY(16, 8, false, 4);
  case BuiltinType::SveInt32:
    return SVE_INT_ELTTY(32, 4, true, 1);
  case BuiltinType::SveUint32:
    return SVE_INT_ELTTY(32, 4, false, 1);
  case BuiltinType::SveInt32x2:
    return SVE_INT_ELTTY(32, 4, true, 2);
  case BuiltinType::SveUint32x2:
    return SVE_INT_ELTTY(32, 4, false, 2);
  case BuiltinType::SveInt32x3:
    return SVE_INT_ELTTY(32, 4, true, 3);
  case BuiltinType::SveUint32x3:
    return SVE_INT_ELTTY(32, 4, false, 3);
  case BuiltinType::SveInt32x4:
    return SVE_INT_ELTTY(32, 4, true, 4);
  case BuiltinType::SveUint32x4:
    return SVE_INT_ELTTY(32, 4, false, 4);
  case BuiltinType::SveInt64:
    return SVE_INT_ELTTY(64, 2, true, 1);
  case BuiltinType::SveUint64:
    return SVE_INT_ELTTY(64, 2, false, 1);
  case BuiltinType::SveInt64x2:
    return SVE_INT_ELTTY(64, 2, true, 2);
  case BuiltinType::SveUint64x2:
    return SVE_INT_ELTTY(64, 2, false, 2);
  case BuiltinType::SveInt64x3:
    return SVE_INT_ELTTY(64, 2, true, 3);
  case BuiltinType::SveUint64x3:
    return SVE_INT_ELTTY(64, 2, false, 3);
  case BuiltinType::SveInt64x4:
    return SVE_INT_ELTTY(64, 2, true, 4);
  case BuiltinType::SveUint64x4:
    return SVE_INT_ELTTY(64, 2, false, 4);
  case BuiltinType::SveBool:
    return SVE_ELTTY(BoolTy, 16, 1);
  case BuiltinType::SveBoolx2:
    return SVE_ELTTY(BoolTy, 16, 2);
  case BuiltinType::SveBoolx4:
    return SVE_ELTTY(BoolTy, 16, 4);
  case BuiltinType::SveFloat16:
    return SVE_ELTTY(HalfTy, 8, 1);
  case BuiltinType::SveFloat16x2:
    return SVE_ELTTY(HalfTy, 8, 2);
  case BuiltinType::SveFloat16x3:
    return SVE_ELTTY(HalfTy, 8, 3);
  case BuiltinType::SveFloat16x4:
    return SVE_ELTTY(HalfTy, 8, 4);
  case BuiltinType::SveFloat32:
    return SVE_ELTTY(FloatTy, 4, 1);
  case BuiltinType::SveFloat32x2:
    return SVE_ELTTY(FloatTy, 4, 2);
  case BuiltinType::SveFloat32x3:
    return SVE_ELTTY(FloatTy, 4, 3);
  case BuiltinType::SveFloat32x4:
    return SVE_ELTTY(FloatTy, 4, 4);
  case BuiltinType::SveFloat64:
    return SVE_ELTTY(DoubleTy, 2, 1);
  case BuiltinType::SveFloat64x2:
    return SVE_ELTTY(DoubleTy, 2, 2);
  case BuiltinType::SveFloat64x3:
    return SVE_ELTTY(DoubleTy, 2, 3);
  case BuiltinType::SveFloat64x4:
    return SVE_ELTTY(DoubleTy, 2, 4);
  case BuiltinType::SveBFloat16:
    return SVE_ELTTY(BFloat16Ty, 8, 1);
  case BuiltinType::SveBFloat16x2:
    return SVE_ELTTY(BFloat16Ty, 8, 2);
  case BuiltinType::SveBFloat16x3:
    return SVE_ELTTY(BFloat16Ty, 8, 3);
  case BuiltinType::SveBFloat16x4:
    return SVE_ELTTY(BFloat16Ty, 8, 4);
  }
}

QualType TreeContext::getScalableVectorType(QualType EltTy, unsigned NumElts,
                                            unsigned NumFields) const {
  if (Target->hasAArch64SVETypes()) {
    uint64_t EltTySize = getTypeSize(EltTy);
#define SVE_VECTOR_TYPE(Name, MangledName, Id, SingletonId, NumEls, ElBits,    \
                        IsSigned, IsFP, IsBF)                                  \
  if (!EltTy->isBooleanType() &&                                               \
      ((EltTy->hasIntegerRepresentation() &&                                   \
        EltTy->hasSignedIntegerRepresentation() == IsSigned) ||                \
       (EltTy->hasFloatingRepresentation() && !EltTy->isBFloat16Type() &&      \
        IsFP && !IsBF) ||                                                      \
       (EltTy->hasFloatingRepresentation() && EltTy->isBFloat16Type() &&       \
        IsBF && !IsFP)) &&                                                     \
      EltTySize == ElBits && NumElts == NumEls) {                              \
    return SingletonId;                                                        \
  }
#define SVE_PREDICATE_TYPE(Name, MangledName, Id, SingletonId, NumEls)         \
  if (EltTy->isBooleanType() && NumElts == NumEls)                             \
    return SingletonId;
#define SVE_OPAQUE_TYPE(Name, MangledName, Id, SingleTonId)
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
  }
  return QualType();
}

QualType TreeContext::getVectorType(QualType vecType, unsigned NumElts,
                                    VectorKind VecKind) const {
  assert(vecType->isBuiltinType() ||
         (vecType->isBitIntType() &&
          // Only support _BitInt elements with byte-sized power of 2 NumBits.
          llvm::isPowerOf2_32(vecType->castAs<BitIntType>()->getNumBits()) &&
          vecType->castAs<BitIntType>()->getNumBits() >= 8));

  llvm::FoldingSetNodeID ID;
  VectorType::Profile(ID, vecType, NumElts, Type::Vector, VecKind);

  void *InsertPos = nullptr;
  if (VectorType *VTP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(VTP, 0);

  // If the element type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!vecType.isCanonical()) {
    Canonical = getVectorType(getCanonicalType(vecType), NumElts, VecKind);

    VectorType *NewIP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!");
    (void)NewIP;
  }
  auto *New = new (*this, alignof(VectorType))
      VectorType(vecType, NumElts, Canonical, VecKind);
  VectorTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

QualType TreeContext::getExtVectorType(QualType vecType,
                                       unsigned NumElts) const {
  assert(vecType->isBuiltinType() ||
         (vecType->isBitIntType() &&
          // Only support _BitInt elements with byte-sized power of 2 NumBits.
          llvm::isPowerOf2_32(vecType->castAs<BitIntType>()->getNumBits()) &&
          vecType->castAs<BitIntType>()->getNumBits() >= 8));

  llvm::FoldingSetNodeID ID;
  VectorType::Profile(ID, vecType, NumElts, Type::ExtVector,
                      VectorKind::Generic);
  void *InsertPos = nullptr;
  if (VectorType *VTP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(VTP, 0);

  // If the element type isn't canonical, this won't be a canonical type either,
  // so fill in the canonical type field.
  QualType Canonical;
  if (!vecType.isCanonical()) {
    Canonical = getExtVectorType(getCanonicalType(vecType), NumElts);

    VectorType *NewIP = VectorTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!");
    (void)NewIP;
  }
  auto *New = new (*this, alignof(ExtVectorType))
      ExtVectorType(vecType, NumElts, Canonical);
  VectorTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

QualType TreeContext::getConstantMatrixType(QualType ElementTy,
                                            unsigned NumRows,
                                            unsigned NumColumns) const {
  llvm::FoldingSetNodeID ID;
  ConstantMatrixType::Profile(ID, ElementTy, NumRows, NumColumns,
                              Type::ConstantMatrix);

  assert(MatrixType::isValidElementType(ElementTy) &&
         "need a valid element type");
  assert(ConstantMatrixType::isDimensionValid(NumRows) &&
         ConstantMatrixType::isDimensionValid(NumColumns) &&
         "need valid matrix dimensions");
  void *InsertPos = nullptr;
  if (ConstantMatrixType *MTP = MatrixTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(MTP, 0);

  QualType Canonical;
  if (!ElementTy.isCanonical()) {
    Canonical =
        getConstantMatrixType(getCanonicalType(ElementTy), NumRows, NumColumns);

    ConstantMatrixType *NewIP = MatrixTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Matrix type shouldn't already exist in the map");
    (void)NewIP;
  }

  auto *New = new (*this, alignof(ConstantMatrixType))
      ConstantMatrixType(ElementTy, NumRows, NumColumns, Canonical);
  MatrixTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

namespace {
bool isCanonicalResultType(QualType T) { return T.isCanonical(); }
} // namespace

QualType
TreeContext::getFunctionNoProtoType(QualType ResultTy,
                                    const FunctionType::ExtInfo &Info) const {
  // Unique functions, to guarantee there is only one function of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  FunctionNoProtoType::Profile(ID, ResultTy, Info);

  void *InsertPos = nullptr;
  if (FunctionNoProtoType *FT =
          FunctionNoProtoTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(FT, 0);

  QualType Canonical;
  if (!isCanonicalResultType(ResultTy)) {
    Canonical =
        getFunctionNoProtoType(getCanonicalFunctionResultType(ResultTy), Info);

    FunctionNoProtoType *NewIP =
        FunctionNoProtoTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!");
    (void)NewIP;
  }

  auto *New = new (*this, alignof(FunctionNoProtoType))
      FunctionNoProtoType(ResultTy, Canonical, Info);
  Types.push_back(New);
  FunctionNoProtoTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

CanQualType
TreeContext::getCanonicalFunctionResultType(QualType ResultType) const {
  CanQualType CanResultType = getCanonicalType(ResultType);

  return CanResultType;
}

namespace {
bool isCanonicalExceptionSpecification(
    const FunctionProtoType::ExceptionSpecInfo &ESI) {
  return ESI.Type == EST_None || ESI.Type == EST_NoThrow;
}
} // namespace

QualType TreeContext::getFunctionTypeInternal(
    QualType ResultTy, llvm::ArrayRef<QualType> ArgArray,
    const FunctionProtoType::ExtProtoInfo &EPI, bool OnlyWantCanonical) const {
  size_t NumArgs = ArgArray.size();

  // Unique functions, to guarantee there is only one function of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  FunctionProtoType::Profile(ID, ResultTy, ArgArray.begin(), NumArgs, EPI,
                             *this, true);

  QualType Canonical;
  bool Unique = false;

  void *InsertPos = nullptr;
  if (FunctionProtoType *FPT =
          FunctionProtoTypes.FindNodeOrInsertPos(ID, InsertPos)) {
    QualType Existing = QualType(FPT, 0);

    return Existing;
  }

  bool IsCanonicalExceptionSpec =
      isCanonicalExceptionSpecification(EPI.ExceptionSpec);

  // Determine whether the type being created is already canonical or not.
  bool isCanonical =
      !Unique && IsCanonicalExceptionSpec && isCanonicalResultType(ResultTy);
  for (unsigned i = 0; i != NumArgs && isCanonical; ++i)
    if (!ArgArray[i].isCanonicalAsParam())
      isCanonical = false;

  if (OnlyWantCanonical)
    assert(isCanonical &&
           "given non-canonical parameters constructing canonical type");

  // If this type isn't canonical, get the canonical version of it if we don't
  // already have it. Exception spec only participates in canonicalization when
  // noexcept is part of the type.
  if (!isCanonical && Canonical.isNull()) {
    llvm::SmallVector<QualType, 16> CanonicalArgs;
    CanonicalArgs.reserve(NumArgs);
    for (unsigned i = 0; i != NumArgs; ++i)
      CanonicalArgs.push_back(getCanonicalParamType(ArgArray[i]));

    FunctionProtoType::ExtProtoInfo CanonicalEPI = EPI;

    if (!IsCanonicalExceptionSpec)
      CanonicalEPI.ExceptionSpec = FunctionProtoType::ExceptionSpecInfo();

    // Adjust the canonical function result type.
    CanQualType CanResultTy = getCanonicalFunctionResultType(ResultTy);
    Canonical =
        getFunctionTypeInternal(CanResultTy, CanonicalArgs, CanonicalEPI, true);

    FunctionProtoType *NewIP =
        FunctionProtoTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!");
    (void)NewIP;
  }

  size_t Size = FunctionProtoType::totalSizeToAlloc<
      QualType, SourceLocation, FunctionType::FunctionTypeExtraBitfields,
      FunctionProtoType::ExtParameterInfo, Qualifiers>(
      NumArgs, EPI.Variadic, EPI.requiresFunctionProtoTypeExtraBitfields(),
      EPI.ExtParameterInfos ? NumArgs : 0,
      EPI.TypeQuals.hasNonFastQualifiers() ? 1 : 0);

  auto *FTP = (FunctionProtoType *)Allocate(Size, alignof(FunctionProtoType));
  FunctionProtoType::ExtProtoInfo newEPI = EPI;
  new (FTP) FunctionProtoType(ResultTy, ArgArray, Canonical, newEPI);
  Types.push_back(FTP);
  if (!Unique)
    FunctionProtoTypes.InsertNode(FTP, InsertPos);
  return QualType(FTP, 0);
}

QualType TreeContext::adjustStringLiteralBaseType(QualType Ty) const {
  return Ty;
}

QualType TreeContext::getBitIntType(bool IsUnsigned, unsigned NumBits) const {
  llvm::FoldingSetNodeID ID;
  BitIntType::Profile(ID, IsUnsigned, NumBits);

  void *InsertPos = nullptr;
  if (BitIntType *EIT = BitIntTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(EIT, 0);

  auto *New = new (*this, alignof(BitIntType)) BitIntType(IsUnsigned, NumBits);
  BitIntTypes.InsertNode(New, InsertPos);
  Types.push_back(New);
  return QualType(New, 0);
}

QualType TreeContext::getTypeDeclTypeSlow(const TypeDecl *Decl) const {
  assert(Decl && "Passed null for Decl param");
  assert(!Decl->TypeForDecl && "TypeForDecl present in slow case");

  if (const auto *Typedef = dyn_cast<TypedefNameDecl>(Decl))
    return getTypedefType(Typedef);

  if (const auto *Record = dyn_cast<RecordDecl>(Decl)) {
    assert(Record->isFirstDecl() && "struct/union has previous declaration");
    return getRecordType(Record);
  } else if (const auto *Enum = dyn_cast<EnumDecl>(Decl)) {
    assert(Enum->isFirstDecl() && "enum has previous declaration");
    return getEnumType(Enum);
  } else
    llvm_unreachable("TypeDecl without a type?");

  return QualType(Decl->TypeForDecl, 0);
}

QualType TreeContext::getTypedefType(const TypedefNameDecl *Decl,
                                     QualType Underlying) const {
  if (!Decl->TypeForDecl) {
    if (Underlying.isNull())
      Underlying = Decl->getUnderlyingType();
    auto *NewType = new (*this, alignof(TypedefType)) TypedefType(
        Type::Typedef, Decl, QualType(), getCanonicalType(Underlying));
    Decl->TypeForDecl = NewType;
    Types.push_back(NewType);
    return QualType(NewType, 0);
  }
  if (Underlying.isNull() || Decl->getUnderlyingType() == Underlying)
    return QualType(Decl->TypeForDecl, 0);
  assert(hasSameType(Decl->getUnderlyingType(), Underlying));

  llvm::FoldingSetNodeID ID;
  TypedefType::Profile(ID, Decl, Underlying);

  void *InsertPos = nullptr;
  if (TypedefType *T = TypedefTypes.FindNodeOrInsertPos(ID, InsertPos)) {
    assert(!T->typeMatchesDecl() &&
           "non-divergent case should be handled with TypeDecl");
    return QualType(T, 0);
  }

  void *Mem = Allocate(TypedefType::totalSizeToAlloc<QualType>(true),
                       alignof(TypedefType));
  auto *NewType = new (Mem) TypedefType(Type::Typedef, Decl, Underlying,
                                        getCanonicalType(Underlying));
  TypedefTypes.InsertNode(NewType, InsertPos);
  Types.push_back(NewType);
  return QualType(NewType, 0);
}

QualType TreeContext::getRecordType(const RecordDecl *Decl) const {
  if (Decl->TypeForDecl)
    return QualType(Decl->TypeForDecl, 0);

  if (const RecordDecl *PrevDecl = Decl->getPreviousDecl())
    if (PrevDecl->TypeForDecl)
      return QualType(Decl->TypeForDecl = PrevDecl->TypeForDecl, 0);

  auto *newType = new (*this, alignof(RecordType)) RecordType(Decl);
  Decl->TypeForDecl = newType;
  Types.push_back(newType);
  return QualType(newType, 0);
}

QualType TreeContext::getEnumType(const EnumDecl *Decl) const {
  if (Decl->TypeForDecl)
    return QualType(Decl->TypeForDecl, 0);

  if (const EnumDecl *PrevDecl = Decl->getPreviousDecl())
    if (PrevDecl->TypeForDecl)
      return QualType(Decl->TypeForDecl = PrevDecl->TypeForDecl, 0);

  auto *newType = new (*this, alignof(EnumType)) EnumType(Decl);
  Decl->TypeForDecl = newType;
  Types.push_back(newType);
  return QualType(newType, 0);
}

QualType TreeContext::getAttributedType(attr::Kind attrKind,
                                        QualType modifiedType,
                                        QualType equivalentType) const {
  llvm::FoldingSetNodeID id;
  AttributedType::Profile(id, attrKind, modifiedType, equivalentType);

  void *insertPos = nullptr;
  AttributedType *type = AttributedTypes.FindNodeOrInsertPos(id, insertPos);
  if (type)
    return QualType(type, 0);

  QualType canon = getCanonicalType(equivalentType);
  type = new (*this, alignof(AttributedType))
      AttributedType(canon, attrKind, modifiedType, equivalentType);

  Types.push_back(type);
  AttributedTypes.InsertNode(type, insertPos);

  return QualType(type, 0);
}

QualType TreeContext::getBTFTagAttributedType(const BTFTypeTagAttr *BTFAttr,
                                              QualType Wrapped) {
  llvm::FoldingSetNodeID ID;
  BTFTagAttributedType::Profile(ID, Wrapped, BTFAttr);

  void *InsertPos = nullptr;
  BTFTagAttributedType *Ty =
      BTFTagAttributedTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (Ty)
    return QualType(Ty, 0);

  QualType Canon = getCanonicalType(Wrapped);
  Ty = new (*this, alignof(BTFTagAttributedType))
      BTFTagAttributedType(Canon, Wrapped, BTFAttr);

  Types.push_back(Ty);
  BTFTagAttributedTypes.InsertNode(Ty, InsertPos);

  return QualType(Ty, 0);
}

QualType TreeContext::getElaboratedType(ElaboratedTypeKeyword Keyword,
                                        QualType NamedType,
                                        TagDecl *OwnedTagDecl) const {
  llvm::FoldingSetNodeID ID;
  ElaboratedType::Profile(ID, Keyword, NamedType, OwnedTagDecl);

  void *InsertPos = nullptr;
  ElaboratedType *T = ElaboratedTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (T)
    return QualType(T, 0);

  QualType Canon = NamedType;
  if (!Canon.isCanonical()) {
    Canon = getCanonicalType(NamedType);
    ElaboratedType *CheckT = ElaboratedTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!CheckT && "Elaborated canonical type broken");
    (void)CheckT;
  }

  void *Mem =
      Allocate(ElaboratedType::totalSizeToAlloc<TagDecl *>(!!OwnedTagDecl),
               alignof(ElaboratedType));
  T = new (Mem) ElaboratedType(Keyword, NamedType, Canon, OwnedTagDecl);

  Types.push_back(T);
  ElaboratedTypes.InsertNode(T, InsertPos);
  return QualType(T, 0);
}

QualType TreeContext::getParenType(QualType InnerType) const {
  llvm::FoldingSetNodeID ID;
  ParenType::Profile(ID, InnerType);

  void *InsertPos = nullptr;
  ParenType *T = ParenTypes.FindNodeOrInsertPos(ID, InsertPos);
  if (T)
    return QualType(T, 0);

  QualType Canon = InnerType;
  if (!Canon.isCanonical()) {
    Canon = getCanonicalType(InnerType);
    ParenType *CheckT = ParenTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!CheckT && "Paren canonical type broken");
    (void)CheckT;
  }

  T = new (*this, alignof(ParenType)) ParenType(InnerType, Canon);
  Types.push_back(T);
  ParenTypes.InsertNode(T, InsertPos);
  return QualType(T, 0);
}

QualType
TreeContext::getMacroQualifiedType(QualType UnderlyingTy,
                                   const IdentifierInfo *MacroII) const {
  QualType Canon = UnderlyingTy;
  if (!Canon.isCanonical())
    Canon = getCanonicalType(UnderlyingTy);

  auto *newType = new (*this, alignof(MacroQualifiedType))
      MacroQualifiedType(UnderlyingTy, Canon, MacroII);
  Types.push_back(newType);
  return QualType(newType, 0);
}

QualType TreeContext::getTypeOfExprType(Expr *tofExpr, TypeOfKind Kind) const {
  TypeOfExprType *toe;
  if (tofExpr->isTypeDependent()) {
    llvm::FoldingSetNodeID ID;
    DependentTypeOfExprType::Profile(ID, *this, tofExpr,
                                     Kind == TypeOfKind::Unqualified);

    void *InsertPos = nullptr;
    DependentTypeOfExprType *Canon =
        DependentTypeOfExprTypes.FindNodeOrInsertPos(ID, InsertPos);
    if (Canon) {
      // We already have a "canonical" version of an identical, dependent
      // typeof(expr) type. Use that as our canonical type.
      toe = new (*this, alignof(TypeOfExprType))
          TypeOfExprType(tofExpr, Kind, QualType((TypeOfExprType *)Canon, 0));
    } else {
      Canon = new (*this, alignof(DependentTypeOfExprType))
          DependentTypeOfExprType(tofExpr, Kind);
      DependentTypeOfExprTypes.InsertNode(Canon, InsertPos);
      toe = Canon;
    }
  } else {
    QualType Canonical = getCanonicalType(tofExpr->getType());
    toe = new (*this, alignof(TypeOfExprType))
        TypeOfExprType(tofExpr, Kind, Canonical);
  }
  Types.push_back(toe);
  return QualType(toe, 0);
}

QualType TreeContext::getTypeOfType(QualType tofType, TypeOfKind Kind) const {
  QualType Canonical = getCanonicalType(tofType);
  auto *tot =
      new (*this, alignof(TypeOfType)) TypeOfType(tofType, Canonical, Kind);
  Types.push_back(tot);
  return QualType(tot, 0);
}

QualType TreeContext::getReferenceQualifiedType(const Expr *E) const {
  return E->getType();
}

QualType TreeContext::getAutoTypeInternal(QualType DeducedType,
                                          AutoTypeKeyword Keyword,
                                          bool IsDependent,
                                          bool IsCanon) const {
  if (DeducedType.isNull() && Keyword == AutoTypeKeyword::Auto && !IsDependent)
    return getAutoDeductType();

  void *InsertPos = nullptr;
  llvm::FoldingSetNodeID ID;
  AutoType::Profile(ID, *this, DeducedType, Keyword, IsDependent);
  if (AutoType *AT = AutoTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(AT, 0);

  QualType Canon;
  if (!IsCanon && !DeducedType.isNull())
    Canon = DeducedType.getCanonicalType();

  void *Mem = Allocate(sizeof(AutoType), alignof(AutoType));
  auto *AT =
      new (Mem) AutoType(DeducedType, Keyword,
                         IsDependent ? TypeDependence::DependentInstantiation
                                     : TypeDependence::None,
                         Canon);
  Types.push_back(AT);
  AutoTypes.InsertNode(AT, InsertPos);
  return QualType(AT, 0);
}

QualType TreeContext::getAutoType(QualType DeducedType, AutoTypeKeyword Keyword,
                                  bool IsDependent) const {
  assert((!IsDependent || DeducedType.isNull()) &&
         "A dependent auto should be undeduced");
  return getAutoTypeInternal(DeducedType, Keyword, IsDependent);
}

QualType TreeContext::getAtomicType(QualType T) const {
  // Unique pointers, to guarantee there is only one pointer of a particular
  // structure.
  llvm::FoldingSetNodeID ID;
  AtomicType::Profile(ID, T);

  void *InsertPos = nullptr;
  if (AtomicType *AT = AtomicTypes.FindNodeOrInsertPos(ID, InsertPos))
    return QualType(AT, 0);

  // If the atomic value type isn't canonical, this won't be a canonical type
  // either, so fill in the canonical type field.
  QualType Canonical;
  if (!T.isCanonical()) {
    Canonical = getAtomicType(getCanonicalType(T));

    AtomicType *NewIP = AtomicTypes.FindNodeOrInsertPos(ID, InsertPos);
    assert(!NewIP && "Shouldn't be in the map!");
    (void)NewIP;
  }
  auto *New = new (*this, alignof(AtomicType)) AtomicType(T, Canonical);
  Types.push_back(New);
  AtomicTypes.InsertNode(New, InsertPos);
  return QualType(New, 0);
}

QualType TreeContext::getAutoDeductType() const {
  if (AutoDeductTy.isNull())
    AutoDeductTy = QualType(new (*this, alignof(AutoType))
                                AutoType(QualType(), AutoTypeKeyword::Auto,
                                         TypeDependence::None, QualType()),
                            0);
  return AutoDeductTy;
}

QualType TreeContext::getTagDeclType(const TagDecl *Decl) const {
  assert(Decl);
  return getTypeDeclType(const_cast<TagDecl *>(Decl));
}

CanQualType TreeContext::getSizeType() const {
  return getFromTargetType(Target->getSizeType());
}

CanQualType TreeContext::getSignedSizeType() const {
  return getFromTargetType(Target->getSignedSizeType());
}

CanQualType TreeContext::getIntMaxType() const {
  return getFromTargetType(Target->getIntMaxType());
}

CanQualType TreeContext::getUIntMaxType() const {
  return getFromTargetType(Target->getUIntMaxType());
}

QualType TreeContext::getSignedWCharType() const { return WCharTy; }

QualType TreeContext::getUnsignedWCharType() const { return UnsignedIntTy; }

QualType TreeContext::getIntPtrType() const {
  return getFromTargetType(Target->getIntPtrType());
}

QualType TreeContext::getUIntPtrType() const {
  return getCorrespondingUnsignedType(getIntPtrType());
}

QualType TreeContext::getPointerDiffType() const {
  return getFromTargetType(Target->getPtrDiffType(LangAS::Default));
}

QualType TreeContext::getUnsignedPointerDiffType() const {
  return getFromTargetType(Target->getUnsignedPtrDiffType(LangAS::Default));
}

QualType TreeContext::getProcessIDType() const {
  return getFromTargetType(Target->getProcessIDType());
}

CanQualType TreeContext::getCanonicalParamType(QualType T) const {
  // Push qualifiers into arrays, and then discard any remaining
  // qualifiers.
  T = getCanonicalType(T);
  T = getVariableArrayDecayedType(T);
  const Type *Ty = T.getTypePtr();
  QualType Result;
  if (isa<ArrayType>(Ty)) {
    Result = getArrayDecayedType(QualType(Ty, 0));
  } else if (isa<FunctionType>(Ty)) {
    Result = getPointerType(QualType(Ty, 0));
  } else {
    Result = QualType(Ty, 0);
  }

  return CanQualType::CreateUnsafe(Result);
}

QualType TreeContext::getUnqualifiedArrayType(QualType type,
                                              Qualifiers &quals) {
  SplitQualType splitType = type.getSplitUnqualifiedType();

  const auto *AT =
      dyn_cast<ArrayType>(splitType.Ty->getUnqualifiedDesugaredType());

  // If we don't have an array, just use the results in splitType.
  if (!AT) {
    quals = splitType.Quals;
    return QualType(splitType.Ty, 0);
  }

  // Otherwise, recurse on the array's element type.
  QualType elementType = AT->getElementType();
  QualType unqualElementType = getUnqualifiedArrayType(elementType, quals);

  // If that didn't change the element type, AT has no qualifiers, so we
  // can just use the results in splitType.
  if (elementType == unqualElementType) {
    assert(quals.empty()); // from the recursive call
    quals = splitType.Quals;
    return QualType(splitType.Ty, 0);
  }

  // Otherwise, add in the qualifiers from the outermost type, then
  // build the type back up.
  quals.addConsistentQualifiers(splitType.Quals);

  if (const auto *CAT = dyn_cast<ConstantArrayType>(AT)) {
    return getConstantArrayType(unqualElementType, CAT->getSize(),
                                CAT->getSizeExpr(), CAT->getSizeModifier(), 0);
  }

  if (const auto *IAT = dyn_cast<IncompleteArrayType>(AT)) {
    return getIncompleteArrayType(unqualElementType, IAT->getSizeModifier(), 0);
  }

  if (const auto *VAT = dyn_cast<VariableArrayType>(AT)) {
    return getVariableArrayType(
        unqualElementType, VAT->getSizeExpr(), VAT->getSizeModifier(),
        VAT->getIndexTypeCVRQualifiers(), VAT->getBracketsRange());
  }

  llvm_unreachable("unexpected array type");
}

void TreeContext::UnwrapSimilarArrayTypes(QualType &T1, QualType &T2,
                                          bool AllowPiMismatch) {
  while (true) {
    auto *AT1 = getAsArrayType(T1);
    if (!AT1)
      return;

    auto *AT2 = getAsArrayType(T2);
    if (!AT2)
      return;

    // Same constant bound, both incomplete, or one complete constant array vs
    // one incomplete array.
    if (auto *CAT1 = dyn_cast<ConstantArrayType>(AT1)) {
      auto *CAT2 = dyn_cast<ConstantArrayType>(AT2);
      if (!(CAT2 && CAT1->getSize() == CAT2->getSize()))
        return;
    } else if (isa<IncompleteArrayType>(AT1)) {
      if (!isa<IncompleteArrayType>(AT2))
        return;
    } else {
      return;
    }

    T1 = AT1->getElementType();
    T2 = AT2->getElementType();
  }
}

bool TreeContext::UnwrapSimilarTypes(QualType &T1, QualType &T2,
                                     bool AllowPiMismatch) {
  UnwrapSimilarArrayTypes(T1, T2, AllowPiMismatch);

  const auto *T1PtrType = T1->getAs<PointerType>();
  const auto *T2PtrType = T2->getAs<PointerType>();
  if (T1PtrType && T2PtrType) {
    T1 = T1PtrType->getPointeeType();
    T2 = T2PtrType->getPointeeType();
    return true;
  }

  return false;
}

bool TreeContext::hasSimilarType(QualType T1, QualType T2) {
  while (true) {
    Qualifiers Quals;
    T1 = getUnqualifiedArrayType(T1, Quals);
    T2 = getUnqualifiedArrayType(T2, Quals);
    if (hasSameType(T1, T2))
      return true;
    if (!UnwrapSimilarTypes(T1, T2))
      return false;
  }
}

bool TreeContext::hasCvrSimilarType(QualType T1, QualType T2) {
  while (true) {
    Qualifiers Quals1, Quals2;
    T1 = getUnqualifiedArrayType(T1, Quals1);
    T2 = getUnqualifiedArrayType(T2, Quals2);

    Quals1.removeCVRQualifiers();
    Quals2.removeCVRQualifiers();
    if (Quals1 != Quals2)
      return false;

    if (hasSameType(T1, T2))
      return true;

    if (!UnwrapSimilarTypes(T1, T2, /*AllowPiMismatch*/ false))
      return false;
  }
}

namespace {
bool hasSameOverloadableAttrs(const FunctionDecl *A, const FunctionDecl *B) {
  // Note that pass_object_size attributes are represented in the function's
  // ExtParameterInfo, so we don't need to check them here.

  llvm::FoldingSetNodeID Cand1ID, Cand2ID;
  auto AEnableIfAttrs = A->specific_attrs<EnableIfAttr>();
  auto BEnableIfAttrs = B->specific_attrs<EnableIfAttr>();

  for (auto Pair : zip_longest(AEnableIfAttrs, BEnableIfAttrs)) {
    std::optional<EnableIfAttr *> Cand1A = std::get<0>(Pair);
    std::optional<EnableIfAttr *> Cand2A = std::get<1>(Pair);

    if (!Cand1A || !Cand2A)
      return false;

    Cand1ID.clear();
    Cand2ID.clear();

    (*Cand1A)->getCond()->Profile(Cand1ID, A->getTreeContext(), true);
    (*Cand2A)->getCond()->Profile(Cand2ID, B->getTreeContext(), true);

    // Return false if any of the enable_if expressions of A and B are
    // different.
    if (Cand1ID != Cand2ID)
      return false;
  }
  return true;
}
} // namespace

bool TreeContext::isSameEntity(const NamedDecl *X, const NamedDecl *Y) const {
  // Caution: this function is called by the AST reader during deserialization,
  // so it cannot rely on AST invariants being met. Non-trivial accessors
  // should be avoided, along with any traversal of redeclaration chains.

  if (X == Y)
    return true;

  if (X->getDeclName() != Y->getDeclName())
    return false;

  // Must be in the same context.
  //
  // Note that we can't use DeclContext::Equals here, because the DeclContexts
  // could be two different declarations of the same function. (We will fix the
  // semantic DC to refer to the primary definition after merging.)
  if (!declaresSameEntity(cast<Decl>(X->getDeclContext()->getRedeclContext()),
                          cast<Decl>(Y->getDeclContext()->getRedeclContext())))
    return false;

  // Two typedefs refer to the same entity if they have the same underlying
  // type.
  if (const auto *TypedefX = dyn_cast<TypedefNameDecl>(X))
    if (const auto *TypedefY = dyn_cast<TypedefNameDecl>(Y))
      return hasSameType(TypedefX->getUnderlyingType(),
                         TypedefY->getUnderlyingType());

  // Must have the same kind.
  if (X->getKind() != Y->getKind())
    return false;

  // Compatible tags match.
  if (const auto *TagX = dyn_cast<TagDecl>(X)) {
    const auto *TagY = cast<TagDecl>(Y);
    return TagX->getTagKind() == TagY->getTagKind();
  }

  // Functions with the same type and linkage match.
  if (const auto *FuncX = dyn_cast<FunctionDecl>(X)) {
    const auto *FuncY = cast<FunctionDecl>(Y);

    if (FuncX->isMultiVersion() != FuncY->isMultiVersion())
      return false;

    // Multiversioned functions with different feature strings are represented
    // as separate declarations.
    if (FuncX->isMultiVersion()) {
      const auto *TAX = FuncX->getAttr<TargetAttr>();
      const auto *TAY = FuncY->getAttr<TargetAttr>();
      assert(TAX && TAY && "Multiversion Function without target attribute");

      if (TAX->getFeaturesStr() != TAY->getFeaturesStr())
        return false;
    }

    auto GetTypeAsWritten = [](const FunctionDecl *FD) {
      // Map to the first declaration that we've already merged into this one.
      // The TSI of redeclarations might not match (due to calling conventions
      // being inherited onto the type but not the TSI), but the TSI type of
      // the first declaration of the function should match across modules.
      FD = FD->getCanonicalDecl();
      return FD->getTypeSourceInfo() ? FD->getTypeSourceInfo()->getType()
                                     : FD->getType();
    };
    QualType XT = GetTypeAsWritten(FuncX), YT = GetTypeAsWritten(FuncY);
    if (!hasSameType(XT, YT))
      return false;

    return FuncX->getLinkageInternal() == FuncY->getLinkageInternal() &&
           hasSameOverloadableAttrs(FuncX, FuncY);
  }

  // Variables with the same type and linkage match.
  if (const auto *VarX = dyn_cast<VarDecl>(X)) {
    const auto *VarY = cast<VarDecl>(Y);
    if (VarX->getLinkageInternal() == VarY->getLinkageInternal()) {
      // During deserialization, we might compare variables before we load
      // their types. Assume the types will end up being the same.
      if (VarX->getType().isNull() || VarY->getType().isNull())
        return true;

      if (hasSameType(VarX->getType(), VarY->getType()))
        return true;

      // We can get decls with different types on the redecl chain when
      // completing an incomplete array type; compare element types.
      const ArrayType *VarXTy = getAsArrayType(VarX->getType());
      const ArrayType *VarYTy = getAsArrayType(VarY->getType());
      if (!VarXTy || !VarYTy)
        return false;
      if (VarXTy->isIncompleteArrayType() || VarYTy->isIncompleteArrayType())
        return hasSameType(VarXTy->getElementType(), VarYTy->getElementType());
    }
    return false;
  }

  // Fields with the same name and the same type match.
  if (const auto *FDX = dyn_cast<FieldDecl>(X)) {
    const auto *FDY = cast<FieldDecl>(Y);
    return hasSameType(FDX->getType(), FDY->getType());
  }

  // Indirect fields with the same target field match.
  if (const auto *IFDX = dyn_cast<IndirectFieldDecl>(X)) {
    const auto *IFDY = cast<IndirectFieldDecl>(Y);
    return IFDX->getAnonField()->getCanonicalDecl() ==
           IFDY->getAnonField()->getCanonicalDecl();
  }

  // Enumerators with the same name match.
  if (isa<EnumConstantDecl>(X))
    return true;

  return false;
}

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
