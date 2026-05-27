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

