#include "Core/TBAAEmitter.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
using namespace neverc;
using namespace Emit;

// ===----------------------------------------------------------------------===
// Construction & core type info
// ===----------------------------------------------------------------------===

TBAAEmitter::TBAAEmitter(TreeContext &Ctx, llvm::LLVMContext &VMContext,
                         const CodeGenOptions &CGO, const LangOptions &Features)
    : Context(Ctx), CodeGenOpts(CGO), Features(Features), MDHelper(VMContext),
      Root(nullptr), Char(nullptr) {}

TBAAEmitter::~TBAAEmitter() {}

llvm::MDNode *TBAAEmitter::getRoot() {
  // Define the root of the tree. This identifies the tree, so that
  // if our LLVM IR is linked with LLVM IR from a different front-end
  // (or a different version of this front-end), their TBAA trees will
  // remain distinct, and the optimizer will treat them conservatively.
  if (!Root)
    Root = MDHelper.createTBAARoot("Simple C TBAA");

  return Root;
}

llvm::MDNode *TBAAEmitter::createScalarTypeNode(llvm::StringRef Name,
                                                llvm::MDNode *Parent,
                                                uint64_t Size) {
  if (CodeGenOpts.NewStructPathTBAA) {
    llvm::Metadata *Id = MDHelper.createString(Name);
    return MDHelper.createTBAATypeNode(Parent, Size, Id);
  }
  return MDHelper.createTBAAScalarTypeNode(Name, Parent);
}

llvm::MDNode *TBAAEmitter::getChar() {
  // char may alias any user-accessible memory (strict aliasing root).
  if (!Char)
    Char = createScalarTypeNode("omnipotent char", getRoot(), /* Size= */ 1);

  return Char;
}

namespace {

LLVM_ATTRIBUTE_ALWAYS_INLINE
bool typeHasMayAlias(QualType QTy) {
  if (auto *TD = QTy->getAsTagDecl())
    if (LLVM_UNLIKELY(TD->hasAttr<MayAliasAttr>()))
      return true;

  while (auto *TT = QTy->getAs<TypedefType>()) {
    if (LLVM_UNLIKELY(TT->getDecl()->hasAttr<MayAliasAttr>()))
      return true;
    QTy = TT->desugar();
  }
  return false;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
bool isValidBaseType(QualType QTy) {
  const auto *TTy = QTy->getAs<RecordType>();
  if (LLVM_UNLIKELY(!TTy))
    return false;
  const RecordDecl *RD = TTy->getDecl()->getDefinition();
  return RD && !RD->hasFlexibleArrayMember() && RD->isStruct();
}

} // namespace

llvm::MDNode *TBAAEmitter::getTypeInfoHelper(const Type *Ty) {
  uint64_t Size = Context.getTypeSizeInChars(Ty).getQuantity();

  if (const BuiltinType *BTy = dyn_cast<BuiltinType>(Ty)) {
    switch (BTy->getKind()) {
    // Character types are special and can alias anything (C treats plain,
    // signed, and unsigned char as character types for aliasing).
    case BuiltinType::Char_U:
    case BuiltinType::Char_S:
    case BuiltinType::UChar:
    case BuiltinType::SChar:
      return getChar();

    // Unsigned types can alias their corresponding signed types.
    case BuiltinType::UShort:
      return getTypeInfo(Context.ShortTy);
    case BuiltinType::UInt:
      return getTypeInfo(Context.IntTy);
    case BuiltinType::ULong:
      return getTypeInfo(Context.LongTy);
    case BuiltinType::ULongLong:
      return getTypeInfo(Context.LongLongTy);
    case BuiltinType::UInt128:
      return getTypeInfo(Context.Int128Ty);

    case BuiltinType::UShortFract:
      return getTypeInfo(Context.ShortFractTy);
    case BuiltinType::UFract:
      return getTypeInfo(Context.FractTy);
    case BuiltinType::ULongFract:
      return getTypeInfo(Context.LongFractTy);

    case BuiltinType::SatUShortFract:
      return getTypeInfo(Context.SatShortFractTy);
    case BuiltinType::SatUFract:
      return getTypeInfo(Context.SatFractTy);
    case BuiltinType::SatULongFract:
      return getTypeInfo(Context.SatLongFractTy);

    case BuiltinType::UShortAccum:
      return getTypeInfo(Context.ShortAccumTy);
    case BuiltinType::UAccum:
      return getTypeInfo(Context.AccumTy);
    case BuiltinType::ULongAccum:
      return getTypeInfo(Context.LongAccumTy);

    case BuiltinType::SatUShortAccum:
      return getTypeInfo(Context.SatShortAccumTy);
    case BuiltinType::SatUAccum:
      return getTypeInfo(Context.SatAccumTy);
    case BuiltinType::SatULongAccum:
      return getTypeInfo(Context.SatLongAccumTy);

    // Treat all other builtin types as distinct types. This includes
    // treating wchar_t, char16_t, and char32_t as distinct from their
    // "underlying types".
    default:
      return createScalarTypeNode(BTy->getName(Features), getChar(), Size);
    }
  }

  if (Ty->isPointerType())
    return createScalarTypeNode("any pointer", getChar(), Size);

  // Accesses to arrays are accesses to objects of their element types.
  if (CodeGenOpts.NewStructPathTBAA && Ty->isArrayType())
    return getTypeInfo(cast<ArrayType>(Ty)->getElementType());

  // Enum types are distinct for TBAA (not a typedef of the integer type).
  if (const EnumType *ETy = dyn_cast<EnumType>(Ty))
    return getTypeInfo(ETy->getDecl()->getIntegerType());

  if (const auto *EIT = dyn_cast<BitIntType>(Ty)) {
    llvm::SmallString<256> OutName;
    llvm::raw_svector_ostream Out(OutName);
    // Don't specify signed/unsigned since integer types can alias despite sign
    // differences.
    Out << "_BitInt(" << EIT->getNumBits() << ')';
    return createScalarTypeNode(OutName, getChar(), Size);
  }

  // For now, handle any other kind of type conservatively.
  return getChar();
}

llvm::MDNode *TBAAEmitter::getTypeInfo(QualType QTy) {
  if (LLVM_UNLIKELY(CodeGenOpts.OptimizationLevel == 0 ||
                    CodeGenOpts.RelaxedAliasing))
    return nullptr;

  if (LLVM_UNLIKELY(typeHasMayAlias(QTy)))
    return getChar();

  if (LLVM_UNLIKELY(isValidBaseType(QTy)))
    return getBaseTypeInfo(QTy);

  const Type *Ty = Context.getCanonicalType(QTy).getTypePtr();

  auto It = MetadataCache.find(Ty);
  if (LLVM_LIKELY(It != MetadataCache.end()))
    return It->second;

  llvm::MDNode *TypeNode = getTypeInfoHelper(Ty);
  MetadataCache[Ty] = TypeNode;
  return TypeNode;
}

// ===----------------------------------------------------------------------===
// Access info & struct path TBAA
// ===----------------------------------------------------------------------===

TBAAAccessInfo TBAAEmitter::getAccessInfo(QualType AccessType) {
  // Pointee values may have incomplete types, but they shall never be
  // dereferenced.
  if (AccessType->isIncompleteType())
    return TBAAAccessInfo::getIncompleteInfo();

  if (typeHasMayAlias(AccessType))
    return TBAAAccessInfo::getMayAliasInfo();

  uint64_t Size = Context.getTypeSizeInChars(AccessType).getQuantity();
  return TBAAAccessInfo(getTypeInfo(AccessType), Size);
}

bool TBAAEmitter::collectFields(
    uint64_t BaseOffset, QualType QTy,
    llvm::SmallVectorImpl<llvm::MDBuilder::TBAAStructField> &Fields,
    bool MayAlias) {
  /* Not handled yet: inheritance layout, bitfields, ... */

  if (const RecordType *TTy = QTy->getAs<RecordType>()) {
    const RecordDecl *RD = TTy->getDecl()->getDefinition();
    if (RD->hasFlexibleArrayMember())
      return false;

    const StructRecordLayout &Layout = Context.getStructRecordLayout(RD);

    unsigned idx = 0;
    for (const auto *FD : RD->fields()) {
      if (FD->isZeroSize(Context) || FD->isUnnamedBitfield()) {
        ++idx;
        continue;
      }
      uint64_t Offset =
          BaseOffset + Layout.getFieldOffset(idx) / Context.getCharWidth();
      QualType FieldQTy = FD->getType();
      ++idx;
      if (!collectFields(Offset, FieldQTy, Fields,
                         MayAlias || typeHasMayAlias(FieldQTy)))
        return false;
    }
    return true;
  }

  /* Otherwise, treat whatever it is as a field. */
  uint64_t Offset = BaseOffset;
  uint64_t Size = Context.getTypeSizeInChars(QTy).getQuantity();
  llvm::MDNode *TBAAType = MayAlias ? getChar() : getTypeInfo(QTy);
  llvm::MDNode *TBAATag = getAccessTagInfo(TBAAAccessInfo(TBAAType, Size));
  Fields.push_back(llvm::MDBuilder::TBAAStructField(Offset, Size, TBAATag));
  return true;
}

llvm::MDNode *TBAAEmitter::getTBAAStructInfo(QualType QTy) {
  const Type *Ty = Context.getCanonicalType(QTy).getTypePtr();

  if (llvm::MDNode *N = StructMetadataCache[Ty])
    return N;

  llvm::SmallVector<llvm::MDBuilder::TBAAStructField, 4> Fields;
  if (collectFields(0, QTy, Fields, typeHasMayAlias(QTy)))
    return MDHelper.createTBAAStructNode(Fields);

  // For now, handle any other kind of type conservatively.
  return StructMetadataCache[Ty] = nullptr;
}

llvm::MDNode *TBAAEmitter::getBaseTypeInfoHelper(const Type *Ty) {
  if (auto *TTy = dyn_cast<RecordType>(Ty)) {
    const RecordDecl *RD = TTy->getDecl()->getDefinition();
    const StructRecordLayout &Layout = Context.getStructRecordLayout(RD);
    using TBAAStructField = llvm::MDBuilder::TBAAStructField;
    llvm::SmallVector<TBAAStructField, 4> Fields;
    for (FieldDecl *Field : RD->fields()) {
      if (Field->isZeroSize(Context) || Field->isUnnamedBitfield())
        continue;
      QualType FieldQTy = Field->getType();
      llvm::MDNode *TypeNode = isValidBaseType(FieldQTy)
                                   ? getBaseTypeInfo(FieldQTy)
                                   : getTypeInfo(FieldQTy);
      if (!TypeNode)
        return nullptr;

      uint64_t BitOffset = Layout.getFieldOffset(Field->getFieldIndex());
      uint64_t Offset = Context.toCharUnitsFromBits(BitOffset).getQuantity();
      uint64_t Size = Context.getTypeSizeInChars(FieldQTy).getQuantity();
      Fields.push_back(
          llvm::MDBuilder::TBAAStructField(Offset, Size, TypeNode));
    }

    llvm::SmallString<256> OutName = RD->getName();

    if (CodeGenOpts.NewStructPathTBAA) {
      llvm::MDNode *Parent = getChar();
      uint64_t Size = Context.getTypeSizeInChars(Ty).getQuantity();
      llvm::Metadata *Id = MDHelper.createString(OutName);
      return MDHelper.createTBAATypeNode(Parent, Size, Id, Fields);
    }

    llvm::SmallVector<std::pair<llvm::MDNode *, uint64_t>, 4> OffsetsAndTypes;
    for (const auto &Field : Fields)
      OffsetsAndTypes.push_back(std::make_pair(Field.Type, Field.Offset));
    return MDHelper.createTBAAStructTypeNode(OutName, OffsetsAndTypes);
  }

  return nullptr;
}

llvm::MDNode *TBAAEmitter::getBaseTypeInfo(QualType QTy) {
  if (!isValidBaseType(QTy))
    return nullptr;

  const Type *Ty = Context.getCanonicalType(QTy).getTypePtr();

  // nullptr is a valid value in the cache, so use find rather than []
  auto I = BaseTypeMetadataCache.find(Ty);
  if (I != BaseTypeMetadataCache.end())
    return I->second;

  // First calculate the metadata, before recomputing the insertion point, as
  // the helper can recursively call us.
  llvm::MDNode *TypeNode = getBaseTypeInfoHelper(Ty);
  LLVM_ATTRIBUTE_UNUSED auto inserted =
      BaseTypeMetadataCache.insert({Ty, TypeNode});
  assert(inserted.second && "BaseType metadata was already inserted");

  return TypeNode;
}

// ===----------------------------------------------------------------------===
// Access tags & TBAA merge
// ===----------------------------------------------------------------------===

llvm::MDNode *TBAAEmitter::getAccessTagInfo(TBAAAccessInfo Info) {
  assert(!Info.isIncomplete() && "Access to an object of an incomplete type!");

  if (Info.isMayAlias())
    Info = TBAAAccessInfo(getChar(), Info.Size);

  if (!Info.AccessType)
    return nullptr;

  if (!CodeGenOpts.StructPathTBAA)
    Info = TBAAAccessInfo(Info.AccessType, Info.Size);

  llvm::MDNode *&N = AccessTagMetadataCache[Info];
  if (N)
    return N;

  if (!Info.BaseType) {
    Info.BaseType = Info.AccessType;
    assert(!Info.Offset && "Nonzero offset for an access with no base type!");
  }
  if (CodeGenOpts.NewStructPathTBAA) {
    return N = MDHelper.createTBAAAccessTag(Info.BaseType, Info.AccessType,
                                            Info.Offset, Info.Size);
  }
  return N = MDHelper.createTBAAStructTagNode(Info.BaseType, Info.AccessType,
                                              Info.Offset);
}

TBAAAccessInfo TBAAEmitter::mergeTBAAInfoForCast(TBAAAccessInfo SourceInfo,
                                                 TBAAAccessInfo TargetInfo) {
  if (SourceInfo.isMayAlias() || TargetInfo.isMayAlias())
    return TBAAAccessInfo::getMayAliasInfo();
  return TargetInfo;
}

TBAAAccessInfo
TBAAEmitter::mergeTBAAInfoForConditionalOperator(TBAAAccessInfo InfoA,
                                                 TBAAAccessInfo InfoB) {
  if (InfoA == InfoB)
    return InfoA;

  if (!InfoA || !InfoB)
    return TBAAAccessInfo();

  if (InfoA.isMayAlias() || InfoB.isMayAlias())
    return TBAAAccessInfo::getMayAliasInfo();

  return TBAAAccessInfo::getMayAliasInfo();
}

TBAAAccessInfo
TBAAEmitter::mergeTBAAInfoForMemoryTransfer(TBAAAccessInfo DestInfo,
                                            TBAAAccessInfo SrcInfo) {
  if (DestInfo == SrcInfo)
    return DestInfo;

  if (!DestInfo || !SrcInfo)
    return TBAAAccessInfo();

  if (DestInfo.isMayAlias() || SrcInfo.isMayAlias())
    return TBAAAccessInfo::getMayAliasInfo();

  return TBAAAccessInfo::getMayAliasInfo();
}
