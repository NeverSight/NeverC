#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/InMemoryFileStore.h"
#include "neverc/Invoke/Job.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Invoke/ToolChain.h"
#include "neverc/Invoke/Util.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/thread.h"
#include <cassert>
#include <string>
#include <system_error>
#include <thread>
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
                                llvm::sys::ProcessInfo &PI, bool LogOnly) {
  if (getArgs().hasArg(options::OPT_v) && !getDriver().CCGenDiagnostics)
    C.Print(llvm::errs(), "\n", /*Quote=*/false);

  if (LogOnly) {
    PI.ReturnCode = 0;
    return 0;
  }

  llvm::SmallString<256> Error;
  bool ExecutionFailed;
  int Res = C.Execute(Redirects, &Error, &ExecutionFailed, PI);
  if (PostCallback)
    PostCallback(C, Res);
  if (!Error.empty()) {
    assert(Res && "Error string set with 0 result code!");
    getDriver().Diag(diag::err_drv_command_failure) << llvm::StringRef(Error);
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
} // namespace

void Compilation::ExecuteJobs(const JobList &Jobs,
                              FailingCommandList &FailingCommands,
                              bool LogOnly) {
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

  if (CompileJobs.size() < 2)
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
    llvm::cl::ResetAllOptionOccurrences();

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

    std::vector<std::thread> Workers;
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

    std::vector<std::thread> Workers;
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
