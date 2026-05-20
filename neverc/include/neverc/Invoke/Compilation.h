#ifndef NEVERC_INVOKE_COMPILATION_H
#define NEVERC_INVOKE_COMPILATION_H

#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/Job.h"
#include "neverc/Invoke/Util.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Program.h"
#include <cassert>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {
namespace opt {

class DerivedArgList;
class InputArgList;

} // namespace opt
} // namespace llvm

namespace neverc {
namespace driver {

class Driver;
class ToolChain;

class Compilation {

  const Driver &TheDriver;

  const ToolChain &DefaultToolChain;

  llvm::opt::InputArgList *Args;

  llvm::opt::DerivedArgList *TranslatedArgs;

  std::vector<std::unique_ptr<Action>> AllActions;

  ActionList Actions;

  JobList Jobs;

  struct TCArgsKey final {
    const ToolChain *TC = nullptr;
    llvm::StringRef BoundArch;

    TCArgsKey(const ToolChain *TC, llvm::StringRef BoundArch)
        : TC(TC), BoundArch(BoundArch) {}

    bool operator<(const TCArgsKey &K) const {
      if (TC < K.TC)
        return true;
      if (TC == K.TC && BoundArch < K.BoundArch)
        return true;
      return false;
    }
  };
  std::map<TCArgsKey, llvm::opt::DerivedArgList *> TCArgs;

  llvm::opt::ArgStringList TempFiles;

  ArgStringMap ResultFiles;

  ArgStringMap FailureResultFiles;

  ArgStringMap TimeTraceFiles;

  llvm::SmallVector<llvm::StringRef, 3> Redirects;

  std::function<void(const Command &, int)> PostCallback;

  bool ForDiagnostics = false;

  bool ContainsError;

  bool ForceKeepTempFiles = false;

public:
  Compilation(const Driver &D, const ToolChain &DefaultToolChain,
              llvm::opt::InputArgList *Args,
              llvm::opt::DerivedArgList *TranslatedArgs, bool ContainsError);
  ~Compilation();

  const Driver &getDriver() const { return TheDriver; }

  const ToolChain &getDefaultToolChain() const { return DefaultToolChain; }

  const llvm::opt::InputArgList &getInputArgs() const { return *Args; }

  const llvm::opt::DerivedArgList &getArgs() const { return *TranslatedArgs; }

  llvm::opt::DerivedArgList &getArgs() { return *TranslatedArgs; }

  ActionList &getActions() { return Actions; }
  const ActionList &getActions() const { return Actions; }

  template <typename T, typename... Args> T *MakeAction(Args &&...Arg) {
    T *RawPtr = new T(std::forward<Args>(Arg)...);
    AllActions.push_back(std::unique_ptr<Action>(RawPtr));
    return RawPtr;
  }

  JobList &getJobs() { return Jobs; }
  const JobList &getJobs() const { return Jobs; }

  void addCommand(std::unique_ptr<Command> C) { Jobs.addJob(std::move(C)); }

  llvm::opt::ArgStringList &getTempFiles() { return TempFiles; }
  const llvm::opt::ArgStringList &getTempFiles() const { return TempFiles; }

  const ArgStringMap &getResultFiles() const { return ResultFiles; }

  const ArgStringMap &getFailureResultFiles() const {
    return FailureResultFiles;
  }

  void setPostCallback(const std::function<void(const Command &, int)> &CB) {
    PostCallback = CB;
  }

  llvm::StringRef getSysRoot() const;

  const llvm::opt::DerivedArgList &
  getArgsForToolChain(const ToolChain *TC, llvm::StringRef BoundArch);

  const char *addTempFile(const char *Name) {
    TempFiles.push_back(Name);
    return Name;
  }

  const char *addResultFile(const char *Name, const JobAction *JA) {
    ResultFiles[JA] = Name;
    return Name;
  }

  const char *addFailureResultFile(const char *Name, const JobAction *JA) {
    FailureResultFiles[JA] = Name;
    return Name;
  }

  const char *getTimeTraceFile(const JobAction *JA) const {
    return TimeTraceFiles.lookup(JA);
  }
  void addTimeTraceFile(const char *Name, const JobAction *JA) {
    assert(!TimeTraceFiles.contains(JA));
    TimeTraceFiles[JA] = Name;
  }

  bool CleanupFile(const char *File, bool IssueErrors = false) const;

  bool CleanupFileList(const llvm::opt::ArgStringList &Files,
                       bool IssueErrors = false) const;

  bool CleanupFileMap(const ArgStringMap &Files, const JobAction *JA,
                      bool IssueErrors = false) const;

  int ExecuteCommand(const Command &C, const Command *&FailingCommand,
                     llvm::sys::ProcessInfo &PI, bool LogOnly = false);

  void ExecuteJobs(
      const JobList &Jobs,
      llvm::SmallVectorImpl<std::pair<int, const Command *>> &FailingCommands,
      bool LogOnly = false);

  int ExecuteJob(
      const Command &Job,
      llvm::SmallVectorImpl<std::pair<int, const Command *>> &FailingCommands,
      bool LogOnly = false);

  void ExecuteJobsSingle(
      const JobList &Jobs,
      llvm::SmallVectorImpl<std::pair<int, const Command *>> &FailingCommands,
      bool LogOnly = false);

  void initCompilationForDiagnostics();

  bool isForDiagnostics() const { return ForDiagnostics; }

  bool containsError() const { return ContainsError; }

  void setContainsError() { ContainsError = true; }

  void Redirect(llvm::ArrayRef<llvm::StringRef> Redirects);
};

} // namespace driver
} // namespace neverc

#endif // NEVERC_INVOKE_COMPILATION_H
