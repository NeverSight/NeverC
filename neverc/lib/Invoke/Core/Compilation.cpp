#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/InMemoryFileStore.h"
#include "neverc/Invoke/Job.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Invoke/Tool.h"
#include "neverc/Invoke/ToolChain.h"
#include "neverc/Invoke/Util.h"
#include "Plugin/JobExecutionBridge.h"
#include "Plugin/JobGraph.h"
#include "neverc/Plugin/Host/PluginLLVMOptionSnapshot.h"
#include "neverc/Plugin/Host/LinkExecutionHooksBridge.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/thread.h"
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace neverc;
using namespace driver;
using namespace llvm::opt;

// ===----------------------------------------------------------------------===
// Construction & temp file management
// ===----------------------------------------------------------------------===

Compilation::Compilation(const Driver &D, const ToolChain &_DefaultToolChain,
                         InputArgList *_Args, DerivedArgList *_TranslatedArgs,
                         bool ContainsError)
    : TheDriver(D), DefaultToolChain(_DefaultToolChain), Args(_Args),
      TranslatedArgs(_TranslatedArgs), ContainsError(ContainsError) {}

Compilation::~Compilation() {
  neverc::InMemoryFileStore::instance().clear();

  // Remove temporary files. This must be done before arguments are freed, as
  // the file names might be derived from the input arguments.
  if (!TheDriver.isSaveTempsEnabled() && !ForceKeepTempFiles)
    CleanupFileList(TempFiles);

  delete TranslatedArgs;
  delete Args;

  // Free any derived arg lists.
  for (auto Arg : TCArgs)
    if (Arg.second != TranslatedArgs)
      delete Arg.second;
}

void Compilation::configurePluginSession(Command &C) {
  if (C.getKind() == Command::CK_FrontendCommand) {
    DirectInvocationOpts &DirectOpts =
        static_cast<FrontendCommand &>(C).getDirectOpts();
    DirectOpts.Outputs = &Outputs;
    if (PluginSession)
      DirectOpts.PluginSession = PluginSession;
    // Thread the frozen dyncode request to the in-process cc1
    // codegen instead of a process-global singleton.
    if (getDriver().DynCodeContext)
      DirectOpts.DynCode = getDriver().DynCodeContext;
  } else if (C.getKind() == Command::CK_LinkerCommand) {
    auto &Linker = static_cast<LinkerCommand &>(C);
    ::linker::LinkerDriverConfig &Config = Linker.getDriverConfig();
    auto Request = std::make_shared<::linker::LinkExecutionRequest>();
    Request->TargetTriple = DefaultToolChain.getTripleString();
    Request->OutputURI = Config.outputFile;
    Request->OutputKind =
        Config.relocatable
            ? ::linker::LinkExecutionOutputKind::Relocatable
            : (Config.shared
                   ? ::linker::LinkExecutionOutputKind::SharedLibrary
                   : (Config.bundle
                          ? ::linker::LinkExecutionOutputKind::Bundle
                          : ::linker::LinkExecutionOutputKind::Executable));
    Request->Inputs.reserve(C.getInputInfos().size());
    for (const InputInfo &Input : C.getInputInfos()) {
      std::string Path;
      if (Input.isFilename())
        Path = Input.getFilename();
      else if (Input.isInputArg())
        Path = Input.getInputArg().getValue();
      else
        continue;

      ::linker::LinkExecutionInput LinkInput;
      LinkInput.Ordinal = Request->Inputs.size();
      LinkInput.LogicalURI = Path;
      const auto Extension =
          llvm::sys::path::extension(Path).lower();
      if (Extension == ".a" || Extension == ".lib")
        LinkInput.Kind = ::linker::LinkExecutionInputKind::Archive;
      else if (Extension == ".so" || Extension == ".dylib" ||
               Extension == ".dll")
        LinkInput.Kind =
            ::linker::LinkExecutionInputKind::SharedLibrary;
      else if (Extension == ".bc" ||
               Input.getType() == types::TY_LLVM_BC ||
               Input.getType() == types::TY_LTO_BC)
        LinkInput.Kind = ::linker::LinkExecutionInputKind::Bitcode;
      else
        LinkInput.Kind = ::linker::LinkExecutionInputKind::Object;
      Request->Inputs.push_back(std::move(LinkInput));
    }
    Request->ArgumentProvenance.reserve(C.getArguments().size());
    for (const char *Argument : C.getArguments())
      Request->ArgumentProvenance.emplace_back(Argument ? Argument : "");
    Config.executionRequest = std::move(Request);

    if (PluginSession) {
      Config.pluginSession = PluginSession;
      Config.executionHooks =
          std::make_shared<plugin::LinkExecutionHooksBridge>(
              PluginSession, Outputs);
    }
  }
}

void Compilation::addCommand(std::unique_ptr<Command> C) {
  if (C)
    configurePluginSession(*C);
  Jobs.addJob(std::move(C));
}

const char *Compilation::addResultFile(const char *Name,
                                       const JobAction *JA) {
  ResultFiles[JA] = Name;
  if (Name && Name[0] != '\0' && llvm::sys::fs::exists(Name))
    PreexistingResultActions.insert(JA);
  else
    PreexistingResultActions.erase(JA);
  return Name;
}

void Compilation::setPluginSession(
    std::shared_ptr<plugin::PluginSession> Session) {
  PluginSession = std::move(Session);
  propagatePluginSessionToJobs();
}

void Compilation::propagatePluginSessionToJobs() {
  if (!PluginSession)
    return;
  for (Command &Job : Jobs)
    configurePluginSession(Job);
}

void Compilation::setPluginJobExecutionPlan(
    std::unique_ptr<DriverJobExecutionPlan> Plan) {
  PluginJobExecutionPlan = std::move(Plan);
}

const DriverJobExecutionPlan *
Compilation::getPluginJobExecutionPlan() const {
  return PluginJobExecutionPlan.get();
}

void Compilation::setPluginJobExecutionRuntime(
    std::unique_ptr<DriverJobExecutionRuntime> Runtime) {
  PluginJobExecutionRuntime = std::move(Runtime);
}

const DerivedArgList &
Compilation::getArgsForToolChain(const ToolChain *TC,
                                 llvm::StringRef BoundArch) {
  if (!TC)
    TC = &DefaultToolChain;

  DerivedArgList *&Entry = TCArgs[{TC, BoundArch}];
  if (!Entry) {
    llvm::SmallVector<Arg *, 4> AllocatedArgs;
    DerivedArgList *NewDAL =
        TC->TranslateXarchArgs(*TranslatedArgs, &AllocatedArgs);

    if (!NewDAL) {
      Entry = TC->TranslateArgs(*TranslatedArgs, BoundArch);
      if (!Entry)
        Entry = TranslatedArgs;
    } else {
      Entry = TC->TranslateArgs(*NewDAL, BoundArch);
      if (!Entry)
        Entry = NewDAL;
      else
        delete NewDAL;
    }

    // Add allocated arguments to the final DAL.
    for (auto *ArgPtr : AllocatedArgs)
      Entry->AddSynthesizedArg(ArgPtr);
  }

  return *Entry;
}

bool Compilation::CleanupFile(const char *File, bool IssueErrors) const {
  llvm::StringRef Path(File);
  if (Path.starts_with("<inmem>/"))
    return true;

  // Don't try to remove files which we don't have write access to (but may be
  // able to remove), or non-regular files. Underlying tools may have
  // intentionally not overwritten them.
  if (!llvm::sys::fs::can_write(File) || !llvm::sys::fs::is_regular_file(File))
    return true;

  if (std::error_code EC = llvm::sys::fs::remove(File)) {
    if (IssueErrors)
      getDriver().Diag(diag::err_drv_unable_to_remove_file) << EC.message();
    return false;
  }
  return true;
}

bool Compilation::CleanupFileList(const llvm::opt::ArgStringList &Files,
                                  bool IssueErrors) const {
  bool Success = true;
  for (const auto &File : Files)
    Success &= CleanupFile(File, IssueErrors);
  return Success;
}

bool Compilation::CleanupFileMap(const ArgStringMap &Files, const JobAction *JA,
                                 bool IssueErrors) const {
  bool Success = true;
  for (const auto &File : Files) {
    // If specified, only delete the files associated with the JobAction.
    // Otherwise, delete all files in the map.
    if (JA && File.first != JA)
      continue;
    Success &= CleanupFile(File.second, IssueErrors);
  }
  return Success;
}

// ===----------------------------------------------------------------------===
// Job execution
// ===----------------------------------------------------------------------===

int Compilation::ExecuteCommand(const Command &C,
                                const Command *&FailingCommand,
                                llvm::sys::ProcessInfo &PI, bool LogOnly,
                                const DriverJobGraphNode *PluginRequest) {
  if (getArgs().hasArg(options::OPT_v) && !getDriver().CCGenDiagnostics)
    C.Print(llvm::errs(), "\n", /*Quote=*/false);

  if (LogOnly) {
    PI.ReturnCode = 0;
    return 0;
  }

  // Before a plugin-mediated link runs, hand the plugin the bytes of any inputs
  // that live only in the in-memory LTO store (synthetic "<inmem>/" paths). The
  // plugin link bridge reads inputs through its own real-filesystem VFS and
  // cannot see the store, so without this it fails with "No such file or
  // directory" on an integrated compile+link (e.g. a `-r` Android kernel
  // module, which implies LTO). Every compile job has finished by the time a
  // link job executes, so all referenced buffers are present. Only the plugin
  // bridge consumes AuthorizedBlob, so the native link path is left untouched.
  if (C.getKind() == Command::CK_LinkerCommand) {
    const auto &LinkerCfg =
        static_cast<const LinkerCommand &>(C).getDriverConfig();
    if (LinkerCfg.executionHooks && LinkerCfg.executionRequest) {
      auto Request = std::const_pointer_cast<::linker::LinkExecutionRequest>(
          LinkerCfg.executionRequest);
      for (::linker::LinkExecutionInput &Input : Request->Inputs) {
        if (!Input.AuthorizedBlob.empty())
          continue;
        if (std::optional<llvm::MemoryBufferRef> Buffer =
                neverc::InMemoryFileStore::instance().tryGet(Input.LogicalURI)) {
          llvm::StringRef Bytes = Buffer->getBuffer();
          Input.AuthorizedBlob.assign(Bytes.bytes_begin(), Bytes.bytes_end());
        }
      }
    }
  }

  llvm::SmallString<256> Error;
  bool ExecutionFailed = false;
  int Res = 0;
  if (PluginJobExecutionRuntime && PluginRequest) {
    bool BuiltinProviderInvoked = false;
    auto Outcome = PluginJobExecutionRuntime->execute(
        *PluginRequest, C, Redirects, BuiltinProviderInvoked);
    if (!BuiltinProviderInvoked)
      PluginTransactionProtectedResultActions.insert(
          llvm::cast<JobAction>(&C.getSource()));
    if (!Outcome) {
      std::string Message =
          llvm::toString(Outcome.takeError()).str().str();
      Error.assign(Message.begin(), Message.end());
      ExecutionFailed = true;
      Res = 1;
    } else {
      Res = Outcome->ExitCode;
      ExecutionFailed = Outcome->ExecutionFailed;
      Error.assign(Outcome->ErrorMessage.begin(),
                   Outcome->ErrorMessage.end());
    }
  } else {
    Res = C.Execute(Redirects, &Error, &ExecutionFailed, PI);
  }
  if (PostCallback)
    PostCallback(C, Res);
  if (!Error.empty()) {
    assert(Res && "Error string set with 0 result code!");
    getDriver().Diag(PluginRequest ? diag::err_drv_plugin_phase
                                   : diag::err_drv_command_failure)
        << llvm::StringRef(Error);
  }

  if (Res)
    FailingCommand = &C;
  PI.ReturnCode = Res;
  return ExecutionFailed ? 1 : Res;
}

using FailingCommandList =
    llvm::SmallVectorImpl<std::pair<int, const Command *>>;

namespace {
bool actionFailed(const Action *A, const FailingCommandList &FailingCommands) {
  if (FailingCommands.empty())
    return false;

  for (const auto &CI : FailingCommands)
    if (A == &(CI.second->getSource()))
      return true;

  for (const auto *AI : A->inputs())
    if (actionFailed(AI, FailingCommands))
      return true;

  return false;
}

bool inputsOk(const Command &C, const FailingCommandList &FailingCommands) {
  return !actionFailed(&C.getSource(), FailingCommands);
}

bool actionDependsOn(const Action *A, const Action *Dependency) {
  for (const Action *Input : A->inputs()) {
    if (Input == Dependency || actionDependsOn(Input, Dependency))
      return true;
  }
  return false;
}

bool haveInterdependentJobs(llvm::ArrayRef<const Command *> Jobs) {
  for (size_t I = 0; I != Jobs.size(); ++I)
    for (size_t J = 0; J != Jobs.size(); ++J)
      if (I != J &&
          actionDependsOn(&Jobs[I]->getSource(), &Jobs[J]->getSource()))
        return true;
  return false;
}
} // namespace

void Compilation::ExecuteJobs(const JobList &Jobs,
                              FailingCommandList &FailingCommands,
                              bool LogOnly) {
  if (PluginJobExecutionPlan) {
    enum class JobState : uint8_t {
      Pending,
      Succeeded,
      Failed,
      Skipped,
    };
    llvm::DenseMap<NevercJobID, JobState> States;
    for (const DriverJobExecutionPlanNode &Node :
         PluginJobExecutionPlan->nodes())
      States[Node.ID] = JobState::Pending;

    size_t Remaining = PluginJobExecutionPlan->nodes().size();
    while (Remaining != 0) {
      bool Progress = false;
      for (const DriverJobExecutionPlanNode &Node :
           PluginJobExecutionPlan->nodes()) {
        if (States.lookup(Node.ID) != JobState::Pending)
          continue;
        bool Ready = true;
        bool DependencyFailed = false;
        for (NevercJobID Dependency : Node.Dependencies) {
          JobState State = States.lookup(Dependency);
          if (State == JobState::Pending) {
            Ready = false;
            break;
          }
          if (State == JobState::Failed || State == JobState::Skipped)
            DependencyFailed = true;
        }
        if (!Ready)
          continue;
        Progress = true;
        --Remaining;
        if (DependencyFailed) {
          States[Node.ID] = JobState::Skipped;
          continue;
        }
        const Command *FailingCommand = nullptr;
        llvm::sys::ProcessInfo PI;
        int Result = ExecuteCommand(
            *Node.Job, FailingCommand, PI, LogOnly, &Node.Request);
        if (Result != 0) {
          States[Node.ID] = JobState::Failed;
          FailingCommands.push_back({Result, FailingCommand});
        } else {
          States[Node.ID] = JobState::Succeeded;
        }
      }
      if (!Progress) {
        getDriver().Diag(diag::err_drv_plugin_phase)
            << "plugin job plan contains an unschedulable dependency";
        break;
      }
    }
    return;
  }

  if (PluginSession) {
    ExecuteJobsSingle(Jobs, FailingCommands, LogOnly);
    return;
  }

  if (LogOnly)
    return ExecuteJobsSingle(Jobs, FailingCommands, LogOnly);

  // Partition jobs into compile (FrontendCommand) and non-compile groups.
  llvm::SmallVector<const Command *, 64> CompileJobs;
  llvm::SmallVector<const Command *, 4> OtherJobs;
  for (const auto &Job : Jobs) {
    if (Job.getKind() == Command::CK_FrontendCommand)
      CompileJobs.push_back(&Job);
    else
      OtherJobs.push_back(&Job);
  }

  // Check if all compile jobs write LTO bitcode to InMemoryFileStore.
  // If so, run them in-process (parallel threads, zero disk I/O).
  bool linkerFollows = !OtherJobs.empty();
  bool allInMemory = linkerFollows;
  if (allInMemory) {
    for (const auto *Job : CompileJobs) {
      if (Job->getKind() != Command::CK_FrontendCommand) {
        allInMemory = false;
        break;
      }
      const auto *FC = static_cast<const FrontendCommand *>(Job);
      if (!FC->getDirectOpts().InMemoryLTOOutput) {
        allInMemory = false;
        break;
      }
    }
  }

  if (CompileJobs.size() < 2 || haveInterdependentJobs(CompileJobs))
    return ExecuteJobsSingle(Jobs, FailingCommands, LogOnly);

  unsigned NumThreads = std::min(llvm::thread::hardware_concurrency(),
                                 (unsigned)CompileJobs.size());
  if (NumThreads < 2)
    return ExecuteJobsSingle(Jobs, FailingCommands, LogOnly);

  if (getArgs().hasArg(options::OPT_v))
    llvm::errs() << " [parallel compile: " << CompileJobs.size() << " jobs, "
                 << NumThreads << " threads]\n";

  struct CompileResult {
    int ExitCode = 0;
    const Command *Cmd = nullptr;
  };
  std::vector<CompileResult> Results(CompileJobs.size());
  std::atomic<unsigned> NextJob{0};

  if (allInMemory) {
    // In-process parallel compilation: bitcode stays in InMemoryFileStore.
    // LLVM cl options are reset once before threading; each worker skips
    // global-state operations via ParallelSafe flag.
    {
      plugin::PluginLLVMOptionExclusiveLease Lock(
          plugin::pluginLLVMOptionGate());
      llvm::cl::ResetAllOptionOccurrences();
    }

    for (const auto *Job : CompileJobs) {
      auto *FC = const_cast<FrontendCommand *>(
          static_cast<const FrontendCommand *>(Job));
      FC->getDirectOpts().ParallelSafe = true;
    }

    auto InProcWorker = [&]() {
      while (true) {
        unsigned idx = NextJob.fetch_add(1, std::memory_order_relaxed);
        if (idx >= CompileJobs.size())
          break;
        const Command *FailingCommand = nullptr;
        llvm::sys::ProcessInfo PI;
        int R = ExecuteCommand(*CompileJobs[idx], FailingCommand, PI, false);
        Results[idx].ExitCode = R;
        if (R != 0)
          Results[idx].Cmd = CompileJobs[idx];
      }
    };

    // llvm::thread, not std::thread: these workers run cc1 in-process, whose
    // IRgen/codegen can recurse deeply on adversarial input. std::thread takes
    // the platform default stack (only 512 KiB on macOS) and overflows on such
    // recursion; llvm::thread sizes its stack for exactly this (DefaultStackSize:
    // 8 MiB Linux/macOS, 64 MiB Windows), matching the codegen workers in
    // ParallelCodeGenMerge. On Windows the neverc /STACK reserve also covers it
    // (default-stack threads inherit it), but macOS relies on this.
    std::vector<llvm::thread> Workers;
    Workers.reserve(NumThreads);
    for (unsigned i = 0; i < NumThreads; ++i)
      Workers.emplace_back(InProcWorker);
    for (auto &T : Workers)
      T.join();
  } else {
    // Subprocess spawning: outputs go to the filesystem.
    auto SubprocWorker = [&]() {
      while (true) {
        unsigned idx = NextJob.fetch_add(1, std::memory_order_relaxed);
        if (idx >= CompileJobs.size())
          break;
        const Command *Job = CompileJobs[idx];

        llvm::SmallVector<llvm::StringRef, 128> Argv;
        Argv.push_back(Job->getExecutable());
        Argv.push_back("-cc1");
        for (const char *A : Job->getArguments())
          Argv.push_back(A);

        llvm::SmallString<256> ErrMsg;
        bool ExecFailed = false;
        int R = llvm::sys::ExecuteAndWait(
            Job->getExecutable(), Argv, std::nullopt, {},
            /*secondsToWait=*/0, /*memoryLimit=*/0, &ErrMsg, &ExecFailed);
        Results[idx].ExitCode = R;
        if (R != 0)
          Results[idx].Cmd = Job;
      }
    };

    // llvm::thread for a single, uniform thread type across neverc. Unlike the
    // in-memory pool above, these workers only spawn a child neverc -cc1 and
    // block on it (ExecuteAndWait), so the larger stack is not strictly needed
    // here.
    std::vector<llvm::thread> Workers;
    Workers.reserve(NumThreads);
    for (unsigned i = 0; i < NumThreads; ++i)
      Workers.emplace_back(SubprocWorker);
    for (auto &T : Workers)
      T.join();
  }

  for (auto &R : Results) {
    if (R.ExitCode != 0 && R.Cmd)
      FailingCommands.push_back({R.ExitCode, R.Cmd});
  }

  // All compiler writes are done; freeze the store so linker reads
  // skip the shared_mutex entirely.
  if (allInMemory)
    neverc::InMemoryFileStore::instance().freeze();

  for (auto *Job : OtherJobs)
    ExecuteJob(*Job, FailingCommands, LogOnly);
}

void Compilation::ExecuteJobsSingle(const JobList &Jobs,
                                    FailingCommandList &FailingCommands,
                                    bool LogOnly) {
  for (const auto &Job : Jobs)
    ExecuteJob(Job, FailingCommands, LogOnly);
}

int Compilation::ExecuteJob(const Command &Job,
                            FailingCommandList &FailingCommands, bool LogOnly) {
  if (!inputsOk(Job, FailingCommands))
    return 1;
  const Command *FailingCommand = nullptr;
  llvm::sys::ProcessInfo PI;
  if (int Res = ExecuteCommand(Job, FailingCommand, PI, LogOnly)) {
    FailingCommands.push_back(std::make_pair(Res, FailingCommand));
  }
  return 0;
}

// ===----------------------------------------------------------------------===
// Diagnostics & redirection
// ===----------------------------------------------------------------------===

void Compilation::initCompilationForDiagnostics() {
  ForDiagnostics = true;

  // Free actions and jobs.
  Actions.clear();
  AllActions.clear();
  Jobs.clear();
  PluginJobExecutionPlan.reset();
  PluginJobExecutionRuntime.reset();
  PluginCommandCreator.reset();

  // Remove temporary files.
  if (!TheDriver.isSaveTempsEnabled() && !ForceKeepTempFiles)
    CleanupFileList(TempFiles);

  // Clear temporary/results file lists.
  TempFiles.clear();
  ResultFiles.clear();
  FailureResultFiles.clear();

  // Remove any user specified output.  Claim any unclaimed arguments, so as
  // to avoid emitting warnings about unused args.
  OptSpecifier OutputOpts[] = {
      options::OPT_o,  options::OPT_MD, options::OPT_MMD, options::OPT_M,
      options::OPT_MM, options::OPT_MF, options::OPT_MG,  options::OPT_MJ,
      options::OPT_MQ, options::OPT_MT, options::OPT_MV};
  for (const auto &Opt : OutputOpts) {
    if (TranslatedArgs->hasArg(Opt))
      TranslatedArgs->eraseArg(Opt);
  }
  TranslatedArgs->ClaimAllArgs();

  // Force re-creation of the toolchain Args, otherwise our modifications just
  // above will have no effect.
  for (auto Arg : TCArgs)
    if (Arg.second != TranslatedArgs)
      delete Arg.second;
  TCArgs.clear();

  // Redirect stdout/stderr to /dev/null.
  Redirects = {llvm::StringRef(), llvm::StringRef(""), llvm::StringRef("")};

  // Temporary files added by diagnostics should be kept.
  ForceKeepTempFiles = true;
}

llvm::StringRef Compilation::getSysRoot() const { return getDriver().SysRoot; }

void Compilation::Redirect(llvm::ArrayRef<llvm::StringRef> Redirects) {
  this->Redirects.assign(Redirects.begin(), Redirects.end());
}
