#ifndef NEVERC_LIB_EMIT_CORE_MODULEEMITTER_H
#define NEVERC_LIB_EMIT_CORE_MODULEEMITTER_H

#include "Core/TypeEmitter.h"
#include "Core/TypeEmitterCache.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepOptions.h"
#include "neverc/Tree/Core/Mangle.h"
#include "neverc/Tree/Decl/GlobalDecl.h"
#include "neverc/Tree/Type/TypeOrdering.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueHandle.h"
#include <optional>

namespace llvm {
class Module;
class Constant;
class ConstantInt;
class Function;
class GlobalValue;
class DataLayout;
class FunctionType;
class LLVMContext;

namespace vfs {
class FileSystem;
}
} // namespace llvm

namespace neverc {
class TreeContext;
class AtomicType;
class FunctionDecl;
class IdentifierInfo;
class CharUnits;
class Decl;
class Stmt;
class StringLiteral;
class NamedDecl;
class ValueDecl;
class VarDecl;
class LangOptions;
class CodeGenOptions;
class HeaderIndexOptions;
class DiagnosticsEngine;
class AnnotateAttr;
namespace Emit {

class FunctionEmitter;
class TBAAEmitter;
class CGABI;
class DebugEmitter;
class TargetCodeGenInfo;

enum ForDefinition_t : bool { NotForDefinition = false, ForDefinition = true };

class ModuleEmitter : public TypeEmitterCache {
  ModuleEmitter(const ModuleEmitter &) = delete;
  void operator=(const ModuleEmitter &) = delete;

public:
  struct Structor {
    Structor()
        : Priority(0), LexOrder(~0u), Initializer(nullptr),
          AssociatedData(nullptr) {}
    Structor(int Priority, unsigned LexOrder, llvm::Constant *Initializer,
             llvm::Constant *AssociatedData)
        : Priority(Priority), LexOrder(LexOrder), Initializer(Initializer),
          AssociatedData(AssociatedData) {}
    int Priority;
    unsigned LexOrder;
    llvm::Constant *Initializer;
    llvm::Constant *AssociatedData;
  };

  typedef std::vector<Structor> CtorList;

private:
  TreeContext &Context;
  const LangOptions &LangOpts;
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>
      FS;                                  // Only used for debug info.
  const HeaderIndexOptions &HeaderIdxOpts; // Only used for debug info.
  const PrepOptions &PrepOpts;
  const CodeGenOptions &CodeGenOpts;
  unsigned NumAutoVarInit = 0;
  llvm::Module &TheModule;
  DiagnosticsEngine &Diags;
  const TargetInfo &Target;
  std::unique_ptr<CGABI> ABI;
  llvm::LLVMContext &VMContext;
  std::string ModuleNameHash;
  std::unique_ptr<TBAAEmitter> TBAA;

  mutable std::unique_ptr<TargetCodeGenInfo> TheTargetCodeGenInfo;

  mutable std::string CachedDefaultTargetFeaturesStr;
  mutable bool HasCachedDefaultTargetFeatures = false;

  // This should not be moved earlier, since its initialization depends on some
  // of the previous reference members being already initialized and also checks
  // if TheTargetCodeGenInfo is NULL
  TypeEmitter Types;

  std::unique_ptr<DebugEmitter> DebugInfo;

  // A set of references that have only been seen via a weakref so far. This is
  // used to remove the weak of the reference if we ever see a direct reference
  // or a definition.
  llvm::SmallPtrSet<llvm::GlobalValue *, 10> WeakRefReferences;

  llvm::DenseMap<llvm::StringRef, GlobalDecl> DeferredDecls;

  std::vector<GlobalDecl> DeferredDeclsToEmit;
  void addDeferredDeclToEmit(GlobalDecl GD) {
    DeferredDeclsToEmit.emplace_back(GD);
  }

  std::vector<GlobalDecl> Aliases;

  std::vector<GlobalDecl> MultiVersionFuncs;

  llvm::MapVector<llvm::StringRef, llvm::TrackingVH<llvm::Constant>>
      Replacements;

  llvm::SmallVector<std::pair<llvm::GlobalValue *, llvm::Constant *>, 8>
      GlobalValReplacements;

  llvm::DenseMap<const VarDecl *, llvm::GlobalVariable *> InitializerConstants;

  llvm::DenseSet<GlobalDecl> DiagnosedConflictingDefinitions;

  std::vector<llvm::WeakTrackingVH> LLVMUsed;
  std::vector<llvm::WeakTrackingVH> LLVMCompilerUsed;

  CtorList GlobalCtors;

  CtorList GlobalDtors;

  llvm::MapVector<GlobalDecl, llvm::StringRef> MangledDeclNames;
  llvm::StringMap<GlobalDecl, llvm::BumpPtrAllocator> Manglings;

  std::vector<llvm::Constant *> Annotations;

  // Store deferred function annotations so they can be emitted at the end with
  // the most up to date ValueDecl that will have all annotations.
  llvm::DenseMap<llvm::StringRef, const ValueDecl *> DeferredAnnotations;

  llvm::StringMap<llvm::Constant *> AnnotationStrings;

  llvm::DenseMap<unsigned, llvm::Constant *> AnnotationArgs;

  llvm::DenseMap<llvm::Constant *, llvm::GlobalVariable *> ConstantStringMap;
  llvm::DenseMap<const Decl *, llvm::Constant *> StaticLocalDeclMap;
  llvm::DenseMap<const Decl *, llvm::GlobalVariable *> StaticLocalDeclGuardMap;

  typedef llvm::MapVector<IdentifierInfo *, llvm::GlobalValue *>
      StaticExternCMap;
  StaticExternCMap StaticExternCValues;

  std::vector<llvm::Function *> GlobalInits;

  llvm::SmallVector<llvm::MDNode *, 16> LinkerOptionsMetadata;

  llvm::SmallVector<llvm::MDNode *, 16> ELFDependentLibraries;

  bool isTriviallyRecursive(const FunctionDecl *F);
  bool shouldEmitFunction(GlobalDecl GD);
  llvm::DenseMap<const CompoundLiteralExpr *, llvm::GlobalVariable *>
      EmittedCompoundLiterals;

  GlobalDecl initializedGlobalDecl;

  llvm::Function *LifetimeStartFn = nullptr;

  llvm::Function *LifetimeEndFn = nullptr;

  typedef llvm::DenseMap<QualType, llvm::Metadata *> MetadataTypeMap;
  MetadataTypeMap MetadataIdMap;
  MetadataTypeMap GeneralizedMetadataIdMap;

public:
  ModuleEmitter(TreeContext &C,
                llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS,
                const HeaderIndexOptions &headersearchopts,
                const PrepOptions &ppopts, const CodeGenOptions &CodeGenOpts,
                llvm::Module &M, DiagnosticsEngine &Diags);

  ~ModuleEmitter();

  void clear();

  void release();

  bool getExpressionLocationsEnabled() const;

  const std::string &getModuleNameHash() const { return ModuleNameHash; }

  llvm::Constant *getStaticLocalDeclAddress(const VarDecl *D) {
    return StaticLocalDeclMap[D];
  }
  void setStaticLocalDeclAddress(const VarDecl *D, llvm::Constant *C) {
    StaticLocalDeclMap[D] = C;
  }

  llvm::Constant *
  getOrCreateStaticVarDecl(const VarDecl &D,
                           llvm::GlobalValue::LinkageTypes Linkage);

  llvm::GlobalVariable *getStaticLocalDeclGuardAddress(const VarDecl *D) {
    return StaticLocalDeclGuardMap[D];
  }
  void setStaticLocalDeclGuardAddress(const VarDecl *D,
                                      llvm::GlobalVariable *C) {
    StaticLocalDeclGuardMap[D] = C;
  }

  Address createUnnamedGlobalFrom(const VarDecl &D, llvm::Constant *Constant,
                                  CharUnits Align);

  bool lookupRepresentativeDecl(llvm::StringRef MangledName,
                                GlobalDecl &Result) const;

  DebugEmitter *getModuleDebugInfo() { return DebugInfo.get(); }

  TreeContext &getContext() const { return Context; }
  const LangOptions &getLangOpts() const { return LangOpts; }
  const llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> &getFileSystem() const {
    return FS;
  }
  const HeaderIndexOptions &getHeaderIdxOpts() const { return HeaderIdxOpts; }
  const PrepOptions &getPrepOpts() const { return PrepOpts; }
  const CodeGenOptions &getCodeGenOpts() const { return CodeGenOpts; }
  llvm::Module &getModule() const { return TheModule; }
  DiagnosticsEngine &getDiags() const { return Diags; }
  const llvm::DataLayout &getDataLayout() const {
    return TheModule.getDataLayout();
  }
  const TargetInfo &getTarget() const { return Target; }
  const llvm::Triple &getTriple() const { return Target.getTriple(); }
  bool supportsCOMDAT() const;
  void maybeSetTrivialComdat(const Decl &D, llvm::GlobalObject &GO);

  CGABI &getCGABI() const { return *ABI; }
  llvm::LLVMContext &getLLVMContext() { return VMContext; }

  bool shouldUseTBAA() const { return TBAA != nullptr; }

  const TargetCodeGenInfo &getTargetCodeGenInfo();

  TypeEmitter &getTypes() { return Types; }

  CtorList &getGlobalCtors() { return GlobalCtors; }
  CtorList &getGlobalDtors() { return GlobalDtors; }

  llvm::MDNode *getTBAATypeInfo(QualType QTy);

  TBAAAccessInfo getTBAAAccessInfo(QualType AccessType);

  llvm::MDNode *getTBAAStructInfo(QualType QTy);

  llvm::MDNode *getTBAABaseTypeInfo(QualType QTy);

  llvm::MDNode *getTBAAAccessTagInfo(TBAAAccessInfo Info);

  TBAAAccessInfo mergeTBAAInfoForCast(TBAAAccessInfo SourceInfo,
                                      TBAAAccessInfo TargetInfo);

  TBAAAccessInfo mergeTBAAInfoForConditionalOperator(TBAAAccessInfo InfoA,
                                                     TBAAAccessInfo InfoB);

  TBAAAccessInfo mergeTBAAInfoForMemoryTransfer(TBAAAccessInfo DestInfo,
                                                TBAAAccessInfo SrcInfo);

  TBAAAccessInfo getTBAAInfoForSubobject(LValue Base, QualType AccessType) {
    if (Base.getTBAAInfo().isMayAlias())
      return TBAAAccessInfo::getMayAliasInfo();
    return getTBAAAccessInfo(AccessType);
  }

  bool isPaddedAtomicType(QualType type);
  bool isPaddedAtomicType(const AtomicType *type);

  void decorateInstructionWithTBAA(llvm::Instruction *Inst,
                                   TBAAAccessInfo TBAAInfo);

  llvm::ConstantInt *getSize(CharUnits numChars);

  void setGlobalVisibility(llvm::GlobalValue *GV, const NamedDecl *D) const;

  void setDSOLocal(llvm::GlobalValue *GV) const;

  bool shouldMapVisibilityToDLLExport(const NamedDecl *D) const {
    return getLangOpts().hasDefaultVisibilityExportMapping() && D &&
           (D->getLinkageAndVisibility().getVisibility() ==
            DefaultVisibility) &&
           (getLangOpts().isAllDefaultVisibilityExportMapping() ||
            (getLangOpts().isExplicitDefaultVisibilityExportMapping() &&
             D->getLinkageAndVisibility().isVisibilityExplicit()));
  }
  void setDLLImportDLLExport(llvm::GlobalValue *GV, GlobalDecl D) const;
  void setDLLImportDLLExport(llvm::GlobalValue *GV, const NamedDecl *D) const;
  void setGVProperties(llvm::GlobalValue *GV, GlobalDecl GD) const;
  void setGVProperties(llvm::GlobalValue *GV, const NamedDecl *D) const;

  void setGVPropertiesAux(llvm::GlobalValue *GV, const NamedDecl *D) const;

  void setTLSMode(llvm::GlobalValue *GV, const VarDecl &D) const;

  llvm::GlobalVariable::ThreadLocalMode getDefaultLLVMTLSModel() const;

  static llvm::GlobalValue::VisibilityTypes getLLVMVisibility(Visibility V) {
    switch (V) {
    case DefaultVisibility:
      return llvm::GlobalValue::DefaultVisibility;
    case HiddenVisibility:
      return llvm::GlobalValue::HiddenVisibility;
    case ProtectedVisibility:
      return llvm::GlobalValue::ProtectedVisibility;
    }
    llvm_unreachable("unknown visibility!");
  }

  llvm::Constant *
  addrOfGlobal(GlobalDecl GD,
               ForDefinition_t IsForDefinition = NotForDefinition);

  llvm::Function *
  createGlobalInitOrCleanUpFunction(llvm::FunctionType *ty,
                                    const llvm::Twine &name,
                                    const ABIFunctionInfo &FI, bool TLS = false,
                                    llvm::GlobalVariable::LinkageTypes Linkage =
                                        llvm::GlobalVariable::InternalLinkage);

  LangAS getGlobalVarAddressSpace(const VarDecl *D);

  LangAS getGlobalConstantAddressSpace() const;

  llvm::Constant *
  getGlobalVarAddr(const VarDecl *D, llvm::Type *Ty = nullptr,
                   ForDefinition_t IsForDefinition = NotForDefinition);

  llvm::Constant *
  addrOfFunction(GlobalDecl GD, llvm::Type *Ty = nullptr,
                 bool DontDefer = false,
                 ForDefinition_t IsForDefinition = NotForDefinition);

  // Return the function body address of the given function.
  llvm::Constant *getFunctionStart(const ValueDecl *Decl);

  ConstantAddress getWeakRefReference(const ValueDecl *VD);

  CharUnits getMinimumObjectSize(QualType Ty) {
    return getContext().getTypeSizeInChars(Ty);
  }

  llvm::Constant *getConstantArrayFromStringLiteral(const StringLiteral *E);

  ConstantAddress
  addrOfConstantStringFromLiteral(const StringLiteral *S,
                                  llvm::StringRef Name = ".str");

  ConstantAddress addrOfConstantCString(const std::string &Str,
                                        const char *GlobalName = nullptr);

  ConstantAddress addrOfConstantCompoundLiteral(const CompoundLiteralExpr *E);

  llvm::GlobalVariable *
  getAddrOfConstantCompoundLiteralIfEmitted(const CompoundLiteralExpr *E);

  void setAddrOfConstantCompoundLiteral(const CompoundLiteralExpr *CLE,
                                        llvm::GlobalVariable *GV);

  llvm::Constant *getBuiltinLibFunction(const FunctionDecl *FD,
                                        unsigned BuiltinID);

  llvm::Function *getIntrinsic(unsigned IID,
                               llvm::ArrayRef<llvm::Type *> Tys = std::nullopt);

  void lowerTopLevel(Decl *D);

  void genMainVoidAlias();

  void addUsedGlobal(llvm::GlobalValue *GV);

  void addCompilerUsedGlobal(llvm::GlobalValue *GV);

  void addUsedOrCompilerUsedGlobal(llvm::GlobalValue *GV);

  llvm::FunctionCallee
  createRuntimeFunction(llvm::FunctionType *Ty, llvm::StringRef Name,
                        llvm::AttributeList ExtraAttrs = llvm::AttributeList(),
                        bool Local = false, bool AssumeConvergent = false);

  llvm::Constant *createRuntimeVariable(llvm::Type *Ty, llvm::StringRef Name);

  llvm::Function *getLLVMLifetimeStartFn();
  llvm::Function *getLLVMLifetimeEndFn();

  // Make sure that this type is translated.
  void updateCompletedType(const TagDecl *TD);

  void genExplicitCastExprType(const ExplicitCastExpr *E,
                               FunctionEmitter *FE = nullptr);

  llvm::Constant *genNullConstant(QualType T);

  void error(SourceLocation loc, llvm::StringRef message);

  void errorUnsupported(const Stmt *S, const char *Type);

  void errorUnsupported(const Decl *D, const char *Type);

  void setInternalFunctionAttributes(GlobalDecl GD, llvm::Function *F,
                                     const ABIFunctionInfo &FI);

  void setLLVMFunctionAttributes(GlobalDecl GD, const ABIFunctionInfo &Info,
                                 llvm::Function *F);

  void setLLVMFunctionAttributesForDefinition(const Decl *D, llvm::Function *F);

  void setLLVMFunctionFEnvAttributes(const FunctionDecl *D, llvm::Function *F);

  bool returnTypeUsesSRet(const ABIFunctionInfo &FI);

  bool returnSlotInterferesWithArgs(const ABIFunctionInfo &FI);

  void constructAttributeList(llvm::StringRef Name, const ABIFunctionInfo &Info,
                              FnCalleeInfo CalleeInfo,
                              llvm::AttributeList &Attrs, unsigned &CallingConv,
                              bool AttrOnCallSite);

  // in order to generate the library call or the intrinsic for the function
  // name 'Name'.
  void adjustMemoryAttribute(llvm::StringRef Name, FnCalleeInfo CalleeInfo,
                             llvm::AttributeList &Attrs);

  void addDefaultFunctionDefinitionAttributes(llvm::AttrBuilder &attrs);

  llvm::StringRef getMangledName(GlobalDecl GD);
  const GlobalDecl getMangledNameDecl(llvm::StringRef);

  void genTentativeDefinition(const VarDecl *D);

  void genExternalDeclaration(const VarDecl *D);

  void appendLinkerOptions(llvm::StringRef Opts);

  void addDetectMismatch(llvm::StringRef Name, llvm::StringRef Value);

  void addDependentLib(llvm::StringRef Lib);

  llvm::GlobalVariable::LinkageTypes getFunctionLinkage(GlobalDecl GD);

  void setFunctionLinkage(GlobalDecl GD, llvm::Function *F) {
    F->setLinkage(getFunctionLinkage(GD));
  }

  CharUnits getTargetTypeStoreSize(llvm::Type *Ty) const;

  llvm::GlobalValue::LinkageTypes
  getLLVMLinkageForDeclarator(const DeclaratorDecl *D, GVALinkage Linkage);

  llvm::GlobalValue::LinkageTypes
  getLLVMLinkageVarDefinition(const VarDecl *VD);

  void genGlobalAnnotations();

  llvm::Constant *genAnnotationString(llvm::StringRef Str);

  llvm::Constant *genAnnotationUnit(SourceLocation Loc);

  llvm::Constant *genAnnotationLineNo(SourceLocation L);

  llvm::Constant *genAnnotationArgs(const AnnotateAttr *Attr);

  llvm::Constant *genAnnotateAttr(llvm::GlobalValue *GV, const AnnotateAttr *AA,
                                  SourceLocation L);

  void addGlobalAnnotations(const ValueDecl *D, llvm::GlobalValue *GV);

  void lowerGlobal(GlobalDecl D);

  llvm::GlobalValue *getGlobalValue(llvm::StringRef Ref);

  void setCommonAttributes(GlobalDecl GD, llvm::GlobalValue *GV);

  void addReplacement(llvm::StringRef Name, llvm::Constant *C);

  void addGlobalValReplacement(llvm::GlobalValue *GV, llvm::Constant *C);

  llvm::Metadata *createMetadataIdentifierForType(QualType T);

  llvm::Metadata *createMetadataIdentifierGeneralized(QualType T);

  bool mayDropFunctionReturn(const TreeContext &Context,
                             QualType ReturnType) const;

  llvm::FunctionCallee getTerminateFn();

  llvm::Constant *getNullPointer(llvm::PointerType *T, QualType QT);

  CharUnits getNaturalTypeAlignment(QualType T,
                                    LValueBaseInfo *BaseInfo = nullptr,
                                    TBAAAccessInfo *TBAAInfo = nullptr,
                                    bool forPointeeType = false);
  CharUnits getNaturalPointeeTypeAlignment(QualType T,
                                           LValueBaseInfo *BaseInfo = nullptr,
                                           TBAAAccessInfo *TBAAInfo = nullptr);
  bool stopAutoInit();

  void moveLazyEmissionStates(ModuleEmitter *NewBuilder);

  llvm::Constant *
  obtainLLVMGlobal(llvm::StringRef MangledName, llvm::Type *Ty,
                   LangAS AddrSpace, const VarDecl *D,
                   ForDefinition_t IsForDefinition = NotForDefinition);

  void addGlobalCtor(llvm::Function *Ctor, int Priority = 65535,
                     unsigned LexOrder = ~0U,
                     llvm::Constant *AssociatedData = nullptr);
  void addGlobalDtor(llvm::Function *Dtor, int Priority = 65535,
                     bool IsDtorAttrFunc = false);

private:
  llvm::Constant *
  obtainLLVMFunction(llvm::StringRef MangledName, llvm::Type *Ty, GlobalDecl D,
                     bool DontDefer = false,
                     llvm::AttributeList ExtraAttrs = llvm::AttributeList(),
                     ForDefinition_t IsForDefinition = NotForDefinition);

  // References to multiversion functions are resolved through an implicitly
  // defined resolver function. This function is responsible for creating
  // the resolver symbol for the provided declaration. The value returned
  // will be for an ifunc (llvm::GlobalIFunc) if the current target supports
  // that feature and for a regular function (llvm::GlobalValue) otherwise.
  llvm::Constant *getOrCreateMultiVersionResolver(GlobalDecl GD);

  // In scenarios where a function is not known to be a multiversion function
  // until a later declaration, it is sometimes necessary to change the
  // previously created mangled name to align with requirements of whatever
  // multiversion function kind the function is now known to be. This function
  // is responsible for performing such mangled name updates.
  void updateMultiVersionNames(GlobalDecl GD, const FunctionDecl *FD,
                               llvm::StringRef &CurName);

  bool getCPUAndFeaturesAttributes(GlobalDecl GD,
                                   llvm::AttrBuilder &AttrBuilder,
                                   bool SetTargetFeatures = true);
  void setNonAliasAttributes(GlobalDecl GD, llvm::GlobalObject *GO,
                             bool SkipCPUFeatures = false);

  void setFunctionAttributes(GlobalDecl GD, llvm::Function *F,
                             bool IsIncompleteFunction);

  void genGlobalDef(GlobalDecl D, llvm::GlobalValue *GV = nullptr);

  void genGlobalFunctionDefinition(GlobalDecl GD, llvm::GlobalValue *GV);
  void genMultiVersionFunctionDefinition(GlobalDecl GD, llvm::GlobalValue *GV);

  void genGlobalVarDefinition(const VarDecl *D, bool IsTentative = false);
  void genExternalVarDeclaration(const VarDecl *D);
  void genAliasDefinition(GlobalDecl GD);
  void emitIFuncDefinition(GlobalDecl GD);
  void emitCPUDispatchDefinition(GlobalDecl GD);

  void genDeclContext(const DeclContext *DC);

  void genCtorList(CtorList &Fns, const char *GlobalName);

  void genDeferred();

  void applyReplacements();

  void applyGlobalValReplacements();

  void checkAliases();

  std::map<int, llvm::TinyPtrVector<llvm::Function *>> DtorsUsingAtExit;

  void registerGlobalDtorsWithAtExit();

  void emitMultiVersionFunctions();

  void emitLLVMUsed();

  void emitOverrideSection();

  void genModuleLinkOptions();

  bool checkAndReplaceExternCIFuncs(llvm::GlobalValue *Elem,
                                    llvm::GlobalValue *Func);

  void genStaticExternCAliases();

  void genVersionIdentMetadata();

  void genCommandLineMetadata();

  bool mustBeEmitted(const ValueDecl *D);

  bool mayBeEmittedEagerly(const ValueDecl *D);

  void getTrivialDefaultFunctionAttributes(llvm::StringRef Name,
                                           bool HasOptnone, bool AttrOnCallSite,
                                           llvm::AttrBuilder &FuncAttrs);

  void getDefaultFunctionAttributes(llvm::StringRef Name, bool HasOptnone,
                                    bool AttrOnCallSite,
                                    llvm::AttrBuilder &FuncAttrs);

  llvm::Metadata *createMetadataIdentifierImpl(QualType T, MetadataTypeMap &Map,
                                               llvm::StringRef Suffix);
};

} // end namespace Emit
} // end namespace neverc

#endif // NEVERC_LIB_EMIT_CORE_MODULEEMITTER_H
