#ifndef NEVERC_LIB_EMIT_BACKEND_BACKENDCONSUMER_H
#define NEVERC_LIB_EMIT_BACKEND_BACKENDCONSUMER_H

#include "neverc/Emit/Backend/BackendUtil.h"
#include "neverc/Emit/Core/EmitterAction.h"
#include "neverc/Foundation/Core/SourceLocation.h"

#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class DiagnosticInfoDontCall;
}

namespace neverc {

using llvm::raw_pwrite_stream;

class TreeContext;
class EmitterAction;
class PrepOptions;

namespace plugin {
class PluginIRGenProviderRuntime;
class PluginSourcePhaseRuntime;
class PluginTaskContext;
} // namespace plugin

namespace dyncode {
class DynCodeExecutionContext;
} // namespace dyncode

class EmitterConsumer : public TreeConsumer {
  using LinkModule = EmitterAction::LinkModule;

  virtual void anchor();
  DiagnosticsEngine &Diags;
  BackendAction Action;
  const HeaderIndexOptions &HeaderIdxOpts;
  const CodeGenOptions &CodeGenOpts;
  const TargetOptions &TargetOpts;
  const LangOptions &LangOpts;
  std::unique_ptr<raw_pwrite_stream> AsmOutStream;
  TreeContext *Context;
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS;

  llvm::Timer LLVMIRGeneration;
  unsigned LLVMIRGenerationRefCount;

  bool IRGenFinished = false;

  bool TimerIsEnabled = false;

  std::unique_ptr<IRGenerator> Gen;
  std::unique_ptr<plugin::PluginIRGenProviderRuntime> PluginIRGen;
  plugin::PluginTaskContext *PluginTask = nullptr;
  plugin::PluginSourcePhaseRuntime *PluginSourcePhases = nullptr;
  // Frozen dyncode request for this codegen; null when not a
  // dyncode compile.  Threaded task-locally from the CompilerInstance.
  std::shared_ptr<const dyncode::DynCodeExecutionContext> DynCodeCtx;
  std::unique_ptr<llvm::Module> PluginGeneratedModule;
  bool BuiltinIRGenFinished = false;
  bool PluginSemanticUnitReady = false;

  llvm::SmallVector<LinkModule, 4> LinkModules;

  // A map from mangled names to their function's source location, used for
  // backend diagnostics as the NeverC AST may be unavailable. We actually use
  // the mangled name's hash as the key because mangled names can be very
  // long and take up lots of space. Using a hash can cause name collision,
  // but that is rare and the consequences are pointing to a wrong source
  // location which is not severe. This is a vector instead of an actual map
  // because we optimize for time building this map rather than time
  // retrieving an entry, as backend diagnostics are uncommon.
  std::vector<std::pair<llvm::hash_code, FullSourceLoc>> ManglingFullSourceLocs;

  // This is here so that the diagnostic printer knows the module a diagnostic
  // refers to.
  llvm::Module *CurLinkModule = nullptr;

  llvm::Expected<llvm::Module *> generateBuiltinIRModule();
  void completePluginIRGen();

public:
  EmitterConsumer(BackendAction Action, DiagnosticsEngine &Diags,
                  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
                  const HeaderIndexOptions &HeaderIdxOpts,
                  const PrepOptions &PPOpts, const CodeGenOptions &CodeGenOpts,
                  const TargetOptions &TargetOpts, const LangOptions &LangOpts,
                  const std::string &InFile,
                  llvm::SmallVector<LinkModule, 4> LinkModules,
                  std::unique_ptr<raw_pwrite_stream> OS, llvm::LLVMContext &C);

  // This constructor is used in installing an empty EmitterConsumer
  // to use the NeverC diagnostic handler for IR input files. It avoids
  // initializing the OS field.
  EmitterConsumer(BackendAction Action, DiagnosticsEngine &Diags,
                  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
                  const HeaderIndexOptions &HeaderIdxOpts,
                  const PrepOptions &PPOpts, const CodeGenOptions &CodeGenOpts,
                  const TargetOptions &TargetOpts, const LangOptions &LangOpts,
                  llvm::Module *Module,
                  llvm::SmallVector<LinkModule, 4> LinkModules,
                  llvm::LLVMContext &C);
  ~EmitterConsumer() override;

  llvm::Error
  enablePluginIRGen(plugin::PluginTaskContext &Task,
                    plugin::PluginSourcePhaseRuntime &SourcePhases,
                    llvm::StringRef TargetTriple, llvm::StringRef DataLayout);

  void setDynCodeContext(
      std::shared_ptr<const dyncode::DynCodeExecutionContext> Value) {
    DynCodeCtx = std::move(Value);
  }

  llvm::Module *getModule() const;
  std::unique_ptr<llvm::Module> takeModule();

  IRGenerator *getCodeGenerator();

  void Initialize(TreeContext &Ctx) override;
  bool ProcessTopLevelDecl(DeclGroupRef D) override;
  void ProcessInlineFunctionDefinition(FunctionDecl *D) override;
  void ProcessInterestingDecl(DeclGroupRef D) override;
  void ProcessTranslationUnit(TreeContext &C) override;
  void ProcessTagDeclDefinition(TagDecl *D) override;
  void ProcessTagDeclRequiredDefinition(const TagDecl *D) override;
  void FinalizeTentativeDefinition(VarDecl *D) override;
  void FinalizeExternalDeclaration(VarDecl *D) override;

  // Links each entry in LinkModules into our module.  Returns true on error.
  bool LinkInModules(llvm::Module *M, bool ShouldLinkFiles = true);

  const FullSourceLoc
  getBestLocationFromDebugLoc(const llvm::DiagnosticInfoWithLocationBase &D,
                              bool &BadDebugInfo, llvm::StringRef &Filename,
                              unsigned &Line, unsigned &Column) const;

  std::optional<FullSourceLoc>
  getFunctionSourceLocation(const llvm::Function &F) const;

  void DiagnosticHandlerImpl(const llvm::DiagnosticInfo &DI);
  bool InlineAsmDiagHandler(const llvm::DiagnosticInfoInlineAsm &D);
  void SrcMgrDiagHandler(const llvm::DiagnosticInfoSrcMgr &D);
  bool StackSizeDiagHandler(const llvm::DiagnosticInfoStackSize &D);
  bool ResourceLimitDiagHandler(const llvm::DiagnosticInfoResourceLimit &D);

  void UnsupportedDiagHandler(const llvm::DiagnosticInfoUnsupported &D);
  void genOptimizationMessage(const llvm::DiagnosticInfoOptimizationBase &D,
                              unsigned DiagID);
  void OptimizationRemarkHandler(const llvm::DiagnosticInfoOptimizationBase &D);
  void
  OptimizationRemarkHandler(const llvm::OptimizationRemarkAnalysisFPCommute &D);
  void
  OptimizationRemarkHandler(const llvm::OptimizationRemarkAnalysisAliasing &D);
  void
  OptimizationFailureHandler(const llvm::DiagnosticInfoOptimizationFailure &D);
  void DontCallDiagHandler(const llvm::DiagnosticInfoDontCall &D);
};

} // namespace neverc
#endif // NEVERC_LIB_EMIT_BACKEND_BACKENDCONSUMER_H
