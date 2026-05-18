#include "neverc/Invoke/ToolChain.h"
#include "ToolChains/Arch/AArch64.h"
#include "ToolChains/CommonArgs.h"
#include "ToolChains/NeverC.h"
#include "neverc/Config/config.h"
#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/InputInfo.h"
#include "neverc/Invoke/Job.h"
#include "neverc/Invoke/Options.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/TargetParser/TargetParser.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <cstddef>
#include <string>

using namespace neverc;
using namespace driver;
using namespace tools;
using namespace llvm;
using namespace llvm::opt;

// ===----------------------------------------------------------------------===
// Construction & configuration
// ===----------------------------------------------------------------------===

ToolChain::ToolChain(const Driver &D, const llvm::Triple &T,
                     const ArgList &Args)
    : D(D), Triple(T), Args(Args) {
  auto addIfExists = [this](path_list &List, const std::string &Path) {
    if (getVFS().exists(Path))
      List.push_back(Path);
  };

  if (std::optional<std::string> Path = getRuntimePath())
    getLibraryPaths().push_back(*Path);
  if (std::optional<std::string> Path = getStdlibPath())
    getFilePaths().push_back(*Path);
  for (const auto &Path : getArchSpecificLibPaths())
    addIfExists(getFilePaths(), Path);
}

void ToolChain::setTripleEnvironment(llvm::Triple::EnvironmentType Env) {
  Triple.setEnvironment(Env);
  if (EffectiveTriple != llvm::Triple())
    EffectiveTriple.setEnvironment(Env);
}

ToolChain::~ToolChain() = default;

llvm::vfs::FileSystem &ToolChain::getVFS() const {
  return getDriver().getVFS();
}

bool ToolChain::useIntegratedAs() const {
  // NeverC always uses the integrated assembler; -fno-integrated-as is a
  // no-op kept only for build-system compatibility (e.g. Linux kernel).
  if (const Arg *A = Args.getLastArg(options::OPT_fno_integrated_as))
    A->claim();
  return true;
}

bool ToolChain::useIntegratedBackend() const {
  assert(
      ((IsIntegratedBackendDefault() && IsIntegratedBackendSupported()) ||
       (!IsIntegratedBackendDefault() || IsNonIntegratedBackendSupported())) &&
      "(Non-)integrated backend set incorrectly!");

  bool IBackend = Args.hasFlag(options::OPT_fintegrated_objemitter,
                               options::OPT_fno_integrated_objemitter,
                               IsIntegratedBackendDefault());

  // Diagnose when integrated-objemitter options are not supported by this
  // toolchain.
  unsigned DiagID;
  if ((IBackend && !IsIntegratedBackendSupported()) ||
      (!IBackend && !IsNonIntegratedBackendSupported()))
    DiagID = neverc::diag::err_drv_unsupported_opt_for_target;
  else
    DiagID = neverc::diag::warn_drv_unsupported_opt_for_target;
  Arg *A = Args.getLastArg(options::OPT_fno_integrated_objemitter);
  if (A && !IsNonIntegratedBackendSupported())
    D.Diag(DiagID) << A->getAsString(Args) << Triple.getTriple();
  A = Args.getLastArg(options::OPT_fintegrated_objemitter);
  if (A && !IsIntegratedBackendSupported())
    D.Diag(DiagID) << A->getAsString(Args) << Triple.getTriple();

  return IBackend;
}

bool ToolChain::useRelaxRelocations() const {
  return ENABLE_X86_RELAX_RELOCATIONS;
}

bool ToolChain::defaultToIEEELongDouble() const { return false; }

namespace {
void getAArch64MultilibFlags(const Driver &D, const llvm::Triple &Triple,
                             const llvm::opt::ArgList &Args,
                             Multilib::flags_list &Result) {
  std::vector<llvm::StringRef> Features;
  tools::aarch64::getAArch64TargetFeatures(D, Triple, Args, Features, false);
  const auto UnifiedFeatures = tools::unifyTargetFeatures(Features);
  llvm::DenseSet<llvm::StringRef> FeatureSet(UnifiedFeatures.begin(),
                                             UnifiedFeatures.end());
  std::vector<std::string> MArch;
  for (const auto &Ext : AArch64::Extensions)
    if (FeatureSet.contains(Ext.Feature))
      MArch.push_back(Ext.Name.str());
  for (const auto &Ext : AArch64::Extensions)
    if (FeatureSet.contains(Ext.NegFeature))
      MArch.push_back(("no" + Ext.Name).str());
  MArch.insert(MArch.begin(), ("-march=" + Triple.getArchName()).str());
  Result.push_back(llvm::join(MArch, "+"));
}
} // namespace

Multilib::flags_list
ToolChain::getMultilibFlags(const llvm::opt::ArgList &Args) const {
  using namespace neverc::driver::options;

  std::vector<std::string> Result;
  const llvm::Triple Triple(ComputeEffectiveTriple(Args));
  Result.push_back("--target=" + Triple.str());

  switch (Triple.getArch()) {
  case llvm::Triple::aarch64:
    getAArch64MultilibFlags(D, Triple, Args, Result);
    break;
  default:
    break;
  }

  // Sort and remove duplicates.
  std::sort(Result.begin(), Result.end());
  Result.erase(std::unique(Result.begin(), Result.end()), Result.end());
  return Result;
}

namespace {

struct DriverSuffix {
  const char *Suffix;
};

} // namespace

namespace {
const DriverSuffix *findDriverSuffix(llvm::StringRef ProgName, size_t &Pos) {
  // A list of known driver suffixes. Suffixes are compared against the
  // program name in order.
  static const DriverSuffix DriverSuffixes[] = {
      {"neverc"},
  };

  for (const auto &DS : DriverSuffixes) {
    llvm::StringRef Suffix(DS.Suffix);
    if (ProgName.ends_with(Suffix)) {
      Pos = ProgName.size() - Suffix.size();
      return &DS;
    }
  }
  return nullptr;
}

std::string normalizeProgramName(llvm::StringRef Argv0) {
  std::string ProgName = std::string(llvm::sys::path::filename(Argv0));
  if (is_style_windows(llvm::sys::path::Style::native)) {
    // Transform to lowercase for case insensitive file systems.
    std::transform(ProgName.begin(), ProgName.end(), ProgName.begin(),
                   ::tolower);
  }
  return ProgName;
}

const DriverSuffix *parseDriverSuffix(llvm::StringRef ProgName, size_t &Pos) {
  // Try to infer frontend type and default target from the program name by
  // comparing it against DriverSuffixes in order.

  // If there is a match, the function tries to identify a target as prefix.
  // E.g. "x86_64-linux-neverc" as interpreted as suffix "neverc" with target
  // prefix "x86_64-linux". If such a target prefix is found, it may be
  // added via -target as implicit first argument.
  const DriverSuffix *DS = findDriverSuffix(ProgName, Pos);

  if (!DS && ProgName.ends_with(".exe")) {
    // Try again after stripping the executable suffix:
    // neverc.exe -> neverc
    ProgName = ProgName.drop_back(llvm::StringRef(".exe").size());
    DS = findDriverSuffix(ProgName, Pos);
  }

  if (!DS) {
    // Try again after stripping any trailing version number:
    // neverc3.5 -> neverc
    ProgName = ProgName.rtrim("0123456789.");
    DS = findDriverSuffix(ProgName, Pos);
  }

  if (!DS) {
    // Try again after stripping trailing -component.
    // neverc-tot -> neverc
    ProgName = ProgName.slice(0, ProgName.rfind('-'));
    DS = findDriverSuffix(ProgName, Pos);
  }
  return DS;
}
} // namespace

ParsedDriverName ToolChain::getTargetPrefixFromProgramName(llvm::StringRef PN) {
  std::string ProgName = normalizeProgramName(PN);
  size_t SuffixPos;
  const DriverSuffix *DS = parseDriverSuffix(ProgName, SuffixPos);
  if (!DS)
    return {};

  size_t LastComponent = ProgName.rfind('-', SuffixPos);
  if (LastComponent == std::string::npos)
    return {};

  // Infer target from the prefix.
  llvm::StringRef Prefix(ProgName);
  Prefix = Prefix.slice(0, LastComponent);
  std::string IgnoredError;
  bool IsRegistered =
      llvm::TargetRegistry::lookupTarget(std::string(Prefix), IgnoredError);
  return ParsedDriverName(std::string(Prefix), IsRegistered);
}

llvm::StringRef ToolChain::getDefaultUniversalArchName() const {
  // In universal driver terms, the arch name accepted by -arch isn't exactly
  // the same as the ones that appear in the triple. Roughly speaking, this is
  // an inverse of the darwin::getArchTypeForDarwinArchName() function.
  switch (Triple.getArch()) {
  case llvm::Triple::aarch64:
    return "arm64";
  default:
    return Triple.getArchName();
  }
}

std::string ToolChain::getInputFilename(const InputInfo &Input) const {
  return Input.getFilename();
}

ToolChain::UnwindTableLevel
ToolChain::getDefaultUnwindTableLevel(const ArgList &Args) const {
  return UnwindTableLevel::None;
}

unsigned ToolChain::GetDefaultDwarfVersion() const { return 5; }

Tool *ToolChain::getNeverC() const {
  if (!NeverC)
    NeverC.reset(new tools::NeverC(*this, useIntegratedBackend()));
  return NeverC.get();
}

Tool *ToolChain::buildLinker() const { return nullptr; }

Tool *ToolChain::buildStaticLibTool() const { return nullptr; }

Tool *ToolChain::getAssemble() const { return getNeverCAs(); }

Tool *ToolChain::getNeverCAs() const {
  if (!Assemble)
    Assemble.reset(new tools::NeverCAs(*this));
  return Assemble.get();
}

Tool *ToolChain::getLink() const {
  if (!Link)
    Link.reset(buildLinker());
  return Link.get();
}

Tool *ToolChain::getStaticLibTool() const {
  if (!StaticLibTool)
    StaticLibTool.reset(buildStaticLibTool());
  return StaticLibTool.get();
}

Tool *ToolChain::getTool(Action::ActionClass AC) const {
  switch (AC) {
  case Action::AssembleJobClass:
    return getAssemble();

  case Action::LinkJobClass:
    return getLink();

  case Action::StaticLibJobClass:
    return getStaticLibTool();

  case Action::InputClass:
  case Action::BindArchClass:
  case Action::LipoJobClass:
  case Action::DsymutilJobClass:
    llvm_unreachable("Invalid tool kind.");

  case Action::CompileJobClass:
  case Action::PreprocessJobClass:
  case Action::BackendJobClass:
    return getNeverC();
  }

  llvm_unreachable("Invalid tool kind.");
}

namespace {
llvm::StringRef getArchNameForCompilerRTLib(const ToolChain &TC) {
  const llvm::Triple &Triple = TC.getTriple();

  if (TC.isBareMetal())
    return Triple.getArchName();

  return llvm::Triple::getArchTypeName(TC.getArch());
}
} // namespace

llvm::StringRef ToolChain::getOSLibName() const {
  if (Triple.isOSDarwin())
    return "darwin";
  return getOS();
}

std::string ToolChain::getCompilerRTPath() const {
  llvm::SmallString<128> Path(getDriver().ResourceDir);
  if (isBareMetal()) {
    llvm::sys::path::append(Path, "lib", getOSLibName());
    if (!SelectedMultilibs.empty()) {
      Path += SelectedMultilibs.back().gccSuffix();
    }
  } else if (Triple.isOSUnknown()) {
    llvm::sys::path::append(Path, "lib");
  } else {
    llvm::sys::path::append(Path, "lib", getOSLibName());
  }
  return std::string(Path.str());
}

std::string ToolChain::getCompilerRTBasename(const ArgList &Args,
                                             llvm::StringRef Component,
                                             FileType Type) const {
  std::string CRTAbsolutePath = getCompilerRT(Args, Component, Type);
  return llvm::sys::path::filename(CRTAbsolutePath).str();
}

std::string ToolChain::buildCompilerRTBasename(const llvm::opt::ArgList &Args,
                                               llvm::StringRef Component,
                                               FileType Type,
                                               bool AddArch) const {
  const llvm::Triple &TT = getTriple();
  bool IsMSVCWindows = TT.isWindowsMSVCEnvironment();

  const char *Prefix =
      IsMSVCWindows || Type == ToolChain::FT_Object ? "" : "lib";
  const char *Suffix;
  switch (Type) {
  case ToolChain::FT_Object:
    Suffix = IsMSVCWindows ? ".obj" : ".o";
    break;
  case ToolChain::FT_Static:
    Suffix = IsMSVCWindows ? ".lib" : ".a";
    break;
  case ToolChain::FT_Shared:
    Suffix = TT.isOSWindows() ? ".lib" : ".so";
    break;
  }

  std::string ArchAndEnv;
  if (AddArch) {
    llvm::StringRef Arch = getArchNameForCompilerRTLib(*this);
    ArchAndEnv = ("-" + Arch).str();
  }
  return (Prefix + llvm::Twine("clang_rt.") + Component + ArchAndEnv + Suffix)
      .str();
}

std::string ToolChain::getCompilerRT(const ArgList &Args,
                                     llvm::StringRef Component,
                                     FileType Type) const {
  // Check for runtime files in the new layout without the architecture first.
  std::string CRTBasename =
      buildCompilerRTBasename(Args, Component, Type, /*AddArch=*/false);
  for (const auto &LibPath : getLibraryPaths()) {
    llvm::SmallString<128> P(LibPath);
    llvm::sys::path::append(P, CRTBasename);
    if (getVFS().exists(P))
      return std::string(P.str());
  }

  // Fall back to the old expected compiler-rt name if the new one does not
  // exist.
  CRTBasename =
      buildCompilerRTBasename(Args, Component, Type, /*AddArch=*/true);
  llvm::SmallString<128> Path(getCompilerRTPath());
  llvm::sys::path::append(Path, CRTBasename);
  return std::string(Path.str());
}

const char *ToolChain::getCompilerRTArgString(const llvm::opt::ArgList &Args,
                                              llvm::StringRef Component,
                                              FileType Type) const {
  return Args.MakeArgString(getCompilerRT(Args, Component, Type));
}

std::optional<std::string>
ToolChain::getTargetSubDirPath(llvm::StringRef BaseDir) const {
  auto getPathForTriple =
      [&](const llvm::Triple &Triple) -> std::optional<std::string> {
    llvm::SmallString<128> P(BaseDir);
    llvm::sys::path::append(P, Triple.str());
    if (getVFS().exists(P))
      return std::string(P);
    return {};
  };

  if (auto Path = getPathForTriple(getTriple()))
    return *Path;

  return {};
}

std::optional<std::string> ToolChain::getRuntimePath() const {
  llvm::SmallString<128> P(D.ResourceDir);
  llvm::sys::path::append(P, "lib");
  return getTargetSubDirPath(P);
}

std::optional<std::string> ToolChain::getStdlibPath() const {
  llvm::SmallString<128> P(D.Dir);
  llvm::sys::path::append(P, "..", "lib");
  return getTargetSubDirPath(P);
}

ToolChain::path_list ToolChain::getArchSpecificLibPaths() const {
  path_list Paths;

  auto AddPath = [&](const llvm::ArrayRef<llvm::StringRef> &SS) {
    llvm::SmallString<128> Path(getDriver().ResourceDir);
    llvm::sys::path::append(Path, "lib");
    for (auto &S : SS)
      llvm::sys::path::append(Path, S);
    Paths.push_back(std::string(Path.str()));
  };

  AddPath({getTriple().str()});
  AddPath({getOSLibName(), llvm::Triple::getArchTypeName(getArch())});
  return Paths;
}

Tool *ToolChain::SelectTool(const JobAction &JA) const {
  if (getDriver().ShouldUseNeverCCompiler(JA))
    return getNeverC();
  Action::ActionClass AC = JA.getKind();
  if (AC == Action::AssembleJobClass)
    return getNeverCAs();
  return getTool(AC);
}

std::string ToolChain::GetFilePath(const char *Name) const {
  return D.GetFilePath(Name, *this);
}

std::string ToolChain::GetProgramPath(const char *Name) const {
  return D.GetProgramPath(Name, *this);
}

std::string ToolChain::GetLinkerPath() const {
  // Always use the embedded linker (neverc itself).  -fuse-ld=* is claimed in
  // Driver::handleArguments for gcc compatibility (e.g. Linux kernel).
  return getDriver().getNeverCProgramPath();
}

types::ID ToolChain::LookupTypeForExtension(llvm::StringRef Ext) const {
  types::ID id = types::lookupTypeForExtension(Ext);

  return id;
}

// ===----------------------------------------------------------------------===
// Capability queries & includes
// ===----------------------------------------------------------------------===

bool ToolChain::HasNativeLLVMSupport() const { return false; }

bool ToolChain::isCrossCompiling() const {
  llvm::Triple HostTriple(LLVM_HOST_TRIPLE);
  return HostTriple.getArch() != getArch();
}

llvm::ExceptionHandling
ToolChain::GetExceptionModel(const llvm::opt::ArgList &Args) const {
  return llvm::ExceptionHandling::None;
}

bool ToolChain::isThreadModelSupported(const llvm::StringRef Model) const {
  if (Model == "single") {
    return false;
  } else if (Model == "posix")
    return true;

  return false;
}

std::string ToolChain::ComputeLLVMTriple(const ArgList &Args,
                                         types::ID InputType) const {
  switch (getTriple().getArch()) {
  default:
    return getTripleString();

  case llvm::Triple::x86_64: {
    llvm::Triple Triple = getTriple();
    if (!Triple.isOSBinFormatMachO())
      return getTripleString();
    return Triple.getTriple();
  }
  case llvm::Triple::aarch64: {
    llvm::Triple Triple = getTriple();
    if (!Triple.isOSBinFormatMachO())
      return getTripleString();

    Triple.setArchName("arm64");
    return Triple.getTriple();
  }
  }
}

std::string ToolChain::ComputeEffectiveTriple(const ArgList &Args,
                                              types::ID InputType) const {
  return ComputeLLVMTriple(Args, InputType);
}

std::string ToolChain::computeSysRoot() const { return D.SysRoot; }

void ToolChain::AddNeverCSystemIncludeArgs(const ArgList &DriverArgs,
                                           ArgStringList &FrontendArgs) const {
  // Each toolchain should provide the appropriate include flags.
}

void ToolChain::addNeverCTargetOptions(const ArgList &DriverArgs,
                                       ArgStringList &FrontendArgs) const {}

void ToolChain::addNeverCFrontendAsTargetOptions(
    const ArgList &Args, ArgStringList &FrontendAsArgs) const {}

void ToolChain::addNeverCWarningOptions(ArgStringList &FrontendArgs) const {}

ToolChain::RuntimeLibType
ToolChain::GetRuntimeLibType(const ArgList &Args) const {
  if (runtimeLibType)
    return *runtimeLibType;

  const Arg *A = Args.getLastArg(options::OPT_rtlib_EQ);
  llvm::StringRef LibName = A ? A->getValue() : NEVERC_DEFAULT_RTLIB;

  // Only use "platform" in tests to override NEVERC_DEFAULT_RTLIB!
  if (LibName == "compiler-rt")
    runtimeLibType = ToolChain::RLT_CompilerRT;
  else if (LibName == "libgcc")
    runtimeLibType = ToolChain::RLT_Libgcc;
  else if (LibName == "platform")
    runtimeLibType = GetDefaultRuntimeLibType();
  else {
    if (A)
      getDriver().Diag(diag::err_drv_invalid_rtlib_name)
          << A->getAsString(Args);

    runtimeLibType = GetDefaultRuntimeLibType();
  }

  return *runtimeLibType;
}

ToolChain::UnwindLibType
ToolChain::GetUnwindLibType(const ArgList &Args) const {
  if (unwindLibType)
    return *unwindLibType;

  const Arg *A = Args.getLastArg(options::OPT_unwindlib_EQ);
  llvm::StringRef LibName = A ? A->getValue() : NEVERC_DEFAULT_UNWINDLIB;

  if (LibName == "none")
    unwindLibType = ToolChain::UNW_None;
  else if (LibName == "platform" || LibName == "") {
    ToolChain::RuntimeLibType RtLibType = GetRuntimeLibType(Args);
    if (RtLibType == ToolChain::RLT_CompilerRT)
      unwindLibType = ToolChain::UNW_None;
    else if (RtLibType == ToolChain::RLT_Libgcc)
      unwindLibType = ToolChain::UNW_Libgcc;
  } else if (LibName == "libunwind") {
    if (GetRuntimeLibType(Args) == RLT_Libgcc)
      getDriver().Diag(diag::err_drv_incompatible_unwindlib);
    unwindLibType = ToolChain::UNW_CompilerRT;
  } else if (LibName == "libgcc")
    unwindLibType = ToolChain::UNW_Libgcc;
  else {
    if (A)
      getDriver().Diag(diag::err_drv_invalid_unwindlib_name)
          << A->getAsString(Args);

    unwindLibType = GetDefaultUnwindLibType();
  }

  return *unwindLibType;
}

/*static*/ void ToolChain::addSystemInclude(const ArgList &DriverArgs,
                                            ArgStringList &FrontendArgs,
                                            const llvm::Twine &Path) {
  FrontendArgs.push_back("-internal-isystem");
  FrontendArgs.push_back(DriverArgs.MakeArgString(Path));
}

/*static*/ void ToolChain::addExternCSystemInclude(const ArgList &DriverArgs,
                                                   ArgStringList &FrontendArgs,
                                                   const llvm::Twine &Path) {
  FrontendArgs.push_back("-internal-externc-isystem");
  FrontendArgs.push_back(DriverArgs.MakeArgString(Path));
}

void ToolChain::addExternCSystemIncludeIfExists(const ArgList &DriverArgs,
                                                ArgStringList &FrontendArgs,
                                                const llvm::Twine &Path) {
  if (llvm::sys::fs::exists(Path))
    addExternCSystemInclude(DriverArgs, FrontendArgs, Path);
}

/*static*/ void
ToolChain::addSystemIncludes(const ArgList &DriverArgs,
                             ArgStringList &FrontendArgs,
                             llvm::ArrayRef<llvm::StringRef> Paths) {
  for (const auto &Path : Paths) {
    FrontendArgs.push_back("-internal-isystem");
    FrontendArgs.push_back(DriverArgs.MakeArgString(Path));
  }
}

/*static*/ std::string ToolChain::concat(llvm::StringRef Path,
                                         const llvm::Twine &A,
                                         const llvm::Twine &B,
                                         const llvm::Twine &C,
                                         const llvm::Twine &D) {
  llvm::SmallString<128> Result(Path);
  llvm::sys::path::append(Result, llvm::sys::path::Style::posix, A, B, C, D);
  return std::string(Result);
}

void ToolChain::AddFilePathLibArgs(const ArgList &Args,
                                   ArgStringList &CmdArgs) const {
  for (const auto &LibPath : getFilePaths())
    if (LibPath.length() > 0)
      CmdArgs.push_back(Args.MakeArgString(llvm::StringRef("-L") + LibPath));
}

bool ToolChain::isFastMathRuntimeAvailable(const ArgList &Args,
                                           std::string &Path) const {
  // Do not check for -fno-fast-math or -fno-unsafe-math when -Ofast passed
  // (to keep the linker options consistent with gcc and neverc itself).
  if (!isOptimizationLevelFast(Args)) {
    // Check if -ffast-math or -funsafe-math.
    Arg *A =
        Args.getLastArg(options::OPT_ffast_math, options::OPT_fno_fast_math,
                        options::OPT_funsafe_math_optimizations,
                        options::OPT_fno_unsafe_math_optimizations);

    if (!A || A->getOption().getID() == options::OPT_fno_fast_math ||
        A->getOption().getID() == options::OPT_fno_unsafe_math_optimizations)
      return false;
  }
  // If crtfastmath.o exists add it to the arguments.
  Path = GetFilePath("crtfastmath.o");
  return (Path != "crtfastmath.o"); // Not found.
}

bool ToolChain::addFastMathRuntimeIfAvailable(const ArgList &Args,
                                              ArgStringList &CmdArgs) const {
  std::string Path;
  if (isFastMathRuntimeAvailable(Args, Path)) {
    CmdArgs.push_back(Args.MakeArgString(Path));
    return true;
  }

  return false;
}

// ===----------------------------------------------------------------------===
// Argument translation & Xarch handling
// ===----------------------------------------------------------------------===

namespace {
llvm::VersionTuple separateMSVCFullVersion(unsigned Version) {
  if (Version < 100)
    return llvm::VersionTuple(Version);

  if (Version < 10000)
    return llvm::VersionTuple(Version / 100, Version % 100);

  unsigned Build = 0, Factor = 1;
  for (; Version > 10000; Version = Version / 10, Factor = Factor * 10)
    Build = Build + (Version % 10) * Factor;
  return llvm::VersionTuple(Version / 100, Version % 100, Build);
}
} // namespace

llvm::VersionTuple
ToolChain::computeMSVCVersion(const Driver *D,
                              const llvm::opt::ArgList &Args) const {
  const Arg *MSCVersion = Args.getLastArg(options::OPT_fmsc_version);
  const Arg *MSCompatibilityVersion =
      Args.getLastArg(options::OPT_fms_compatibility_version);

  if (MSCVersion && MSCompatibilityVersion) {
    if (D)
      D->Diag(diag::err_drv_argument_not_allowed_with)
          << MSCVersion->getAsString(Args)
          << MSCompatibilityVersion->getAsString(Args);
    return llvm::VersionTuple();
  }

  if (MSCompatibilityVersion) {
    llvm::VersionTuple MSVT;
    if (MSVT.tryParse(MSCompatibilityVersion->getValue())) {
      if (D)
        D->Diag(diag::err_drv_invalid_value)
            << MSCompatibilityVersion->getAsString(Args)
            << MSCompatibilityVersion->getValue();
    } else {
      return MSVT;
    }
  }

  if (MSCVersion) {
    unsigned Version = 0;
    if (llvm::StringRef(MSCVersion->getValue()).getAsInteger(10, Version)) {
      if (D)
        D->Diag(diag::err_drv_invalid_value)
            << MSCVersion->getAsString(Args) << MSCVersion->getValue();
    } else {
      return separateMSVCFullVersion(Version);
    }
  }

  return llvm::VersionTuple();
}

void ToolChain::TranslateXarchArgs(
    const llvm::opt::DerivedArgList &Args, llvm::opt::Arg *&A,
    llvm::opt::DerivedArgList *DAL,
    llvm::SmallVectorImpl<llvm::opt::Arg *> *AllocatedArgs) const {
  const OptTable &Opts = getDriver().getOpts();
  unsigned ValuePos = 1;
  if (A->getOption().matches(options::OPT_Xarch_host))
    ValuePos = 0;

  unsigned Index = Args.getBaseArgs().MakeIndex(A->getValue(ValuePos));
  unsigned Prev = Index;
  std::unique_ptr<llvm::opt::Arg> XarchArg(Opts.ParseOneArg(Args, Index));

  // If the argument parsing failed or more than one argument was
  // consumed, the -Xarch_ argument's parameter tried to consume
  // extra arguments. Emit an error and ignore.
  //
  // We also want to disallow any options which would alter the
  // driver behavior; that isn't going to work in our model. We
  // use options::NoXarchOption to control this.
  if (!XarchArg || Index > Prev + 1) {
    getDriver().Diag(diag::err_drv_invalid_Xarch_argument_with_args)
        << A->getAsString(Args);
    return;
  } else if (XarchArg->getOption().hasFlag(options::NoXarchOption)) {
    auto &Diags = getDriver().getDiags();
    unsigned DiagID =
        Diags.getCustomDiagID(DiagnosticsEngine::Error,
                              "invalid Xarch argument: '%0', not all driver "
                              "options can be forwared via Xarch argument");
    Diags.Report(DiagID) << A->getAsString(Args);
    return;
  }
  XarchArg->setBaseArg(A);
  A = XarchArg.release();
  if (!AllocatedArgs)
    DAL->AddSynthesizedArg(A);
  else
    AllocatedArgs->push_back(A);
}

llvm::opt::DerivedArgList *ToolChain::TranslateXarchArgs(
    const llvm::opt::DerivedArgList &Args,
    llvm::SmallVectorImpl<llvm::opt::Arg *> *AllocatedArgs) const {
  DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());
  bool Modified = false;

  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT_Xarch_host)) {
      Modified = true;
      TranslateXarchArgs(Args, A, DAL, AllocatedArgs);
    }
    DAL->append(A);
  }

  if (Modified)
    return DAL;

  delete DAL;
  return nullptr;
}
