#ifndef NEVERC_LIB_EMIT_DEBUG_DEBUGEMITTERINFO_H
#define NEVERC_LIB_EMIT_DEBUG_DEBUGEMITTERINFO_H

#include "Core/EmitterBuilder.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/LangOpts/CodeGenOptions.h"
#include "neverc/Tree/Core/PrettyPrinter.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/Type.h"
#include "neverc/Tree/Type/TypeOrdering.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Allocator.h"
#include <optional>

namespace llvm {
class MDNode;
}

namespace neverc {
class GlobalDecl;
class VarDecl;

namespace Emit {
class ModuleEmitter;
class FunctionEmitter;

class DebugEmitter {
  friend class ApplyDebugLocation;
  friend class SaveAndRestoreLocation;
  ModuleEmitter &ME;
  const llvm::codegenoptions::DebugInfoKind DebugKind;
  llvm::DIBuilder DBuilder;
  llvm::DICompileUnit *TheCU = nullptr;
  SourceLocation CurLoc;
  llvm::MDNode *CurInlinedAt = nullptr;

  llvm::DenseMap<const void *, llvm::TrackingMDRef> TypeCache;

  llvm::SmallDenseMap<QualType, llvm::Metadata *> SizeExprCache;

  class PrintingCallbacks final : public neverc::PrintingCallbacks {
    const DebugEmitter &Self;

  public:
    PrintingCallbacks(const DebugEmitter &Self) : Self(Self) {}
    std::string remapPath(llvm::StringRef Path) const override {
      return Self.remapDIPath(Path);
    }
  };
  PrintingCallbacks PrintCB = {*this};

  std::vector<void *> RetainedTypes;

  std::vector<std::pair<const TagType *, llvm::TrackingMDRef>> ReplaceMap;

  std::vector<std::pair<const DeclaratorDecl *, llvm::TrackingMDRef>>
      FwdDeclReplaceMap;

  std::vector<llvm::TypedTrackingMDRef<llvm::DIScope>> LexicalBlockStack;
  llvm::DenseMap<const Decl *, llvm::TrackingMDRef> RegionMap;
  std::vector<unsigned> FnBeginRegionCount;

  llvm::BumpPtrAllocator DebugInfoNames;
  llvm::StringRef CWDName;

  llvm::DenseMap<const char *, llvm::TrackingMDRef> DIFileCache;
  llvm::DenseMap<const FunctionDecl *, llvm::TrackingMDRef> SPCache;
  llvm::DenseMap<const Decl *, llvm::TrackingMDRef> DeclCache;
  llvm::DenseMap<const Decl *, llvm::TrackingMDRef> ImportedDeclCache;
  llvm::DIType *CreateType(const BuiltinType *Ty);
  llvm::DIType *CreateType(const ComplexType *Ty);
  llvm::DIType *CreateType(const BitIntType *Ty);
  llvm::DIType *CreateQualifiedType(QualType Ty, llvm::DIFile *Fg);
  llvm::DIType *CreateQualifiedType(const FunctionProtoType *Ty,
                                    llvm::DIFile *Fg);
  llvm::DIType *CreateType(const TypedefType *Ty, llvm::DIFile *Fg);
  llvm::DIType *CreateType(const PointerType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const FunctionType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const RecordType *Tyg);

  std::pair<llvm::DIType *, llvm::DIType *>
  CreateTypeDefinition(const RecordType *Ty);
  llvm::DICompositeType *CreateLimitedType(const RecordType *Ty);

  llvm::DIType *CreateType(const VectorType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const ConstantMatrixType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const ArrayType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateType(const AtomicType *Ty, llvm::DIFile *F);
  llvm::DIType *CreateEnumType(const EnumType *Ty);
  llvm::DIType *CreateTypeDefinition(const EnumType *Ty);

  llvm::DIType *getTypeOrNull(const QualType);
  llvm::DISubroutineType *
  getOrCreateFunctionType(const Decl *D, QualType FnType, llvm::DIFile *F);

  llvm::DINodeArray CollectBTFDeclTagAnnotations(const Decl *D);

  llvm::DIType *createFieldType(llvm::StringRef name, QualType type,
                                SourceLocation loc, uint64_t offsetInBits,
                                uint32_t AlignInBits, llvm::DIFile *tunit,
                                llvm::DIScope *scope,
                                const RecordDecl *RD = nullptr,
                                llvm::DINodeArray Annotations = nullptr);

  llvm::DIType *createFieldType(llvm::StringRef name, QualType type,
                                SourceLocation loc, uint64_t offsetInBits,
                                llvm::DIFile *tunit, llvm::DIScope *scope,
                                const RecordDecl *RD = nullptr) {
    return createFieldType(name, type, loc, offsetInBits, 0, tunit, scope, RD);
  }

  llvm::DIDerivedType *createBitFieldType(const FieldDecl *BitFieldDecl,
                                          llvm::DIScope *RecordTy,
                                          const RecordDecl *RD);

  llvm::DIDerivedType *createBitFieldSeparatorIfNeeded(
      const FieldDecl *BitFieldDecl, const llvm::DIDerivedType *BitFieldDI,
      llvm::ArrayRef<llvm::Metadata *> PreviousFieldsDI, const RecordDecl *RD);

  void CollectRecordNormalField(const FieldDecl *Field, uint64_t OffsetInBits,
                                llvm::DIFile *F,
                                llvm::SmallVectorImpl<llvm::Metadata *> &E,
                                llvm::DIType *RecordTy, const RecordDecl *RD);
  void CollectRecordNestedType(const TypeDecl *RD,
                               llvm::SmallVectorImpl<llvm::Metadata *> &E);
  void CollectRecordFields(const RecordDecl *Decl, llvm::DIFile *F,
                           llvm::SmallVectorImpl<llvm::Metadata *> &E,
                           llvm::DICompositeType *RecordTy);

  void CreateLexicalBlock(SourceLocation Loc);

  void AppendAddressSpaceXDeref(unsigned AddressSpace,
                                llvm::SmallVectorImpl<uint64_t> &Expr) const;

public:
  DebugEmitter(ModuleEmitter &ME);
  ~DebugEmitter();

  void finalize();

  std::string remapDIPath(llvm::StringRef) const;

  void registerVLASizeExpression(QualType Ty, llvm::Metadata *SizeExpr) {
    SizeExprCache[Ty] = SizeExpr;
  }

  void setDwoId(uint64_t Signature);

  void setLocation(SourceLocation Loc);

  SourceLocation getLocation() const { return CurLoc; }

  void setInlinedAt(llvm::MDNode *InlinedAt) { CurInlinedAt = InlinedAt; }

  llvm::MDNode *getInlinedAt() const { return CurInlinedAt; }

  // Converts a SourceLocation to a DebugLoc
  llvm::DebugLoc sourceLocToDebugLoc(SourceLocation Loc);

  void genLocation(CGBuilderTy &Builder, SourceLocation Loc);

  QualType getFunctionType(const FunctionDecl *FD, QualType RetTy,
                           const llvm::SmallVectorImpl<const VarDecl *> &Args);

  void emitFunctionStart(GlobalDecl GD, SourceLocation Loc,
                         SourceLocation ScopeLoc, QualType FnType,
                         llvm::Function *Fn);

  void genInlineFunctionStart(CGBuilderTy &Builder, GlobalDecl GD);
  void genInlineFunctionEnd(CGBuilderTy &Builder);

  void genFunctionDecl(GlobalDecl GD, SourceLocation Loc, QualType FnType,
                       llvm::Function *Fn = nullptr);

  void genFuncDeclForCallSite(llvm::CallBase *CallOrInvoke, QualType CalleeType,
                              const FunctionDecl *CalleeDecl);

  void genFunctionEnd(CGBuilderTy &Builder, llvm::Function *Fn);

  void genLexicalBlockStart(CGBuilderTy &Builder, SourceLocation Loc);

  void genLexicalBlockEnd(CGBuilderTy &Builder, SourceLocation Loc);

  llvm::DILocalVariable *
  genDeclareOfAutoVariable(const VarDecl *Decl, llvm::Value *AI,
                           CGBuilderTy &Builder,
                           const bool UsePointerValue = false);

  void genLabel(const LabelDecl *D, CGBuilderTy &Builder);

  llvm::DILocalVariable *
  genDeclareOfArgVariable(const VarDecl *Decl, llvm::Value *AI, unsigned ArgNo,
                          CGBuilderTy &Builder, bool UsePointerValue = false);

  void genGlobalVariable(llvm::GlobalVariable *GV, const VarDecl *Decl);

  void genGlobalVariable(const ValueDecl *VD, const APValue &Init);

  void genExternalVariable(llvm::GlobalVariable *GV, const VarDecl *Decl);

  void genGlobalAlias(const llvm::GlobalValue *GV, const GlobalDecl Decl);

  void genExplicitCastType(QualType Ty);

  void genAndRetainType(QualType Ty);

  void AddStringLiteralDebugInfo(llvm::GlobalVariable *GV,
                                 const StringLiteral *S);

  llvm::DIType *getOrCreateRecordType(QualType Ty, SourceLocation L);

  llvm::DIType *getOrCreateStandaloneType(QualType Ty, SourceLocation Loc);

  void addHeapAllocSiteMetadata(llvm::CallBase *CallSite, QualType AllocatedTy,
                                SourceLocation Loc);

  void completeType(const EnumDecl *ED);
  void completeType(const RecordDecl *RD);
  void completeRequiredType(const RecordDecl *RD);
  void completeRecordData(const RecordDecl *RD);
  void completeRecord(const RecordDecl *RD);

  llvm::DIMacro *CreateMacro(llvm::DIMacroFile *Parent, unsigned MType,
                             SourceLocation LineLoc, llvm::StringRef Name,
                             llvm::StringRef Value);

  llvm::DIMacroFile *CreateTempMacroFile(llvm::DIMacroFile *Parent,
                                         SourceLocation LineLoc,
                                         SourceLocation FileLoc);

private:
  llvm::DILocalVariable *genDeclare(const VarDecl *decl, llvm::Value *AI,
                                    std::optional<unsigned> ArgNo,
                                    CGBuilderTy &Builder,
                                    const bool UsePointerValue = false);

  std::string GetName(const Decl *, bool Qualified = false) const;

  llvm::DIScope *getDeclContextDescriptor(const Decl *D);
  llvm::DIScope *getContextDescriptor(const Decl *Context,
                                      llvm::DIScope *Default);

  llvm::DIScope *getCurrentContextDescriptor(const Decl *Decl);

  llvm::DICompositeType *getOrCreateRecordFwdDecl(const RecordType *,
                                                  llvm::DIScope *);

  llvm::StringRef getCurrentDirname();

  void CreateCompileUnit();

  std::optional<llvm::DIFile::ChecksumKind>
  computeChecksum(FileID FID, llvm::SmallString<64> &Checksum) const;

  std::optional<llvm::StringRef> getSource(const SourceManager &SM, FileID FID);

  llvm::DIFile *getOrCreateFile(SourceLocation Loc);

  llvm::DIFile *
  createFile(llvm::StringRef FileName,
             std::optional<llvm::DIFile::ChecksumInfo<llvm::StringRef>> CSInfo,
             std::optional<llvm::StringRef> Source);

  llvm::DIType *getOrCreateType(QualType Ty, llvm::DIFile *Fg);

  llvm::DICompositeType *getOrCreateLimitedType(const RecordType *Ty);

  llvm::DIType *CreateTypeNode(QualType Ty, llvm::DIFile *Fg);

  llvm::DIType *CreateMemberType(llvm::DIFile *Unit, QualType FType,
                                 llvm::StringRef Name, uint64_t *Offset);

  llvm::DINode *getDeclarationOrDefinition(const Decl *D);

  llvm::DISubprogram *getFunctionDeclaration(const Decl *D);

  llvm::DISubprogram *getFunctionFwdDeclOrStub(GlobalDecl GD, bool Stub);

  llvm::DISubprogram *getFunctionForwardDeclaration(GlobalDecl GD);

  llvm::DISubprogram *getFunctionStub(GlobalDecl GD);

  llvm::DIGlobalVariable *
  getGlobalVariableForwardDeclaration(const VarDecl *VD);

  llvm::DIGlobalVariableExpression *
  CollectAnonRecordDecls(const RecordDecl *RD, llvm::DIFile *Unit,
                         unsigned LineNo, llvm::StringRef LinkageName,
                         llvm::GlobalVariable *Var, llvm::DIScope *DContext);

  llvm::DINode::DIFlags getCallSiteRelatedAttrs() const;

  PrintingPolicy getPrintingPolicy() const;

  llvm::StringRef getFunctionName(const FunctionDecl *FD);

  llvm::StringRef getRecordName(const RecordDecl *RD);

  unsigned getLineNumber(SourceLocation Loc);

  unsigned getColumnNumber(SourceLocation Loc, bool Force = false);

  void collectFunctionDeclProps(GlobalDecl GD, llvm::DIFile *Unit,
                                llvm::StringRef &Name,
                                llvm::StringRef &LinkageName,
                                llvm::DIScope *&FDContext,
                                llvm::DINodeArray &TParamsArray,
                                llvm::DINode::DIFlags &Flags);

  void collectVarDeclProps(const VarDecl *VD, llvm::DIFile *&Unit,
                           unsigned &LineNo, QualType &T, llvm::StringRef &Name,
                           llvm::StringRef &LinkageName,
                           llvm::DIScope *&VDContext);

  llvm::DIExpression *createConstantValueExpression(const neverc::ValueDecl *VD,
                                                    const APValue &Val);

  llvm::StringRef internString(llvm::StringRef A,
                               llvm::StringRef B = llvm::StringRef()) {
    char *Data = DebugInfoNames.Allocate<char>(A.size() + B.size());
    if (!A.empty())
      std::memcpy(Data, A.data(), A.size());
    if (!B.empty())
      std::memcpy(Data + A.size(), B.data(), B.size());
    return llvm::StringRef(Data, A.size() + B.size());
  }
};

class ApplyDebugLocation {
private:
  void init(SourceLocation TemporaryLocation, bool DefaultToEmpty = false);
  ApplyDebugLocation(FunctionEmitter &FE, bool DefaultToEmpty,
                     SourceLocation TemporaryLocation);

  llvm::DebugLoc OriginalLocation;
  FunctionEmitter *FE;

public:
  ApplyDebugLocation(FunctionEmitter &FE, SourceLocation TemporaryLocation);
  ApplyDebugLocation(FunctionEmitter &FE, const Expr *E);
  ApplyDebugLocation(FunctionEmitter &FE, llvm::DebugLoc Loc);
  ApplyDebugLocation(ApplyDebugLocation &&Other) : FE(Other.FE) {
    Other.FE = nullptr;
  }

  // Define copy assignment operator.
  ApplyDebugLocation &operator=(ApplyDebugLocation &&Other) {
    if (this != &Other) {
      FE = Other.FE;
      Other.FE = nullptr;
    }
    return *this;
  }

  ~ApplyDebugLocation();

  static ApplyDebugLocation CreateArtificial(FunctionEmitter &FE) {
    return ApplyDebugLocation(FE, false, SourceLocation());
  }
  static ApplyDebugLocation
  CreateDefaultArtificial(FunctionEmitter &FE,
                          SourceLocation TemporaryLocation) {
    return ApplyDebugLocation(FE, false, TemporaryLocation);
  }

  static ApplyDebugLocation CreateEmpty(FunctionEmitter &FE) {
    return ApplyDebugLocation(FE, true, SourceLocation());
  }
};

class ApplyInlineDebugLocation {
  SourceLocation SavedLocation;
  FunctionEmitter *FE;

public:
  ApplyInlineDebugLocation(FunctionEmitter &FE, GlobalDecl InlinedFn);
  ~ApplyInlineDebugLocation();
};

} // namespace Emit
} // namespace neverc

#endif // NEVERC_LIB_EMIT_DEBUG_DEBUGEMITTERINFO_H
