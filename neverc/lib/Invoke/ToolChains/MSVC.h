#ifndef NEVERC_LIB_DRIVER_TOOLCHAINS_MSVC_H
#define NEVERC_LIB_DRIVER_TOOLCHAINS_MSVC_H

#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/Tool.h"
#include "neverc/Invoke/ToolChain.h"
#include "llvm/WindowsDriver/MSVCPaths.h"

namespace neverc {
namespace driver {
namespace tools {

namespace visualstudio {
class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("visualstudio::Linker", "linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // end namespace visualstudio

} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY MSVCToolChain : public ToolChain {
public:
  MSVCToolChain(const Driver &D, const llvm::Triple &Triple,
                const llvm::opt::ArgList &Args);

  llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args,
                llvm::StringRef BoundArch) const override;

  UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &Args) const override;
  std::string getSubDirectoryPath(llvm::SubDirectoryType Type,
                                  llvm::StringRef SubdirParent = "") const;
  std::string getSubDirectoryPath(llvm::SubDirectoryType Type,
                                  llvm::Triple::ArchType TargetArch) const;

  bool getIsVS2017OrNewer() const {
    return VSLayout == llvm::ToolsetLayout::VS2017OrNewer;
  }

  void AddNeverCSystemIncludeArgs(
      const llvm::opt::ArgList &DriverArgs,
      llvm::opt::ArgStringList &FrontendArgs) const override;

  bool getWindowsSDKLibraryPath(const llvm::opt::ArgList &Args,
                                std::string &path) const;
  bool getUniversalCRTLibraryPath(const llvm::opt::ArgList &Args,
                                  std::string &path) const;
  bool useUniversalCRT() const;
  llvm::VersionTuple
  computeMSVCVersion(const Driver *D,
                     const llvm::opt::ArgList &Args) const override;

  std::string ComputeEffectiveTriple(const llvm::opt::ArgList &Args,
                                     types::ID InputType) const override;

  void printVerboseInfo(llvm::raw_ostream &OS) const override;

  bool FoundMSVCInstall() const { return !VCToolChainPath.empty(); }

protected:
  void AddSystemIncludeWithSubfolder(const llvm::opt::ArgList &DriverArgs,
                                     llvm::opt::ArgStringList &FrontendArgs,
                                     const std::string &folder,
                                     const llvm::Twine &subfolder1,
                                     const llvm::Twine &subfolder2 = "",
                                     const llvm::Twine &subfolder3 = "") const;

  Tool *buildLinker() const override;

private:
  std::optional<llvm::StringRef> WinSdkDir, WinSdkVersion, WinSysRoot;
  std::string VCToolChainPath;
  std::string NeverCWinSysRootStorage;
  llvm::ToolsetLayout VSLayout = llvm::ToolsetLayout::OlderVS;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace neverc

#endif // NEVERC_LIB_DRIVER_TOOLCHAINS_MSVC_H
