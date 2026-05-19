#include "MSVC.h"
#include "CommonArgs.h"
#include "neverc/Config/config.h"
#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/Options.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace neverc::driver;
using namespace neverc::driver::toolchains;
using namespace neverc::driver::tools;
using namespace neverc;
using namespace llvm::opt;

// ===----------------------------------------------------------------------===
// Runtime library selection
// ===----------------------------------------------------------------------===

namespace {
const char *getMSVCRuntimeDefaultLib(const ArgList &Args) {
  unsigned RTOptionID = options::OPT_msvc_runtime_MT;

  if (Args.hasArg(options::OPT_create_dll_debug))
    RTOptionID = options::OPT_msvc_runtime_MTd;

  if (Arg *A = Args.getLastArg(options::OPT_msvc_runtime_Group))
    RTOptionID = A->getOption().getID();

  if (Arg *A = Args.getLastArg(options::OPT_fms_runtime_lib_EQ)) {
    llvm::StringRef Value = A->getValue();
    if (Value == "static")
      RTOptionID = options::OPT_msvc_runtime_MT;
    else if (Value == "static_dbg")
      RTOptionID = options::OPT_msvc_runtime_MTd;
    else if (Value == "dll")
      RTOptionID = options::OPT_msvc_runtime_MD;
    else if (Value == "dll_dbg")
      RTOptionID = options::OPT_msvc_runtime_MDd;
  }

  switch (RTOptionID) {
  case options::OPT_msvc_runtime_MD:
    return "msvcrt";
  case options::OPT_msvc_runtime_MDd:
    return "msvcrtd";
  case options::OPT_msvc_runtime_MT:
    return "libcmt";
  case options::OPT_msvc_runtime_MTd:
    return "libcmtd";
  default:
    llvm_unreachable("Unexpected option ID.");
  }
}
} // namespace

// ===----------------------------------------------------------------------===
// Linker job construction
// ===----------------------------------------------------------------------===

void visualstudio::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                        const InputInfo &Output,
                                        const InputInfoList &Inputs,
                                        const ArgList &Args,
                                        const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  auto &TC = static_cast<const toolchains::MSVCToolChain &>(getToolChain());

  assert((Output.isFilename() || Output.isNothing()) && "invalid output");
  // Output file is passed via LinkerDriverConfig.outputFile.

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles)) {
    if (!Args.hasArg(options::OPT_nodefaultlibs) &&
        !Args.hasArg(options::OPT_fms_omit_default_lib)) {
      const char *CRTLib = getMSVCRuntimeDefaultLib(Args);
      CmdArgs.push_back(
          Args.MakeArgString(llvm::Twine("--defaultlib=") + CRTLib));

      llvm::StringRef CRT(CRTLib);
      bool IsDLL = CRT == "msvcrt" || CRT == "msvcrtd";
      bool IsDebug = CRT == "libcmtd" || CRT == "msvcrtd";
      CmdArgs.push_back(IsDLL ? (IsDebug ? "--defaultlib=vcruntimed"
                                         : "--defaultlib=vcruntime")
                               : (IsDebug ? "--defaultlib=libvcruntimed"
                                          : "--defaultlib=libvcruntime"));
      CmdArgs.push_back(IsDLL ? (IsDebug ? "--defaultlib=ucrtd"
                                         : "--defaultlib=ucrt")
                               : (IsDebug ? "--defaultlib=libucrtd"
                                          : "--defaultlib=libucrt"));

      CmdArgs.push_back("--defaultlib=oldnames");
    }
  }

  // Layout: <InstalledDir>/../sdk/msvc/{crt/lib/<arch>,
  // sdk/lib/{um,ucrt}/<arch>}
  {
    llvm::SmallString<128> MsvcRoot;
    if (getBundledMsvcSdkRoot(TC.getDriver(), MsvcRoot)) {
      llvm::StringRef Arch = getBundledMsvcArchName(TC.getTriple());
      llvm::SmallString<128> P;

      P = MsvcRoot;
      llvm::sys::path::append(P, "crt", "lib", Arch);
      if (llvm::sys::fs::is_directory(P))
        CmdArgs.push_back(Args.MakeArgString(llvm::Twine("--libpath=") + P));

      P = MsvcRoot;
      llvm::sys::path::append(P, "sdk", "lib", "um", Arch);
      if (llvm::sys::fs::is_directory(P))
        CmdArgs.push_back(Args.MakeArgString(llvm::Twine("--libpath=") + P));

      P = MsvcRoot;
      llvm::sys::path::append(P, "sdk", "lib", "ucrt", Arch);
      if (llvm::sys::fs::is_directory(P))
        CmdArgs.push_back(Args.MakeArgString(llvm::Twine("--libpath=") + P));
    }
  }

  // If the VC environment hasn't been configured (perhaps because the user
  // did not run vcvarsall), try to build a consistent link environment.  If
  // the environment variable is set however, assume the user knows what
  // they're doing. If the user passes /vctoolsdir or /winsdkdir, trust that
  // over env vars.
  if (const Arg *A =
          Args.getLastArg(options::OPT_diasdkdir, options::OPT_winsysroot)) {
    // cl.exe doesn't find the DIA SDK automatically, so this too requires
    // explicit flags and doesn't automatically look in "DIA SDK" relative
    // to the path we found for VCToolChainPath.
    llvm::SmallString<128> DIAPath(A->getValue());
    if (A->getOption().getID() == options::OPT_winsysroot)
      llvm::sys::path::append(DIAPath, "DIA SDK");

    // The DIA SDK always uses the legacy vc arch, even in new MSVC versions.
    llvm::sys::path::append(DIAPath, "lib",
                            llvm::archToLegacyVCArch(TC.getArch()));
    CmdArgs.push_back(Args.MakeArgString(llvm::Twine("--libpath=") + DIAPath));
  }
  if (!llvm::sys::Process::GetEnv("LIB") ||
      Args.getLastArg(options::OPT_vctoolsdir, options::OPT_winsysroot)) {
    CmdArgs.push_back(Args.MakeArgString(
        llvm::Twine("--libpath=") +
        TC.getSubDirectoryPath(llvm::SubDirectoryType::Lib)));
    CmdArgs.push_back(Args.MakeArgString(
        llvm::Twine("--libpath=") +
        TC.getSubDirectoryPath(llvm::SubDirectoryType::Lib, "atlmfc")));
  }
  if (!llvm::sys::Process::GetEnv("LIB") ||
      Args.getLastArg(options::OPT_winsdkdir, options::OPT_winsysroot)) {
    if (TC.useUniversalCRT()) {
      std::string UniversalCRTLibPath;
      if (TC.getUniversalCRTLibraryPath(Args, UniversalCRTLibPath))
        CmdArgs.push_back(Args.MakeArgString(llvm::Twine("--libpath=") +
                                             UniversalCRTLibPath));
    }
    std::string WindowsSdkLibPath;
    if (TC.getWindowsSDKLibraryPath(Args, WindowsSdkLibPath))
      CmdArgs.push_back(
          Args.MakeArgString(std::string("--libpath=") + WindowsSdkLibPath));
  }

  // Add the compiler-rt library directories to libpath if they exist to help
  // the linker find the various sanitizer, builtin, and profiling runtimes.
  for (const auto &LibPath : TC.getLibraryPaths()) {
    if (TC.getVFS().exists(LibPath))
      CmdArgs.push_back(Args.MakeArgString("--libpath=" + LibPath));
  }
  auto CRTPath = TC.getCompilerRTPath();
  if (TC.getVFS().exists(CRTPath))
    CmdArgs.push_back(Args.MakeArgString("--libpath=" + CRTPath));

  if (Args.hasArg(options::OPT_L))
    for (const auto &LibPath : Args.getAllArgValues(options::OPT_L))
      CmdArgs.push_back(Args.MakeArgString("--libpath=" + LibPath));

  // /debug, /functionpadmin, /Brepro are now set via LinkerDriverConfig.
  bool WantDebug = Args.hasArg(options::OPT_g_Group, options::OPT_g_Flag);
  bool WantFunctionPadMin = Args.hasArg(options::OPT_fms_hotpatch);
  bool DefaultIncrementalLinkerCompatible =
      C.getDefaultToolChain().getTriple().isWindowsMSVCEnvironment();
  bool WantRepro = !Args.hasFlag(options::OPT_mincremental_linker_compatible,
                                 options::OPT_mno_incremental_linker_compatible,
                                 DefaultIncrementalLinkerCompatible);

  // DLL output is controlled via LinkerDriverConfig.shared (/dll removed
  // from COFF Options.td).  Still generate the implicit -implib:.
  bool DLL = Args.hasArg(options::OPT_create_dll, options::OPT_create_dll_debug,
                         options::OPT_shared);
  if (DLL) {
    llvm::SmallString<128> ImplibName(Output.getFilename());
    llvm::sys::path::replace_extension(ImplibName, "lib");
    CmdArgs.push_back(
        Args.MakeArgString(std::string("--implib=") + ImplibName));
  }

  Args.AddAllArgValues(CmdArgs, options::OPT_Xmslink);

  // Control Flow Guard — build a guard spec string for LinkerDriverConfig.
  std::string GuardSpecStr;
  for (const Arg *A : Args.filtered(options::OPT_fms_guard)) {
    llvm::StringRef GuardArgs = A->getValue();
    if (GuardArgs.equals_insensitive("cf") ||
        GuardArgs.equals_insensitive("cf,nochecks")) {
      if (!GuardSpecStr.empty())
        GuardSpecStr += ',';
      GuardSpecStr += "cf";
    } else if (GuardArgs.equals_insensitive("cf-")) {
      if (!GuardSpecStr.empty())
        GuardSpecStr += ',';
      GuardSpecStr += "no";
    } else if (GuardArgs.equals_insensitive("ehcont")) {
      if (!GuardSpecStr.empty())
        GuardSpecStr += ',';
      GuardSpecStr += "ehcont";
    } else if (GuardArgs.equals_insensitive("ehcont-")) {
      if (!GuardSpecStr.empty())
        GuardSpecStr += ',';
      GuardSpecStr += "noehcont";
    }
  }

  // Add compiler-rt lib in case if it was explicitly
  // specified as an argument for --rtlib option.
  if (!Args.hasArg(options::OPT_nostdlib)) {
    AddRunTimeLibs(TC, TC.getDriver(), CmdArgs, Args);
  }

  for (Arg *A : Args.filtered(options::OPT_vfsoverlay))
    CmdArgs.push_back(
        Args.MakeArgString(std::string("--vfsoverlay=") + A->getValue()));

  // Add filenames, libraries, and other linker inputs.
  for (const auto &Input : Inputs) {
    if (Input.isFilename()) {
      CmdArgs.push_back(Input.getFilename());
      continue;
    }

    const Arg &A = Input.getInputArg();

    // Render -l options differently for the MSVC linker.
    if (A.getOption().matches(options::OPT_l)) {
      llvm::StringRef Lib = A.getValue();
      const char *LinkLibArg;
      if (Lib.ends_with(".lib"))
        LinkLibArg = Args.MakeArgString(Lib);
      else
        LinkLibArg = Args.MakeArgString(Lib + ".lib");
      CmdArgs.push_back(LinkLibArg);
      continue;
    }

    // Otherwise, this is some other kind of linker input option like -Wl, -z,
    // or -L. Render it, even if MSVC doesn't understand it.
    A.renderAsInput(Args, CmdArgs);
  }

  const char *Exec = Args.MakeArgString(TC.GetLinkerPath());

  auto LCmd = std::make_unique<LinkerCommand>(
      JA, *this, ResponseFileSupport::AtFileUTF16(), Exec, CmdArgs, Inputs,
      LinkerFlavor::WinLink, Output);
  populateLinkerDriverConfig(TC, Args, LCmd->getDriverConfig());
  auto &CoffCfg = LCmd->getDriverConfig();
  if (Output.isFilename())
    CoffCfg.outputFile = Output.getFilename();
  CoffCfg.shared = Args.hasArg(options::OPT_shared);
  CoffCfg.debugInfo = WantDebug;
  CoffCfg.repro = WantRepro;
  CoffCfg.functionPadMin = WantFunctionPadMin;
  if (!GuardSpecStr.empty())
    CoffCfg.guardSpec = std::move(GuardSpecStr);

  C.addCommand(std::move(LCmd));
}

MSVCToolChain::MSVCToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  getProgramPaths().push_back(getDriver().getInstalledDir());
  if (getDriver().getInstalledDir() != getDriver().Dir)
    getProgramPaths().push_back(getDriver().Dir);

  std::optional<llvm::StringRef> VCToolsDir, VCToolsVersion;
  if (Arg *A = Args.getLastArg(options::OPT_vctoolsdir))
    VCToolsDir = A->getValue();
  if (Arg *A = Args.getLastArg(options::OPT_vctoolsversion))
    VCToolsVersion = A->getValue();
  if (Arg *A = Args.getLastArg(options::OPT_winsdkdir))
    WinSdkDir = A->getValue();
  if (Arg *A = Args.getLastArg(options::OPT_winsdkversion))
    WinSdkVersion = A->getValue();
  if (Arg *A = Args.getLastArg(options::OPT_winsysroot))
    WinSysRoot = A->getValue();

  // Check the command line first, that's the user explicitly telling us what to
  // use. Check the environment next, in case we're being invoked from a VS
  // command prompt. Failing that, just try to find the newest Visual Studio
  // version we can and use its default VC toolchain.
  llvm::findVCToolChainViaCommandLine(getVFS(), VCToolsDir, VCToolsVersion,
                                      WinSysRoot, VCToolChainPath, VSLayout) ||
      llvm::findVCToolChainViaEnvironment(getVFS(), VCToolChainPath,
                                          VSLayout) ||
      llvm::findVCToolChainViaSetupConfig(getVFS(), VCToolsVersion,
                                          VCToolChainPath, VSLayout) ||
      llvm::findVCToolChainViaRegistry(VCToolChainPath, VSLayout);

  // Optional override: `NEVERC_WIN_SYSROOT` env var points at a real
  // Visual-Studio-style sysroot (contains `VC/Tools/MSVC/<version>/` and
  // the Windows Kits siblings).  Only consulted when nothing above already
  // resolved a toolchain path, and only activates when the target directory
  // actually contains `VC/Tools/MSVC/`, so a bogus env value cannot poison
  // `VCToolChainPath` with a non-existent subpath.  The NeverC bundled
  // sysroot at `<InstalledDir>/../sdk/msvc` does NOT match this layout —
  // it is consumed by `getBundledMsvcSdkRoot` directly inside the
  // include/lib resolution helpers, so no WinSysRoot synthesis is needed
  // for the bundled flow.
  if (VCToolChainPath.empty()) {
    if (auto Env = llvm::sys::Process::GetEnv("NEVERC_WIN_SYSROOT")) {
      llvm::SmallString<256> Probe(*Env);
      llvm::sys::path::append(Probe, "VC", "Tools", "MSVC");
      if (getVFS().exists(Probe)) {
        NeverCWinSysRootStorage.assign(Env->begin(), Env->end());
        WinSysRoot = NeverCWinSysRootStorage;
        llvm::findVCToolChainViaCommandLine(getVFS(), VCToolsDir,
                                            VCToolsVersion, *WinSysRoot,
                                            VCToolChainPath, VSLayout);
      }
    }
  }
}

Tool *MSVCToolChain::buildLinker() const {
  return new tools::visualstudio::Linker(*this);
}

ToolChain::UnwindTableLevel
MSVCToolChain::getDefaultUnwindTableLevel(const ArgList &Args) const {
  // Don't emit unwind tables by default for MachO targets.
  if (getTriple().isOSBinFormatMachO())
    return UnwindTableLevel::None;

  if (getArch() == llvm::Triple::x86_64)
    return UnwindTableLevel::Asynchronous;

  return UnwindTableLevel::None;
}

// ===----------------------------------------------------------------------===
// ToolChain defaults & SDK paths
// ===----------------------------------------------------------------------===

void MSVCToolChain::printVerboseInfo(llvm::raw_ostream &) const {}

std::string
MSVCToolChain::getSubDirectoryPath(llvm::SubDirectoryType Type,
                                   llvm::StringRef SubdirParent) const {
  return llvm::getSubDirectoryPath(Type, VSLayout, VCToolChainPath, getArch(),
                                   SubdirParent);
}

std::string
MSVCToolChain::getSubDirectoryPath(llvm::SubDirectoryType Type,
                                   llvm::Triple::ArchType TargetArch) const {
  return llvm::getSubDirectoryPath(Type, VSLayout, VCToolChainPath, TargetArch,
                                   "");
}

bool MSVCToolChain::getWindowsSDKLibraryPath(const ArgList &Args,
                                             std::string &path) const {
  std::string sdkPath;
  int sdkMajor = 0;
  std::string windowsSDKIncludeVersion;
  std::string windowsSDKLibVersion;

  path.clear();
  if (!llvm::getWindowsSDKDir(getVFS(), WinSdkDir, WinSdkVersion, WinSysRoot,
                              sdkPath, sdkMajor, windowsSDKIncludeVersion,
                              windowsSDKLibVersion))
    return false;

  llvm::SmallString<128> libPath(sdkPath);
  llvm::sys::path::append(libPath, "Lib");
  if (sdkMajor >= 10)
    if (!(WinSdkDir.has_value() || WinSysRoot.has_value()) &&
        WinSdkVersion.has_value())
      windowsSDKLibVersion = *WinSdkVersion;
  if (sdkMajor >= 8)
    llvm::sys::path::append(libPath, windowsSDKLibVersion, "um");
  return llvm::appendArchToWindowsSDKLibPath(sdkMajor, libPath, getArch(),
                                             path);
}

bool MSVCToolChain::useUniversalCRT() const {
  return llvm::useUniversalCRT(VSLayout, VCToolChainPath, getArch(), getVFS());
}

bool MSVCToolChain::getUniversalCRTLibraryPath(const ArgList &Args,
                                               std::string &Path) const {
  std::string UniversalCRTSdkPath;
  std::string UCRTVersion;

  Path.clear();
  if (!llvm::getUniversalCRTSdkDir(getVFS(), WinSdkDir, WinSdkVersion,
                                   WinSysRoot, UniversalCRTSdkPath,
                                   UCRTVersion))
    return false;

  if (!(WinSdkDir.has_value() || WinSysRoot.has_value()) &&
      WinSdkVersion.has_value())
    UCRTVersion = *WinSdkVersion;

  llvm::StringRef ArchName = llvm::archToWindowsSDKArch(getArch());
  if (ArchName.empty())
    return false;

  llvm::SmallString<128> LibPath(UniversalCRTSdkPath);
  llvm::sys::path::append(LibPath, "Lib", UCRTVersion, "ucrt", ArchName);

  Path = std::string(LibPath.str());
  return true;
}

namespace {
llvm::VersionTuple getMSVCVersionFromExe(const std::string &BinDir) {
  llvm::VersionTuple Version;
#ifdef _WIN32
  llvm::SmallString<128> ClExe(BinDir);
  llvm::sys::path::append(ClExe, "cl.exe");

  llvm::SmallVector<wchar_t, 256> ClExeWide;
  if (!llvm::ConvertUTF8toWide(ClExe.c_str(), ClExeWide))
    return Version;
  ClExeWide.push_back(L'\0');

  const DWORD VersionSize =
      ::GetFileVersionInfoSizeW(ClExeWide.data(), nullptr);
  if (VersionSize == 0)
    return Version;

  llvm::SmallVector<uint8_t, 4 * 1024> VersionBlock(VersionSize);
  if (!::GetFileVersionInfoW(ClExeWide.data(), 0, VersionSize,
                             VersionBlock.data()))
    return Version;

  VS_FIXEDFILEINFO *FileInfo = nullptr;
  UINT FileInfoSize = 0;
  if (!::VerQueryValueW(VersionBlock.data(), L"\\",
                        reinterpret_cast<LPVOID *>(&FileInfo), &FileInfoSize) ||
      FileInfoSize < sizeof(*FileInfo))
    return Version;

  const unsigned Major = (FileInfo->dwFileVersionMS >> 16) & 0xFFFF;
  const unsigned Minor = (FileInfo->dwFileVersionMS) & 0xFFFF;
  const unsigned Micro = (FileInfo->dwFileVersionLS >> 16) & 0xFFFF;

  Version = llvm::VersionTuple(Major, Minor, Micro);
#endif
  return Version;
}
} // namespace

void MSVCToolChain::AddSystemIncludeWithSubfolder(
    const ArgList &DriverArgs, ArgStringList &FrontendArgs,
    const std::string &folder, const llvm::Twine &subfolder1,
    const llvm::Twine &subfolder2, const llvm::Twine &subfolder3) const {
  llvm::SmallString<128> path(folder);
  llvm::sys::path::append(path, subfolder1, subfolder2, subfolder3);
  addSystemInclude(DriverArgs, FrontendArgs, path);
}

// ===----------------------------------------------------------------------===
// System includes & toolchain construction
// ===----------------------------------------------------------------------===

void MSVCToolChain::AddNeverCSystemIncludeArgs(
    const ArgList &DriverArgs, ArgStringList &FrontendArgs) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    AddSystemIncludeWithSubfolder(DriverArgs, FrontendArgs,
                                  getDriver().ResourceDir, "include");
  }

  // Add %INCLUDE%-like directories from the -imsvc flag.
  for (const auto &Path : DriverArgs.getAllArgValues(options::OPT_imsvc))
    addSystemInclude(DriverArgs, FrontendArgs, Path);

  auto AddSystemIncludesFromEnv = [&](llvm::StringRef Var) -> bool {
    if (auto Val = llvm::sys::Process::GetEnv(Var)) {
      llvm::SmallVector<llvm::StringRef, 8> Dirs;
      llvm::StringRef(*Val).split(Dirs, ";", /*MaxSplit=*/-1,
                                  /*KeepEmpty=*/false);
      if (!Dirs.empty()) {
        addSystemIncludes(DriverArgs, FrontendArgs, Dirs);
        return true;
      }
    }
    return false;
  };

  // Add %INCLUDE%-like dirs via /external:env: flags.
  for (const auto &Var :
       DriverArgs.getAllArgValues(options::OPT_external_env)) {
    AddSystemIncludesFromEnv(Var);
  }

  // Add DIA SDK include if requested.
  if (const Arg *A = DriverArgs.getLastArg(options::OPT_diasdkdir,
                                           options::OPT_winsysroot)) {
    // cl.exe doesn't find the DIA SDK automatically, so this too requires
    // explicit flags and doesn't automatically look in "DIA SDK" relative
    // to the path we found for VCToolChainPath.
    llvm::SmallString<128> DIASDKPath(A->getValue());
    if (A->getOption().getID() == options::OPT_winsysroot)
      llvm::sys::path::append(DIASDKPath, "DIA SDK");
    AddSystemIncludeWithSubfolder(DriverArgs, FrontendArgs,
                                  std::string(DIASDKPath), "include");
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  // Layout: <InstalledDir>/../sdk/msvc/{crt/include,
  // sdk/include/{ucrt,um,shared}}
  {
    llvm::SmallString<128> MsvcRoot;
    if (getBundledMsvcSdkRoot(getDriver(), MsvcRoot)) {
      llvm::SmallString<128> P;
      P = MsvcRoot;
      llvm::sys::path::append(P, "crt", "include");
      if (getVFS().exists(P))
        addSystemInclude(DriverArgs, FrontendArgs, P);

      P = MsvcRoot;
      llvm::sys::path::append(P, "sdk", "include", "ucrt");
      if (getVFS().exists(P))
        addSystemInclude(DriverArgs, FrontendArgs, P);

      P = MsvcRoot;
      llvm::sys::path::append(P, "sdk", "include", "um");
      if (getVFS().exists(P))
        addSystemInclude(DriverArgs, FrontendArgs, P);

      P = MsvcRoot;
      llvm::sys::path::append(P, "sdk", "include", "shared");
      if (getVFS().exists(P))
        addSystemInclude(DriverArgs, FrontendArgs, P);
    }
  }

  // Honor %INCLUDE% and %EXTERNAL_INCLUDE%. It should have essential search
  // paths set by vcvarsall.bat. Skip if the user expressly set a vctoolsdir.
  if (!DriverArgs.getLastArg(options::OPT_vctoolsdir,
                             options::OPT_winsysroot)) {
    bool Found = AddSystemIncludesFromEnv("INCLUDE");
    Found |= AddSystemIncludesFromEnv("EXTERNAL_INCLUDE");
    if (Found)
      return;
  }

  // When built with access to the proper Windows APIs, try to actually find
  // the correct include paths first.
  if (!VCToolChainPath.empty()) {
    addSystemInclude(DriverArgs, FrontendArgs,
                     getSubDirectoryPath(llvm::SubDirectoryType::Include));
    addSystemInclude(
        DriverArgs, FrontendArgs,
        getSubDirectoryPath(llvm::SubDirectoryType::Include, "atlmfc"));

    if (useUniversalCRT()) {
      std::string UniversalCRTSdkPath;
      std::string UCRTVersion;
      if (llvm::getUniversalCRTSdkDir(getVFS(), WinSdkDir, WinSdkVersion,
                                      WinSysRoot, UniversalCRTSdkPath,
                                      UCRTVersion)) {
        if (!(WinSdkDir.has_value() || WinSysRoot.has_value()) &&
            WinSdkVersion.has_value())
          UCRTVersion = *WinSdkVersion;
        AddSystemIncludeWithSubfolder(DriverArgs, FrontendArgs,
                                      UniversalCRTSdkPath, "Include",
                                      UCRTVersion, "ucrt");
      }
    }

    std::string WindowsSDKDir;
    int major = 0;
    std::string windowsSDKIncludeVersion;
    std::string windowsSDKLibVersion;
    if (llvm::getWindowsSDKDir(getVFS(), WinSdkDir, WinSdkVersion, WinSysRoot,
                               WindowsSDKDir, major, windowsSDKIncludeVersion,
                               windowsSDKLibVersion)) {
      if (major >= 10)
        if (!(WinSdkDir.has_value() || WinSysRoot.has_value()) &&
            WinSdkVersion.has_value())
          windowsSDKIncludeVersion = windowsSDKLibVersion = *WinSdkVersion;
      if (major >= 8) {
        // Note: windowsSDKIncludeVersion is empty for SDKs prior to v10.
        // Anyway, llvm::sys::path::append is able to manage it.
        AddSystemIncludeWithSubfolder(DriverArgs, FrontendArgs, WindowsSDKDir,
                                      "Include", windowsSDKIncludeVersion,
                                      "shared");
        AddSystemIncludeWithSubfolder(DriverArgs, FrontendArgs, WindowsSDKDir,
                                      "Include", windowsSDKIncludeVersion,
                                      "um");
      } else {
        AddSystemIncludeWithSubfolder(DriverArgs, FrontendArgs, WindowsSDKDir,
                                      "Include");
      }
    }

    return;
  }

#if defined(_WIN32)
  // As a fallback, select default install paths.
  const llvm::StringRef Paths[] = {
      "C:/Program Files/Microsoft Visual Studio 10.0/VC/include",
      "C:/Program Files/Microsoft Visual Studio 9.0/VC/include",
      "C:/Program Files/Microsoft Visual Studio 9.0/VC/PlatformSDK/Include",
      "C:/Program Files/Microsoft Visual Studio 8/VC/include",
      "C:/Program Files/Microsoft Visual Studio 8/VC/PlatformSDK/Include"};
  addSystemIncludes(DriverArgs, FrontendArgs, Paths);
#endif
}

llvm::VersionTuple
MSVCToolChain::computeMSVCVersion(const Driver *D, const ArgList &Args) const {
  bool IsWindowsMSVC = getTriple().isWindowsMSVCEnvironment();
  llvm::VersionTuple MSVT = ToolChain::computeMSVCVersion(D, Args);
  if (MSVT.empty())
    MSVT = getTriple().getEnvironmentVersion();
  if (MSVT.empty() && IsWindowsMSVC)
    MSVT =
        getMSVCVersionFromExe(getSubDirectoryPath(llvm::SubDirectoryType::Bin));
  if (MSVT.empty() &&
      Args.hasFlag(options::OPT_fms_extensions, options::OPT_fno_ms_extensions,
                   IsWindowsMSVC)) {
    // -fms-compatibility-version=19.33 is default, aka 2022, 17.3
    // NOTE: when changing this value, also update
    // docs/CommandGuide and docs/UsersManual
    // accordingly.
    MSVT = llvm::VersionTuple(19, 33);
  }
  return MSVT;
}

std::string MSVCToolChain::ComputeEffectiveTriple(const ArgList &Args,
                                                  types::ID InputType) const {
  // The MSVC version doesn't care about the architecture, even though it
  // may look at the triple internally.
  llvm::VersionTuple MSVT = computeMSVCVersion(/*D=*/nullptr, Args);
  MSVT =
      llvm::VersionTuple(MSVT.getMajor(), MSVT.getMinor(), MSVT.getSubminor());

  // For the rest of the triple, however, a computed architecture name may
  // be needed.
  llvm::Triple Triple(ToolChain::ComputeEffectiveTriple(Args, InputType));
  if (Triple.getEnvironment() == llvm::Triple::MSVC) {
    llvm::StringRef ObjFmt = Triple.getEnvironmentName().split('-').second;
    if (ObjFmt.empty())
      Triple.setEnvironmentName(
          (llvm::Twine("msvc") + MSVT.getAsString()).str());
    else
      Triple.setEnvironmentName(
          (llvm::Twine("msvc") + MSVT.getAsString() + llvm::Twine('-') + ObjFmt)
              .str());
  }
  return Triple.getTriple();
}

// ===----------------------------------------------------------------------===
// MSVC option translation
// ===----------------------------------------------------------------------===

namespace {
void translateOptArg(Arg *A, llvm::opt::DerivedArgList &DAL,
                     bool SupportsForcingFramePointer, const char *ExpandChar,
                     const OptTable &Opts) {
  assert(A->getOption().matches(options::OPT_msvc_optimize));

  llvm::StringRef OptStr = A->getValue();
  for (size_t I = 0, E = OptStr.size(); I != E; ++I) {
    const char &OptChar = *(OptStr.data() + I);
    switch (OptChar) {
    default:
      break;
    case '1':
    case '2':
    case 'x':
    case 'd':
      // Ignore /O[12xd] flags that aren't the last one on the command line.
      // Only the last one gets expanded.
      if (&OptChar != ExpandChar) {
        A->claim();
        break;
      }
      if (OptChar == 'd') {
        DAL.AddFlagArg(A, Opts.getOption(options::OPT_O0));
      } else {
        if (OptChar == '1') {
          DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "s");
        } else if (OptChar == '2' || OptChar == 'x') {
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_fbuiltin));
          DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "2");
        }
        if (SupportsForcingFramePointer &&
            !DAL.hasArgNoClaim(options::OPT_fno_omit_frame_pointer))
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_fomit_frame_pointer));
        if (OptChar == '1' || OptChar == '2')
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_ffunction_sections));
      }
      break;
    case 'b':
      if (I + 1 != E && isdigit(OptStr[I + 1])) {
        switch (OptStr[I + 1]) {
        case '0':
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_fno_inline));
          break;
        case '1':
          DAL.AddFlagArg(A,
                         Opts.getOption(options::OPT_finline_hint_functions));
          break;
        case '2':
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_finline_functions));
          break;
        }
        ++I;
      }
      break;
    case 'g':
      A->claim();
      break;
    case 'i':
      if (I + 1 != E && OptStr[I + 1] == '-') {
        ++I;
        DAL.AddFlagArg(A, Opts.getOption(options::OPT_fno_builtin));
      } else {
        DAL.AddFlagArg(A, Opts.getOption(options::OPT_fbuiltin));
      }
      break;
    case 's':
      DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "s");
      break;
    case 't':
      DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "2");
      break;
    case 'y': {
      bool OmitFramePointer = true;
      if (I + 1 != E && OptStr[I + 1] == '-') {
        OmitFramePointer = false;
        ++I;
      }
      if (SupportsForcingFramePointer) {
        if (OmitFramePointer)
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_fomit_frame_pointer));
        else
          DAL.AddFlagArg(A,
                         Opts.getOption(options::OPT_fno_omit_frame_pointer));
      } else {
        // Don't warn about /Oy- in x86-64 builds (where
        // SupportsForcingFramePointer is false).  The flag having no effect
        // there is a compiler-internal optimization, and people shouldn't have
        // to special-case their build files for x86-64 MSVC targets.
        A->claim();
      }
      break;
    }
    }
  }
}

void translateDArg(Arg *A, llvm::opt::DerivedArgList &DAL,
                   const OptTable &Opts) {
  assert(A->getOption().matches(options::OPT_D));

  llvm::StringRef Val = A->getValue();
  size_t Hash = Val.find('#');
  if (Hash == llvm::StringRef::npos || Hash > Val.find('=')) {
    DAL.append(A);
    return;
  }

  std::string NewVal = std::string(Val);
  NewVal[Hash] = '=';
  DAL.AddJoinedArg(A, Opts.getOption(options::OPT_D), NewVal);
}
} // namespace

llvm::opt::DerivedArgList *
MSVCToolChain::TranslateArgs(const llvm::opt::DerivedArgList &Args,
                             llvm::StringRef BoundArch) const {
  DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());
  const OptTable &Opts = getDriver().getOpts();

  // /Oy and /Oy- don't have an effect on X86-64
  bool SupportsForcingFramePointer = getArch() != llvm::Triple::x86_64;

  // The -O[12xd] flag actually expands to several flags.  We must desugar the
  // flags so that options embedded can be negated.  For example, the '-O2' flag
  // enables '-Oy'.  Expanding '-O2' into its constituent flags allows us to
  // correctly handle '-O2 -Oy-' where the trailing '-Oy-' disables a single
  // aspect of '-O2'.
  //
  // Note that this expansion logic only applies to the *last* of '[12xd]'.

  // First step is to search for the character we'd like to expand.
  const char *ExpandChar = nullptr;
  for (Arg *A : Args.filtered(options::OPT_msvc_optimize)) {
    llvm::StringRef OptStr = A->getValue();
    for (size_t I = 0, E = OptStr.size(); I != E; ++I) {
      char OptChar = OptStr[I];
      char PrevChar = I > 0 ? OptStr[I - 1] : '0';
      if (PrevChar == 'b') {
        // OptChar does not expand; it's an argument to the previous char.
        continue;
      }
      if (OptChar == '1' || OptChar == '2' || OptChar == 'x' || OptChar == 'd')
        ExpandChar = OptStr.data() + I;
    }
  }

  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT_msvc_optimize)) {
      // The -O flag actually takes an amalgam of other options.  For example,
      // '/Ogyb2' is equivalent to '/Og' '/Oy' '/Ob2'.
      translateOptArg(A, *DAL, SupportsForcingFramePointer, ExpandChar, Opts);
    } else if (A->getOption().matches(options::OPT_D)) {
      // Translate -Dfoo#bar into -Dfoo=bar.
      translateDArg(A, *DAL, Opts);
    } else if (A->getOption().matches(options::OPT_fms_permissive) ||
               A->getOption().matches(options::OPT_fno_ms_permissive)) {
      // C++ conformance flags; silently ignore in C-only compiler.
    } else {
      DAL->append(A);
    }
  }

  return DAL;
}
