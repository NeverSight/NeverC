#include "Gnu.h"
#include "CommonArgs.h"
#include "neverc/Config/config.h" // for GCC_INSTALL_PREFIX
#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/MultilibBuilder.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Invoke/Tool.h"
#include "neverc/Invoke/ToolChain.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/TargetParser.h"
#include <system_error>

using namespace neverc::driver;
using namespace neverc::driver::toolchains;
using namespace neverc;
using namespace llvm::opt;

using tools::addMultilibFlag;
using tools::addPathIfExists;

// ===----------------------------------------------------------------------===
// Linker option helpers
// ===----------------------------------------------------------------------===

namespace {
const char *getLDMOption(const llvm::Triple &T, const ArgList &Args) {
  switch (T.getArch()) {
  case llvm::Triple::aarch64:
    return "aarch64linux";
  case llvm::Triple::x86_64:
    return "elf_x86_64";
  default:
    return nullptr;
  }
}

bool getStaticPIE(const ArgList &Args, const ToolChain &TC) {
  bool HasStaticPIE = Args.hasArg(options::OPT_static_pie);
  if (HasStaticPIE && Args.hasArg(options::OPT_no_pie)) {
    const Driver &D = TC.getDriver();
    const llvm::opt::OptTable &Opts = D.getOpts();
    llvm::StringRef StaticPIEName = Opts.getOptionName(options::OPT_static_pie);
    llvm::StringRef NoPIEName = Opts.getOptionName(options::OPT_no_pie);
    D.Diag(diag::err_drv_cannot_mix_options) << StaticPIEName << NoPIEName;
  }
  return HasStaticPIE;
}

bool getStatic(const ArgList &Args) {
  return Args.hasArg(options::OPT_static) &&
         !Args.hasArg(options::OPT_static_pie);
}
} // namespace

// ===----------------------------------------------------------------------===
// Tool job construction
// ===----------------------------------------------------------------------===

void tools::gnutools::StaticLibTool::ConstructJob(
    Compilation &C, const JobAction &JA, const InputInfo &Output,
    const InputInfoList &Inputs, const ArgList &Args,
    const char *LinkingOutput) const {
  const Driver &D = getToolChain().getDriver();

  Args.ClaimAllArgs(options::OPT_g_Group);
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  Args.ClaimAllArgs(options::OPT_w);

  auto OutputFileName = Output.getFilename();
  if (Output.isFilename() && llvm::sys::fs::exists(OutputFileName)) {
    if (std::error_code EC = llvm::sys::fs::remove(OutputFileName)) {
      D.Diag(diag::err_drv_unable_to_remove_file) << EC.message();
      return;
    }
  }

  ArgStringList CmdArgs;
  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("ar"));
  C.addCommand(std::make_unique<ArchiveCommand>(
      JA, *this, ResponseFileSupport::None(), Exec, CmdArgs, Inputs, Output));
}

void tools::gnutools::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                           const InputInfo &Output,
                                           const InputInfoList &Inputs,
                                           const ArgList &Args,
                                           const char *LinkingOutput) const {
  const auto &ToolChain = static_cast<const Generic_ELF &>(getToolChain());
  const Driver &D = ToolChain.getDriver();

  const llvm::Triple &Triple = getToolChain().getEffectiveTriple();

  const llvm::Triple::ArchType Arch = ToolChain.getArch();
  const bool IsStaticPIE = getStaticPIE(Args, ToolChain);
  const bool IsStatic = getStatic(Args);
  const bool HasCRTBeginEndFiles = ToolChain.getTriple().hasEnvironment();

  ArgStringList CmdArgs;

  // Silence warning for "neverc -g foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_g_Group);
  // and "neverc -emit-llvm foo.o -o foo"
  Args.ClaimAllArgs(options::OPT_emit_llvm);
  // and for "neverc -w foo.o -o foo". Other warning options are already
  // handled somewhere else.
  Args.ClaimAllArgs(options::OPT_w);

  // --sysroot, -s, --eh-frame-hdr, -m, -EL/-EB, --no-dynamic-linker,
  // --export-dynamic are now passed via LinkerDriverConfig.

  // Validate emulation early; it's set on LinkerDriverConfig below.
  if (!getLDMOption(ToolChain.getTriple(), Args)) {
    D.Diag(diag::err_target_unknown_triple) << Triple.str();
    return;
  }

  ToolChain.addExtraOpts(CmdArgs);

  // Output type (shared/pie/relocatable), dynamic linker, export-dynamic
  // and no-dynamic-linker are now conveyed via LinkerDriverConfig.
  const bool IsShared = Args.hasArg(options::OPT_shared);
  bool IsPIE = false;
  bool NoDynLinker = false;
  bool ExportDynamic = false;
  std::string DynLinkerPath;
  // -static is now conveyed via LinkerDriverConfig.staticLink (set below).
  if (IsStaticPIE) {
    IsPIE = true;
    NoDynLinker = true;
    CmdArgs.push_back("-z");
    CmdArgs.push_back("text");
  } else if (!IsStatic && !Args.hasArg(options::OPT_r)) {
    ExportDynamic = Args.hasArg(options::OPT_rdynamic);
    if (!IsShared) {
      IsPIE = Args.hasFlag(options::OPT_pie, options::OPT_no_pie, true);
      DynLinkerPath =
          (llvm::Twine(D.DyldPrefix) + ToolChain.getDynamicLinker(Args)).str();
    }
  }

  // Output file is passed via LinkerDriverConfig.outputFile.

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles,
                   options::OPT_r)) {
    const char *crt1 = nullptr;
    if (!Args.hasArg(options::OPT_shared)) {
      if (IsPIE)
        crt1 = "Scrt1.o";
      else if (IsStaticPIE)
        crt1 = "rcrt1.o";
      else
        crt1 = "crt1.o";
    }
    if (crt1)
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath(crt1)));

    CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crti.o")));

    if (HasCRTBeginEndFiles) {
      std::string P;
      if (ToolChain.GetRuntimeLibType(Args) == ToolChain::RLT_CompilerRT) {
        std::string crtbegin =
            ToolChain.getCompilerRT(Args, "crtbegin", ToolChain::FT_Object);
        if (ToolChain.getVFS().exists(crtbegin))
          P = crtbegin;
      }
      if (P.empty()) {
        const char *crtbegin;
        if (Args.hasArg(options::OPT_shared))
          crtbegin = "crtbeginS.o";
        else if (IsStatic)
          crtbegin = "crtbeginT.o";
        else if (IsPIE || IsStaticPIE)
          crtbegin = "crtbeginS.o";
        else
          crtbegin = "crtbegin.o";
        P = ToolChain.GetFilePath(crtbegin);
      }
      CmdArgs.push_back(Args.MakeArgString(P));
    }

    // Add crtfastmath.o if available and fast math is enabled.
    ToolChain.addFastMathRuntimeIfAvailable(Args, CmdArgs);
  }

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_u});

  ToolChain.AddFilePathLibArgs(Args, CmdArgs);

  AddLinkerInputs(ToolChain, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_r)) {
    if (!Args.hasArg(options::OPT_nodefaultlibs)) {
      if (IsStatic || IsStaticPIE)
        CmdArgs.push_back("--start-group");

      bool WantPthread = Args.hasArg(options::OPT_pthread) ||
                         Args.hasArg(options::OPT_pthreads);

      AddRunTimeLibs(ToolChain, D, CmdArgs, Args);

      if (WantPthread)
        CmdArgs.push_back("-lpthread");

      if (Args.hasArg(options::OPT_fsplit_stack))
        CmdArgs.push_back("--wrap=pthread_create");

      if (!Args.hasArg(options::OPT_nolibc))
        CmdArgs.push_back("-lc");

      if (IsStatic || IsStaticPIE)
        CmdArgs.push_back("--end-group");
      else
        AddRunTimeLibs(ToolChain, D, CmdArgs, Args);
    }

    if (!Args.hasArg(options::OPT_nostartfiles)) {
      if (HasCRTBeginEndFiles) {
        std::string P;
        if (ToolChain.GetRuntimeLibType(Args) == ToolChain::RLT_CompilerRT) {
          std::string crtend =
              ToolChain.getCompilerRT(Args, "crtend", ToolChain::FT_Object);
          if (ToolChain.getVFS().exists(crtend))
            P = crtend;
        }
        if (P.empty()) {
          const char *crtend;
          if (Args.hasArg(options::OPT_shared))
            crtend = "crtendS.o";
          else if (IsPIE || IsStaticPIE)
            crtend = "crtendS.o";
          else
            crtend = "crtend.o";
          P = ToolChain.GetFilePath(crtend);
        }
        CmdArgs.push_back(Args.MakeArgString(P));
      }
      CmdArgs.push_back(Args.MakeArgString(ToolChain.GetFilePath("crtn.o")));
    }
  }

  Args.AddAllArgs(CmdArgs, options::OPT_T);

  const char *Exec = Args.MakeArgString(ToolChain.GetLinkerPath());

  auto LCmd = std::make_unique<LinkerCommand>(
      JA, *this, ResponseFileSupport::AtFileCurCP(), Exec, CmdArgs, Inputs,
      LinkerFlavor::Gnu, Output);
  populateLinkerDriverConfig(ToolChain, Args, LCmd->getDriverConfig());
  auto &GnuCfg = LCmd->getDriverConfig();
  GnuCfg.outputFile = Output.getFilename();
  GnuCfg.shared = IsShared;
  GnuCfg.pie = IsPIE;
  GnuCfg.relocatable = Args.hasArg(options::OPT_r);
  GnuCfg.staticLink = IsStatic || IsStaticPIE;
  GnuCfg.noDynamicLinker = NoDynLinker;
  GnuCfg.exportDynamic = ExportDynamic;
  if (!DynLinkerPath.empty())
    GnuCfg.dynamicLinker = std::move(DynLinkerPath);
  if (const char *LDMOption = getLDMOption(ToolChain.getTriple(), Args))
    GnuCfg.emulation = LDMOption;
  if (Triple.isAArch64())
    GnuCfg.endianness = 1; // little-endian

  C.addCommand(std::move(LCmd));
}

namespace {
// Filter to remove Multilibs that don't exist as a suffix to Path
class FilterNonExistent {
  llvm::StringRef Base, File;
  llvm::vfs::FileSystem &VFS;

public:
  FilterNonExistent(llvm::StringRef Base, llvm::StringRef File,
                    llvm::vfs::FileSystem &VFS)
      : Base(Base), File(File), VFS(VFS) {}
  bool operator()(const Multilib &M) {
    return !VFS.exists(Base + M.gccSuffix() + File);
  }
};
} // end anonymous namespace

namespace {
bool findBiarchMultilibs(const Driver &D, const llvm::Triple &,
                         llvm::StringRef Path, DetectedMultilibs &Result) {
  MultilibBuilder DefaultBuilder;
  DefaultBuilder.flag("-m64");
  Multilib Default = DefaultBuilder.makeMultilib();

  Multilib Alt64 = MultilibBuilder()
                       .gccSuffix("/64")
                       .includeSuffix("/64")
                       .flag("-m64")
                       .makeMultilib();

  Result.Multilibs.push_back(Default);
  Result.Multilibs.push_back(Alt64);

  FilterNonExistent NonExistent(Path, "/crtbegin.o", D.getVFS());
  Result.Multilibs.FilterOut(NonExistent);

  Multilib::flags_list Flags;
  addMultilibFlag(true, "-m64", Flags);
  if (!Result.Multilibs.select(Flags, Result.SelectedMultilibs))
    return false;

  if (Result.SelectedMultilibs.back() == Alt64)
    Result.BiarchSibling = Default;

  return true;
}
} // namespace

// ===----------------------------------------------------------------------===
// GCC version detection & installation
// ===----------------------------------------------------------------------===

bool Generic_GCC::GCCVersion::isOlderThan(
    int RHSMajor, int RHSMinor, int RHSPatch,
    llvm::StringRef RHSPatchSuffix) const {
  if (Major != RHSMajor)
    return Major < RHSMajor;
  if (Minor != RHSMinor) {
    // Note that versions without a specified minor sort higher than those with
    // a minor.
    if (RHSMinor == -1)
      return true;
    if (Minor == -1)
      return false;
    return Minor < RHSMinor;
  }
  if (Patch != RHSPatch) {
    // Note that versions without a specified patch sort higher than those with
    // a patch.
    if (RHSPatch == -1)
      return true;
    if (Patch == -1)
      return false;

    // Otherwise just sort on the patch itself.
    return Patch < RHSPatch;
  }
  if (PatchSuffix != RHSPatchSuffix) {
    // Sort empty suffixes higher.
    if (RHSPatchSuffix.empty())
      return true;
    if (PatchSuffix.empty())
      return false;

    // Provide a lexicographic sort to make this a total ordering.
    return PatchSuffix < RHSPatchSuffix;
  }

  // The versions are equal.
  return false;
}

/*static*/
Generic_GCC::GCCVersion
Generic_GCC::GCCVersion::Parse(llvm::StringRef VersionText) {
  const GCCVersion BadVersion = {VersionText.str(), -1, -1, -1, "", "", ""};
  std::pair<llvm::StringRef, llvm::StringRef> First = VersionText.split('.');
  std::pair<llvm::StringRef, llvm::StringRef> Second = First.second.split('.');

  llvm::StringRef MajorStr = First.first;
  llvm::StringRef MinorStr = Second.first;
  llvm::StringRef PatchStr = Second.second;

  GCCVersion GoodVersion = {VersionText.str(), -1, -1, -1, "", "", ""};

  // Parse version number strings such as:
  //   5
  //   4.4
  //   4.4-patched
  //   4.4.0
  //   4.4.x
  //   4.4.2-rc4
  //   4.4.x-patched
  //   10-win32
  // Split on '.', handle 1, 2 or 3 such segments. Each segment must contain
  // purely a number, except for the last one, where a non-number suffix
  // is stored in PatchSuffix. The third segment is allowed to not contain
  // a number at all.

  auto TryParseLastNumber = [&](llvm::StringRef Segment, int &Number,
                                std::string &OutStr) -> bool {
    // Look for a number prefix and parse that, and split out any trailing
    // string into GoodVersion.PatchSuffix.

    if (size_t EndNumber = Segment.find_first_not_of("0123456789")) {
      llvm::StringRef NumberStr = Segment.slice(0, EndNumber);
      if (NumberStr.getAsInteger(10, Number) || Number < 0)
        return false;
      OutStr = NumberStr;
      GoodVersion.PatchSuffix = Segment.substr(EndNumber);
      return true;
    }
    return false;
  };
  auto TryParseNumber = [](llvm::StringRef Segment, int &Number) -> bool {
    if (Segment.getAsInteger(10, Number) || Number < 0)
      return false;
    return true;
  };

  if (MinorStr.empty()) {
    // If no minor string, major is the last segment
    if (!TryParseLastNumber(MajorStr, GoodVersion.Major, GoodVersion.MajorStr))
      return BadVersion;
    return GoodVersion;
  }

  if (!TryParseNumber(MajorStr, GoodVersion.Major))
    return BadVersion;
  GoodVersion.MajorStr = MajorStr;

  if (PatchStr.empty()) {
    // If no patch string, minor is the last segment
    if (!TryParseLastNumber(MinorStr, GoodVersion.Minor, GoodVersion.MinorStr))
      return BadVersion;
    return GoodVersion;
  }

  if (!TryParseNumber(MinorStr, GoodVersion.Minor))
    return BadVersion;
  GoodVersion.MinorStr = MinorStr;

  // For the last segment, tolerate a missing number.
  std::string DummyStr;
  TryParseLastNumber(PatchStr, GoodVersion.Patch, DummyStr);
  return GoodVersion;
}

namespace {
llvm::StringRef getGCCToolchainDir(const ArgList &Args,
                                   llvm::StringRef SysRoot) {
  const Arg *A = Args.getLastArg(neverc::driver::options::OPT_gcc_toolchain);
  if (A)
    return A->getValue();

  // If we have a SysRoot, ignore GCC_INSTALL_PREFIX.
  // GCC_INSTALL_PREFIX specifies the gcc installation for the default
  // sysroot and is likely not valid with a different sysroot.
  if (!SysRoot.empty())
    return "";

  return GCC_INSTALL_PREFIX;
}
} // namespace

void Generic_GCC::GCCInstallationDetector::init(
    const llvm::Triple &TargetTriple, const ArgList &Args) {
  llvm::Triple BiarchVariantTriple = TargetTriple;
  // The library directories which may contain GCC installations.
  llvm::SmallVector<llvm::StringRef, 4> CandidateLibDirs,
      CandidateBiarchLibDirs;
  // The compatible GCC triples for this particular architecture.
  llvm::SmallVector<llvm::StringRef, 16> CandidateTripleAliases;
  llvm::SmallVector<llvm::StringRef, 16> CandidateBiarchTripleAliases;
  // Add some triples that we want to check first.
  CandidateTripleAliases.push_back(TargetTriple.str());
  std::string TripleNoVendor = TargetTriple.getArchName().str() + "-" +
                               TargetTriple.getOSAndEnvironmentName().str();
  if (TargetTriple.getVendor() == llvm::Triple::UnknownVendor)
    CandidateTripleAliases.push_back(TripleNoVendor);

  CollectLibDirsAndTriples(TargetTriple, BiarchVariantTriple, CandidateLibDirs,
                           CandidateTripleAliases, CandidateBiarchLibDirs,
                           CandidateBiarchTripleAliases);

  // If --gcc-install-dir= is specified, skip filesystem detection.
  if (const Arg *A =
          Args.getLastArg(neverc::driver::options::OPT_gcc_install_dir_EQ);
      A && A->getValue()[0]) {
    llvm::StringRef InstallDir = A->getValue();
    if (!ScanGCCForMultilibs(TargetTriple, InstallDir)) {
      D.Diag(diag::err_drv_invalid_gcc_install_dir) << InstallDir;
    } else {
      (void)InstallDir.consume_back("/");
      llvm::StringRef VersionText = llvm::sys::path::filename(InstallDir);
      llvm::StringRef TripleText =
          llvm::sys::path::filename(llvm::sys::path::parent_path(InstallDir));

      Version = GCCVersion::Parse(VersionText);
      GCCTriple.setTriple(TripleText);
      GCCInstallPath = std::string(InstallDir);
      GCCParentLibPath = GCCInstallPath + "/../../..";
      IsValid = true;
    }
    return;
  }

  llvm::SmallVector<std::string, 8> Prefixes;
  llvm::StringRef GCCToolchainDir = getGCCToolchainDir(Args, D.SysRoot);
  if (GCCToolchainDir != "") {
    if (GCCToolchainDir.back() == '/')
      GCCToolchainDir = GCCToolchainDir.drop_back(); // remove the /

    Prefixes.push_back(std::string(GCCToolchainDir));
  } else {
    // If we have a SysRoot, try that first.
    if (!D.SysRoot.empty()) {
      Prefixes.push_back(D.SysRoot);
      AddDefaultGCCPrefixes(TargetTriple, Prefixes, D.SysRoot);
    }

    // Then look for gcc installed alongside neverc.
    Prefixes.push_back(D.InstalledDir + "/..");

    // Next, look for prefix(es) that correspond to distribution-supplied gcc
    // installations.
    if (D.SysRoot.empty()) {
      // Typically /usr.
      AddDefaultGCCPrefixes(TargetTriple, Prefixes, D.SysRoot);
    }

    // Try to respect gcc-config on Gentoo if --gcc-toolchain is not provided.
    // This avoids accidentally enforcing the system GCC version when using a
    // custom toolchain.
    llvm::SmallVector<llvm::StringRef, 16> GentooTestTriples;
    // Try to match an exact triple as target triple first.
    // e.g. crossdev -S x86_64-gentoo-linux-gnu will install gcc libs for
    // x86_64-gentoo-linux-gnu. But "neverc -target x86_64-gentoo-linux-gnu"
    // may pick the libraries for x86_64-pc-linux-gnu even when exact matching
    // triple x86_64-gentoo-linux-gnu is present.
    GentooTestTriples.push_back(TargetTriple.str());
    GentooTestTriples.append(CandidateTripleAliases.begin(),
                             CandidateTripleAliases.end());
    if (ScanGentooConfigs(TargetTriple, Args, GentooTestTriples,
                          CandidateBiarchTripleAliases))
      return;
  }

  // Loop over the various components which exist and select the best GCC
  // installation available. GCC installs are ranked by version number.
  const GCCVersion VersionZero = GCCVersion::Parse("0.0.0");
  Version = VersionZero;
  for (const std::string &Prefix : Prefixes) {
    auto &VFS = D.getVFS();
    if (!VFS.exists(Prefix))
      continue;
    for (llvm::StringRef Suffix : CandidateLibDirs) {
      const std::string LibDir = concat(Prefix, Suffix);
      if (!VFS.exists(LibDir))
        continue;
      // Maybe filter out <libdir>/gcc and <libdir>/gcc-cross.
      bool GCCDirExists = VFS.exists(LibDir + "/gcc");
      bool GCCCrossDirExists = VFS.exists(LibDir + "/gcc-cross");
      for (llvm::StringRef Candidate : CandidateTripleAliases)
        ScanLibDirForGCCTriple(TargetTriple, Args, LibDir, Candidate, false,
                               GCCDirExists, GCCCrossDirExists);
    }
    for (llvm::StringRef Suffix : CandidateBiarchLibDirs) {
      const std::string LibDir = Prefix + Suffix.str();
      if (!VFS.exists(LibDir))
        continue;
      bool GCCDirExists = VFS.exists(LibDir + "/gcc");
      bool GCCCrossDirExists = VFS.exists(LibDir + "/gcc-cross");
      for (llvm::StringRef Candidate : CandidateBiarchTripleAliases)
        ScanLibDirForGCCTriple(TargetTriple, Args, LibDir, Candidate, true,
                               GCCDirExists, GCCCrossDirExists);
    }

    // Skip other prefixes once a GCC installation is found.
    if (Version > VersionZero)
      break;
  }
}

void Generic_GCC::GCCInstallationDetector::print(llvm::raw_ostream &OS) const {
  for (const auto &InstallPath : CandidateGCCInstallPaths)
    OS << "Found candidate GCC installation: " << InstallPath << "\n";

  if (!GCCInstallPath.empty())
    OS << "Selected GCC installation: " << GCCInstallPath << "\n";

  for (const auto &Multilib : Multilibs)
    OS << "Candidate multilib: " << Multilib << "\n";

  if (Multilibs.size() != 0 || !SelectedMultilib.isDefault())
    OS << "Selected multilib: " << SelectedMultilib << "\n";
}

bool Generic_GCC::GCCInstallationDetector::getBiarchSibling(Multilib &M) const {
  if (BiarchSibling) {
    M = *BiarchSibling;
    return true;
  }
  return false;
}

void Generic_GCC::GCCInstallationDetector::AddDefaultGCCPrefixes(
    const llvm::Triple &TargetTriple,
    llvm::SmallVectorImpl<std::string> &Prefixes, llvm::StringRef SysRoot) {

  // For Linux, if --sysroot is not specified, look for RHEL/CentOS devtoolsets
  // and gcc-toolsets.
  if (SysRoot.empty() && TargetTriple.getOS() == llvm::Triple::Linux &&
      D.getVFS().exists("/opt/rh")) {
    Prefixes.push_back("/opt/rh/gcc-toolset-12/root/usr");
    Prefixes.push_back("/opt/rh/gcc-toolset-11/root/usr");
    Prefixes.push_back("/opt/rh/gcc-toolset-10/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-12/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-11/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-10/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-9/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-8/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-7/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-6/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-4/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-3/root/usr");
    Prefixes.push_back("/opt/rh/devtoolset-2/root/usr");
  }

  Prefixes.push_back(concat(SysRoot, "/usr"));
}

/*static*/ void Generic_GCC::GCCInstallationDetector::CollectLibDirsAndTriples(
    const llvm::Triple &TargetTriple, const llvm::Triple &BiarchTriple,
    llvm::SmallVectorImpl<llvm::StringRef> &LibDirs,
    llvm::SmallVectorImpl<llvm::StringRef> &TripleAliases,
    llvm::SmallVectorImpl<llvm::StringRef> &BiarchLibDirs,
    llvm::SmallVectorImpl<llvm::StringRef> &BiarchTripleAliases) {
  // Declare a bunch of static data sets that we'll select between below. These
  // are specifically designed to always refer to string literals to avoid any
  // lifetime or initialization issues.
  //
  // The *Triples variables hard code some triples so that, for example,
  // --target=aarch64 (incomplete triple) can detect lib/aarch64-linux-gnu.
  // They are not needed when the user has correct LLVM_DEFAULT_TARGET_TRIPLE
  // and always uses the full --target (e.g. --target=aarch64-linux-gnu).  The
  // lists should shrink over time. Please don't add more elements to *Triples.
  static const char *const AArch64LibDirs[] = {"/lib64", "/lib"};
  static const char *const AArch64Triples[] = {
      "aarch64-none-linux-gnu", "aarch64-linux-gnu", "aarch64-redhat-linux",
      "aarch64-suse-linux"};
  static const char *const X86_64LibDirs[] = {"/lib64", "/lib"};
  static const char *const X86_64Triples[] = {
      "x86_64-linux-gnu",       "x86_64-unknown-linux-gnu",
      "x86_64-pc-linux-gnu",    "x86_64-redhat-linux6E",
      "x86_64-redhat-linux",    "x86_64-suse-linux",
      "x86_64-manbo-linux-gnu", "x86_64-linux-gnu",
      "x86_64-slackware-linux", "x86_64-unknown-linux",
      "x86_64-amazon-linux"};

  using std::begin;
  using std::end;

  switch (TargetTriple.getArch()) {
  case llvm::Triple::aarch64:
    LibDirs.append(begin(AArch64LibDirs), end(AArch64LibDirs));
    TripleAliases.append(begin(AArch64Triples), end(AArch64Triples));
    BiarchLibDirs.append(begin(AArch64LibDirs), end(AArch64LibDirs));
    BiarchTripleAliases.append(begin(AArch64Triples), end(AArch64Triples));
    break;
  case llvm::Triple::x86_64:
    LibDirs.append(begin(X86_64LibDirs), end(X86_64LibDirs));
    TripleAliases.append(begin(X86_64Triples), end(X86_64Triples));
    break;
  default:
    // By default, just rely on the standard lib directories and the original
    // triple.
    break;
  }

  // Also include the multiarch variant if it's different.
  if (TargetTriple.str() != BiarchTriple.str())
    BiarchTripleAliases.push_back(BiarchTriple.str());
}

bool Generic_GCC::GCCInstallationDetector::ScanGCCForMultilibs(
    const llvm::Triple &TargetTriple, llvm::StringRef Path) {
  DetectedMultilibs Detected;

  if (!findBiarchMultilibs(D, TargetTriple, Path, Detected)) {
    return false;
  }

  Multilibs = Detected.Multilibs;
  SelectedMultilib = Detected.SelectedMultilibs.empty()
                         ? Multilib()
                         : Detected.SelectedMultilibs.back();
  BiarchSibling = Detected.BiarchSibling;

  return true;
}

void Generic_GCC::GCCInstallationDetector::ScanLibDirForGCCTriple(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    const std::string &LibDir, llvm::StringRef CandidateTriple,
    bool NeedsBiarchSuffix, bool GCCDirExists, bool GCCCrossDirExists) {
  // Locations relative to the system lib directory where GCC's triple-specific
  // directories might reside.
  struct GCCLibSuffix {
    // Path from system lib directory to GCC triple-specific directory.
    std::string LibSuffix;
    // Path from GCC triple-specific directory back to system lib directory.
    // This is one '..' component per component in LibSuffix.
    llvm::StringRef ReversePath;
    // Whether this library suffix is relevant for the triple.
    bool Active;
  } Suffixes[] = {
      // This is the normal place.
      {"gcc/" + CandidateTriple.str(), "../..", GCCDirExists},

      // Debian puts cross-compilers in gcc-cross.
      {"gcc-cross/" + CandidateTriple.str(), "../..", GCCCrossDirExists}};

  for (auto &Suffix : Suffixes) {
    if (!Suffix.Active)
      continue;

    llvm::StringRef LibSuffix = Suffix.LibSuffix;
    std::error_code EC;
    for (llvm::vfs::directory_iterator
             LI = D.getVFS().dir_begin(LibDir + "/" + LibSuffix, EC),
             LE;
         !EC && LI != LE; LI = LI.increment(EC)) {
      llvm::StringRef VersionText = llvm::sys::path::filename(LI->path());
      GCCVersion CandidateVersion = GCCVersion::Parse(VersionText);
      if (CandidateVersion.Major != -1) // Filter obviously bad entries.
        if (!CandidateGCCInstallPaths.insert(std::string(LI->path())).second)
          continue; // Saw this path before; no need to look at it again.
      if (CandidateVersion.isOlderThan(4, 1, 1))
        continue;
      if (CandidateVersion <= Version)
        continue;

      if (!ScanGCCForMultilibs(TargetTriple, LI->path()))
        continue;

      Version = CandidateVersion;
      GCCTriple.setTriple(CandidateTriple);
      GCCInstallPath = (LibDir + "/" + LibSuffix + "/" + VersionText).str();
      GCCParentLibPath = (GCCInstallPath + "/../" + Suffix.ReversePath).str();
      IsValid = true;
    }
  }
}

bool Generic_GCC::GCCInstallationDetector::ScanGentooConfigs(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    const llvm::SmallVectorImpl<llvm::StringRef> &CandidateTriples,
    const llvm::SmallVectorImpl<llvm::StringRef> &CandidateBiarchTriples) {
  if (!D.getVFS().exists(concat(D.SysRoot, GentooConfigDir)))
    return false;

  for (llvm::StringRef CandidateTriple : CandidateTriples) {
    if (ScanGentooGccConfig(TargetTriple, Args, CandidateTriple))
      return true;
  }

  for (llvm::StringRef CandidateTriple : CandidateBiarchTriples) {
    if (ScanGentooGccConfig(TargetTriple, Args, CandidateTriple, true))
      return true;
  }
  return false;
}

bool Generic_GCC::GCCInstallationDetector::ScanGentooGccConfig(
    const llvm::Triple &TargetTriple, const ArgList &Args,
    llvm::StringRef CandidateTriple, bool NeedsBiarchSuffix) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      D.getVFS().getBufferForFile(concat(D.SysRoot, GentooConfigDir,
                                         "/config-" + CandidateTriple.str()));
  if (File) {
    llvm::SmallVector<llvm::StringRef, 2> Lines;
    File.get()->getBuffer().split(Lines, "\n");
    for (llvm::StringRef Line : Lines) {
      Line = Line.trim();
      // CURRENT=triple-version
      if (!Line.consume_front("CURRENT="))
        continue;
      llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> ConfigFile =
          D.getVFS().getBufferForFile(
              concat(D.SysRoot, GentooConfigDir, "/" + Line));
      std::pair<llvm::StringRef, llvm::StringRef> ActiveVersion =
          Line.rsplit('-');
      // List of paths to scan for libraries.
      llvm::SmallVector<llvm::StringRef, 4> GentooScanPaths;
      // Scan the Config file to find installed GCC libraries path.
      // Typical content of the GCC config file:
      // LDPATH="/usr/lib/gcc/x86_64-pc-linux-gnu/4.9.x:/usr/lib/gcc/
      // (continued from previous line) x86_64-pc-linux-gnu/4.9.x/32"
      // MANPATH="/usr/share/gcc-data/x86_64-pc-linux-gnu/4.9.x/man"
      // INFOPATH="/usr/share/gcc-data/x86_64-pc-linux-gnu/4.9.x/info"
      // STDCXX_INCDIR="/usr/lib/gcc/x86_64-pc-linux-gnu/4.9.x/include/g++-v4"
      // We are looking for the paths listed in LDPATH=... .
      if (ConfigFile) {
        llvm::SmallVector<llvm::StringRef, 2> ConfigLines;
        ConfigFile.get()->getBuffer().split(ConfigLines, "\n");
        for (llvm::StringRef ConfLine : ConfigLines) {
          ConfLine = ConfLine.trim();
          if (ConfLine.consume_front("LDPATH=")) {
            // Drop '"' from front and back if present.
            ConfLine.consume_back("\"");
            ConfLine.consume_front("\"");
            ConfLine.split(GentooScanPaths, ':', -1, /*AllowEmpty*/ false);
          }
        }
      }
      // Test the path based on the version in /etc/env.d/gcc/config-{tuple}.
      std::string basePath = "/usr/lib/gcc/" + ActiveVersion.first.str() + "/" +
                             ActiveVersion.second.str();
      GentooScanPaths.push_back(llvm::StringRef(basePath));

      // Scan all paths for GCC libraries.
      for (const auto &GentooScanPath : GentooScanPaths) {
        std::string GentooPath = concat(D.SysRoot, GentooScanPath);
        if (D.getVFS().exists(GentooPath + "/crtbegin.o")) {
          if (!ScanGCCForMultilibs(TargetTriple, GentooPath))
            continue;

          Version = GCCVersion::Parse(ActiveVersion.second);
          GCCInstallPath = GentooPath;
          GCCParentLibPath = GentooPath + std::string("/../../..");
          GCCTriple.setTriple(ActiveVersion.first);
          IsValid = true;
          return true;
        }
      }
    }
  }

  return false;
}

// ===----------------------------------------------------------------------===
// ToolChain construction & runtime
// ===----------------------------------------------------------------------===

Generic_GCC::Generic_GCC(const Driver &D, const llvm::Triple &Triple,
                         const ArgList &Args)
    : ToolChain(D, Triple, Args), GCCInstallation(D) {
  getProgramPaths().push_back(getDriver().getInstalledDir());
  if (getDriver().getInstalledDir() != getDriver().Dir)
    getProgramPaths().push_back(getDriver().Dir);
}

Generic_GCC::~Generic_GCC() {}

Tool *Generic_GCC::buildLinker() const {
  return new tools::gnutools::Linker(*this);
}

void Generic_GCC::printVerboseInfo(llvm::raw_ostream &OS) const {
  // Print the information about how we detected the GCC installation.
  GCCInstallation.print(OS);
}

ToolChain::UnwindTableLevel
Generic_GCC::getDefaultUnwindTableLevel(const ArgList &Args) const {
  switch (getArch()) {
  case llvm::Triple::aarch64:
  case llvm::Triple::x86_64:
    return UnwindTableLevel::Asynchronous;
  default:
    return UnwindTableLevel::None;
  }
}

bool Generic_GCC::IsIntegratedAssemblerDefault() const { return true; }

void Generic_GCC::PushPPaths(ToolChain::path_list &PPaths) {
  // Cross-compiling binutils and GCC installations (vanilla and openSUSE at
  // least) put various tools in a triple-prefixed directory off of the parent
  // of the GCC installation. We use the GCC triple here to ensure that we end
  // up with tools that support the same amount of cross compiling as the
  // detected GCC installation.
  if (GCCInstallation.isValid()) {
    PPaths.push_back(llvm::Twine(GCCInstallation.getParentLibPath() + "/../" +
                                 GCCInstallation.getTriple().str() + "/bin")
                         .str());
  }
}

void Generic_GCC::AddMultilibPaths(const Driver &D, const std::string &SysRoot,
                                   const std::string &OSLibDir,
                                   const std::string &MultiarchTriple,
                                   path_list &Paths) {
  // Add the multilib suffixed paths where they are available.
  if (GCCInstallation.isValid()) {
    assert(!SelectedMultilibs.empty());
    const llvm::Triple &GCCTriple = GCCInstallation.getTriple();
    const std::string &LibPath =
        std::string(GCCInstallation.getParentLibPath());

    if (const auto &PathsCallback = Multilibs.filePathsCallback())
      for (const auto &Path : PathsCallback(SelectedMultilibs.back()))
        addPathIfExists(D, GCCInstallation.getInstallPath() + Path, Paths);

    // Add lib/gcc/$triple/$version, with an optional /multilib suffix.
    addPathIfExists(D,
                    GCCInstallation.getInstallPath() +
                        SelectedMultilibs.back().gccSuffix(),
                    Paths);

    // Add lib/gcc/$triple/$libdir
    // For GCC built with --enable-version-specific-runtime-libs.
    addPathIfExists(D, GCCInstallation.getInstallPath() + "/../" + OSLibDir,
                    Paths);

    // GCC cross compiling toolchains will install target libraries which ship
    // as part of the toolchain under <prefix>/<triple>/<libdir> rather than as
    // any part of the GCC installation in
    // <prefix>/<libdir>/gcc/<triple>/<version>. This decision is somewhat
    // debatable, but is the reality today. We need to search this tree even
    // when we have a sysroot somewhere else. It is the responsibility of
    // whomever is doing the cross build targeting a sysroot using a GCC
    // installation that is *not* within the system root to ensure two things:
    //
    //  1) Any DSOs that are linked in from this tree or from the install path
    //     above must be present on the system root and found via an
    //     appropriate rpath.
    //  2) There must not be libraries installed into
    //     <prefix>/<triple>/<libdir> unless they should be preferred over
    //     those within the system root.
    //
    // Note that this matches the GCC behavior. See the below comment for where
    // NeverC diverges from GCC's behavior.
    addPathIfExists(D,
                    LibPath + "/../" + GCCTriple.str() + "/lib/../" + OSLibDir +
                        SelectedMultilibs.back().osSuffix(),
                    Paths);

    // If the GCC installation we found is inside of the sysroot, we want to
    // prefer libraries installed in the parent prefix of the GCC installation.
    // It is important to *not* use these paths when the GCC installation is
    // outside of the system root as that can pick up unintended libraries.
    // This usually happens when there is an external cross compiler on the
    // host system, and a more minimal sysroot available that is the target of
    // the cross. Note that GCC does include some of these directories in some
    // configurations but this seems somewhere between questionable and simply
    // a bug.
    if (llvm::StringRef(LibPath).starts_with(SysRoot))
      addPathIfExists(D, LibPath + "/../" + OSLibDir, Paths);
  }
}

void Generic_GCC::AddMultiarchPaths(const Driver &D, const std::string &SysRoot,
                                    const std::string &OSLibDir,
                                    path_list &Paths) {
  if (GCCInstallation.isValid()) {
    const std::string &LibPath =
        std::string(GCCInstallation.getParentLibPath());
    const llvm::Triple &GCCTriple = GCCInstallation.getTriple();
    const Multilib &Multilib = GCCInstallation.getMultilib();
    addPathIfExists(
        D, LibPath + "/../" + GCCTriple.str() + "/lib" + Multilib.osSuffix(),
        Paths);
  }
}

void Generic_GCC::AddMultilibIncludeArgs(const ArgList &DriverArgs,
                                         ArgStringList &FrontendArgs) const {
  // Add include directories specific to the selected multilib set and multilib.
  if (!GCCInstallation.isValid())
    return;
  // gcc TOOL_INCLUDE_DIR.
  const llvm::Triple &GCCTriple = GCCInstallation.getTriple();
  std::string LibPath(GCCInstallation.getParentLibPath());
  addSystemInclude(DriverArgs, FrontendArgs,
                   llvm::Twine(LibPath) + "/../" + GCCTriple.str() +
                       "/include");

  const auto &Callback = Multilibs.includeDirsCallback();
  if (Callback) {
    for (const auto &Path : Callback(GCCInstallation.getMultilib()))
      addExternCSystemIncludeIfExists(DriverArgs, FrontendArgs,
                                      GCCInstallation.getInstallPath() + Path);
  }
}

llvm::opt::DerivedArgList *
Generic_GCC::TranslateArgs(const llvm::opt::DerivedArgList &Args,
                           llvm::StringRef) const {
  return nullptr;
}

void Generic_ELF::anchor() {}

void Generic_ELF::addNeverCTargetOptions(const ArgList &DriverArgs,
                                         ArgStringList &FrontendArgs) const {
  if (!DriverArgs.hasFlag(options::OPT_fuse_init_array,
                          options::OPT_fno_use_init_array, true))
    FrontendArgs.push_back("-fno-use-init-array");
}
