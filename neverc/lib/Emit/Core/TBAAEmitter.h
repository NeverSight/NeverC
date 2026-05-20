#ifndef NEVERC_LIB_EMIT_CORE_TBAAEMITTER_H
#define NEVERC_LIB_EMIT_CORE_TBAAEMITTER_H

#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"

namespace neverc {
class TreeContext;
class CodeGenOptions;
class LangOptions;
class QualType;
class Type;

namespace Emit {

// TBAAAccessKind - A kind of TBAA memory access descriptor.
enum class TBAAAccessKind : unsigned {
  Ordinary,
  MayAlias,
  Incomplete,
};

// TBAAAccessInfo - Describes a memory access in terms of TBAA.
struct TBAAAccessInfo {
  TBAAAccessInfo(TBAAAccessKind Kind, llvm::MDNode *BaseType,
                 llvm::MDNode *AccessType, uint64_t Offset, uint64_t Size)
      : Kind(Kind), BaseType(BaseType), AccessType(AccessType), Offset(Offset),
        Size(Size) {}

  TBAAAccessInfo(llvm::MDNode *BaseType, llvm::MDNode *AccessType,
                 uint64_t Offset, uint64_t Size)
      : TBAAAccessInfo(TBAAAccessKind::Ordinary, BaseType, AccessType, Offset,
                       Size) {}

  explicit TBAAAccessInfo(llvm::MDNode *AccessType, uint64_t Size)
      : TBAAAccessInfo(/* BaseType= */ nullptr, AccessType, /* Offset= */ 0,
                       Size) {}

  TBAAAccessInfo() : TBAAAccessInfo(/* AccessType= */ nullptr, /* Size= */ 0) {}

  static TBAAAccessInfo getMayAliasInfo() {
    return TBAAAccessInfo(TBAAAccessKind::MayAlias,
                          /* BaseType= */ nullptr, /* AccessType= */ nullptr,
                          /* Offset= */ 0, /* Size= */ 0);
  }

  bool isMayAlias() const { return Kind == TBAAAccessKind::MayAlias; }

  static TBAAAccessInfo getIncompleteInfo() {
    return TBAAAccessInfo(TBAAAccessKind::Incomplete,
                          /* BaseType= */ nullptr, /* AccessType= */ nullptr,
                          /* Offset= */ 0, /* Size= */ 0);
  }

  bool isIncomplete() const { return Kind == TBAAAccessKind::Incomplete; }

  bool operator==(const TBAAAccessInfo &Other) const {
    return Kind == Other.Kind && BaseType == Other.BaseType &&
           AccessType == Other.AccessType && Offset == Other.Offset &&
           Size == Other.Size;
  }

  bool operator!=(const TBAAAccessInfo &Other) const {
    return !(*this == Other);
  }

  explicit operator bool() const { return *this != TBAAAccessInfo(); }

  TBAAAccessKind Kind;

  llvm::MDNode *BaseType;

  llvm::MDNode *AccessType;

  uint64_t Offset;

  uint64_t Size;
};

class TBAAEmitter {
  TreeContext &Context;
  const CodeGenOptions &CodeGenOpts;
  const LangOptions &Features;

  // MDHelper - Helper for creating metadata.
  llvm::MDBuilder MDHelper;

  llvm::DenseMap<const Type *, llvm::MDNode *> MetadataCache;
  llvm::DenseMap<const Type *, llvm::MDNode *> BaseTypeMetadataCache;
  llvm::DenseMap<TBAAAccessInfo, llvm::MDNode *> AccessTagMetadataCache;

  llvm::DenseMap<const Type *, llvm::MDNode *> StructMetadataCache;

  llvm::MDNode *Root;
  llvm::MDNode *Char;

  llvm::MDNode *getRoot();

  llvm::MDNode *getChar();

  bool
  collectFields(uint64_t BaseOffset, QualType Ty,
                llvm::SmallVectorImpl<llvm::MDBuilder::TBAAStructField> &Fields,
                bool MayAlias);

  llvm::MDNode *createScalarTypeNode(llvm::StringRef Name, llvm::MDNode *Parent,
                                     uint64_t Size);

  llvm::MDNode *getTypeInfoHelper(const Type *Ty);

  llvm::MDNode *getBaseTypeInfoHelper(const Type *Ty);

public:
  TBAAEmitter(TreeContext &Ctx, llvm::LLVMContext &VMContext,
              const CodeGenOptions &CGO, const LangOptions &Features);
  ~TBAAEmitter();

  llvm::MDNode *getTypeInfo(QualType QTy);

  TBAAAccessInfo getAccessInfo(QualType AccessType);

  llvm::MDNode *getTBAAStructInfo(QualType QTy);

  llvm::MDNode *getBaseTypeInfo(QualType QTy);

  llvm::MDNode *getAccessTagInfo(TBAAAccessInfo Info);

  TBAAAccessInfo mergeTBAAInfoForCast(TBAAAccessInfo SourceInfo,
                                      TBAAAccessInfo TargetInfo);

  TBAAAccessInfo mergeTBAAInfoForConditionalOperator(TBAAAccessInfo InfoA,
                                                     TBAAAccessInfo InfoB);

  TBAAAccessInfo mergeTBAAInfoForMemoryTransfer(TBAAAccessInfo DestInfo,
                                                TBAAAccessInfo SrcInfo);
};

} // end namespace Emit
} // end namespace neverc

namespace llvm {

template <> struct DenseMapInfo<neverc::Emit::TBAAAccessInfo> {
  static neverc::Emit::TBAAAccessInfo getEmptyKey() {
    unsigned UnsignedKey = DenseMapInfo<unsigned>::getEmptyKey();
    return neverc::Emit::TBAAAccessInfo(
        static_cast<neverc::Emit::TBAAAccessKind>(UnsignedKey),
        DenseMapInfo<MDNode *>::getEmptyKey(),
        DenseMapInfo<MDNode *>::getEmptyKey(),
        DenseMapInfo<uint64_t>::getEmptyKey(),
        DenseMapInfo<uint64_t>::getEmptyKey());
  }

  static neverc::Emit::TBAAAccessInfo getTombstoneKey() {
    unsigned UnsignedKey = DenseMapInfo<unsigned>::getTombstoneKey();
    return neverc::Emit::TBAAAccessInfo(
        static_cast<neverc::Emit::TBAAAccessKind>(UnsignedKey),
        DenseMapInfo<MDNode *>::getTombstoneKey(),
        DenseMapInfo<MDNode *>::getTombstoneKey(),
        DenseMapInfo<uint64_t>::getTombstoneKey(),
        DenseMapInfo<uint64_t>::getTombstoneKey());
  }

  static unsigned getHashValue(const neverc::Emit::TBAAAccessInfo &Val) {
    auto KindValue = static_cast<unsigned>(Val.Kind);
    return DenseMapInfo<unsigned>::getHashValue(KindValue) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.BaseType) ^
           DenseMapInfo<MDNode *>::getHashValue(Val.AccessType) ^
           DenseMapInfo<uint64_t>::getHashValue(Val.Offset) ^
           DenseMapInfo<uint64_t>::getHashValue(Val.Size);
  }

  static bool isEqual(const neverc::Emit::TBAAAccessInfo &LHS,
                      const neverc::Emit::TBAAAccessInfo &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

#endif
