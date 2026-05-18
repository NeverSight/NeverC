#ifndef NEVERC_DRIVER_DRIVER_H
#define NEVERC_DRIVER_DRIVER_H

#include "neverc/Foundation/Core/HeaderInclude.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/InputInfo.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Invoke/Phases.h"
#include "neverc/Invoke/ToolChain.h"
#include "neverc/Invoke/Types.h"
#include "neverc/Invoke/Util.h"
#include "neverc/Linker/Core/Driver/Dispatcher.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"

#include <list>
#include <map>
#include <string>
#include <vector>

namespace llvm {
class Triple;
namespace vfs {
class FileSystem;
}
namespace cl {
class ExpansionContext;
}
} // namespace llvm

namespace neverc {

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

namespace driver {

class Command;
class Compilation;
struct DirectInvocationOpts;
class JobAction;
class ToolChain;

enum LTOKind { LTOK_None, LTOK_Full, LTOK_Unknown };

class Driver {
  DiagnosticsEngine &Diags;

  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS;

  enum SaveTempsMode { SaveTempsNone, SaveTempsCwd, SaveTempsObj } SaveTemps;

  LTOKind LTOMode;
  bool AutoLTO = false;

public:
  // Diag - Forwarding function for diagnostics.
  DiagnosticBuilder Diag(unsigned DiagID) const { return Diags.Report(DiagID); }

public:
  std::string Name;

  std::string Dir;

  std::string NeverCExecutable;

  ParsedDriverName NameParts;

  std::string InstalledDir;

  std::string ResourceDir;

  std::string SystemConfigDir;

  std::string UserConfigDir;

  typedef llvm::SmallVector<std::string, 4> prefix_list;
  prefix_list PrefixDirs;

  std::string SysRoot;

  std::string DyldPrefix;

  std::string DriverTitle;

  std::string HostBits, HostMachine, HostSystem, HostRelease;

  using InputTy = std::pair<types::ID, const llvm::opt::Arg *>;

  using InputList = llvm::SmallVector<InputTy, 16>;

  unsigned CCCPrintBindings : 1;

  unsigned CCGenDiagnostics : 1;

  using FrontendToolFunc =
      llvm::function_ref<int(llvm::SmallVectorImpl<const char *> &ArgV,
                             const DirectInvocationOpts *DirectOpts)>;
  FrontendToolFunc FrontendMain = nullptr;

  using LinkerToolFunc = llvm::function_ref<int(
      llvm::SmallVectorImpl<const char *> &ArgV, LinkerFlavor Flavor,
      const ::linker::LinkerDriverConfig &DriverCfg)>;
  LinkerToolFunc LinkerMain = nullptr;

private:
  std::string TargetTriple;

  std::string CCCGenericGCCName;

  std::vector<std::string> ConfigFiles;

  llvm::BumpPtrAllocator Alloc;

  llvm::StringSaver Saver;

  std::unique_ptr<llvm::opt::InputArgList> CfgOptions;

  std::unique_ptr<llvm::opt::InputArgList> CommandLineOptions;

  const char *PrependArg;

  unsigned CheckInputsExist : 1;

public:
  // getFinalPhase - Determine which compilation mode we are in and record
  // which option we used to determine the final phase.
  phases::ID getFinalPhase(const llvm::opt::DerivedArgList &DAL,
                           llvm::opt::Arg **FinalPhaseArg = nullptr) const;

private:
  unsigned SuppressMissingInputWarning : 1;

  mutable llvm::StringMap<std::unique_ptr<ToolChain>> ToolChains;

private:
  llvm::opt::DerivedArgList *
  TranslateInputArgs(const llvm::opt::InputArgList &Args) const;

  // handleArguments - All code related to claiming and printing diagnostics
  // related to arguments to the driver are done here.
  void handleArguments(Compilation &C, llvm::opt::DerivedArgList &Args,
                       const InputList &Inputs, ActionList &Actions) const;

  // Before executing jobs, sets up response files for commands that need them.
  void setUpResponseFiles(Compilation &C, Command &Cmd);

  void
  generatePrefixedToolNames(llvm::StringRef Tool, const ToolChain &TC,
                            llvm::SmallVectorImpl<std::string> &Names) const;

  bool getCrashDiagnosticFile(llvm::StringRef ReproCrashFilename,
                              llvm::SmallString<128> &CrashDiagDir);

public:
  static std::string GetResourcesPath(llvm::StringRef BinaryPath,
                                      llvm::StringRef CustomResourceDir = "");

  Driver(llvm::StringRef NeverCExecutable, llvm::StringRef TargetTriple,
         DiagnosticsEngine &Diags, std::string Title = "neverc C compiler",
         llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS = nullptr);

  const std::string &getCCCGenericGCCName() const { return CCCGenericGCCName; }

  llvm::ArrayRef<std::string> getConfigFiles() const { return ConfigFiles; }

  const llvm::opt::OptTable &getOpts() const { return getDriverOptTable(); }

  DiagnosticsEngine &getDiags() const { return Diags; }

  llvm::vfs::FileSystem &getVFS() const { return *VFS; }

  bool getCheckInputsExist() const { return CheckInputsExist; }

  void setCheckInputsExist(bool Value) { CheckInputsExist = Value; }

  const char *getPrependArg() const { return PrependArg; }
  void setPrependArg(const char *Value) { PrependArg = Value; }

  void setTargetPrefixFromProgramName(const ParsedDriverName &TM) {
    NameParts = TM;
  }

  const std::string &getTitle() { return DriverTitle; }
  void setTitle(std::string Value) { DriverTitle = std::move(Value); }

  std::string getTargetTriple() const { return TargetTriple; }

  const char *getNeverCProgramPath() const { return NeverCExecutable.c_str(); }

  const char *getInstalledDir() const {
    if (!InstalledDir.empty())
      return InstalledDir.c_str();
    return Dir.c_str();
  }
  void setInstalledDir(llvm::StringRef Value) {
    InstalledDir = std::string(Value);
  }

  bool isSaveTempsEnabled() const { return SaveTemps != SaveTempsNone; }
  bool isSaveTempsObj() const { return SaveTemps == SaveTempsObj; }

  Compilation *CreateCompilation(llvm::ArrayRef<const char *> Args);

  llvm::opt::InputArgList ParseArgStrings(llvm::ArrayRef<const char *> Args,
                                          bool &ContainsError);

  void FormInputs(const ToolChain &TC, llvm::opt::DerivedArgList &Args,
                  InputList &Inputs) const;

  void FormActions(Compilation &C, llvm::opt::DerivedArgList &Args,
                   const InputList &Inputs, ActionList &Actions) const;

  void FormUniversalActions(Compilation &C, const ToolChain &TC,
                            const InputList &BAInputs) const;

  bool DiagnoseInputExistence(const llvm::opt::DerivedArgList &Args,
                              llvm::StringRef Value, types::ID Ty,
                              bool TypoCorrect) const;

  void GenerateJobs(Compilation &C) const;

  int ExecuteCompilation(
      Compilation &C,
      llvm::SmallVectorImpl<std::pair<int, const Command *>> &FailingCommands);

  struct CompilationDiagnosticReport {
    llvm::SmallVector<std::string, 4> TemporaryFiles;
  };

  void generateCompilationDiagnostics(
      Compilation &C, const Command &FailingCommand,
      llvm::StringRef AdditionalInformation = "",
      CompilationDiagnosticReport *GeneratedReport = nullptr);

  enum class CommandStatus {
    Crash = 1,
    Error,
    Ok,
  };

  enum class ReproLevel {
    Off = 0,
    OnCrash = static_cast<int>(CommandStatus::Crash),
    OnError = static_cast<int>(CommandStatus::Error),
    Always = static_cast<int>(CommandStatus::Ok),
  };

  bool maybeGenerateCompilationDiagnostics(
      CommandStatus CS, ReproLevel Level, Compilation &C,
      const Command &FailingCommand, llvm::StringRef AdditionalInformation = "",
      CompilationDiagnosticReport *GeneratedReport = nullptr) {
    if (static_cast<int>(CS) > static_cast<int>(Level))
      return false;
    if (CS != CommandStatus::Crash)
      Diags.Report(diag::err_drv_force_crash);
    // Hack to ensure that diagnostic notes get emitted.
    Diags.setLastDiagnosticIgnored(false);
    generateCompilationDiagnostics(C, FailingCommand, AdditionalInformation,
                                   GeneratedReport);
    return true;
  }

  void PrintActions(const Compilation &C) const;

  void PrintHelp(bool ShowHidden) const;

  void PrintVersion(const Compilation &C, llvm::raw_ostream &OS) const;

  std::string GetFilePath(llvm::StringRef Name, const ToolChain &TC) const;

  std::string GetProgramPath(llvm::StringRef Name, const ToolChain &TC) const;

  void ProcessAutocompletions(llvm::StringRef PassedFlags) const;

  bool ProcessImmediateArgs(const Compilation &C);

  Action *ConstructPhaseAction(Compilation &C, const llvm::opt::ArgList &Args,
                               phases::ID Phase, Action *Input) const;

  InputInfoList GenerateJobsForAction(
      Compilation &C, const Action *A, const ToolChain *TC,
      llvm::StringRef BoundArch, bool AtTopLevel, bool MultipleArchs,
      const char *LinkingOutput,
      std::map<std::pair<const Action *, std::string>, InputInfoList>
          &CachedResults) const;

  const char *getDefaultImageName() const;

  const char *CreateTempFile(Compilation &C, llvm::StringRef Prefix,
                             llvm::StringRef Suffix, bool MultipleArchs = false,
                             llvm::StringRef BoundArch = {},
                             bool NeedUniqueDirectory = false) const;

  const char *GetNamedOutputPath(Compilation &C, const JobAction &JA,
                                 const char *BaseInput,
                                 llvm::StringRef BoundArch, bool AtTopLevel,
                                 bool MultipleArchs) const;

  std::string GetTemporaryPath(llvm::StringRef Prefix,
                               llvm::StringRef Suffix) const;

  std::string GetTemporaryDirectory(llvm::StringRef Prefix) const;

  bool ShouldUseNeverCCompiler(const JobAction &JA) const;

  bool ShouldEmitStaticLibrary(const llvm::opt::ArgList &Args) const;

  bool isUsingLTO() const { return LTOMode != LTOK_None; }

  bool isAutoLTO() const { return AutoLTO; }

  LTOKind getLTOMode() const { return LTOMode; }

private:
  bool loadConfigFiles();

  bool loadDefaultConfigFiles(llvm::cl::ExpansionContext &ExpCtx);

  bool readConfigFile(llvm::StringRef FileName,
                      llvm::cl::ExpansionContext &ExpCtx);

  void setLTOMode(const llvm::opt::ArgList &Args);

  const ToolChain &getToolChain(const llvm::opt::ArgList &Args,
                                const llvm::Triple &Target) const;

  llvm::opt::Visibility getOptionVisibilityMask() const;

  InputInfoList GenerateJobsForActionNoCache(
      Compilation &C, const Action *A, const ToolChain *TC,
      llvm::StringRef BoundArch, bool AtTopLevel, bool MultipleArchs,
      const char *LinkingOutput,
      std::map<std::pair<const Action *, std::string>, InputInfoList>
          &CachedResults) const;

public:
  static bool GetReleaseVersion(llvm::StringRef Str, unsigned &Major,
                                unsigned &Minor, unsigned &Micro,
                                bool &HadExtra);

  static bool GetReleaseVersion(llvm::StringRef Str,
                                llvm::MutableArrayRef<unsigned> Digits);
};

bool isOptimizationLevelFast(const llvm::opt::ArgList &Args);

bool willEmitRemarks(const llvm::opt::ArgList &Args);

llvm::Error expandResponseFiles(llvm::SmallVectorImpl<const char *> &Args,
                                llvm::StringRef ProgName,
                                llvm::StringRef DefaultTargetTriple,
                                llvm::BumpPtrAllocator &Alloc,
                                llvm::vfs::FileSystem *FS = nullptr);

} // end namespace driver
} // end namespace neverc

#endif
