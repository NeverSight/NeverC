#include "neverc/Compiler/AssembleAction.h"
#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/CompilerInvocation.h"
#include "neverc/Compiler/FrontendActions.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Compiler/FrontendOptions.h"
#include "neverc/Compiler/FrontendTool.h"
#include "neverc/Compiler/TextDiagnosticBuffer.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Emit/Core/EmitterFactory.h"
#include "neverc/Foundation/Core/LLVMTimeTraceRootLease.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "neverc/Invoke/DirectInvocationOpts.h"
#include "neverc/Invoke/LLVMCommandLine.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Plugin/Host/PluginLLVMOptionSnapshot.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/HeaderIndexOptions.h"
#include "neverc/Scan/PrepOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Pass.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace neverc;
using namespace llvm::opt;

namespace neverc {

// ===----------------------------------------------------------------------===
// Action creation
// ===----------------------------------------------------------------------===

std::unique_ptr<FrontendAction> CreateFrontendAction(CompilerInstance &CI) {
  using namespace neverc::frontend;

  auto Action = CI.getFrontendOpts().ProgramAction;
  if (auto EmitAction = CreateEmitterAction(Action))
    return EmitAction;

  switch (Action) {
  case Assemble:
    return std::make_unique<AssembleAction>();
  case ParseSyntaxOnly:
    return std::make_unique<SyntaxOnlyAction>();
  case PrintPreprocessedInput:
    return std::make_unique<PrintPreprocessedAction>();
  case RunPreprocessorOnly:
    return std::make_unique<PreprocessOnlyAction>();
  default:
    break;
  }

  llvm_unreachable("Invalid program action!");
}

// ===----------------------------------------------------------------------===
// Invocation execution
// ===----------------------------------------------------------------------===

namespace {
/// Keeps crash-recovered resources on the heap because setjmp/longjmp abandons
/// the protected stack frame. Outside a recovery context the same resource
/// stays inline and introduces no allocation.
template <typename T> class CrashRecoveryOwnedValue {
public:
  template <typename... Args>
  explicit CrashRecoveryOwnedValue(Args &&...Arguments) {
    if (llvm::CrashRecoveryContext::GetCurrent()) {
      CrashOwned =
          std::make_unique<T>(std::forward<Args>(Arguments)...);
      Active = CrashOwned.get();
      CrashCleanup.emplace(Active);
    } else {
      Local.emplace(std::forward<Args>(Arguments)...);
      Active = &*Local;
    }
  }

  CrashRecoveryOwnedValue(const CrashRecoveryOwnedValue &) = delete;
  CrashRecoveryOwnedValue &operator=(const CrashRecoveryOwnedValue &) = delete;
  CrashRecoveryOwnedValue(CrashRecoveryOwnedValue &&) = delete;
  CrashRecoveryOwnedValue &operator=(CrashRecoveryOwnedValue &&) = delete;

  T &get() const { return *Active; }

private:
  std::optional<T> Local;
  std::unique_ptr<T> CrashOwned;
  std::optional<llvm::CrashRecoveryContextCleanupRegistrar<T>> CrashCleanup;
  T *Active = nullptr;
};

class FrontendActionCrashState {
public:
  FrontendActionCrashState(CompilerInstance &Instance,
                           std::unique_ptr<FrontendAction> Action)
      : Instance(Instance), Action(std::move(Action)) {}

  ~FrontendActionCrashState() {
    if (llvm::CrashRecoveryContext::isRecoveringFromCrash())
      Instance.prepareForCrashRecoveryDestruction();
  }

  FrontendActionCrashState(const FrontendActionCrashState &) = delete;
  FrontendActionCrashState &operator=(const FrontendActionCrashState &) =
      delete;

  FrontendAction *get() const { return Action.get(); }
  std::unique_ptr<FrontendAction> take() { return std::move(Action); }

private:
  CompilerInstance &Instance;
  // Its destructor runs after this class's body, so crash preparation destroys
  // the emitter consumer while the action-owned LLVMContext is still alive.
  std::unique_ptr<FrontendAction> Action;
};

bool extractInvocationScopedLLVMOptions(CompilerInstance &CI);
}

bool ExecuteCompilerInvocation(CompilerInstance *CI) {
  if (CI->getFrontendOpts().ShowHelp) {
    driver::getDriverOptTable().printHelp(
        llvm::outs(), "neverc [options] file...",
        "NeverC Compiler: https://github.com/NeverSight/NeverC",
        /*ShowHidden=*/false, /*ShowAllAliases=*/false,
        llvm::opt::Visibility(driver::options::NeverCOption));
    return true;
  }

  if (CI->getFrontendOpts().ShowVersion) {
    llvm::cl::PrintVersionMessage();
    return true;
  }

  if (!extractInvocationScopedLLVMOptions(*CI))
    return false;

  // Honor -mllvm.
  if (!CI->getFrontendOpts().LLVMArgs.empty()) {
    unsigned NumArgs = CI->getFrontendOpts().LLVMArgs.size();
    auto Args = std::make_unique<const char *[]>(NumArgs + 2);
    Args[0] = "neverc (LLVM option parsing)";
    for (unsigned i = 0; i != NumArgs; ++i)
      Args[i + 1] = CI->getFrontendOpts().LLVMArgs[i].c_str();
    Args[NumArgs + 1] = nullptr;
    if (!parseLLVMCommandLineOptions(NumArgs + 1, Args.get()))
      return false;
  }

  // If there were errors in processing arguments, don't do anything else.
  if (CI->getDiagnostics().hasErrorOccurred())
    return false;

  CrashRecoveryOwnedValue<FrontendActionCrashState> ActionState(
      *CI, CreateFrontendAction(*CI));
  if (!ActionState.get().get())
    return false;
  bool Success = CI->ExecuteAction(*ActionState.get().get());
  if (CI->getFrontendOpts().DisableFree)
    llvm::BuryPointer(ActionState.get().take());
  return Success;
}

namespace {
bool extractInvocationScopedLLVMOptions(CompilerInstance &CI) {
  auto &LLVMArgs = CI.getFrontendOpts().LLVMArgs;
  std::vector<std::string> Remaining;
  Remaining.reserve(LLVMArgs.size());

  for (std::string &Argument : LLVMArgs) {
    llvm::StringRef Spelling(Argument);
    if (!Spelling.consume_front("-")) {
      Remaining.push_back(std::move(Argument));
      continue;
    }
    Spelling.consume_front("-");
    auto [Name, Value] = Spelling.split('=');
    if (Name != "relink-builtin-bitcode-postop") {
      Remaining.push_back(std::move(Argument));
      continue;
    }

    bool Enabled = true;
    if (Spelling.contains('=')) {
      if (Value.empty() || Value == "true" || Value == "TRUE" ||
          Value == "True" || Value == "1") {
        Enabled = true;
      } else if (Value == "false" || Value == "FALSE" || Value == "False" ||
                 Value == "0") {
        Enabled = false;
      } else {
        llvm::errs()
            << "neverc (LLVM option parsing): for the "
               "--relink-builtin-bitcode-postop option: '"
            << Value
            << "' is invalid value for boolean argument! Try 0 or 1\n";
        return false;
      }
    }
    CI.getCodeGenOpts().RelinkBuiltinBitcodePostop = Enabled;
  }

  LLVMArgs = std::move(Remaining);
  return true;
}

class FrontendProcessGlobalStateOwner;

struct DirectLLVMFatalErrorHandlerContext {
  DiagnosticsEngine *Diags = nullptr;
  FrontendProcessGlobalStateOwner *Owner = nullptr;
};

void directLLVMErrorHandler(void *UserData, const char *Message,
                            bool GenCrashDiag);

/// Ends a frontend plugin task without taking ownership of the object. The
/// crash-owned resource aggregate destroys the task object later, after this
/// cleanup has released its PluginSession registration. Keeping transition and
/// ownership separate also preserves child-before-parent task ordering. This
/// cleanup covers a backend fatal after task creation and before task teardown;
/// it is not a crash-abandon state machine for a fatal inside a task callback
/// or end().
class CrashRecoveryEndPluginTaskCleanup final
    : public llvm::CrashRecoveryContextCleanupBase<
          CrashRecoveryEndPluginTaskCleanup, plugin::PluginTaskContext> {
public:
  CrashRecoveryEndPluginTaskCleanup(llvm::CrashRecoveryContext *Context,
                                    plugin::PluginTaskContext *Task)
      : llvm::CrashRecoveryContextCleanupBase<
            CrashRecoveryEndPluginTaskCleanup, plugin::PluginTaskContext>(
            Context, Task) {}

  void recoverResources() override {
    llvm::consumeError(this->resource->end());
  }
};

using FrontendPluginTaskCleanupRegistrar =
    llvm::CrashRecoveryContextCleanupRegistrar<
        plugin::PluginTaskContext, CrashRecoveryEndPluginTaskCleanup>;

class CrashRecoveryClearFrontendTimerCleanup final
    : public llvm::CrashRecoveryContextCleanupBase<
          CrashRecoveryClearFrontendTimerCleanup, CompilerInstance> {
public:
  CrashRecoveryClearFrontendTimerCleanup(llvm::CrashRecoveryContext *Context,
                                         CompilerInstance *Instance)
      : llvm::CrashRecoveryContextCleanupBase<
            CrashRecoveryClearFrontendTimerCleanup, CompilerInstance>(
            Context, Instance) {}

  void recoverResources() override { this->resource->clearFrontendTimer(); }
};

using FrontendTimerCrashCleanupRegistrar =
    llvm::CrashRecoveryContextCleanupRegistrar<
        CompilerInstance, CrashRecoveryClearFrontendTimerCleanup>;

/// Registers the parent first and child second so crash recovery's LIFO list
/// ends the translation-unit task before its invocation parent. Normal task
/// teardown unregisters each entry only after end() reaches its terminal state
/// and before deleting the corresponding object.
class FrontendPluginTaskCrashCleanups {
public:
  FrontendPluginTaskCrashCleanups(
      plugin::PluginTaskContext *InvocationTask,
      plugin::PluginTaskContext *TranslationUnitTask) {
    if (InvocationTask)
      Invocation.emplace(InvocationTask);
    if (TranslationUnitTask)
      TranslationUnit.emplace(TranslationUnitTask);
  }

  FrontendPluginTaskCrashCleanups(
      const FrontendPluginTaskCrashCleanups &) = delete;
  FrontendPluginTaskCrashCleanups &
  operator=(const FrontendPluginTaskCrashCleanups &) = delete;

  void unregisterTranslationUnit() noexcept {
    if (TranslationUnit)
      TranslationUnit->unregister();
  }

  void unregisterInvocation() noexcept {
    if (Invocation)
      Invocation->unregister();
  }

private:
  std::optional<FrontendPluginTaskCleanupRegistrar> Invocation;
  std::optional<FrontendPluginTaskCleanupRegistrar> TranslationUnit;
};

/// Holds every process-global LLVM frontend mutation under one recoverable
/// option-gate lease. Crash recovery skips C++ stack unwinding, so restoring
/// the captured option objects and occurrence metadata, removing the fatal
/// handler, and unlocking the gate must also be registered explicitly with
/// the active recovery context. LLVM options whose callbacks mutate separate
/// storage require their own restoration contract before in-process use.
class FrontendProcessGlobalStateOwner {
public:
  FrontendProcessGlobalStateOwner() { FatalHandlerContext.Owner = this; }

  FrontendProcessGlobalStateOwner(bool MutatesLLVMOptions,
                                  bool RequiresExclusiveLease)
      : FrontendProcessGlobalStateOwner() {
    acquire(MutatesLLVMOptions, RequiresExclusiveLease);
  }

  void acquire(bool MutatesLLVMOptions, bool RequiresExclusiveLease) {
    assert(!OptionSnapshot && !WriteLease && !ReadLease && !Released &&
           "frontend process-global owner acquired twice");
    if (MutatesLLVMOptions) {
      OptionSnapshot.emplace(plugin::pluginLLVMOptionGate());
    } else if (RequiresExclusiveLease) {
      WriteLease.emplace(plugin::pluginLLVMOptionGate());
    } else {
      ReadLease.emplace(plugin::pluginLLVMOptionGate());
    }
  }

  ~FrontendProcessGlobalStateOwner() { release(); }

  FrontendProcessGlobalStateOwner(
      const FrontendProcessGlobalStateOwner &) = delete;
  FrontendProcessGlobalStateOwner &
  operator=(const FrontendProcessGlobalStateOwner &) = delete;
  FrontendProcessGlobalStateOwner(FrontendProcessGlobalStateOwner &&) = delete;
  FrontendProcessGlobalStateOwner &
  operator=(FrontendProcessGlobalStateOwner &&) = delete;

  bool holdsExclusiveLease() const noexcept {
    return OptionSnapshot.has_value() || WriteLease.has_value();
  }

  bool ownsOptionSnapshot() const noexcept {
    return OptionSnapshot.has_value();
  }

  void installFatalErrorHandler(DiagnosticsEngine &Diags) {
    assert(!FatalHandler && "LLVM fatal handler already installed");
    FatalHandlerContext.Diags = &Diags;
    FatalHandler.emplace(directLLVMErrorHandler, &FatalHandlerContext);
  }

  void removeFatalErrorHandler() noexcept {
    FatalHandler.reset();
    FatalHandlerContext.Diags = nullptr;
  }

private:
  void release() noexcept {
    if (Released)
      return;

    removeFatalErrorHandler();

    // A snapshot owns its own exclusive lease and restores both option values
    // and occurrence metadata before releasing that lease. Plain read/write
    // leases are mutually exclusive with the snapshot path.
    OptionSnapshot.reset();
    ReadLease.reset();
    WriteLease.reset();
    Released = true;
  }

  std::optional<plugin::PluginLLVMOptionSnapshot> OptionSnapshot;
  std::optional<plugin::PluginLLVMOptionExclusiveLease> WriteLease;
  std::optional<plugin::PluginLLVMOptionSharedLease> ReadLease;
  DirectLLVMFatalErrorHandlerContext FatalHandlerContext;
  std::optional<llvm::ScopedThreadLocalFatalErrorHandler> FatalHandler;
  bool Released = false;
};

/// Owns every outer ExecuteFrontendDirect resource whose automatic destructor
/// a recovery transfer can skip. Process-global state is deliberately owned by
/// a separate, earlier-registered CrashRecoveryOwnedValue so this aggregate is
/// destroyed first on both normal and fatal paths.
class FrontendDirectResources {
public:
  FrontendDirectResources()
      : DiagnosticID(new DiagnosticIDs()),
        DiagnosticOpts(new DiagnosticOptions()),
        DiagnosticBuffer(new TextDiagnosticBuffer()),
        ParseDiagnostics(DiagnosticID, DiagnosticOpts, DiagnosticBuffer),
        Instance(std::make_unique<CompilerInstance>()) {}

  ~FrontendDirectResources() {
    if (llvm::CrashRecoveryContext::isRecoveringFromCrash() && Instance)
      Instance->prepareForCrashRecoveryDestruction();
  }

  FrontendDirectResources(const FrontendDirectResources &) = delete;
  FrontendDirectResources &operator=(const FrontendDirectResources &) = delete;
  FrontendDirectResources(FrontendDirectResources &&) = delete;
  FrontendDirectResources &operator=(FrontendDirectResources &&) = delete;

  llvm::IntrusiveRefCntPtr<DiagnosticIDs> DiagnosticID;
  llvm::IntrusiveRefCntPtr<DiagnosticOptions> DiagnosticOpts;
  // Owned by ParseDiagnostics; retained only for the one explicit flush.
  TextDiagnosticBuffer *DiagnosticBuffer = nullptr;
  DiagnosticsEngine ParseDiagnostics;
  std::unique_ptr<plugin::PluginTaskContext> PluginInvocationTask;
  std::unique_ptr<CompilerInstance> Instance;
  std::optional<neverc::LLVMTimeTraceProfilerOwner> TimeTraceProfiler;
};

void directLLVMErrorHandler(void *UserData, const char *Message,
                            bool GenCrashDiag) {
  auto &Context = *static_cast<DirectLLVMFatalErrorHandlerContext *>(UserData);
  DiagnosticsEngine *Diags = Context.Diags;
  Context.Owner->removeFatalErrorHandler();
  Diags->Report(diag::err_fe_error_backend) << Message;
  if (llvm::CrashRecoveryContext *CRC =
          llvm::CrashRecoveryContext::GetCurrent()) {
    // This is a diagnosed, same-thread LLVM fatal. The caller owns the output
    // transactions and other recoverable resources; process-wide signal-file
    // cleanup would delete another concurrent invocation's files and Windows'
    // one-shot CleanupExecuted state would poison later recoveries.
    CRC->DumpStackAndCleanupOnFailure = false;
    CRC->HandleExit(GenCrashDiag ? 70 : 1);
  }
  llvm::sys::RunInterruptHandlers();
  llvm::sys::Process::Exit(GenCrashDiag ? 70 : 1);
}

bool finishFrontendPluginTasks(
    CompilerInstance *CI,
    std::unique_ptr<plugin::PluginTaskContext> &InvocationTask,
    FrontendPluginTaskCrashCleanups &CrashCleanups) {
  llvm::Error CleanupErrors = llvm::Error::success();
  if (CI) {
    std::unique_ptr<plugin::PluginTaskContext> TranslationUnitTask =
        CI->takePluginTaskContext();
    if (TranslationUnitTask) {
      CleanupErrors = llvm::joinErrors(
          std::move(CleanupErrors), TranslationUnitTask->end());
      // end() may return cleanup diagnostics after reaching Ended. Only that
      // terminal state permits removing the crash cleanup and destroying the
      // task here. A non-terminal result preserves both until the enclosing
      // teardown, but does not make a live child/callback recoverable; callers
      // must not enter normal teardown with either still active.
      if (TranslationUnitTask->isEnded())
        CrashCleanups.unregisterTranslationUnit();
      else
        CI->setPluginTaskContext(std::move(TranslationUnitTask));
    }
  }
  if (InvocationTask) {
    CleanupErrors = llvm::joinErrors(
        std::move(CleanupErrors), InvocationTask->end());
    if (InvocationTask->isEnded()) {
      CrashCleanups.unregisterInvocation();
      InvocationTask.reset();
    }
  }
  if (!CleanupErrors)
    return true;
  if (CI && CI->hasDiagnostics())
    CI->getDiagnostics().Report(diag::err_drv_plugin_phase)
        << ("failed to end frontend plugin tasks: " +
            llvm::toString(std::move(CleanupErrors)).str().str());
  else
    llvm::consumeError(std::move(CleanupErrors));
  return false;
}

} // namespace
int ExecuteFrontendDirect(llvm::ArrayRef<const char *> Argv, const char *Argv0,
                          void *MainAddr,
                          const driver::DirectInvocationOpts *DirectOpts) {
  // NeverC compiles entirely in-process: the driver's ConstructJob builds a
  // frontend argv that CreateFromArgs parses here; DirectInvocationOpts
  // overlays domains ConstructJob already resolved canonically.
  bool parallelSafe = DirectOpts && DirectOpts->ParallelSafe;

  // Registration order is part of the recovery contract. The process-global
  // owner is registered first, so every frontend object and plugin task is
  // retired before the option snapshot is restored and its gate is unlocked.
  CrashRecoveryOwnedValue<FrontendProcessGlobalStateOwner> ProcessGlobals;
  CrashRecoveryOwnedValue<FrontendDirectResources> ResourceOwner;
  FrontendDirectResources &Resources = ResourceOwner.get();
  std::unique_ptr<plugin::PluginTaskContext> &PluginInvocationTask =
      Resources.PluginInvocationTask;
  std::unique_ptr<CompilerInstance> &CI = Resources.Instance;
  DiagnosticsEngine &Diags = Resources.ParseDiagnostics;
  TextDiagnosticBuffer *DiagsBuffer = Resources.DiagnosticBuffer;
  std::optional<neverc::LLVMTimeTraceProfilerOwner> &TimeTraceProfiler =
      Resources.TimeTraceProfiler;
  if (DirectOpts && DirectOpts->Outputs)
    CI->setOutputCoordinator(*DirectOpts->Outputs);
  if (DirectOpts && DirectOpts->PluginSession) {
    auto Invocation =
        DirectOpts->PluginSession->createTask(NEVERC_TASK_INVOCATION);
    if (!Invocation) {
      llvm::consumeError(Invocation.takeError());
      return 1;
    }
    PluginInvocationTask = std::move(*Invocation);
    auto TranslationUnit = DirectOpts->PluginSession->createTask(
        NEVERC_TASK_TRANSLATION_UNIT, PluginInvocationTask.get());
    if (!TranslationUnit) {
      llvm::consumeError(TranslationUnit.takeError());
      return 1;
    }
    CI->setPluginTaskContext(std::move(*TranslationUnit));
  }
  llvm::ArrayRef<const char *> InvocationArgs = Argv;
  if (DirectOpts && DirectOpts->FrontendOpts &&
      DirectOpts->FrontendOpts->ProgramAction == frontend::Assemble)
    InvocationArgs = {};
  bool Success = CompilerInvocation::CreateFromArgs(
      CI->getInvocation(), InvocationArgs, Diags, Argv0);

  // NeverC always uses the integrated assembler; ignore -fno-integrated-as.
  CI->getInvocation().getCodeGenOpts().DisableIntegratedAS = false;

  // Override option domains that the driver pre-built directly.
  if (DirectOpts) {
    if (DirectOpts->TargetOpts)
      CI->getInvocation().getTargetOpts() = *DirectOpts->TargetOpts;
    if (DirectOpts->LangOpts)
      CI->getInvocation().getLangOpts() = *DirectOpts->LangOpts;
    if (DirectOpts->CodeGenOpts)
      CI->getInvocation().getCodeGenOpts() = *DirectOpts->CodeGenOpts;
    if (DirectOpts->HeaderIdxOpts)
      CI->getInvocation().getHeaderIdxOpts() = *DirectOpts->HeaderIdxOpts;
    if (DirectOpts->PPOpts)
      CI->getInvocation().getPrepOpts() = *DirectOpts->PPOpts;
    if (DirectOpts->FrontendOpts)
      CI->getInvocation().getFrontendOpts() = *DirectOpts->FrontendOpts;

    // The frozen dyncode request travels task-locally instead
    // of via a process-global singleton.
    if (DirectOpts->DynCode)
      CI->setDynCodeContext(DirectOpts->DynCode);

    // In-process cc1 shares the InMemoryFileStore with the linker, so
    // LTO bitcode can stay in memory instead of hitting the filesystem.
    // This is only set when the driver knows a linker step follows in
    // the same process (not for -c / -S / -E / -fsyntax-only).
    if (DirectOpts->InMemoryLTOOutput) {
      CI->getCodeGenOpts().InMemoryLTOOutput = true;
      CI->getCodeGenOpts().DiscardValueNames = true;
      CI->getCodeGenOpts().EmitVersionIdentMetadata = false;
    }
  }

  if (!extractInvocationScopedLLVMOptions(*CI))
    return 1;

  // A frontend with no LLVM options only reads the process-global registry
  // and can share the gate with other parallel-safe invocations. An invocation
  // that must install -mllvm state owns the gate through the entire frontend
  // action. Parse it here and clear the deferred list so
  // ExecuteCompilerInvocation does not recursively acquire the same gate.
  auto &LLVMArgs = CI->getFrontendOpts().LLVMArgs;
  const bool ConfiguresPassTiming =
      CI->getCodeGenOpts().TimePasses || CI->getCodeGenOpts().TimePassesPerRun;
  bool MutatesLLVMOptions =
      !LLVMArgs.empty() || !CI->getCodeGenOpts().DebugPass.empty() ||
      !CI->getCodeGenOpts().LimitFloatPrecision.empty() || ConfiguresPassTiming;
  const bool RequestsTimeTrace =
      !CI->getFrontendOpts().TimeTracePath.empty();
  ProcessGlobals.get().acquire(MutatesLLVMOptions,
                               /*RequiresExclusiveLease=*/
                                   !parallelSafe || RequestsTimeTrace);
  // These narrower cleanups are registered after the aggregate owner, so LIFO
  // recovery first detaches the frontend timer and ends child/parent plugin
  // tasks. The aggregate can then break the remaining ownership graph and
  // safely destroy CompilerInstance before releasing process-global state.
  FrontendPluginTaskCrashCleanups PluginTaskCrashCleanups(
      PluginInvocationTask.get(), CI->getPluginTaskContext());
  // CrashRecoveryContext runs cleanups in reverse registration order. Detach
  // the abandoned frontend timer before ending plugin tasks, matching normal
  // execution where the frontend timing scope ends before task teardown.
  FrontendTimerCrashCleanupRegistrar FrontendTimerCrashCleanup(CI.get());
  if (MutatesLLVMOptions) {
    assert(ProcessGlobals.get().ownsOptionSnapshot() &&
           "LLVM option mutation requires a restoring snapshot");
    std::vector<const char *> Args;
    Args.reserve(LLVMArgs.size() + 1);
    Args.push_back("neverc (LLVM option parsing)");
    for (const std::string &Argument : LLVMArgs)
      Args.push_back(Argument.c_str());
    // Even an otherwise empty option profile must pass through LLVM's parser:
    // ResetAllOptionOccurrences removes default options such as the help
    // alias, and ParseCommandLineOptions is what registers them again.
    if (!parseLLVMCommandLineOptions(Args.size(), Args.data())) {
      LLVMArgs.clear();
      return 1;
    }
    LLVMArgs.clear();
  }

  // LLVM's legacy timing switches are process globals. Configure them only
  // while holding the option gate exclusively; parallel-safe frontends that
  // do not request timing keep a shared lease and never write this state.
  // Preserve an equivalent -mllvm timing request parsed immediately above.
  if (MutatesLLVMOptions) {
    llvm::TimePassesIsEnabled =
        llvm::TimePassesIsEnabled || CI->getCodeGenOpts().TimePasses;
    if (ConfiguresPassTiming)
      llvm::TimePassesPerRun = CI->getCodeGenOpts().TimePassesPerRun;
  }

  if (CI->getFrontendOpts().PrintSupportedCPUs) {
    std::string Error;
    const llvm::Target *TheTarget =
        llvm::TargetRegistry::lookupTarget(CI->getTargetOpts().Triple, Error);
    if (!TheTarget) {
      llvm::errs() << Error;
      return 1;
    }
    llvm::TargetOptions Options;
    std::unique_ptr<llvm::TargetMachine> TM(TheTarget->createTargetMachine(
        CI->getTargetOpts().Triple, "", "+cpuhelp", Options, std::nullopt));
    return 0;
  }

  if (CI->getHeaderIdxOpts().UseBuiltinIncludes &&
      CI->getHeaderIdxOpts().ResourceDir.empty())
    CI->getHeaderIdxOpts().ResourceDir =
        CompilerInvocation::GetResourcesPath(Argv0, MainAddr);

  CI->createDiagnostics();
  if (!CI->hasDiagnostics())
    return 1;

  DiagsBuffer->FlushDiagnostics(CI->getDiagnostics());
  if (!Success) {
    CI->getDiagnosticClient().finish();
    return 1;
  }

  // A fatal can abandon ExecuteCompiler's scope. Join a managed ambient root
  // so recovery invalidates it, or reject an unmanaged root before entering
  // that scope; a normal invocation without a recovery context may borrow the
  // caller's profiler lexically without taking ownership.
  const bool GuardAmbientTimeTrace =
      !RequestsTimeTrace && llvm::CrashRecoveryContext::GetCurrent() &&
      llvm::timeTraceProfilerEnabled();
  if (RequestsTimeTrace || GuardAmbientTimeTrace) {
    TimeTraceProfiler.emplace(CI->getFrontendOpts().TimeTraceGranularity,
                              Argv0);
    if (llvm::StringRef Error = TimeTraceProfiler->acquisitionError();
        !Error.empty()) {
      CI->getDiagnostics().Report(diag::err_fe_backend_unsupported)
          << (llvm::Twine("cannot start frontend time trace: ") + Error).str();
      CI->getDiagnosticClient().finish();
      return 1;
    }
  }

  const bool HoldsExclusiveProcessGlobals =
      ProcessGlobals.get().holdsExclusiveLease();
  ProcessGlobals.get().installFatalErrorHandler(CI->getDiagnostics());

  {
    llvm::TimeTraceScope TimeScope("ExecuteCompiler");
    Success = ExecuteCompilerInvocation(CI.get());
  }

  if (HoldsExclusiveProcessGlobals) {
    llvm::TimerGroup::printAll(llvm::errs());
    llvm::TimerGroup::clearAll();
  }

  if (RequestsTimeTrace && TimeTraceProfiler &&
      TimeTraceProfiler->state() == LLVMTimeTraceRootLeaseState::Owned) {
    if (!CI->hasFileManager())
      CI->createFileManager(createVFSFromCompilerInvocation(
          CI->getInvocation(), CI->getDiagnostics()));

    auto ReportTraceWriteError = [&](llvm::Error Error) {
      CI->getDiagnostics().Report(diag::err_fe_backend_unsupported)
          << (llvm::Twine("could not write frontend time trace: ") +
              llvm::toString(std::move(Error)))
                 .str();
      Success = false;
    };

    // CompilerInstance's generic output registry cannot query a
    // raw_pwrite_stream for fd errors. Keep stdout and existing device/FIFO
    // sinks out of that registry, and let the typed fd writer flush, close
    // when appropriate, and return the error without a fatal destructor.
    llvm::SmallString<256> ResolvedTracePath(
        CI->getFrontendOpts().TimeTracePath);
    if (ResolvedTracePath != "-" &&
        !llvm::sys::path::is_absolute(ResolvedTracePath))
      CI->getFileManager().FixupRelativePath(ResolvedTracePath);
    llvm::sys::fs::file_status TraceStatus;
    std::error_code TraceStatusError;
    if (ResolvedTracePath != "-")
      TraceStatusError = llvm::sys::fs::status(ResolvedTracePath, TraceStatus);
    const bool UsesDirectFD =
        ResolvedTracePath == "-" ||
        (!TraceStatusError && llvm::sys::fs::exists(TraceStatus) &&
         !llvm::sys::fs::is_regular_file(TraceStatus));

    if (UsesDirectFD) {
      if (llvm::Error Error = TimeTraceProfiler->write(
              ResolvedTracePath, ResolvedTracePath))
        ReportTraceWriteError(std::move(Error));
    } else if (auto profilerOutput = CI->createOutputFile(
                   CI->getFrontendOpts().TimeTracePath, /*Binary=*/false,
                   /*RemoveFileOnSignal=*/false, /*useTemporary=*/false)) {
      bool TraceWritten = true;
      if (llvm::Error Error = TimeTraceProfiler->write(*profilerOutput)) {
        ReportTraceWriteError(std::move(Error));
        TraceWritten = false;
      }
      profilerOutput.reset();
      CI->clearOutputFiles(/*EraseFiles=*/!TraceWritten);
      if (CI->getDiagnostics().hasErrorOccurred())
        Success = false;
    } else
      Success = false;
  }

  if (CI->getFrontendOpts().DisableFree) {
    bool PluginTasksEnded = finishFrontendPluginTasks(
        CI.get(), PluginInvocationTask, PluginTaskCrashCleanups);
    llvm::BuryPointer(std::move(CI));
    return !Success || !PluginTasksEnded;
  }

  bool PluginTasksEnded = finishFrontendPluginTasks(
      CI.get(), PluginInvocationTask, PluginTaskCrashCleanups);
  return !Success || !PluginTasksEnded;
}

} // namespace neverc
