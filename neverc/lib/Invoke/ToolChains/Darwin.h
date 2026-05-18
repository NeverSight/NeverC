#ifndef NEVERC_LIB_DRIVER_TOOLCHAINS_DARWIN_H
#define NEVERC_LIB_DRIVER_TOOLCHAINS_DARWIN_H

#include "neverc/Foundation/Core/DarwinSDKInfo.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Invoke/Tool.h"
#include "neverc/Invoke/ToolChain.h"

namespace linker {
struct LinkerDriverConfig;
}

namespace neverc {
namespace driver {

namespace toolchains {
class MachO;
} // end namespace toolchains

namespace tools {

namespace darwin {
llvm::Triple::ArchType getArchTypeForMachOArchName(llvm::StringRef Str);
void setTripleTypeForMachOArchName(llvm::Triple &T, llvm::StringRef Str,
                                   const llvm::opt::ArgList &Args);

class LLVM_LIBRARY_VISIBILITY MachOTool : public Tool {
  virtual void anchor();

protected:
  void AddMachOArch(const llvm::opt::ArgList &Args,
                    llvm::opt::ArgStringList &CmdArgs) const;

  const toolchains::MachO &getMachOToolChain() const {
    return reinterpret_cast<const toolchains::MachO &>(getToolChain());
  }

public:
  MachOTool(const char *Name, const char *ShortName, const ToolChain &TC)
      : Tool(Name, ShortName, TC) {}
};

class LLVM_LIBRARY_VISIBILITY Linker : public MachOTool {
  void AddLinkArgs(Compilation &C, const llvm::opt::ArgList &Args,
                   llvm::opt::ArgStringList &CmdArgs,
                   const InputInfoList &Inputs,
                   llvm::VersionTuple Version) const;

public:
  Linker(const ToolChain &TC) : MachOTool("darwin::Linker", "linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY StaticLibTool : public MachOTool {
public:
  StaticLibTool(const ToolChain &TC)
      : MachOTool("darwin::StaticLibTool", "static-lib-linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY Lipo : public MachOTool {
public:
  Lipo(const ToolChain &TC) : MachOTool("darwin::Lipo", "lipo", TC) {}

  bool hasIntegratedCPP() const override { return false; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY Dsymutil : public MachOTool {
public:
  Dsymutil(const ToolChain &TC)
      : MachOTool("darwin::Dsymutil", "dsymutil", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isDsymutilJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // end namespace darwin
} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY MachO : public ToolChain {
protected:
  Tool *buildLinker() const override;
  Tool *buildStaticLibTool() const override;
  Tool *getTool(Action::ActionClass AC) const override;

private:
  mutable std::unique_ptr<tools::darwin::Lipo> Lipo;
  mutable std::unique_ptr<tools::darwin::Dsymutil> Dsymutil;

  mutable std::optional<llvm::VersionTuple> LinkerVersion;

public:
  MachO(const Driver &D, const llvm::Triple &Triple,
        const llvm::opt::ArgList &Args);
  ~MachO() override;

  llvm::StringRef getMachOArchName(const llvm::opt::ArgList &Args) const;

  llvm::VersionTuple getLinkerVersion(const llvm::opt::ArgList &Args) const;

  virtual void AddLinkRuntimeLibArgs(const llvm::opt::ArgList &Args,
                                     llvm::opt::ArgStringList &CmdArgs,
                                     bool ForceLinkBuiltinRT = false) const;

  virtual void addStartObjectFileArgs(const llvm::opt::ArgList &Args,
                                      llvm::opt::ArgStringList &CmdArgs) const {
  }

  virtual void addMinVersionArgs(const llvm::opt::ArgList &Args,
                                 llvm::opt::ArgStringList &CmdArgs) const {}

  virtual void addPlatformVersionArgs(const llvm::opt::ArgList &Args,
                                      llvm::opt::ArgStringList &CmdArgs) const {
  }

  virtual bool isKernelStatic() const { return false; }

  bool isTargetIOSBased() const { return false; }

  enum RuntimeLinkOptions : unsigned {
    /// Link the library in even if it can't be found in the VFS.
    RLO_AlwaysLink = 1 << 0,

    /// Use the embedded runtime from the macho_embedded directory.
    RLO_IsEmbedded = 1 << 1,

    /// Emit rpaths for @executable_path as well as the resource directory.
    RLO_AddRPath = 1 << 2,
  };

  void AddLinkRuntimeLib(const llvm::opt::ArgList &Args,
                         llvm::opt::ArgStringList &CmdArgs,
                         llvm::StringRef Component,
                         RuntimeLinkOptions Opts = RuntimeLinkOptions(),
                         bool IsShared = false) const;

  types::ID LookupTypeForExtension(llvm::StringRef Ext) const override;

  bool HasNativeLLVMSupport() const override;

  llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args,
                llvm::StringRef BoundArch) const override;

  bool IsMathErrnoDefault() const override { return false; }

  UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &Args) const override;

  RuntimeLibType GetDefaultRuntimeLibType() const override {
    return ToolChain::RLT_CompilerRT;
  }

  bool UseDwarfDebugFlags() const override;
  std::string GetGlobalDebugPathRemapping() const override;

  llvm::ExceptionHandling
  GetExceptionModel(const llvm::opt::ArgList &Args) const override {
    return llvm::ExceptionHandling::None;
  }

  virtual llvm::StringRef getOSLibraryNameSuffix(bool IgnoreSim = false) const {
    return "";
  }
};

class LLVM_LIBRARY_VISIBILITY Darwin : public MachO {
public:
  mutable bool TargetInitialized;

  enum DarwinPlatformKind { MacOS, IPhoneOS, LastDarwinPlatform = IPhoneOS };
  enum DarwinEnvironmentKind {
    NativeEnvironment,
    Simulator,
  };

  mutable DarwinPlatformKind TargetPlatform;
  mutable DarwinEnvironmentKind TargetEnvironment;

  mutable llvm::VersionTuple TargetVersion;

  mutable std::optional<DarwinSDKInfo> SDKInfo;

private:
  void AddDeploymentTarget(llvm::opt::DerivedArgList &Args) const;

public:
  Darwin(const Driver &D, const llvm::Triple &Triple,
         const llvm::opt::ArgList &Args);
  ~Darwin() override;

  std::string ComputeEffectiveTriple(const llvm::opt::ArgList &Args,
                                     types::ID InputType) const override;

  void addMinVersionArgs(const llvm::opt::ArgList &Args,
                         llvm::opt::ArgStringList &CmdArgs) const override;

  void addPlatformVersionArgs(const llvm::opt::ArgList &Args,
                              llvm::opt::ArgStringList &CmdArgs) const override;

  void populatePlatformVersionConfig(const llvm::opt::ArgList &Args,
                                     ::linker::LinkerDriverConfig &Cfg) const;

  void addStartObjectFileArgs(const llvm::opt::ArgList &Args,
                              llvm::opt::ArgStringList &CmdArgs) const override;

  bool isKernelStatic() const override {
    return !(isTargetIPhoneOS() && !isIPhoneOSVersionLT(6, 0));
  }

protected:
  void setTarget(DarwinPlatformKind Platform, DarwinEnvironmentKind Environment,
                 unsigned Major, unsigned Minor, unsigned Micro) const {
    if (TargetInitialized && TargetPlatform == Platform &&
        TargetEnvironment == Environment &&
        TargetVersion == llvm::VersionTuple(Major, Minor, Micro))
      return;

    assert(!TargetInitialized && "Target already initialized!");
    TargetInitialized = true;
    TargetPlatform = Platform;
    TargetEnvironment = Environment;
    TargetVersion = llvm::VersionTuple(Major, Minor, Micro);
    if (Environment == Simulator)
      const_cast<Darwin *>(this)->setTripleEnvironment(llvm::Triple::Simulator);
  }

public:
  bool isTargetIPhoneOS() const {
    assert(TargetInitialized && "Target not initialized!");
    return TargetPlatform == IPhoneOS && TargetEnvironment == NativeEnvironment;
  }

  bool isTargetIOSSimulator() const {
    assert(TargetInitialized && "Target not initialized!");
    return TargetPlatform == IPhoneOS && TargetEnvironment == Simulator;
  }

  bool isTargetIOSBased() const {
    assert(TargetInitialized && "Target not initialized!");
    return isTargetIPhoneOS() || isTargetIOSSimulator();
  }

  bool isTargetMacOS() const {
    assert(TargetInitialized && "Target not initialized!");
    return TargetPlatform == MacOS;
  }

  bool isTargetMacOSBased() const {
    assert(TargetInitialized && "Target not initialized!");
    return TargetPlatform == MacOS;
  }

  bool isTargetAppleSiliconMac() const {
    assert(TargetInitialized && "Target not initialized!");
    return isTargetMacOSBased() && getArch() == llvm::Triple::aarch64;
  }

  bool isTargetInitialized() const { return TargetInitialized; }

  llvm::VersionTuple getTripleTargetVersion() const {
    assert(TargetInitialized && "Target not initialized!");
    return TargetVersion;
  }

  bool isIPhoneOSVersionLT(unsigned V0, unsigned V1 = 0,
                           unsigned V2 = 0) const {
    assert(isTargetIOSBased() && "Unexpected call for non iOS target!");
    return TargetVersion < llvm::VersionTuple(V0, V1, V2);
  }

  bool isMacosxVersionLT(unsigned V0, unsigned V1 = 0, unsigned V2 = 0) const {
    assert(isTargetMacOSBased() && getTriple().isMacOSX() &&
           "Unexpected call for non OS X target!");
    llvm::VersionTuple MinVers =
        llvm::Triple(getTriple().getArchName(), "apple", "macos")
            .getMinimumSupportedOSVersion();
    return (!MinVers.empty() && MinVers > TargetVersion
                ? MinVers
                : TargetVersion) < llvm::VersionTuple(V0, V1, V2);
  }

protected:
  void
  addNeverCTargetOptions(const llvm::opt::ArgList &DriverArgs,
                         llvm::opt::ArgStringList &FrontendArgs) const override;

  void addNeverCFrontendAsTargetOptions(
      const llvm::opt::ArgList &Args,
      llvm::opt::ArgStringList &FrontendAsArgs) const override;

  llvm::StringRef getPlatformFamily() const;
  llvm::StringRef getOSLibraryNameSuffix(bool IgnoreSim = false) const override;

public:
  static llvm::StringRef getSDKName(llvm::StringRef isysroot);

  // Darwin tools support multiple architectures and most development is
  // done against SDKs, so compiling for a different architecture should
  // not get any special treatment.
  bool isCrossCompiling() const override { return false; }

  llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args,
                llvm::StringRef BoundArch) const override;

  LangOptions::StackProtectorMode
  GetDefaultStackProtectorLevel(bool Kernel) const override {
    if (isTargetIOSBased())
      return LangOptions::SSPOn;
    else if (isTargetMacOSBased() && !isMacosxVersionLT(10, 6))
      return LangOptions::SSPOn;
    else if (isTargetMacOSBased() && !isMacosxVersionLT(10, 5) && !Kernel)
      return LangOptions::SSPOn;

    return LangOptions::SSPOff;
  }

  llvm::ExceptionHandling
  GetExceptionModel(const llvm::opt::ArgList &Args) const override;

  void printVerboseInfo(llvm::raw_ostream &OS) const override;
};

class LLVM_LIBRARY_VISIBILITY DarwinNeverC : public Darwin {
public:
  DarwinNeverC(const Driver &D, const llvm::Triple &Triple,
               const llvm::opt::ArgList &Args);

  RuntimeLibType
  GetRuntimeLibType(const llvm::opt::ArgList &Args) const override;

  void AddLinkRuntimeLibArgs(const llvm::opt::ArgList &Args,
                             llvm::opt::ArgStringList &CmdArgs,
                             bool ForceLinkBuiltinRT = false) const override;

  void AddNeverCSystemIncludeArgs(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &FrontendArgs) const override;

  void addNeverCWarningOptions(
      llvm::opt::ArgStringList &FrontendArgs) const override;

  unsigned GetDefaultDwarfVersion() const override;
  // Until dtrace (via CTF) and LLDB can deal with distributed debug info,
  // Darwin defaults to standalone/full debug info.
  bool GetDefaultStandaloneDebug() const override { return true; }
  llvm::DebuggerKind getDefaultDebuggerTuning() const override {
    return llvm::DebuggerKind::LLDB;
  }

private:
  llvm::SmallString<128>
  GetEffectiveSysroot(const llvm::opt::ArgList &DriverArgs) const;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace neverc

#endif // NEVERC_LIB_DRIVER_TOOLCHAINS_DARWIN_H
