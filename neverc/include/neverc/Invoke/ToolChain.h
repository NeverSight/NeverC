#ifndef NEVERC_INVOKE_TOOLCHAIN_H
#define NEVERC_INVOKE_TOOLCHAIN_H

#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/Multilib.h"
#include "neverc/Invoke/Types.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <climits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace llvm {
namespace opt {

class Arg;
class ArgList;
class DerivedArgList;

} // namespace opt
namespace vfs {

class FileSystem;

} // namespace vfs
} // namespace llvm

namespace neverc {

namespace driver {

class Driver;
class InputInfo;
class Tool;

struct ParsedDriverName {
  std::string TargetPrefix;

  bool TargetIsValid = false;

  ParsedDriverName() = default;
  ParsedDriverName(std::string Target, bool IsRegistered)
      : TargetPrefix(std::move(Target)), TargetIsValid(IsRegistered) {}

  bool isEmpty() const { return TargetPrefix.empty() && !TargetIsValid; }
};

class ToolChain {
public:
  using path_list = llvm::SmallVector<std::string, 16>;

  enum RuntimeLibType { RLT_CompilerRT, RLT_Libgcc };

  enum UnwindLibType { UNW_None, UNW_CompilerRT, UNW_Libgcc };

  enum class UnwindTableLevel {
    None,
    Synchronous,
    Asynchronous,
  };

  enum FileType { FT_Object, FT_Static, FT_Shared };

private:
  friend class RegisterEffectiveTriple;

  const Driver &D;
  llvm::Triple Triple;
  const llvm::opt::ArgList &Args;

  path_list LibraryPaths;

  path_list FilePaths;

  path_list ProgramPaths;

  mutable std::unique_ptr<Tool> NeverC;
  mutable std::unique_ptr<Tool> Assemble;
  mutable std::unique_ptr<Tool> Link;
  mutable std::unique_ptr<Tool> StaticLibTool;
  Tool *getNeverC() const;
  Tool *getAssemble() const;
  Tool *getLink() const;
  Tool *getStaticLibTool() const;
  Tool *getNeverCAs() const;

  mutable llvm::Triple EffectiveTriple;

  void setEffectiveTriple(llvm::Triple ET) const {
    EffectiveTriple = std::move(ET);
  }

  mutable std::optional<RuntimeLibType> runtimeLibType;
  mutable std::optional<UnwindLibType> unwindLibType;

protected:
  MultilibSet Multilibs;
  llvm::SmallVector<Multilib> SelectedMultilibs;

  ToolChain(const Driver &D, const llvm::Triple &T,
            const llvm::opt::ArgList &Args);

  void setTripleEnvironment(llvm::Triple::EnvironmentType Env);

  virtual Tool *buildLinker() const;
  virtual Tool *buildStaticLibTool() const;
  virtual Tool *getTool(Action::ActionClass AC) const;

  virtual std::string buildCompilerRTBasename(const llvm::opt::ArgList &Args,
                                              llvm::StringRef Component,
                                              FileType Type,
                                              bool AddArch) const;

  std::optional<std::string> getTargetSubDirPath(llvm::StringRef BaseDir) const;

  ///@{
  static void addSystemInclude(const llvm::opt::ArgList &DriverArgs,
                               llvm::opt::ArgStringList &FrontendArgs,
                               const llvm::Twine &Path);
  static void addExternCSystemInclude(const llvm::opt::ArgList &DriverArgs,
                                      llvm::opt::ArgStringList &FrontendArgs,
                                      const llvm::Twine &Path);
  static void
  addExternCSystemIncludeIfExists(const llvm::opt::ArgList &DriverArgs,
                                  llvm::opt::ArgStringList &FrontendArgs,
                                  const llvm::Twine &Path);
  static void addSystemIncludes(const llvm::opt::ArgList &DriverArgs,
                                llvm::opt::ArgStringList &FrontendArgs,
                                llvm::ArrayRef<llvm::StringRef> Paths);

  static std::string concat(llvm::StringRef Path, const llvm::Twine &A,
                            const llvm::Twine &B = "",
                            const llvm::Twine &C = "",
                            const llvm::Twine &D = "");
  ///@}

public:
  virtual ~ToolChain();

  // Accessors

  const Driver &getDriver() const { return D; }
  llvm::vfs::FileSystem &getVFS() const;
  const llvm::Triple &getTriple() const { return Triple; }

  virtual std::string getInputFilename(const InputInfo &Input) const;

  llvm::Triple::ArchType getArch() const { return Triple.getArch(); }
  llvm::StringRef getArchName() const { return Triple.getArchName(); }
  llvm::StringRef getPlatform() const { return Triple.getVendorName(); }
  llvm::StringRef getOS() const { return Triple.getOSName(); }

  llvm::StringRef getDefaultUniversalArchName() const;

  std::string getTripleString() const { return Triple.getTriple(); }

  const llvm::Triple &getEffectiveTriple() const {
    assert(!EffectiveTriple.getTriple().empty() && "No effective triple");
    return EffectiveTriple;
  }

  bool hasEffectiveTriple() const {
    return !EffectiveTriple.getTriple().empty();
  }

  path_list &getLibraryPaths() { return LibraryPaths; }
  const path_list &getLibraryPaths() const { return LibraryPaths; }

  path_list &getFilePaths() { return FilePaths; }
  const path_list &getFilePaths() const { return FilePaths; }

  path_list &getProgramPaths() { return ProgramPaths; }
  const path_list &getProgramPaths() const { return ProgramPaths; }

  const MultilibSet &getMultilibs() const { return Multilibs; }

  const llvm::SmallVector<Multilib> &getSelectedMultilibs() const {
    return SelectedMultilibs;
  }

  Multilib::flags_list getMultilibFlags(const llvm::opt::ArgList &) const;

  static ParsedDriverName
  getTargetPrefixFromProgramName(llvm::StringRef ProgName);

  // Tool access.

  virtual llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args,
                llvm::StringRef BoundArch) const {
    return nullptr;
  }

  virtual void TranslateXarchArgs(
      const llvm::opt::DerivedArgList &Args, llvm::opt::Arg *&A,
      llvm::opt::DerivedArgList *DAL,
      llvm::SmallVectorImpl<llvm::opt::Arg *> *AllocatedArgs = nullptr) const;

  virtual llvm::opt::DerivedArgList *TranslateXarchArgs(
      const llvm::opt::DerivedArgList &Args,
      llvm::SmallVectorImpl<llvm::opt::Arg *> *AllocatedArgs) const;

  virtual Tool *SelectTool(const JobAction &JA) const;

  // Helper methods

  std::string GetFilePath(const char *Name) const;
  std::string GetProgramPath(const char *Name) const;

  std::string GetLinkerPath() const;

  virtual void printVerboseInfo(llvm::raw_ostream &OS) const {}

  // Platform defaults information

  virtual bool isCrossCompiling() const;

  virtual bool HasNativeLLVMSupport() const;

  virtual types::ID LookupTypeForExtension(llvm::StringRef Ext) const;

  virtual bool IsIntegratedAssemblerDefault() const { return true; }

  virtual bool IsIntegratedBackendDefault() const { return true; }

  virtual bool IsIntegratedBackendSupported() const { return true; }

  virtual bool IsNonIntegratedBackendSupported() const { return false; }

  virtual bool useIntegratedAs() const;

  virtual bool useIntegratedBackend() const;

  virtual bool parseInlineAsmUsingAsmParser() const { return false; }

  virtual bool IsMathErrnoDefault() const { return true; }

  virtual bool useRelaxRelocations() const;

  bool defaultToIEEELongDouble() const;

  virtual LangOptions::StackProtectorMode
  GetDefaultStackProtectorLevel(bool Kernel) const {
    return LangOptions::SSPOff;
  }

  virtual LangOptions::TrivialAutoVarInitKind
  GetDefaultTrivialAutoVarInit() const {
    return LangOptions::TrivialAutoVarInitKind::Uninitialized;
  }

  virtual RuntimeLibType GetDefaultRuntimeLibType() const {
    return ToolChain::RLT_Libgcc;
  }

  virtual UnwindLibType GetDefaultUnwindLibType() const {
    return ToolChain::UNW_None;
  }

  virtual std::string getCompilerRTPath() const;

  virtual std::string getCompilerRT(const llvm::opt::ArgList &Args,
                                    llvm::StringRef Component,
                                    FileType Type = ToolChain::FT_Static) const;

  const char *
  getCompilerRTArgString(const llvm::opt::ArgList &Args,
                         llvm::StringRef Component,
                         FileType Type = ToolChain::FT_Static) const;

  std::string getCompilerRTBasename(const llvm::opt::ArgList &Args,
                                    llvm::StringRef Component,
                                    FileType Type = ToolChain::FT_Static) const;

  // Returns the target specific runtime path if it exists.
  std::optional<std::string> getRuntimePath() const;

  // Returns target specific standard library path if it exists.
  std::optional<std::string> getStdlibPath() const;

  // Returns <ResourceDir>/lib/<OSName>/<arch> or <ResourceDir>/lib/<triple>.
  virtual path_list getArchSpecificLibPaths() const;

  // Returns <OSname> part of above.
  virtual llvm::StringRef getOSLibName() const;

  virtual UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &Args) const;

  virtual bool
  IsAArch64OutlineAtomicsDefault(const llvm::opt::ArgList &Args) const {
    return false;
  }

  virtual bool UseDwarfDebugFlags() const { return false; }

  virtual std::string GetGlobalDebugPathRemapping() const { return {}; }

  // Return the DWARF version to emit, in the absence of arguments
  // to the contrary.
  virtual unsigned GetDefaultDwarfVersion() const;

  // Some toolchains may have different restrictions on the DWARF version and
  // may need to adjust it.
  virtual unsigned getMaxDwarfVersion() const { return UINT_MAX; }

  // True if the driver should assume "-fstandalone-debug"
  // in the absence of an option specifying otherwise,
  // provided that debugging was requested in the first place.
  // i.e. a value of 'true' does not imply that debugging is wanted.
  virtual bool GetDefaultStandaloneDebug() const { return false; }

  // Return the default debugger "tuning."
  virtual llvm::DebuggerKind getDefaultDebuggerTuning() const {
    return llvm::DebuggerKind::GDB;
  }

  virtual bool supportsDebugInfoOption(const llvm::opt::Arg *) const {
    return true;
  }

  virtual void
  adjustDebugInfoKind(llvm::codegenoptions::DebugInfoKind &DebugInfoKind,
                      const llvm::opt::ArgList &Args) const {}

  virtual llvm::ExceptionHandling
  GetExceptionModel(const llvm::opt::ArgList &Args) const;

  virtual std::string getThreadModel() const { return "posix"; }

  virtual bool isThreadModelSupported(const llvm::StringRef Model) const;

  virtual bool isBareMetal() const { return false; }

  virtual std::string getMultiarchTriple(const Driver &D,
                                         const llvm::Triple &TargetTriple,
                                         llvm::StringRef SysRoot) const {
    return TargetTriple.str();
  }

  virtual std::string
  ComputeLLVMTriple(const llvm::opt::ArgList &Args,
                    types::ID InputType = types::TY_INVALID) const;

  virtual std::string
  ComputeEffectiveTriple(const llvm::opt::ArgList &Args,
                         types::ID InputType = types::TY_INVALID) const;

  virtual std::string computeSysRoot() const;

  virtual void
  AddNeverCSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &FrontendArgs) const;

  virtual void
  addNeverCTargetOptions(const llvm::opt::ArgList &DriverArgs,
                         llvm::opt::ArgStringList &FrontendArgs) const;

  virtual void addNeverCFrontendAsTargetOptions(
      const llvm::opt::ArgList &Args,
      llvm::opt::ArgStringList &FrontendAsArgs) const;

  virtual void
  addNeverCWarningOptions(llvm::opt::ArgStringList &FrontendArgs) const;

  // GetRuntimeLibType - Determine the runtime library type to use with the
  // given compilation arguments.
  virtual RuntimeLibType
  GetRuntimeLibType(const llvm::opt::ArgList &Args) const;

  // GetUnwindLibType - Determine the unwind library type to use with the
  // given compilation arguments.
  virtual UnwindLibType GetUnwindLibType(const llvm::opt::ArgList &Args) const;

  void AddFilePathLibArgs(const llvm::opt::ArgList &Args,
                          llvm::opt::ArgStringList &CmdArgs) const;

  virtual bool isFastMathRuntimeAvailable(const llvm::opt::ArgList &Args,
                                          std::string &Path) const;

  bool addFastMathRuntimeIfAvailable(const llvm::opt::ArgList &Args,
                                     llvm::opt::ArgStringList &CmdArgs) const;

  virtual llvm::VersionTuple
  computeMSVCVersion(const Driver *D, const llvm::opt::ArgList &Args) const;

  virtual llvm::DenormalMode getDefaultDenormalModeForType(
      const llvm::opt::ArgList &DriverArgs, const JobAction &JA,
      const llvm::fltSemantics *FPType = nullptr) const {
    return llvm::DenormalMode::getIEEE();
  }
};

class RegisterEffectiveTriple {
  const ToolChain &TC;

public:
  RegisterEffectiveTriple(const ToolChain &TC, llvm::Triple T) : TC(TC) {
    TC.setEffectiveTriple(std::move(T));
  }

  ~RegisterEffectiveTriple() { TC.setEffectiveTriple(llvm::Triple()); }
};

} // namespace driver

} // namespace neverc

#endif // NEVERC_INVOKE_TOOLCHAIN_H
