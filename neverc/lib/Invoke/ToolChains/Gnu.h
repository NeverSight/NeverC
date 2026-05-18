#ifndef NEVERC_LIB_DRIVER_TOOLCHAINS_GNU_H
#define NEVERC_LIB_DRIVER_TOOLCHAINS_GNU_H

#include "neverc/Invoke/Tool.h"
#include "neverc/Invoke/ToolChain.h"
#include <set>

namespace neverc {
namespace driver {

struct DetectedMultilibs {
  MultilibSet Multilibs;

  llvm::SmallVector<Multilib> SelectedMultilibs;

  std::optional<Multilib> BiarchSibling;
};

namespace tools {

namespace gnutools {
class LLVM_LIBRARY_VISIBILITY Linker : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("GNU::Linker", "linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

class LLVM_LIBRARY_VISIBILITY StaticLibTool : public Tool {
public:
  StaticLibTool(const ToolChain &TC)
      : Tool("GNU::StaticLibTool", "static-lib-linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
} // end namespace gnutools
} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY Generic_GCC : public ToolChain {
public:
  struct GCCVersion {
    /// The unparsed text of the version.
    std::string Text;

    /// The parsed major, minor, and patch numbers.
    int Major, Minor, Patch;

    /// The text of the parsed major, and major+minor versions.
    std::string MajorStr, MinorStr;

    /// Any textual suffix on the patch number.
    std::string PatchSuffix;

    static GCCVersion Parse(llvm::StringRef VersionText);
    bool isOlderThan(int RHSMajor, int RHSMinor, int RHSPatch,
                     llvm::StringRef RHSPatchSuffix = llvm::StringRef()) const;
    bool operator<(const GCCVersion &RHS) const {
      return isOlderThan(RHS.Major, RHS.Minor, RHS.Patch, RHS.PatchSuffix);
    }
    bool operator>(const GCCVersion &RHS) const { return RHS < *this; }
    bool operator<=(const GCCVersion &RHS) const { return !(*this > RHS); }
    bool operator>=(const GCCVersion &RHS) const { return !(*this < RHS); }
  };

  class GCCInstallationDetector {
    bool IsValid;
    llvm::Triple GCCTriple;
    const Driver &D;

    std::string GCCInstallPath;
    std::string GCCParentLibPath;

    /// The primary multilib appropriate for the given flags.
    Multilib SelectedMultilib;
    /// On Biarch systems, this corresponds to the default multilib when
    /// targeting the non-default multilib. Otherwise, it is empty.
    std::optional<Multilib> BiarchSibling;

    GCCVersion Version;

    // We retain the list of install paths that were considered and rejected in
    // order to print out detailed information in verbose mode.
    std::set<std::string> CandidateGCCInstallPaths;

    /// The set of multilibs that the detected installation supports.
    MultilibSet Multilibs;

    // Gentoo-specific toolchain configurations are stored here.
    const std::string GentooConfigDir = "/etc/env.d/gcc";

  public:
    explicit GCCInstallationDetector(const Driver &D) : IsValid(false), D(D) {}
    void init(const llvm::Triple &TargetTriple, const llvm::opt::ArgList &Args);

    /// Check whether we detected a valid GCC install.
    bool isValid() const { return IsValid; }

    /// Get the GCC triple for the detected install.
    const llvm::Triple &getTriple() const { return GCCTriple; }

    /// Get the detected GCC installation path.
    llvm::StringRef getInstallPath() const { return GCCInstallPath; }

    /// Get the detected GCC parent lib path.
    llvm::StringRef getParentLibPath() const { return GCCParentLibPath; }

    /// Get the detected Multilib
    const Multilib &getMultilib() const { return SelectedMultilib; }

    /// Get the whole MultilibSet
    const MultilibSet &getMultilibs() const { return Multilibs; }

    /// Get the biarch sibling multilib (if it exists).
    /// \return true iff such a sibling exists
    bool getBiarchSibling(Multilib &M) const;

    /// Get the detected GCC version string.
    const GCCVersion &getVersion() const { return Version; }

    /// Print information about the detected GCC installation.
    void print(llvm::raw_ostream &OS) const;

  private:
    static void CollectLibDirsAndTriples(
        const llvm::Triple &TargetTriple, const llvm::Triple &BiarchTriple,
        llvm::SmallVectorImpl<llvm::StringRef> &LibDirs,
        llvm::SmallVectorImpl<llvm::StringRef> &TripleAliases,
        llvm::SmallVectorImpl<llvm::StringRef> &BiarchLibDirs,
        llvm::SmallVectorImpl<llvm::StringRef> &BiarchTripleAliases);

    void AddDefaultGCCPrefixes(const llvm::Triple &TargetTriple,
                               llvm::SmallVectorImpl<std::string> &Prefixes,
                               llvm::StringRef SysRoot);

    bool ScanGCCForMultilibs(const llvm::Triple &TargetTriple,
                             llvm::StringRef Path);

    void ScanLibDirForGCCTriple(const llvm::Triple &TargetArch,
                                const llvm::opt::ArgList &Args,
                                const std::string &LibDir,
                                llvm::StringRef CandidateTriple,
                                bool NeedsBiarchSuffix, bool GCCDirExists,
                                bool GCCCrossDirExists);

    bool ScanGentooConfigs(
        const llvm::Triple &TargetTriple, const llvm::opt::ArgList &Args,
        const llvm::SmallVectorImpl<llvm::StringRef> &CandidateTriples,
        const llvm::SmallVectorImpl<llvm::StringRef> &BiarchTriples);

    bool ScanGentooGccConfig(const llvm::Triple &TargetTriple,
                             const llvm::opt::ArgList &Args,
                             llvm::StringRef CandidateTriple,
                             bool NeedsBiarchSuffix = false);
  };

protected:
  GCCInstallationDetector GCCInstallation;

public:
  Generic_GCC(const Driver &D, const llvm::Triple &Triple,
              const llvm::opt::ArgList &Args);
  ~Generic_GCC() override;

  void printVerboseInfo(llvm::raw_ostream &OS) const override;

  UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &Args) const override;
  bool IsIntegratedAssemblerDefault() const override;
  llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args,
                llvm::StringRef BoundArch) const override;

protected:
  Tool *buildLinker() const override;

  void PushPPaths(ToolChain::path_list &PPaths);
  void AddMultilibPaths(const Driver &D, const std::string &SysRoot,
                        const std::string &OSLibDir,
                        const std::string &MultiarchTriple, path_list &Paths);
  void AddMultiarchPaths(const Driver &D, const std::string &SysRoot,
                         const std::string &OSLibDir, path_list &Paths);
  void AddMultilibIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                              llvm::opt::ArgStringList &FrontendArgs) const;
};

class LLVM_LIBRARY_VISIBILITY Generic_ELF : public Generic_GCC {
  virtual void anchor();

public:
  Generic_ELF(const Driver &D, const llvm::Triple &Triple,
              const llvm::opt::ArgList &Args)
      : Generic_GCC(D, Triple, Args) {}

  void
  addNeverCTargetOptions(const llvm::opt::ArgList &DriverArgs,
                         llvm::opt::ArgStringList &FrontendArgs) const override;

  virtual std::string getDynamicLinker(const llvm::opt::ArgList &Args) const {
    return {};
  }

  virtual void addExtraOpts(llvm::opt::ArgStringList &CmdArgs) const {}
};

} // end namespace toolchains
} // end namespace driver
} // end namespace neverc

#endif // NEVERC_LIB_DRIVER_TOOLCHAINS_GNU_H
