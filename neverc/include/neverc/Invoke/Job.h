#ifndef NEVERC_INVOKE_JOB_H
#define NEVERC_INVOKE_JOB_H

#include "neverc/Invoke/DirectInvocationOpts.h"
#include "neverc/Invoke/InputInfo.h"
#include "neverc/Invoke/Util.h"
#include "neverc/Linker/Core/Driver/Dispatcher.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace neverc {
namespace driver {

class Action;
class InputInfo;
class Tool;

struct CrashReportInfo {
  llvm::StringRef Filename;
  llvm::StringRef VFSPath;

  CrashReportInfo(llvm::StringRef Filename, llvm::StringRef VFSPath)
      : Filename(Filename), VFSPath(VFSPath) {}
};

// Encodes the kind of response file supported for a command invocation.
// Response files are necessary if the command line gets too large, requiring
// the arguments to be transferred to a file.
struct ResponseFileSupport {
  enum ResponseFileKind {
    // Provides full support for response files, which means we can transfer
    // all tool input arguments to a file.
    RF_Full,
    // Input file names can live in a file, but flags can't. This is a special
    // case for old versions of Apple's ld64.
    RF_FileList,
    // Does not support response files: all arguments must be passed via
    // command line.
    RF_None
  };
  ResponseFileKind ResponseKind;

  llvm::sys::WindowsEncodingMethod ResponseEncoding;

  const char *ResponseFlag;

  static constexpr ResponseFileSupport None() {
    return {RF_None, llvm::sys::WEM_UTF8, nullptr};
  }

  static constexpr ResponseFileSupport AtFileUTF8() {
    return {RF_Full, llvm::sys::WEM_UTF8, "@"};
  }

  static constexpr ResponseFileSupport AtFileCurCP() {
    return {RF_Full, llvm::sys::WEM_CurrentCodePage, "@"};
  }

  static constexpr ResponseFileSupport AtFileUTF16() {
    return {RF_Full, llvm::sys::WEM_UTF16, "@"};
  }
};

class Command {
  const Action &Source;

  const Tool &Creator;

  ResponseFileSupport ResponseSupport;

  const char *Executable;

  const char *PrependArg;

  llvm::opt::ArgStringList Arguments;

  std::vector<InputInfo> InputInfoList;

  std::vector<std::string> OutputFilenames;

  const char *ResponseFile = nullptr;

  llvm::opt::ArgStringList InputFileList;

  std::string ResponseFileFlag;

  std::vector<const char *> Environment;

  std::vector<std::optional<std::string>> RedirectFiles;

  mutable llvm::sys::ProcessStatistics ProcStat;
  mutable bool HasProcStat = false;

  void buildArgvForResponseFile(llvm::SmallVectorImpl<const char *> &Out) const;

  void writeResponseFile(llvm::raw_ostream &OS) const;

public:
  bool PrintInputFilenames = false;

  bool InProcess = false;

  enum CommandKind {
    CK_Command,
    CK_FrontendCommand,
    CK_LinkerCommand,
    CK_ArchiveCommand
  };

  Command(const Action &Source, const Tool &Creator,
          ResponseFileSupport ResponseSupport, const char *Executable,
          const llvm::opt::ArgStringList &Arguments,
          llvm::ArrayRef<InputInfo> Inputs,
          llvm::ArrayRef<InputInfo> Outputs = std::nullopt,
          const char *PrependArg = nullptr);
  Command(const Command &) = default;
  virtual ~Command() = default;

  virtual CommandKind getKind() const { return CK_Command; }

  virtual void Print(llvm::raw_ostream &OS, const char *Terminator, bool Quote,
                     CrashReportInfo *CrashInfo = nullptr) const;

  virtual int Execute(llvm::ArrayRef<llvm::StringRef> Redirects,
                      llvm::SmallVectorImpl<char> *ErrMsg,
                      bool *ExecutionFailed, llvm::sys::ProcessInfo &PI) const;

  const Action &getSource() const { return Source; }

  const Tool &getCreator() const { return Creator; }

  const ResponseFileSupport &getResponseFileSupport() {
    return ResponseSupport;
  }

  void setResponseFile(const char *FileName);

  void setInputFileList(llvm::opt::ArgStringList List) {
    InputFileList = std::move(List);
  }

  virtual void setEnvironment(llvm::ArrayRef<const char *> NewEnvironment);

  void
  setRedirectFiles(const std::vector<std::optional<std::string>> &Redirects);

  void replaceArguments(llvm::opt::ArgStringList List) {
    Arguments = std::move(List);
  }

  void replaceExecutable(const char *Exe) { Executable = Exe; }

  const char *getExecutable() const { return Executable; }

  const llvm::opt::ArgStringList &getArguments() const { return Arguments; }

  const std::vector<InputInfo> &getInputInfos() const { return InputInfoList; }

  const std::vector<std::string> &getOutputFilenames() const {
    return OutputFilenames;
  }

  const llvm::sys::ProcessStatistics *getProcessStatistics() const {
    return HasProcStat ? &ProcStat : nullptr;
  }

protected:
  void PrintFileNames() const;

  bool hasResponseFile() const { return ResponseFile != nullptr; }
};

class FrontendCommand : public Command {
  DirectInvocationOpts DirectOpts;

public:
  FrontendCommand(const Action &Source, const Tool &Creator,
                  ResponseFileSupport ResponseSupport, const char *Executable,
                  const llvm::opt::ArgStringList &Arguments,
                  llvm::ArrayRef<InputInfo> Inputs,
                  llvm::ArrayRef<InputInfo> Outputs = std::nullopt,
                  const char *PrependArg = nullptr);

  CommandKind getKind() const override { return CK_FrontendCommand; }

  void Print(llvm::raw_ostream &OS, const char *Terminator, bool Quote,
             CrashReportInfo *CrashInfo = nullptr) const override;

  int Execute(llvm::ArrayRef<llvm::StringRef> Redirects,
              llvm::SmallVectorImpl<char> *ErrMsg, bool *ExecutionFailed,
              llvm::sys::ProcessInfo &PI) const override;

  void setEnvironment(llvm::ArrayRef<const char *> NewEnvironment) override;

  DirectInvocationOpts &getDirectOpts() { return DirectOpts; }
  const DirectInvocationOpts &getDirectOpts() const { return DirectOpts; }
};

class LinkerCommand : public Command {
  LinkerFlavor Flavor;
  ::linker::LinkerDriverConfig DriverCfg;

public:
  LinkerCommand(const Action &Source, const Tool &Creator,
                ResponseFileSupport ResponseSupport, const char *Executable,
                const llvm::opt::ArgStringList &Arguments,
                llvm::ArrayRef<InputInfo> Inputs, LinkerFlavor Flavor,
                llvm::ArrayRef<InputInfo> Outputs = std::nullopt,
                const char *PrependArg = nullptr);

  CommandKind getKind() const override { return CK_LinkerCommand; }

  LinkerFlavor getFlavor() const { return Flavor; }

  ::linker::LinkerDriverConfig &getDriverConfig() { return DriverCfg; }
  const ::linker::LinkerDriverConfig &getDriverConfig() const {
    return DriverCfg;
  }

  void Print(llvm::raw_ostream &OS, const char *Terminator, bool Quote,
             CrashReportInfo *CrashInfo = nullptr) const override;

  int Execute(llvm::ArrayRef<llvm::StringRef> Redirects,
              llvm::SmallVectorImpl<char> *ErrMsg, bool *ExecutionFailed,
              llvm::sys::ProcessInfo &PI) const override;

  void setEnvironment(llvm::ArrayRef<const char *> NewEnvironment) override;
};

class ArchiveCommand : public Command {
public:
  ArchiveCommand(const Action &Source, const Tool &Creator,
                 ResponseFileSupport ResponseSupport, const char *Executable,
                 const llvm::opt::ArgStringList &Arguments,
                 llvm::ArrayRef<InputInfo> Inputs,
                 llvm::ArrayRef<InputInfo> Outputs = std::nullopt);

  CommandKind getKind() const override { return CK_ArchiveCommand; }

  void Print(llvm::raw_ostream &OS, const char *Terminator, bool Quote,
             CrashReportInfo *CrashInfo = nullptr) const override;

  int Execute(llvm::ArrayRef<llvm::StringRef> Redirects,
              llvm::SmallVectorImpl<char> *ErrMsg, bool *ExecutionFailed,
              llvm::sys::ProcessInfo &PI) const override;

  void setEnvironment(llvm::ArrayRef<const char *> NewEnvironment) override;
};

class JobList {
public:
  using list_type = llvm::SmallVector<std::unique_ptr<Command>, 4>;
  using size_type = list_type::size_type;
  using iterator = llvm::pointee_iterator<list_type::iterator>;
  using const_iterator = llvm::pointee_iterator<list_type::const_iterator>;

private:
  list_type Jobs;

public:
  void Print(llvm::raw_ostream &OS, const char *Terminator, bool Quote,
             CrashReportInfo *CrashInfo = nullptr) const;

  void addJob(std::unique_ptr<Command> J) { Jobs.push_back(std::move(J)); }

  void clear();

  const list_type &getJobs() const { return Jobs; }

  bool empty() const { return Jobs.empty(); }
  size_type size() const { return Jobs.size(); }
  iterator begin() { return Jobs.begin(); }
  const_iterator begin() const { return Jobs.begin(); }
  iterator end() { return Jobs.end(); }
  const_iterator end() const { return Jobs.end(); }
};

} // namespace driver
} // namespace neverc

#endif // NEVERC_INVOKE_JOB_H
