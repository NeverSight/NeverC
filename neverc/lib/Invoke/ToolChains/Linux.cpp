#include "Linux.h"
#include "CommonArgs.h"
#include "neverc/Config/config.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"
#include <system_error>

using namespace neverc::driver;
using namespace neverc::driver::toolchains;
using namespace neverc;
using namespace llvm::opt;

using tools::addPathIfExists;

// ===----------------------------------------------------------------------===
// Linux toolchain
// ===----------------------------------------------------------------------===

std::string Linux::getMultiarchTriple(const Driver &D,
                                      const llvm::Triple &TargetTriple,
                                      llvm::StringRef SysRoot) const {
  if (TargetTriple.isAndroid()) {
    switch (TargetTriple.getArch()) {
    case llvm::Triple::x86_64:
      return "x86_64-linux-android";
    case llvm::Triple::aarch64:
      return "aarch64-linux-android";
    default:
      return TargetTriple.str();
    }
  }
  switch (TargetTriple.getArch()) {
  case llvm::Triple::x86_64:
    return "x86_64-linux-gnu";
  case llvm::Triple::aarch64:
    return "aarch64-linux-gnu";
  default:
    return TargetTriple.str();
  }
}

namespace {
llvm::StringRef getOSLibDir(const llvm::Triple &) { return "lib64"; }
} // namespace

Linux::Linux(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  GCCInstallation.init(Triple, Args);
  Multilibs = GCCInstallation.getMultilibs();
  SelectedMultilibs.assign({GCCInstallation.getMultilib()});
  std::string SysRoot = computeSysRoot();

  llvm::SmallString<128> BundledCheck;
  if (D.SysRoot.empty()) {
    if (Triple.isAndroid() &&
        tools::getBundledAndroidSysroot(D, Triple, BundledCheck)) {
      UseBundledAndroidSysroot = true;
      UseBundledSysroot = true;
    } else if (!Triple.isAndroid() &&
               tools::getBundledLinuxSysroot(D, Triple, BundledCheck)) {
      UseBundledSysroot = true;
    }
  }

  ToolChain::path_list &PPaths = getProgramPaths();

  Generic_GCC::PushPPaths(PPaths);

  ExtraOpts.push_back("-z");
  ExtraOpts.push_back("relro");

  if (GCCInstallation.getParentLibPath().contains("opt/rh/"))
    PPaths.push_back(
        llvm::Twine(GCCInstallation.getParentLibPath() + "/../bin").str());

  path_list &Paths = getFilePaths();

  const std::string OSLibDir = std::string(getOSLibDir(Triple));
  const std::string MultiarchTriple = getMultiarchTriple(D, Triple, SysRoot);

  Generic_GCC::AddMultilibPaths(D, SysRoot, OSLibDir, MultiarchTriple, Paths);

  addPathIfExists(D, concat(SysRoot, "/lib", MultiarchTriple), Paths);
  addPathIfExists(D, concat(SysRoot, "/lib/..", OSLibDir), Paths);

  addPathIfExists(D, concat(SysRoot, "/usr/lib", MultiarchTriple), Paths);
  addPathIfExists(D, concat(SysRoot, "/usr/lib/..", OSLibDir), Paths);

  Generic_GCC::AddMultiarchPaths(D, SysRoot, OSLibDir, Paths);

  addPathIfExists(D, concat(SysRoot, "/lib"), Paths);
  addPathIfExists(D, concat(SysRoot, "/usr/lib"), Paths);

  if (!UseBundledAndroidSysroot && UseBundledSysroot) {
    // When GCC isn't detected (common during cross-compilation), search for
    // bundled GCC runtime objects (crtbeginS.o, crtendS.o, libgcc.a, etc.)
    // inside the sysroot.
    if (!GCCInstallation.isValid()) {
      for (const char *Ver : {"12", "13", "14", "11", "10"}) {
        std::string GCCPath =
            concat(SysRoot, "/usr/lib/gcc/", MultiarchTriple, "/") + Ver;
        if (D.getVFS().exists(GCCPath)) {
          Paths.push_back(GCCPath);
          break;
        }
      }
    }

    // On a native Linux host, also add system library paths as fallback
    // for any libs not included in the bundle.
    llvm::Triple HostTriple(llvm::sys::getDefaultTargetTriple());
    if (HostTriple.isOSLinux()) {
      addPathIfExists(D, concat("", "/lib", MultiarchTriple), Paths);
      addPathIfExists(D, concat("", "/lib/..", OSLibDir), Paths);
      addPathIfExists(D, concat("", "/usr/lib", MultiarchTriple), Paths);
      addPathIfExists(D, concat("", "/usr/lib/..", OSLibDir), Paths);
      addPathIfExists(D, "/lib", Paths);
      addPathIfExists(D, "/usr/lib", Paths);
    }
  }
}

ToolChain::RuntimeLibType Linux::GetDefaultRuntimeLibType() const {
  if (getTriple().isAndroid())
    return ToolChain::RLT_CompilerRT;
  return Generic_ELF::GetDefaultRuntimeLibType();
}

unsigned Linux::GetDefaultDwarfVersion() const {
  return ToolChain::GetDefaultDwarfVersion();
}

bool Linux::HasNativeLLVMSupport() const { return true; }

Tool *Linux::buildLinker() const { return new tools::gnutools::Linker(*this); }

Tool *Linux::buildStaticLibTool() const {
  return new tools::gnutools::StaticLibTool(*this);
}

std::string Linux::computeSysRoot() const {
  if (!getDriver().SysRoot.empty())
    return getDriver().SysRoot;

  llvm::SmallString<128> BundledRoot;
  if (getTriple().isAndroid()) {
    if (tools::getBundledAndroidSysroot(getDriver(), getTriple(), BundledRoot))
      return std::string(BundledRoot);
  } else {
    if (tools::getBundledLinuxSysroot(getDriver(), getTriple(), BundledRoot))
      return std::string(BundledRoot);
  }

  return std::string();
}

std::string Linux::getDynamicLinker(const ArgList &Args) const {
  if (getTriple().isAndroid()) {
    return "/system/bin/linker64";
  }
  switch (getArch()) {
  case llvm::Triple::aarch64:
    return "/lib/ld-linux-aarch64.so.1";
  case llvm::Triple::x86_64:
    return "/lib64/ld-linux-x86-64.so.2";
  default:
    llvm_unreachable("unsupported architecture");
  }
}

void Linux::AddNeverCSystemIncludeArgs(const ArgList &DriverArgs,
                                       ArgStringList &FrontendArgs) const {
  const Driver &D = getDriver();
  std::string SysRoot = computeSysRoot();

  if (DriverArgs.hasArg(neverc::driver::options::OPT_nostdinc))
    return;

  // Add 'include' in the resource directory, which is similar to
  // GCC_INCLUDE_DIR (private headers) in GCC.
  llvm::SmallString<128> ResourceDirInclude(D.ResourceDir);
  llvm::sys::path::append(ResourceDirInclude, "include");
  if (!DriverArgs.hasArg(options::OPT_nobuiltininc))
    addSystemInclude(DriverArgs, FrontendArgs, ResourceDirInclude);

  if (DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  // LOCAL_INCLUDE_DIR
  addSystemInclude(DriverArgs, FrontendArgs,
                   concat(SysRoot, "/usr/local/include"));
  // TOOL_INCLUDE_DIR
  AddMultilibIncludeArgs(DriverArgs, FrontendArgs);

  // Check for configure-time C include directories.
  llvm::StringRef CIncludeDirs(C_INCLUDE_DIRS);
  if (CIncludeDirs != "") {
    llvm::SmallVector<llvm::StringRef, 5> dirs;
    CIncludeDirs.split(dirs, ":");
    for (llvm::StringRef dir : dirs) {
      llvm::StringRef Prefix =
          llvm::sys::path::is_absolute(dir) ? "" : llvm::StringRef(SysRoot);
      addExternCSystemInclude(DriverArgs, FrontendArgs, Prefix + dir);
    }
    return;
  }

  // On systems using multiarch, add /usr/include/$triple before /usr/include.
  std::string MultiarchIncludeDir = getMultiarchTriple(D, getTriple(), SysRoot);
  if (!MultiarchIncludeDir.empty() &&
      D.getVFS().exists(concat(SysRoot, "/usr/include", MultiarchIncludeDir)))
    addExternCSystemInclude(
        DriverArgs, FrontendArgs,
        concat(SysRoot, "/usr/include", MultiarchIncludeDir));

  addExternCSystemInclude(DriverArgs, FrontendArgs,
                          concat(SysRoot, "/include"));

  addExternCSystemInclude(DriverArgs, FrontendArgs,
                          concat(SysRoot, "/usr/include"));

  // When using the bundled sysroot on a native Linux host, also add
  // system include paths as fallback for headers not in the bundle.
  if (UseBundledSysroot) {
    llvm::Triple HostTriple(llvm::sys::getDefaultTargetTriple());
    if (HostTriple.isOSLinux()) {
      if (!MultiarchIncludeDir.empty() &&
          D.getVFS().exists(concat("", "/usr/include", MultiarchIncludeDir)))
        addExternCSystemInclude(DriverArgs, FrontendArgs,
                                concat("", "/usr/include", MultiarchIncludeDir));
      addExternCSystemInclude(DriverArgs, FrontendArgs, "/usr/include");
    }
  }
}

bool Linux::IsAArch64OutlineAtomicsDefault(const ArgList &Args) const {
  // Outline atomics for AArch64 are supported by compiler-rt
  // and libgcc since 9.3.1
  assert(getTriple().isAArch64() && "expected AArch64 target!");
  ToolChain::RuntimeLibType RtLib = GetRuntimeLibType(Args);
  if (RtLib == ToolChain::RLT_CompilerRT)
    return true;
  assert(RtLib == ToolChain::RLT_Libgcc && "unexpected runtime library type!");
  if (GCCInstallation.getVersion().isOlderThan(9, 3, 1))
    return false;
  return true;
}

bool Linux::IsMathErrnoDefault() const {
  return Generic_ELF::IsMathErrnoDefault();
}

llvm::DenormalMode
Linux::getDefaultDenormalModeForType(const llvm::opt::ArgList &DriverArgs,
                                     const JobAction &JA,
                                     const llvm::fltSemantics *FPType) const {
  switch (getTriple().getArch()) {
  case llvm::Triple::x86_64: {
    std::string Unused;
    // DAZ and FTZ are turned on in crtfastmath.o
    if (!DriverArgs.hasArg(options::OPT_nostdlib, options::OPT_nostartfiles) &&
        isFastMathRuntimeAvailable(DriverArgs, Unused))
      return llvm::DenormalMode::getPreserveSign();
    return llvm::DenormalMode::getIEEE();
  }
  default:
    return llvm::DenormalMode::getIEEE();
  }
}

void Linux::addExtraOpts(llvm::opt::ArgStringList &CmdArgs) const {
  for (const auto &Opt : ExtraOpts)
    CmdArgs.push_back(Opt.c_str());
}
