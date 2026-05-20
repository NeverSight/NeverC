#ifndef NEVERC_TREE_ASTCONTEXT_H
#define NEVERC_TREE_ASTCONTEXT_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Diagnostic/PartialDiagnostic.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/PrettyPrinter.h"
#include "neverc/Tree/Core/TreeFwd.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Tree/Type/CanonicalType.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/TypeSize.h"
#include <optional>

namespace llvm {

class APFixedPoint;
class FixedPointSemantics;
struct fltSemantics;
template <typename T, unsigned N> class SmallPtrSet;

} // namespace llvm

namespace neverc {

using llvm::dyn_cast_or_null;

class TreeMutationListener;
class StructRecordLayout;
class AtomicExpr;
class CharUnits;
class DiagnosticsEngine;
class Expr;
enum class FloatModeKind;
class GlobalDecl;
class IdentifierTable;
class LangOptions;
class MangleContext;
struct ParsedTargetAttr;
class StoredDeclsMap;
class TargetAttr;
class TargetInfo;

namespace Builtin {

class Context;

} // namespace Builtin

enum class AlignRequirementKind {
  None,

  RequiredByTypedef,

  RequiredByRecord,

  RequiredByEnum,
};

struct TypeInfo {
  uint64_t Width = 0;
  unsigned Align = 0;
  AlignRequirementKind AlignRequirement;

  TypeInfo() : AlignRequirement(AlignRequirementKind::None) {}
  TypeInfo(uint64_t Width, unsigned Align,
           AlignRequirementKind AlignRequirement)
      : Width(Width), Align(Align), AlignRequirement(AlignRequirement) {}
  bool isAlignRequired() {
    return AlignRequirement != AlignRequirementKind::None;
  }
};

struct TypeInfoChars {
  CharUnits Width;
  CharUnits Align;
  AlignRequirementKind AlignRequirement;

  TypeInfoChars() : AlignRequirement(AlignRequirementKind::None) {}
  TypeInfoChars(CharUnits Width, CharUnits Align,
                AlignRequirementKind AlignRequirement)
      : Width(Width), Align(Align), AlignRequirement(AlignRequirement) {}
  bool isAlignRequired() {
    return AlignRequirement != AlignRequirementKind::None;
  }
};

class TreeContext : public llvm::RefCountedBase<TreeContext> {
  mutable llvm::SmallVector<Type *, 0> Types;
  mutable llvm::FoldingSet<ExtQuals> ExtQualNodes;
  mutable llvm::FoldingSet<ComplexType> ComplexTypes;
  mutable llvm::FoldingSet<PointerType> PointerTypes{GeneralTypesLog2InitSize};
  mutable llvm::FoldingSet<AdjustedType> AdjustedTypes;
  mutable llvm::ContextualFoldingSet<ConstantArrayType, TreeContext &>
      ConstantArrayTypes;
  mutable llvm::FoldingSet<IncompleteArrayType> IncompleteArrayTypes;
  mutable std::vector<VariableArrayType *> VariableArrayTypes;
  mutable llvm::FoldingSet<VectorType> VectorTypes;
  mutable llvm::FoldingSet<ConstantMatrixType> MatrixTypes;
  mutable llvm::FoldingSet<FunctionNoProtoType> FunctionNoProtoTypes;
  mutable llvm::ContextualFoldingSet<FunctionProtoType, TreeContext &>
      FunctionProtoTypes;
  mutable llvm::ContextualFoldingSet<DependentTypeOfExprType, TreeContext &>
      DependentTypeOfExprTypes;
  mutable llvm::FoldingSet<ParenType> ParenTypes{GeneralTypesLog2InitSize};
  mutable llvm::FoldingSet<TypedefType> TypedefTypes{GeneralTypesLog2InitSize};
  mutable llvm::FoldingSet<ElaboratedType> ElaboratedTypes{
      GeneralTypesLog2InitSize};
  mutable llvm::ContextualFoldingSet<AutoType, TreeContext &> AutoTypes;
  mutable llvm::FoldingSet<AtomicType> AtomicTypes;
  mutable llvm::FoldingSet<AttributedType> AttributedTypes;
  mutable llvm::FoldingSet<BitIntType> BitIntTypes;
  llvm::FoldingSet<BTFTagAttributedType> BTFTagAttributedTypes;

  mutable llvm::DenseMap<const RecordDecl *, const StructRecordLayout *>
      StructRecordLayouts;

  using TypeInfoMap = llvm::DenseMap<const Type *, struct TypeInfo>;
  mutable TypeInfoMap MemoizedTypeInfo;

  using UnadjustedAlignMap = llvm::DenseMap<const Type *, unsigned>;
  mutable UnadjustedAlignMap MemoizedUnadjustedAlign;

  mutable llvm::StringMap<StringLiteral *> StringLiteralCache;

  mutable std::string CUIDHash;

  mutable TypedefDecl *Int128Decl = nullptr;

  mutable TypedefDecl *UInt128Decl = nullptr;

  mutable TypedefDecl *BuiltinVaListDecl = nullptr;

  mutable TypedefDecl *BuiltinMSVaListDecl = nullptr;

  mutable IdentifierInfo *BoolName = nullptr;

  TypeDecl *FILEDecl = nullptr;

  TypeDecl *jmp_bufDecl = nullptr;

  TypeDecl *sigjmp_bufDecl = nullptr;

  TypeDecl *ucontext_tDecl = nullptr;

  llvm::DenseMap<const Decl *, AttrVec *> DeclAttrs;

  static constexpr unsigned ConstantArrayTypesLog2InitSize = 8;
  static constexpr unsigned GeneralTypesLog2InitSize = 9;
  static constexpr unsigned FunctionProtoTypesLog2InitSize = 12;

  TreeContext &this_() { return *this; }

private:
  llvm::MapVector<const NamedDecl *, unsigned> MangleNumbers;
  llvm::MapVector<const VarDecl *, unsigned> StaticLocalNumbers;

  using ParameterIndexTable = llvm::DenseMap<const VarDecl *, unsigned>;
  ParameterIndexTable ParamIndices;

  TranslationUnitDecl *TUDecl = nullptr;
  mutable ExternCContextDecl *ExternCContext = nullptr;

  SourceManager &SourceMgr;

  LangOptions &LangOpts;

  mutable llvm::BumpPtrAllocator BumpAlloc;

  PartialDiagnostic::DiagStorageAllocator DiagAllocator;

  bool AddrSpaceMapMangling;

  const TargetInfo *Target = nullptr;
  neverc::PrintingPolicy PrintingPolicy;

  DeclListNode *ListNodeFreeList = nullptr;

public:
  IdentifierTable &Idents;
  Builtin::Context &BuiltinInfo;
  TreeMutationListener *Listener = nullptr;

  // A traversal scope limits the parts of the AST visible to certain analyses.
  // RecursiveTreeVisitor only visits specified children of TranslationUnitDecl.
  //
  // The scope is defined by a set of "top-level" declarations which will be
  // visible under the TranslationUnitDecl.
  // Initially, it is the entire TU, represented by {getTranslationUnitDecl()}.
  //
  // After setTraversalScope({foo, bar}), the exposed AST looks like:
  // TranslationUnitDecl
  //  - foo
  //    - ...
  //  - bar
  //    - ...
  // All other siblings of foo and bar are pruned from the tree.
  // (However they are still accessible via TranslationUnitDecl->decls())
  std::vector<Decl *> getTraversalScope() const { return TraversalScope; }
  void setTraversalScope(const std::vector<Decl *> &);

  const neverc::PrintingPolicy &getPrintingPolicy() const {
    return PrintingPolicy;
  }

  void setPrintingPolicy(const neverc::PrintingPolicy &Policy) {
    PrintingPolicy = Policy;
  }

  SourceManager &getSourceManager() { return SourceMgr; }
  const SourceManager &getSourceManager() const { return SourceMgr; }

  // Cleans up some of the data structures. This allows us to do cleanup
  // normally done in the destructor earlier. Renders much of the TreeContext
  // unusable, mostly the actual AST nodes, so should be called when we no
  // longer need access to the AST.
  void cleanup();

  llvm::BumpPtrAllocator &getAllocator() const { return BumpAlloc; }

  void *Allocate(size_t Size, unsigned Align = 8) const {
    return BumpAlloc.Allocate(Size, Align);
  }
  template <typename T> T *Allocate(size_t Num = 1) const {
    return static_cast<T *>(Allocate(Num * sizeof(T), alignof(T)));
  }
  void Deallocate(void *Ptr) const {}

  DeclListNode *AllocateDeclListNode(neverc::NamedDecl *ND) {
    if (DeclListNode *Alloc = ListNodeFreeList) {
      ListNodeFreeList = Alloc->Rest.dyn_cast<DeclListNode *>();
      Alloc->D = ND;
      Alloc->Rest = nullptr;
      return Alloc;
    }
    return new (*this) DeclListNode(ND);
  }
  void DeallocateDeclListNode(DeclListNode *N) {
    N->Rest = ListNodeFreeList;
    ListNodeFreeList = N;
  }

  size_t getAllocatedMemory() const { return BumpAlloc.getTotalMemory(); }

  size_t getSideTableAllocatedMemory() const;

  PartialDiagnostic::DiagStorageAllocator &getDiagAllocator() {
    return DiagAllocator;
  }

  const TargetInfo &getTargetInfo() const { return *Target; }

  QualType getIntTypeForBitwidth(unsigned DestWidth, unsigned Signed) const;

  QualType getRealTypeForBitwidth(unsigned DestWidth,
                                  FloatModeKind ExplicitType) const;

  bool AtomicUsesUnsupportedLibcall(const AtomicExpr *E) const;

  const LangOptions &getLangOpts() const { return LangOpts; }

  // If this condition is false, typo correction must be performed eagerly
  // rather than delayed in many places, as it makes use of dependent types.
  // This tracks RecoveryAST (off when there is no recovery/dependent-type
  // path).
  bool isDependenceAllowed() const { return LangOpts.RecoveryAST; }

  DiagnosticsEngine &getDiagnostics() const;

  FullSourceLoc getFullLoc(SourceLocation Loc) const {
    return FullSourceLoc(Loc, SourceMgr);
  }

public:
  AttrVec &getDeclAttrs(const Decl *D);

  void eraseDeclAttrs(const Decl *D);

  TranslationUnitDecl *getTranslationUnitDecl() const { return TUDecl; }
  void addTranslationUnitDecl() {
    assert(!TUDecl);
    TUDecl = TranslationUnitDecl::Create(*this);
    TraversalScope = {TUDecl};
  }

  ExternCContextDecl *getExternCContextDecl() const;

  // Builtin Types.
  CanQualType VoidTy;
  CanQualType BoolTy;
  CanQualType CharTy;
  CanQualType WCharTy;    // Target wide character type.
  CanQualType WideCharTy; // Often matches WCharTy; integer type in C99.
  CanQualType
      WIntTy; // [C99 7.24.1], integer type unchanged by default promotions.
  CanQualType Char8Ty;  // UTF-8 code unit type (char8_t).
  CanQualType Char16Ty; // char16_t; also an integer type in C99 (uchar.h).
  CanQualType Char32Ty; // char32_t; also an integer type in C99 (uchar.h).
  CanQualType SignedCharTy, ShortTy, IntTy, LongTy, LongLongTy, Int128Ty;
  CanQualType UnsignedCharTy, UnsignedShortTy, UnsignedIntTy, UnsignedLongTy;
  CanQualType UnsignedLongLongTy, UnsignedInt128Ty;
  CanQualType FloatTy, DoubleTy, LongDoubleTy, Float128Ty;
  CanQualType ShortAccumTy, AccumTy,
      LongAccumTy; // ISO/IEC JTC1 SC22 WG14 N1169 Extension
  CanQualType UnsignedShortAccumTy, UnsignedAccumTy, UnsignedLongAccumTy;
  CanQualType ShortFractTy, FractTy, LongFractTy;
  CanQualType UnsignedShortFractTy, UnsignedFractTy, UnsignedLongFractTy;
  CanQualType SatShortAccumTy, SatAccumTy, SatLongAccumTy;
  CanQualType SatUnsignedShortAccumTy, SatUnsignedAccumTy,
      SatUnsignedLongAccumTy;
  CanQualType SatShortFractTy, SatFractTy, SatLongFractTy;
  CanQualType SatUnsignedShortFractTy, SatUnsignedFractTy,
      SatUnsignedLongFractTy;
  CanQualType HalfTy; // __fp16 (IEEE 754-2008 half)
  CanQualType BFloat16Ty;
  CanQualType Float16Ty; // C11 extension ISO/IEC TS 18661-3
  CanQualType VoidPtrTy, NullPtrTy;
  CanQualType DependentTy, OverloadTy;
  CanQualType BuiltinFnTy;
  CanQualType PseudoObjectTy;
  CanQualType IncompleteMatrixIdxTy;
#define SVE_TYPE(Name, Id, SingletonId) CanQualType SingletonId;
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"

  // Deduced-type placeholder for desugaring. Built on demand.
  mutable QualType AutoDeductTy;

  // Decl used to help define __builtin_va_list for some targets.
  // The decl is built when constructing 'BuiltinVaListDecl'.
  mutable Decl *VaListTagDecl = nullptr;

  TreeContext(LangOptions &LOpts, SourceManager &SM, IdentifierTable &idents,
              Builtin::Context &builtins);
  TreeContext(const TreeContext &) = delete;
  TreeContext &operator=(const TreeContext &) = delete;
  ~TreeContext();

  void setTreeMutationListener(TreeMutationListener *Listener) {
    this->Listener = Listener;
  }

  TreeMutationListener *getTreeMutationListener() const { return Listener; }

  void PrintStats() const;
  const llvm::SmallVectorImpl<Type *> &getTypes() const { return Types; }

  RecordDecl *buildImplicitRecord(
      llvm::StringRef Name,
      RecordDecl::TagKind TK = RecordDecl::TagKind::Struct) const;

  TypedefDecl *buildImplicitTypedef(QualType T, llvm::StringRef Name) const;

  TypedefDecl *getInt128Decl() const;

  TypedefDecl *getUInt128Decl() const;

  //===--------------------------------------------------------------------===//
  //                           Type Constructors
  //===--------------------------------------------------------------------===//

private:
  QualType getExtQualType(const Type *Base, Qualifiers Quals) const;

  QualType getTypeDeclTypeSlow(const TypeDecl *Decl) const;

public:
  QualType getAddrSpaceQualType(QualType T, LangAS AddressSpace) const;

  QualType removeAddrSpaceQualType(QualType T) const;

  QualType removePtrSizeAddrSpace(QualType T) const;

  QualType getRestrictType(QualType T) const {
    return T.withFastQualifiers(Qualifiers::Restrict);
  }

  QualType getVolatileType(QualType T) const {
    return T.withFastQualifiers(Qualifiers::Volatile);
  }

  QualType getConstType(QualType T) const { return T.withConst(); }

  const FunctionType *adjustFunctionType(const FunctionType *Fn,
                                         FunctionType::ExtInfo EInfo);

  CanQualType getCanonicalFunctionResultType(QualType ResultType) const;

  QualType getFunctionTypeWithExceptionSpec(
      QualType Orig, const FunctionProtoType::ExceptionSpecInfo &ESI) const;

  QualType getFunctionTypeWithoutPtrSizes(QualType T);

  bool hasSameFunctionTypeIgnoringPtrSizes(QualType T, QualType U);

  QualType getComplexType(QualType T) const;
  CanQualType getComplexType(CanQualType T) const {
    return CanQualType::CreateUnsafe(getComplexType((QualType)T));
  }

  QualType getPointerType(QualType T) const;
  CanQualType getPointerType(CanQualType T) const {
    return CanQualType::CreateUnsafe(getPointerType((QualType)T));
  }

  QualType getAdjustedType(QualType Orig, QualType New) const;
  CanQualType getAdjustedType(CanQualType Orig, CanQualType New) const {
    return CanQualType::CreateUnsafe(
        getAdjustedType((QualType)Orig, (QualType)New));
  }

  QualType getDecayedType(QualType T) const;
  CanQualType getDecayedType(CanQualType T) const {
    return CanQualType::CreateUnsafe(getDecayedType((QualType)T));
  }
  QualType getDecayedType(QualType Orig, QualType Decayed) const;

  QualType getAtomicType(QualType T) const;

  QualType getBitIntType(bool Unsigned, unsigned NumBits) const;

  QualType getVariableArrayType(QualType EltTy, Expr *NumElts,
                                ArraySizeModifier ASM, unsigned IndexTypeQuals,
                                SourceRange Brackets) const;

  QualType getIncompleteArrayType(QualType EltTy, ArraySizeModifier ASM,
                                  unsigned IndexTypeQuals) const;

  QualType getConstantArrayType(QualType EltTy, const llvm::APInt &ArySize,
                                const Expr *SizeExpr, ArraySizeModifier ASM,
                                unsigned IndexTypeQuals) const;

  QualType getStringLiteralArrayType(QualType EltTy, unsigned Length) const;

  QualType getVariableArrayDecayedType(QualType Ty) const;

  // Convenience struct to return information about a builtin vector type.
  struct BuiltinVectorTypeInfo {
    QualType ElementType;
    llvm::ElementCount EC;
    unsigned NumVectors;
    BuiltinVectorTypeInfo(QualType ElementType, llvm::ElementCount EC,
                          unsigned NumVectors)
        : ElementType(ElementType), EC(EC), NumVectors(NumVectors) {}
  };

  BuiltinVectorTypeInfo
  getBuiltinVectorTypeInfo(const BuiltinType *VecTy) const;

  QualType getScalableVectorType(QualType EltTy, unsigned NumElts,
                                 unsigned NumFields = 1) const;

  QualType getVectorType(QualType VectorType, unsigned NumElts,
                         VectorKind VecKind) const;
  QualType getExtVectorType(QualType VectorType, unsigned NumElts) const;

  QualType getConstantMatrixType(QualType ElementType, unsigned NumRows,
                                 unsigned NumColumns) const;

  QualType getFunctionNoProtoType(QualType ResultTy,
                                  const FunctionType::ExtInfo &Info) const;

  QualType getFunctionNoProtoType(QualType ResultTy) const {
    return getFunctionNoProtoType(ResultTy, FunctionType::ExtInfo());
  }

  QualType getFunctionType(QualType ResultTy, llvm::ArrayRef<QualType> Args,
                           const FunctionProtoType::ExtProtoInfo &EPI) const {
    return getFunctionTypeInternal(ResultTy, Args, EPI, false);
  }

  QualType adjustStringLiteralBaseType(QualType StrLTy) const;

private:
  QualType getFunctionTypeInternal(QualType ResultTy,
                                   llvm::ArrayRef<QualType> Args,
                                   const FunctionProtoType::ExtProtoInfo &EPI,
                                   bool OnlyWantCanonical) const;
  QualType getAutoTypeInternal(QualType DeducedType, AutoTypeKeyword Keyword,
                               bool IsDependent, bool IsCanon = false) const;

public:
  QualType getTypeDeclType(const TypeDecl *Decl,
                           const TypeDecl *PrevDecl = nullptr) const {
    assert(Decl && "Passed null for Decl param");
    if (Decl->TypeForDecl)
      return QualType(Decl->TypeForDecl, 0);

    if (PrevDecl) {
      assert(PrevDecl->TypeForDecl && "previous decl has no TypeForDecl");
      Decl->TypeForDecl = PrevDecl->TypeForDecl;
      return QualType(PrevDecl->TypeForDecl, 0);
    }

    return getTypeDeclTypeSlow(Decl);
  }

  QualType getTypedefType(const TypedefNameDecl *Decl,
                          QualType Underlying = QualType()) const;

  QualType getRecordType(const RecordDecl *Decl) const;

  QualType getEnumType(const EnumDecl *Decl) const;

  QualType getAttributedType(attr::Kind attrKind, QualType modifiedType,
                             QualType equivalentType) const;

  QualType getBTFTagAttributedType(const BTFTypeTagAttr *BTFAttr,
                                   QualType Wrapped);

  QualType getParenType(QualType NamedType) const;

  QualType getMacroQualifiedType(QualType UnderlyingTy,
                                 const IdentifierInfo *MacroII) const;

  QualType getElaboratedType(ElaboratedTypeKeyword Keyword, QualType NamedType,
                             TagDecl *OwnedTagDecl = nullptr) const;

  QualType getTypeOfExprType(Expr *E, TypeOfKind Kind) const;
  QualType getTypeOfType(QualType QT, TypeOfKind Kind) const;

  QualType getReferenceQualifiedType(const Expr *e) const;

  QualType getAutoType(QualType DeducedType, AutoTypeKeyword Keyword,
                       bool IsDependent) const;

  QualType getAutoDeductType() const;

  QualType getTagDeclType(const TagDecl *Decl) const;

  CanQualType getSizeType() const;

  CanQualType getSignedSizeType() const;

  CanQualType getIntMaxType() const;

  CanQualType getUIntMaxType() const;

  QualType getWCharType() const { return WCharTy; }

  QualType getWideCharType() const { return WideCharTy; }

  QualType getSignedWCharType() const;

  QualType getUnsignedWCharType() const;

  QualType getWIntType() const { return WIntTy; }

  QualType getIntPtrType() const;

  QualType getUIntPtrType() const;

  QualType getPointerDiffType() const;

  QualType getUnsignedPointerDiffType() const;

  QualType getProcessIDType() const;

  IdentifierInfo *getBoolName() const {
    if (!BoolName)
      BoolName = &Idents.get("bool");
    return BoolName;
  }

  void setFILEDecl(TypeDecl *FILEDecl) { this->FILEDecl = FILEDecl; }

  QualType getFILEType() const {
    if (FILEDecl)
      return getTypeDeclType(FILEDecl);
    return QualType();
  }

  void setjmp_bufDecl(TypeDecl *jmp_bufDecl) {
    this->jmp_bufDecl = jmp_bufDecl;
  }

  QualType getjmp_bufType() const {
    if (jmp_bufDecl)
      return getTypeDeclType(jmp_bufDecl);
    return QualType();
  }

  void setsigjmp_bufDecl(TypeDecl *sigjmp_bufDecl) {
    this->sigjmp_bufDecl = sigjmp_bufDecl;
  }

  QualType getsigjmp_bufType() const {
    if (sigjmp_bufDecl)
      return getTypeDeclType(sigjmp_bufDecl);
    return QualType();
  }

  void setucontext_tDecl(TypeDecl *ucontext_tDecl) {
    this->ucontext_tDecl = ucontext_tDecl;
  }

  QualType getucontext_tType() const {
    if (ucontext_tDecl)
      return getTypeDeclType(ucontext_tDecl);
    return QualType();
  }

  QualType getLogicalOperationType() const { return IntTy; }

  TypedefDecl *getBuiltinVaListDecl() const;

  QualType getBuiltinVaListType() const {
    return getTypeDeclType(getBuiltinVaListDecl());
  }

  Decl *getVaListTagDecl() const;

  TypedefDecl *getBuiltinMSVaListDecl() const;

  QualType getBuiltinMSVaListType() const {
    return getTypeDeclType(getBuiltinMSVaListDecl());
  }

  bool canBuiltinBeRedeclared(const FunctionDecl *) const;

  QualType getCVRQualifiedType(QualType T, unsigned CVR) const {
    return getQualifiedType(T, Qualifiers::fromCVRMask(CVR));
  }

  QualType getQualifiedType(SplitQualType split) const {
    return getQualifiedType(split.Ty, split.Quals);
  }

  QualType getQualifiedType(QualType T, Qualifiers Qs) const {
    if (!Qs.hasNonFastQualifiers())
      return T.withFastQualifiers(Qs.getFastQualifiers());
    QualifierCollector Qc(Qs);
    const Type *Ptr = Qc.strip(T);
    return getExtQualType(Ptr, Qc);
  }

  QualType getQualifiedType(const Type *T, Qualifiers Qs) const {
    if (!Qs.hasNonFastQualifiers())
      return QualType(T, Qs.getFastQualifiers());
    return getExtQualType(T, Qs);
  }

  unsigned char getFixedPointScale(QualType Ty) const;
  unsigned char getFixedPointIBits(QualType Ty) const;
  llvm::FixedPointSemantics getFixedPointSemantics(QualType Ty) const;
  llvm::APFixedPoint getFixedPointMax(QualType Ty) const;
  llvm::APFixedPoint getFixedPointMin(QualType Ty) const;

  enum GetBuiltinTypeError {
    /// No error
    GE_None,

    /// Missing a type
    GE_Missing_type,

    /// Missing a type from <stdio.h>
    GE_Missing_stdio,

    /// Missing a type from <setjmp.h>
    GE_Missing_setjmp,

    /// Missing a type from <ucontext.h>
    GE_Missing_ucontext
  };

  QualType DecodeTypeStr(const char *&Str, const TreeContext &Context,
                         TreeContext::GetBuiltinTypeError &Error,
                         bool &RequireICE, bool AllowTypeModifiers) const;

  QualType GetBuiltinType(unsigned ID, GetBuiltinTypeError &Error,
                          unsigned *IntegerConstantArgs = nullptr) const;

private:
  CanQualType getFromTargetType(unsigned Type) const;
  TypeInfo getTypeInfoImpl(const Type *T) const;

  //===--------------------------------------------------------------------===//
  //                         Type Predicates.
  //===--------------------------------------------------------------------===//

public:
  bool areCompatibleVectorTypes(QualType FirstVec, QualType SecondVec);

  bool areCompatibleSveTypes(QualType FirstType, QualType SecondType);

  bool areLaxCompatibleSveTypes(QualType FirstType, QualType SecondType);

  //===--------------------------------------------------------------------===//
  //                         Type Sizing and Analysis
  //===--------------------------------------------------------------------===//

  const llvm::fltSemantics &getFloatTypeSemantics(QualType T) const;

  TypeInfo getTypeInfo(const Type *T) const;
  TypeInfo getTypeInfo(QualType T) const { return getTypeInfo(T.getTypePtr()); }

  uint64_t getTypeSize(QualType T) const { return getTypeInfo(T).Width; }
  uint64_t getTypeSize(const Type *T) const { return getTypeInfo(T).Width; }

  uint64_t getCharWidth() const { return getTypeSize(CharTy); }

  CharUnits toCharUnitsFromBits(int64_t BitSize) const;

  int64_t toBits(CharUnits CharSize) const;

  CharUnits getTypeSizeInChars(QualType T) const;
  CharUnits getTypeSizeInChars(const Type *T) const;

  std::optional<CharUnits> getTypeSizeInCharsIfKnown(QualType Ty) const {
    if (Ty->isIncompleteType() || Ty->isDependentType())
      return std::nullopt;
    return getTypeSizeInChars(Ty);
  }

  std::optional<CharUnits> getTypeSizeInCharsIfKnown(const Type *Ty) const {
    return getTypeSizeInCharsIfKnown(QualType(Ty, 0));
  }

  unsigned getTypeAlign(QualType T) const { return getTypeInfo(T).Align; }
  unsigned getTypeAlign(const Type *T) const { return getTypeInfo(T).Align; }

  unsigned getTypeUnadjustedAlign(QualType T) const {
    return getTypeUnadjustedAlign(T.getTypePtr());
  }
  unsigned getTypeUnadjustedAlign(const Type *T) const;

  unsigned getTypeAlignIfKnown(QualType T) const;

  CharUnits getTypeAlignInChars(QualType T) const;
  CharUnits getTypeAlignInChars(const Type *T) const;

  CharUnits getTypeUnadjustedAlignInChars(QualType T) const;
  CharUnits getTypeUnadjustedAlignInChars(const Type *T) const;

  // getTypeInfoDataSizeInChars - Return the size of a type, in chars. If the
  // type is a record, its data size is returned.
  TypeInfoChars getTypeInfoDataSizeInChars(QualType T) const;

  TypeInfoChars getTypeInfoInChars(const Type *T) const;
  TypeInfoChars getTypeInfoInChars(QualType T) const;

  bool isAlignmentRequired(const Type *T) const;
  bool isAlignmentRequired(QualType T) const;

  bool isPromotableIntegerType(QualType T) const; // C99 6.3.1.1p2

  unsigned getPreferredTypeAlign(QualType T) const {
    return getPreferredTypeAlign(T.getTypePtr());
  }
  unsigned getPreferredTypeAlign(const Type *T) const;

  unsigned getTargetDefaultAlignForAttributeAligned() const;

  unsigned getAlignOfGlobalVar(QualType T) const;

  CharUnits getAlignOfGlobalVarInChars(QualType T) const;

  CharUnits getDeclAlign(const Decl *D, bool ForAlignof = false) const;

  const StructRecordLayout &getStructRecordLayout(const RecordDecl *D) const;

  uint64_t getFieldOffset(const ValueDecl *FD) const;

  MangleContext *createMangleContext();

  bool
  hasUniqueObjectRepresentations(QualType Ty,
                                 bool CheckIfTriviallyCopyable = true) const;

  //===--------------------------------------------------------------------===//
  //                            Type Operators
  //===--------------------------------------------------------------------===//

  CanQualType getCanonicalType(QualType T) const {
    return CanQualType::CreateUnsafe(T.getCanonicalType());
  }

  const Type *getCanonicalType(const Type *T) const {
    return T->getCanonicalTypeInternal().getTypePtr();
  }

  CanQualType getCanonicalParamType(QualType T) const;

  bool hasSameType(QualType T1, QualType T2) const {
    return getCanonicalType(T1) == getCanonicalType(T2);
  }
  bool hasSameType(const Type *T1, const Type *T2) const {
    return getCanonicalType(T1) == getCanonicalType(T2);
  }

  bool hasSameExpr(const Expr *X, const Expr *Y) const;

  QualType getUnqualifiedArrayType(QualType T, Qualifiers &Quals);

  bool hasSameUnqualifiedType(QualType T1, QualType T2) const {
    return getCanonicalType(T1).getTypePtr() ==
           getCanonicalType(T2).getTypePtr();
  }

  bool hasSameNullabilityTypeQualifier(QualType SubT, QualType SuperT,
                                       bool IsParam) const {
    auto SubTnullability = SubT->getNullability();
    auto SuperTnullability = SuperT->getNullability();
    if (SubTnullability.has_value() == SuperTnullability.has_value()) {
      // Neither has nullability; return true
      if (!SubTnullability)
        return true;
      // Both have nullability qualifier.
      if (*SubTnullability == *SuperTnullability ||
          *SubTnullability == NullabilityKind::Unspecified ||
          *SuperTnullability == NullabilityKind::Unspecified)
        return true;

      if (IsParam) {
        // Ok for the super-type parameter to be "nonnull" and the
        // sub-type parameter to be "nullable"
        return (*SuperTnullability == NullabilityKind::NonNull &&
                *SubTnullability == NullabilityKind::Nullable);
      }
      // For the return type, it's okay for the super-type to specify
      // "nullable" and the sub-type specify "nonnull"
      return (*SuperTnullability == NullabilityKind::Nullable &&
              *SubTnullability == NullabilityKind::NonNull);
    }
    return true;
  }

  bool UnwrapSimilarTypes(QualType &T1, QualType &T2,
                          bool AllowPiMismatch = true);
  void UnwrapSimilarArrayTypes(QualType &T1, QualType &T2,
                               bool AllowPiMismatch = true);

  bool hasSimilarType(QualType T1, QualType T2);

  bool hasCvrSimilarType(QualType T1, QualType T2);

  CallingConv getDefaultCallingConvention(bool IsVariadic,
                                          bool IsBuiltin = false) const;

  bool isSameEntity(const NamedDecl *X, const NamedDecl *Y) const;

  const ArrayType *getAsArrayType(QualType T) const;
  const ConstantArrayType *getAsConstantArrayType(QualType T) const {
    return dyn_cast_or_null<ConstantArrayType>(getAsArrayType(T));
  }
  const VariableArrayType *getAsVariableArrayType(QualType T) const {
    return dyn_cast_or_null<VariableArrayType>(getAsArrayType(T));
  }
  const IncompleteArrayType *getAsIncompleteArrayType(QualType T) const {
    return dyn_cast_or_null<IncompleteArrayType>(getAsArrayType(T));
  }
  QualType getBaseElementType(const ArrayType *VAT) const;

  QualType getBaseElementType(QualType QT) const;

  uint64_t getConstantArrayElementCount(const ConstantArrayType *CA) const;

  uint64_t
  getArrayInitLoopExprElementCount(const ArrayInitLoopExpr *AILE) const;

  QualType getAdjustedParameterType(QualType T) const;

  QualType getSignatureParameterType(QualType T) const;

  QualType getArrayDecayedType(QualType T) const;

  QualType getPromotedIntegerType(QualType PromotableType) const;

  QualType isPromotableBitField(Expr *E) const;

  int getIntegerTypeOrder(QualType LHS, QualType RHS) const;

  int getFloatingTypeOrder(QualType LHS, QualType RHS) const;

  int getFloatingTypeSemanticOrder(QualType LHS, QualType RHS) const;

  unsigned getTargetAddressSpace(LangAS AS) const;

  LangAS getLangASForBuiltinAddressSpace(unsigned AS) const;

  uint64_t getTargetNullPointerValue(QualType QT) const;

  bool addressSpaceMapManglingFor(LangAS AS) const {
    return AddrSpaceMapMangling || isTargetAddressSpace(AS);
  }

  FunctionProtoType::ExceptionSpecInfo
  mergeExceptionSpecs(FunctionProtoType::ExceptionSpecInfo ESI1,
                      FunctionProtoType::ExceptionSpecInfo ESI2);

  // For two "same" types, return a type which has
  // the common sugar between them. If Unqualified is true,
  // both types need only be the same unqualified type.
  // The result will drop the qualifiers which do not occur
  // in both types.
  QualType getCommonSugaredType(QualType X, QualType Y,
                                bool Unqualified = false);

private:
  // Helper for integer ordering
  unsigned getIntegerRank(const Type *T) const;

public:
  //===--------------------------------------------------------------------===//
  //                    Type Compatibility Predicates
  //===--------------------------------------------------------------------===//

  bool typesAreCompatible(QualType T1, QualType T2,
                          bool CompareUnqualified = false); // C99 6.2.7p1

  // Functions for calculating composite types
  QualType mergeTypes(QualType, QualType, bool Unqualified = false,
                      bool IsConditionalOperator = false);
  QualType mergeFunctionTypes(QualType, QualType, bool Unqualified = false,
                              bool IsConditionalOperator = false);
  QualType mergeFunctionParameterTypes(QualType, QualType,
                                       bool Unqualified = false);
  QualType mergeTransparentUnionType(QualType, QualType,
                                     bool Unqualified = false);

  bool mergeExtParameterInfo(
      const FunctionProtoType *FirstFnType,
      const FunctionProtoType *SecondFnType, bool &CanUseFirst,
      bool &CanUseSecond,
      llvm::SmallVectorImpl<FunctionProtoType::ExtParameterInfo>
          &NewParamInfos);

  //===--------------------------------------------------------------------===//
  //                    Integer Predicates
  //===--------------------------------------------------------------------===//

  // The width of an integer, as defined in C99 6.2.6.2. This is the number
  // of bits in an integer type excluding any padding bits.
  unsigned getIntWidth(QualType T) const;

  // Per C99 6.2.5p6, for every signed integer type, there is a corresponding
  // unsigned integer type.  This method takes a signed type, and returns the
  // corresponding unsigned integer type.
  // With the introduction of fixed point types in ISO N1169, this method also
  // accepts fixed point types and returns the corresponding unsigned type for
  // a given fixed point type.
  QualType getCorrespondingUnsignedType(QualType T) const;

  // Per C99 6.2.5p6, for every signed integer type, there is a corresponding
  // unsigned integer type.  This method takes an unsigned type, and returns the
  // corresponding signed integer type.
  // With the introduction of fixed point types in ISO N1169, this method also
  // accepts fixed point types and returns the corresponding signed type for
  // a given fixed point type.
  QualType getCorrespondingSignedType(QualType T) const;

  // Per ISO N1169, this method accepts fixed point types and returns the
  // corresponding saturated type for a given fixed point type.
  QualType getCorrespondingSaturatedType(QualType Ty) const;

  // This method accepts fixed point types and returns the corresponding signed
  // type. Unlike getCorrespondingUnsignedType(), this only accepts unsigned
  // fixed point types because there are unsigned integer types like bool and
  // char8_t that don't have signed equivalents.
  QualType getCorrespondingSignedFixedPointType(QualType Ty) const;

  //===--------------------------------------------------------------------===//
  //                    Integer Values
  //===--------------------------------------------------------------------===//

  llvm::APSInt MakeIntValue(uint64_t Value, QualType Type) const {
    // If Type is a signed integer type larger than 64 bits, we need to be sure
    // to sign extend Res appropriately.
    llvm::APSInt Res(64, !Type->isSignedIntegerOrEnumerationType());
    Res = Value;
    unsigned Width = getIntWidth(Type);
    if (Width != Res.getBitWidth())
      return Res.extOrTrunc(Width);
    return Res;
  }

  bool isSentinelNullExpr(const Expr *E);

  TypeSourceInfo *CreateTypeSourceInfo(QualType T, unsigned Size = 0) const;

  TypeSourceInfo *
  getTrivialTypeSourceInfo(QualType T,
                           SourceLocation Loc = SourceLocation()) const;

  void AddDeallocation(void (*Callback)(void *), void *Data) const;

  template <typename T> void addDestruction(T *Ptr) const {
    if (!std::is_trivially_destructible<T>::value) {
      auto DestroyPtr = [](void *V) { static_cast<T *>(V)->~T(); };
      AddDeallocation(DestroyPtr, Ptr);
    }
  }

  GVALinkage GetGVALinkageForFunction(const FunctionDecl *FD) const;
  GVALinkage GetGVALinkageForVariable(const VarDecl *VD) const;

  bool DeclMustBeEmitted(const Decl *D);

  void forEachMultiversionedFunctionVersion(
      const FunctionDecl *FD,
      llvm::function_ref<void(FunctionDecl *)> Pred) const;

  void setManglingNumber(const NamedDecl *ND, unsigned Number);
  unsigned getManglingNumber(const NamedDecl *ND) const;

  void setStaticLocalNumber(const VarDecl *VD, unsigned Number);

  void setParameterIndex(const ParmVarDecl *D, unsigned index);

  unsigned getParameterIndex(const ParmVarDecl *D) const;

  StringLiteral *getPredefinedStringLiteralFromCache(llvm::StringRef Key) const;

  ParsedTargetAttr filterFunctionTargetAttrs(const TargetAttr *TD) const;

  std::vector<std::string>
  filterFunctionTargetVersionAttrs(const TargetVersionAttr *TV) const;

  void getFunctionFeatureMap(llvm::StringMap<bool> &FeatureMap,
                             const FunctionDecl *) const;
  void getFunctionFeatureMap(llvm::StringMap<bool> &FeatureMap,
                             GlobalDecl GD) const;

  //===--------------------------------------------------------------------===//
  //                    Statistics
  //===--------------------------------------------------------------------===//

public:
  void InitBuiltinTypes(const TargetInfo &Target);

private:
  void InitBuiltinType(CanQualType &R, BuiltinType::Kind K);

public:
  enum class InlineVariableDefinitionKind {
    None,
    Weak,
  };

  InlineVariableDefinitionKind
  getInlineVariableDefinitionKind(const VarDecl *VD) const;

private:
  friend class DeclContext;

  using DeallocationFunctionsAndArguments =
      llvm::SmallVector<std::pair<void (*)(void *), void *>, 16>;
  mutable DeallocationFunctionsAndArguments Deallocations;

  llvm::PointerIntPair<StoredDeclsMap *, 1> LastSDM;

  std::vector<Decl *> TraversalScope;

  void ReleaseDeclContextMaps();

public:
  enum PragmaSectionFlag : unsigned {
    PSF_None = 0,
    PSF_Read = 0x1,
    PSF_Write = 0x2,
    PSF_Execute = 0x4,
    PSF_Implicit = 0x8,
    PSF_ZeroInit = 0x10,
    PSF_Invalid = 0x80000000U,
  };

  struct SectionInfo {
    NamedDecl *Decl;
    SourceLocation PragmaSectionLocation;
    int SectionFlags;

    SectionInfo() = default;
    SectionInfo(NamedDecl *Decl, SourceLocation PragmaSectionLocation,
                int SectionFlags)
        : Decl(Decl), PragmaSectionLocation(PragmaSectionLocation),
          SectionFlags(SectionFlags) {}
  };

  llvm::StringMap<SectionInfo> SectionInfos;

  llvm::StringRef getCUIDHash() const;
};

const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                      const TreeContext::SectionInfo &Section);

} // namespace neverc

// operator new and delete aren't allowed inside namespaces.

inline void *operator new(size_t Bytes, const neverc::TreeContext &C,
                          size_t Alignment /* = 8 */) {
  return C.Allocate(Bytes, Alignment);
}

inline void operator delete(void *Ptr, const neverc::TreeContext &C, size_t) {
  C.Deallocate(Ptr);
}

inline void *operator new[](size_t Bytes, const neverc::TreeContext &C,
                            size_t Alignment /* = 8 */) {
  return C.Allocate(Bytes, Alignment);
}

inline void operator delete[](void *Ptr, const neverc::TreeContext &C, size_t) {
  C.Deallocate(Ptr);
}

#endif // NEVERC_TREE_ASTCONTEXT_H
