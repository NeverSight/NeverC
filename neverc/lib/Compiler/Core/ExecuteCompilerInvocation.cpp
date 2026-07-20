#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/CompilerInvocation.h"
#include "neverc/Compiler/AssembleAction.h"
#include "neverc/Compiler/FrontendActions.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Compiler/FrontendOptions.h"
#include "neverc/Compiler/FrontendTool.h"
#include "neverc/Compiler/TextDiagnosticBuffer.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Emit/Core/EmitterFactory.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "neverc/Foundation/Target/TargetOptions.h"
#include "neverc/Invoke/DirectInvocationOpts.h"
#include "neverc/Invoke/LLVMCommandLine.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/HeaderIndexOptions.h"
#include "neverc/Scan/PrepOptions.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>
#include <string>
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

  // Honor -mllvm.
  if (!CI->getFrontendOpts().LLVMArgs.empty()) {
    unsigned NumArgs = CI->getFrontendOpts().LLVMArgs.size();
    auto Args = std::make_unique<const char *[]>(NumArgs + 2);
    Args[0] = "neverc (LLVM option parsing)";
    for (unsigned i = 0; i != NumArgs; ++i)
      Args[i + 1] = CI->getFrontendOpts().LLVMArgs[i].c_str();
    Args[NumArgs + 1] = nullptr;
    parseLLVMCommandLineOptions(NumArgs + 1, Args.get());
  }

  // If there were errors in processing arguments, don't do anything else.
  if (CI->getDiagnostics().hasErrorOccurred())
    return false;

  std::unique_ptr<FrontendAction> Act(CreateFrontendAction(*CI));
  if (!Act)
    return false;
  bool Success = CI->ExecuteAction(*Act);
  if (CI->getFrontendOpts().DisableFree)
    llvm::BuryPointer(std::move(Act));
  return Success;
}

namespace {
void directLLVMErrorHandler(void *UserData, const char *Message,
                            bool GenCrashDiag) {
  DiagnosticsEngine &Diags = *static_cast<DiagnosticsEngine *>(UserData);
  Diags.Report(diag::err_fe_error_backend) << Message;
  llvm::sys::RunInterruptHandlers();
  llvm::sys::Process::Exit(GenCrashDiag ? 70 : 1);
}

bool finishFrontendPluginTasks(
    CompilerInstance *CI,
    std::unique_ptr<plugin::PluginTaskContext> &InvocationTask) {
  llvm::Error CleanupErrors = llvm::Error::success();
  if (CI) {
    std::unique_ptr<plugin::PluginTaskContext> TranslationUnitTask =
        CI->takePluginTaskContext();
    if (TranslationUnitTask)
      CleanupErrors = llvm::joinErrors(
          std::move(CleanupErrors), TranslationUnitTask->end());
  }
  if (InvocationTask) {
    CleanupErrors = llvm::joinErrors(
        std::move(CleanupErrors), InvocationTask->end());
    InvocationTask.reset();
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
  std::optional<plugin::PluginLLVMOptionExclusiveLease> LLVMOptionWriteLease;
  std::optional<plugin::PluginLLVMOptionSharedLease> LLVMOptionReadLease;

  std::unique_ptr<plugin::PluginTaskContext> PluginInvocationTask;
  std::unique_ptr<CompilerInstance> CI(new CompilerInstance());
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
  llvm::IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());

  llvm::IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts =
      new DiagnosticOptions();
  TextDiagnosticBuffer *DiagsBuffer = new TextDiagnosticBuffer;
  DiagnosticsEngine Diags(DiagID, &*DiagOpts, DiagsBuffer);

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

  // A frontend with no LLVM options only reads the process-global registry
  // and can share the gate with other parallel-safe invocations. An invocation
  // that must install -mllvm state owns the gate through the entire frontend
  // action. Parse it here and clear the deferred list so
  // ExecuteCompilerInvocation does not recursively acquire the same gate.
  auto &LLVMArgs = CI->getFrontendOpts().LLVMArgs;
  bool MutatesLLVMOptions = !LLVMArgs.empty() ||
                            !CI->getCodeGenOpts().DebugPass.empty() ||
                            !CI->getCodeGenOpts().LimitFloatPrecision.empty();
  if (!parallelSafe || MutatesLLVMOptions) {
    LLVMOptionWriteLease.emplace(plugin::pluginLLVMOptionGate());
    if (!LLVMArgs.empty()) {
      std::vector<const char *> Args;
      Args.reserve(LLVMArgs.size() + 1);
      Args.push_back("neverc (LLVM option parsing)");
      for (const std::string &Argument : LLVMArgs)
        Args.push_back(Argument.c_str());
      parseLLVMCommandLineOptions(Args.size(), Args.data());
      LLVMArgs.clear();
    } else
      llvm::cl::ResetAllOptionOccurrences();
  } else {
    LLVMOptionReadLease.emplace(plugin::pluginLLVMOptionGate());
  }

  if (!parallelSafe && !CI->getFrontendOpts().TimeTracePath.empty())
    llvm::timeTraceProfilerInitialize(
        CI->getFrontendOpts().TimeTraceGranularity, Argv0);

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

  if (!parallelSafe)
    llvm::install_fatal_error_handler(
        directLLVMErrorHandler, static_cast<void *>(&CI->getDiagnostics()));

  DiagsBuffer->FlushDiagnostics(CI->getDiagnostics());
  if (!Success) {
    CI->getDiagnosticClient().finish();
    return 1;
  }

  {
    llvm::TimeTraceScope TimeScope("ExecuteCompiler");
    Success = ExecuteCompilerInvocation(CI.get());
  }

  if (!parallelSafe) {
    llvm::TimerGroup::printAll(llvm::errs());
    llvm::TimerGroup::clearAll();

    if (llvm::timeTraceProfilerEnabled()) {
      if (!CI->hasFileManager())
        CI->createFileManager(createVFSFromCompilerInvocation(
            CI->getInvocation(), CI->getDiagnostics()));
      if (auto profilerOutput = CI->createOutputFile(
              CI->getFrontendOpts().TimeTracePath, /*Binary=*/false,
              /*RemoveFileOnSignal=*/false, /*useTemporary=*/false)) {
        llvm::timeTraceProfilerWrite(*profilerOutput);
        profilerOutput.reset();
        llvm::timeTraceProfilerCleanup();
        CI->clearOutputFiles(false);
      }
    }

    llvm::remove_fatal_error_handler();
  }

  if (CI->getFrontendOpts().DisableFree) {
    bool PluginTasksEnded =
        finishFrontendPluginTasks(CI.get(), PluginInvocationTask);
    llvm::BuryPointer(std::move(CI));
    return !Success || !PluginTasksEnded;
  }

  bool PluginTasksEnded =
      finishFrontendPluginTasks(CI.get(), PluginInvocationTask);
  return !Success || !PluginTasksEnded;
}

} // namespace neverc
