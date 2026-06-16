#include "neverc/Invoke/Driver.h"
#include "neverc/Config/config.h"

#include "ToolChains/Darwin.h"
#include "ToolChains/Gnu.h"
#include "ToolChains/Linux.h"
#include "ToolChains/MSVC.h"
#include "ToolChains/NeverC.h"
#include "llvm/Config/llvm-config.h"

#include "neverc/Foundation/Core/Version.h"
#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/InputInfo.h"
#include "neverc/Invoke/Job.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Invoke/Phases.h"
#include "neverc/Invoke/Tool.h"
#include "neverc/Invoke/ToolChain.h"
#include "neverc/Invoke/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <atomic>
#include <cstdlib> // ::getenv
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#if LLVM_ON_UNIX
#include <unistd.h> // getpid
#endif

using namespace neverc::driver;
using namespace neverc;
using namespace llvm::opt;

// ===----------------------------------------------------------------------===
// Construction & initialization
// ===----------------------------------------------------------------------===

// static
std::string Driver::GetResourcesPath(llvm::StringRef BinaryPath,
                                     llvm::StringRef CustomResourceDir) {
  // Since the resource directory is embedded in the module hash, it's important
  // that all places that need it call this function, so that they get the
  // exact same string ("a/../b/" and "b/" get different hashes, for example).

  // Dir is bin/ or lib/, depending on where BinaryPath is.
  std::string Dir = std::string(llvm::sys::path::parent_path(BinaryPath));

  llvm::SmallString<128> P(Dir);
  if (CustomResourceDir != "") {
    llvm::sys::path::append(P, CustomResourceDir);
  } else {
    // On Windows, the library is in bin/.
    // On non-Windows, the library is in lib/.
    // With a static-library build, the path will contain the
    // path of the embedding binary, which for LLVM binaries will be in bin/.
    // ../lib gets us to lib/ in both cases.
    P = llvm::sys::path::parent_path(Dir);
    // This search path is also created in the embedded COFF linker; any
    // changes here also need to happen in
    // neverc/Linker/Backends/COFF/Driver/CoffDriver.cpp.
    llvm::sys::path::append(P, NEVERC_INSTALL_LIBDIR_BASENAME, "neverc",
                            NEVERC_VERSION_MAJOR_STRING);
  }

  return std::string(P.str());
}

Driver::Driver(llvm::StringRef NeverCExecutable, llvm::StringRef TargetTriple,
               DiagnosticsEngine &Diags, std::string Title,
               llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS)
    : Diags(Diags), VFS(std::move(VFS)), SaveTemps(SaveTempsNone),
      LTOMode(LTOK_None), NeverCExecutable(NeverCExecutable),
      SysRoot(DEFAULT_SYSROOT), DriverTitle(Title), CCCPrintBindings(false),
      CCGenDiagnostics(false), TargetTriple(TargetTriple), Saver(Alloc),
      PrependArg(nullptr), CheckInputsExist(true),
      SuppressMissingInputWarning(false) {
  // Provide a sane fallback if no VFS is specified.
  if (!this->VFS)
    this->VFS = llvm::vfs::getRealFileSystem();

  Name = std::string(llvm::sys::path::filename(NeverCExecutable));
  Dir = std::string(llvm::sys::path::parent_path(NeverCExecutable));
  InstalledDir = Dir; // Provide a sensible default installed dir.

  if ((!SysRoot.empty()) && llvm::sys::path::is_relative(SysRoot)) {
    // Prepend InstalledDir if SysRoot is relative
    llvm::SmallString<128> P(InstalledDir);
    llvm::sys::path::append(P, SysRoot);
    SysRoot = std::string(P);
  }

#if defined(NEVERC_CONFIG_FILE_SYSTEM_DIR)
  SystemConfigDir = NEVERC_CONFIG_FILE_SYSTEM_DIR;
#endif
#if defined(NEVERC_CONFIG_FILE_USER_DIR)
  {
    llvm::SmallString<128> P;
    llvm::sys::fs::expand_tilde(NEVERC_CONFIG_FILE_USER_DIR, P);
    UserConfigDir = static_cast<std::string>(P);
  }
#endif

  ResourceDir = GetResourcesPath(NeverCExecutable, NEVERC_RESOURCE_DIR);
}

InputArgList Driver::ParseArgStrings(llvm::ArrayRef<const char *> ArgStrings,
                                     bool &ContainsError) {
  llvm::PrettyStackTraceString CrashInfo("Command line argument parsing");
  ContainsError = false;

  llvm::opt::Visibility VisibilityMask = getOptionVisibilityMask();
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args = getOpts().ParseArgs(ArgStrings, MissingArgIndex,
                                          MissingArgCount, VisibilityMask);

  if (MissingArgCount) {
    Diag(diag::err_drv_missing_argument)
        << Args.getArgString(MissingArgIndex) << MissingArgCount;
    ContainsError |=
        Diags.getDiagnosticLevel(diag::err_drv_missing_argument,
                                 SourceLocation()) > DiagnosticsEngine::Warning;
  }

  for (const Arg *A : Args) {
    if (A->getOption().hasFlag(options::Unsupported)) {
      Diag(diag::err_drv_unsupported_opt) << A->getAsString(Args);
      ContainsError |= Diags.getDiagnosticLevel(diag::err_drv_unsupported_opt,
                                                SourceLocation()) >
                       DiagnosticsEngine::Warning;
      continue;
    }

    // Warn about -mcpu= without an argument.
    if (A->getOption().matches(options::OPT_mcpu_EQ) && A->containsValue("")) {
      Diag(diag::warn_drv_empty_joined_argument) << A->getAsString(Args);
      ContainsError |= Diags.getDiagnosticLevel(
                           diag::warn_drv_empty_joined_argument,
                           SourceLocation()) > DiagnosticsEngine::Warning;
    }
  }

  for (const Arg *A : Args.filtered(options::OPT_UNKNOWN)) {
    unsigned DiagID;
    auto ArgString = A->getAsString(Args);
    std::string Nearest;
    if (getOpts().findNearest(ArgString, Nearest, VisibilityMask) > 1) {
      DiagID = diag::err_drv_unknown_argument;
      Diags.Report(DiagID) << ArgString;
    } else {
      DiagID = diag::err_drv_unknown_argument_with_suggestion;
      Diags.Report(DiagID) << ArgString << Nearest;
    }
    ContainsError |= Diags.getDiagnosticLevel(DiagID, SourceLocation()) >
                     DiagnosticsEngine::Warning;
  }

  for (const Arg *A : Args.filtered(options::OPT_o)) {
    if (ArgStrings[A->getIndex()] == A->getSpelling())
      continue;

    // Warn on joined arguments that are similar to a long argument.
    std::string ArgString = ArgStrings[A->getIndex()];
    std::string Nearest;
    if (getOpts().findExact("-" + ArgString, Nearest, VisibilityMask))
      Diags.Report(diag::warn_drv_potentially_misspelled_joined_argument)
          << A->getAsString(Args) << Nearest;
  }

  return Args;
}

// Determine which compilation mode we are in. We look for options which
// affect the phase, starting with the earliest phases, and record which
// option we used to determine the final phase.
phases::ID Driver::getFinalPhase(const DerivedArgList &DAL,
                                 Arg **FinalPhaseArg) const {
  Arg *PhaseArg = nullptr;
  phases::ID FinalPhase;

  // -{E,EP,P,M,MM} only run the preprocessor.
  if ((PhaseArg = DAL.getLastArg(options::OPT_E)) ||
      (PhaseArg =
           DAL.getLastArg(options::OPT_msvc_preprocess_no_linemarkers)) ||
      (PhaseArg = DAL.getLastArg(options::OPT_M, options::OPT_MM)) ||
      (PhaseArg = DAL.getLastArg(options::OPT_msvc_preprocess_to_file)) ||
      CCGenDiagnostics) {
    FinalPhase = phases::Preprocess;

    // -fsyntax-only / -print-supported-cpus only run up to the compiler.
  } else if ((PhaseArg = DAL.getLastArg(options::OPT_fsyntax_only)) ||
             (PhaseArg = DAL.getLastArg(options::OPT_print_supported_cpus))) {
    FinalPhase = phases::Compile;

    // -S only runs up to the backend.
  } else if ((PhaseArg = DAL.getLastArg(options::OPT_S))) {
    FinalPhase = phases::Backend;

    // -c compilation only runs up to the assembler.
  } else if ((PhaseArg = DAL.getLastArg(options::OPT_c))) {
    FinalPhase = phases::Assemble;

    // Otherwise do everything.
  } else
    FinalPhase = phases::Link;

  if (FinalPhaseArg)
    *FinalPhaseArg = PhaseArg;

  return FinalPhase;
}

// ===----------------------------------------------------------------------===
// Configuration & target resolution
// ===----------------------------------------------------------------------===

namespace {
Arg *formInputArg(DerivedArgList &Args, const OptTable &Opts,
                  llvm::StringRef Value, bool Claim = true) {
  Arg *A = new Arg(Opts.getOption(options::OPT_INPUT), Value,
                   Args.getBaseArgs().MakeIndex(Value), Value.data());
  Args.AddSynthesizedArg(A);
  if (Claim)
    A->claim();
  return A;
}
} // namespace

DerivedArgList *Driver::TranslateInputArgs(const InputArgList &Args) const {
  const llvm::opt::OptTable &Opts = getOpts();
  DerivedArgList *DAL = new DerivedArgList(Args);

  bool IgnoreUnused = false;
  for (Arg *A : Args) {
    if (IgnoreUnused)
      A->claim();

    if (A->getOption().matches(options::OPT_start_no_unused_arguments)) {
      IgnoreUnused = true;
      continue;
    }
    if (A->getOption().matches(options::OPT_end_no_unused_arguments)) {
      IgnoreUnused = false;
      continue;
    }

    // Unfortunately, we have to parse some forwarding options (-Xassembler,
    // -Xpreprocessor) because we integrate their functionality.
    // -Xlinker / -Wl, values flow through to the embedded linker directly.

    // Rewrite preprocessor options, to replace -Wp,-MD,FOO which is used by
    // some build systems. We don't try to be complete here because we don't
    // care to encourage this usage model.
    if (A->getOption().matches(options::OPT_Wp_COMMA) &&
        (A->getValue(0) == llvm::StringRef("-MD") ||
         A->getValue(0) == llvm::StringRef("-MMD"))) {
      // Rewrite to -MD/-MMD along with -MF.
      if (A->getValue(0) == llvm::StringRef("-MD"))
        DAL->AddFlagArg(A, Opts.getOption(options::OPT_MD));
      else
        DAL->AddFlagArg(A, Opts.getOption(options::OPT_MMD));
      if (A->getNumValues() == 2)
        DAL->AddSeparateArg(A, Opts.getOption(options::OPT_MF), A->getValue(1));
      continue;
    }

    // Pick up inputs via the -- option.
    if (A->getOption().matches(options::OPT__DASH_DASH)) {
      A->claim();
      for (llvm::StringRef Val : A->getValues())
        DAL->append(formInputArg(*DAL, Opts, Val, false));
      continue;
    }

    DAL->append(A);
  }

// Add a default value of -mlinker-version=, if one was given and the user
// didn't specify one.
#if defined(HOST_LINK_VERSION)
  if (!Args.hasArg(options::OPT_mlinker_version_EQ) &&
      strlen(HOST_LINK_VERSION) > 0) {
    DAL->AddJoinedArg(0, Opts.getOption(options::OPT_mlinker_version_EQ),
                      HOST_LINK_VERSION);
    DAL->getLastArg(options::OPT_mlinker_version_EQ)->claim();
  }
#endif

  return DAL;
}

namespace {
llvm::Triple computeTargetTriple(const Driver &D, llvm::StringRef TargetTriple,
                                 const ArgList &Args,
                                 llvm::StringRef DarwinArchName = "") {
  if (const Arg *A = Args.getLastArg(options::OPT_target))
    TargetTriple = A->getValue();

  llvm::Triple Target(llvm::Triple::normalize(TargetTriple));

  if (Target.isOSBinFormatMachO()) {
    // If an explicit Darwin arch name is given, that trumps all.
    if (!DarwinArchName.empty()) {
      tools::darwin::setTripleTypeForMachOArchName(Target, DarwinArchName,
                                                   Args);
      return Target;
    }

    if (Arg *A = Args.getLastArg(options::OPT_arch)) {
      llvm::StringRef ArchName = A->getValue();
      tools::darwin::setTripleTypeForMachOArchName(Target, ArchName, Args);
    }
  }

  // '-m64': select the 64-bit variant of the architecture (e.g. x86_64).
  if (Args.hasArg(options::OPT_m64)) {
    llvm::Triple::ArchType AT = Target.get64BitArchVariant().getArch();
    if (AT != llvm::Triple::UnknownArch && AT != Target.getArch())
      Target.setArch(AT);
  }

  return Target;
}

// Parse the LTO options and record the type of LTO compilation based on which
// -f(no-)?lto(=.*)? option occurs last.
driver::LTOKind parseLTOMode(Driver &D, const llvm::opt::ArgList &Args,
                             OptSpecifier OptEq, OptSpecifier OptNeg) {
  if (!Args.hasFlag(OptEq, OptNeg, false))
    return LTOK_None;

  const Arg *A = Args.getLastArg(OptEq);
  llvm::StringRef LTOName = A->getValue();

  driver::LTOKind LTOMode = llvm::StringSwitch<LTOKind>(LTOName)
                                .Case("full", LTOK_Full)
                                .Default(LTOK_Unknown);

  if (LTOMode == LTOK_Unknown) {
    D.Diag(diag::err_drv_unsupported_option_argument)
        << A->getSpelling() << A->getValue();
    return LTOK_None;
  }
  return LTOMode;
}
} // namespace

void Driver::setLTOMode(const llvm::opt::ArgList &Args) {
  LTOMode =
      parseLTOMode(*this, Args, options::OPT_flto_EQ, options::OPT_fno_lto);

  if (LTOMode == LTOK_None && !Args.hasArg(options::OPT_fno_lto) &&
      !Args.hasArg(options::OPT_S) && !Args.hasArg(options::OPT_E) &&
      !Args.hasArg(options::OPT_fsyntax_only)) {
    LTOMode = LTOK_Full;
    AutoLTO = true;
  }
}

namespace {
void addSingleArg(InputArgList &Args, const Arg *Opt, const Arg *BaseArg) {
  // Config file args belong to different InputArgList objects than Args.
  // Copy the Arg so ownership transfers into Args.
  unsigned Index = Args.MakeIndex(Opt->getSpelling());
  Arg *Copy = new llvm::opt::Arg(Opt->getOption(), Args.getArgString(Index),
                                 Index, BaseArg);
  Copy->getValues() = Opt->getValues();
  if (Opt->isClaimed())
    Copy->claim();
  Copy->setOwnsValues(Opt->getOwnsValues());
  Opt->setOwnsValues(false);
  Args.append(Copy);
}
} // namespace

bool Driver::readConfigFile(llvm::StringRef FileName,
                            llvm::cl::ExpansionContext &ExpCtx) {
  // Try opening the given file.
  auto Status = getVFS().status(FileName);
  if (!Status) {
    Diag(diag::err_drv_cannot_open_config_file)
        << FileName << Status.getError().message();
    return true;
  }
  if (Status->getType() != llvm::sys::fs::file_type::regular_file) {
    Diag(diag::err_drv_cannot_open_config_file)
        << FileName << "not a regular file";
    return true;
  }

  // Try reading the given file.
  llvm::SmallVector<const char *, 32> NewCfgArgs;
  if (llvm::Error Err = ExpCtx.readConfigFile(FileName, NewCfgArgs)) {
    Diag(diag::err_drv_cannot_read_config_file)
        << FileName << toString(std::move(Err));
    return true;
  }

  // Read options from config file.
  llvm::SmallString<128> CfgFileName(FileName);
  llvm::sys::path::native(CfgFileName);
  bool ContainErrors;
  std::unique_ptr<InputArgList> NewOptions = std::make_unique<InputArgList>(
      ParseArgStrings(NewCfgArgs, ContainErrors));
  if (ContainErrors)
    return true;

  // Claim all arguments that come from a configuration file so that the driver
  // does not warn on any that is unused.
  for (Arg *A : *NewOptions)
    A->claim();

  if (!CfgOptions)
    CfgOptions = std::move(NewOptions);
  else {
    // If this is a subsequent config file, append options to the previous one.
    for (auto *Opt : *NewOptions) {
      const Arg *BaseArg = &Opt->getBaseArg();
      if (BaseArg == Opt)
        BaseArg = nullptr;
      addSingleArg(*CfgOptions, Opt, BaseArg);
    }
  }
  ConfigFiles.push_back(std::string(CfgFileName));
  return false;
}

bool Driver::loadConfigFiles() {
  llvm::cl::ExpansionContext ExpCtx(Saver.getAllocator(),
                                    llvm::cl::tokenizeConfigFile);
  ExpCtx.setVFS(&getVFS());

  if (CommandLineOptions) {
    if (CommandLineOptions->hasArg(options::OPT_config_system_dir_EQ)) {
      llvm::SmallString<128> CfgDir;
      CfgDir.append(CommandLineOptions->getLastArgValue(
          options::OPT_config_system_dir_EQ));
      if (CfgDir.empty() || getVFS().makeAbsolute(CfgDir))
        SystemConfigDir.clear();
      else
        SystemConfigDir = static_cast<std::string>(CfgDir);
    }
    if (CommandLineOptions->hasArg(options::OPT_config_user_dir_EQ)) {
      llvm::SmallString<128> CfgDir;
      llvm::sys::fs::expand_tilde(
          CommandLineOptions->getLastArgValue(options::OPT_config_user_dir_EQ),
          CfgDir);
      if (CfgDir.empty() || getVFS().makeAbsolute(CfgDir))
        UserConfigDir.clear();
      else
        UserConfigDir = static_cast<std::string>(CfgDir);
    }
  }

  // Prepare list of directories where config file is searched for.
  llvm::StringRef CfgFileSearchDirs[] = {UserConfigDir, SystemConfigDir, Dir};
  ExpCtx.setSearchDirs(CfgFileSearchDirs);

  // First try to load configuration from the default files, return on error.
  if (loadDefaultConfigFiles(ExpCtx))
    return true;

  // Then load configuration files specified explicitly.
  llvm::SmallString<128> CfgFilePath;
  if (CommandLineOptions) {
    for (auto CfgFileName :
         CommandLineOptions->getAllArgValues(options::OPT_config)) {
      // If argument contains directory separator, treat it as a path to
      // configuration file.
      if (llvm::sys::path::has_parent_path(CfgFileName)) {
        CfgFilePath.assign(CfgFileName);
        if (llvm::sys::path::is_relative(CfgFilePath)) {
          if (getVFS().makeAbsolute(CfgFilePath)) {
            Diag(diag::err_drv_cannot_open_config_file)
                << CfgFilePath << "cannot get absolute path";
            return true;
          }
        }
      } else if (!ExpCtx.findConfigFile(CfgFileName, CfgFilePath)) {
        // Report an error that the config file could not be found.
        Diag(diag::err_drv_config_file_not_found) << CfgFileName;
        for (const llvm::StringRef &SearchDir : CfgFileSearchDirs)
          if (!SearchDir.empty())
            Diag(diag::note_drv_config_file_searched_in) << SearchDir;
        return true;
      }

      // Try to read the config file, return on error.
      if (readConfigFile(CfgFilePath, ExpCtx))
        return true;
    }
  }

  // No error occurred.
  return false;
}

bool Driver::loadDefaultConfigFiles(llvm::cl::ExpansionContext &ExpCtx) {
  // Disable default config if NEVERC_NO_DEFAULT_CONFIG is set to a non-empty
  // value.
  if (const char *NoConfigEnv = ::getenv("NEVERC_NO_DEFAULT_CONFIG")) {
    if (*NoConfigEnv)
      return false;
  }
  if (CommandLineOptions &&
      CommandLineOptions->hasArg(options::OPT_no_default_config))
    return false;

  const std::string RealMode = "neverc";
  std::string Triple;

  // If name prefix is present, no --target= override was passed on the command
  // line
  // and the name prefix is not a valid triple, force it for backwards
  // compatibility.
  if (!NameParts.TargetPrefix.empty() &&
      computeTargetTriple(*this, "/invalid/", *CommandLineOptions).str() ==
          "/invalid/") {
    llvm::Triple PrefixTriple{NameParts.TargetPrefix};
    if (PrefixTriple.getArch() == llvm::Triple::UnknownArch ||
        PrefixTriple.isOSUnknown())
      Triple = PrefixTriple.str();
  }

  // Otherwise, use the real triple as used by the driver.
  if (Triple.empty()) {
    llvm::Triple RealTriple =
        computeTargetTriple(*this, TargetTriple, *CommandLineOptions);
    Triple = RealTriple.str();
    assert(!Triple.empty());
  }

  // Search for config files in the following order:
  // 1. <triple>-neverc.cfg (e.g. x86_64-pc-linux-gnu-neverc.cfg)
  // 2. neverc.cfg
  // 3. <triple>.cfg (e.g. x86_64-pc-linux-gnu.cfg)

  // Try loading <triple>-neverc.cfg, and return if we find a match.
  llvm::SmallString<128> CfgFilePath;
  std::string CfgFileName = Triple + '-' + RealMode + ".cfg";
  if (ExpCtx.findConfigFile(CfgFileName, CfgFilePath))
    return readConfigFile(CfgFilePath, ExpCtx);

  // Try loading <mode>.cfg, and return if loading failed.  If a matching file
  // was not found, still proceed on to try <triple>.cfg.
  CfgFileName = RealMode + ".cfg";
  if (ExpCtx.findConfigFile(CfgFileName, CfgFilePath)) {
    if (readConfigFile(CfgFilePath, ExpCtx))
      return true;
  }

  // Try loading <triple>.cfg and return if we find a match.
  CfgFileName = Triple + ".cfg";
  if (ExpCtx.findConfigFile(CfgFileName, CfgFilePath))
    return readConfigFile(CfgFilePath, ExpCtx);

  // If we were unable to find a config file deduced from executable name,
  // that is not an error.
  return false;
}

namespace {
bool isTargetArchSupported(const llvm::Triple &T) {
  switch (T.getArch()) {
  case llvm::Triple::x86_64:
    // X32 ABI is not supported.
    if (T.isX32())
      return false;
    break;
  case llvm::Triple::aarch64:
    break;
  default:
    return false;
  }
  // Only macOS, iOS, Linux and Win32 are supported targets.
  switch (T.getOS()) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
  case llvm::Triple::Linux:
  case llvm::Triple::Win32:
  case llvm::Triple::UnknownOS:
    return true;
  default:
    return false;
  }
}

bool isArchLinkedInLLVM(const llvm::Triple &T,
                        llvm::StringRef &TargetNameForCMake) {
  switch (T.getArch()) {
  case llvm::Triple::x86_64:
    TargetNameForCMake = "X86";
    break;
  case llvm::Triple::aarch64:
    TargetNameForCMake = "AArch64";
    break;
  default:
    llvm_unreachable("isArchLinkedInLLVM called for unsupported arch");
  }
  std::string Err;
  return llvm::TargetRegistry::lookupTarget(T.getTriple(), Err) != nullptr;
}
} // namespace

Compilation *Driver::CreateCompilation(llvm::ArrayRef<const char *> ArgList) {
  llvm::PrettyStackTraceString CrashInfo("Compilation construction");

  // Arguments specified in command line.
  bool ContainsError;
  CommandLineOptions = std::make_unique<InputArgList>(
      ParseArgStrings(ArgList.slice(1), ContainsError));

  // Try parsing configuration file.
  if (!ContainsError)
    ContainsError = loadConfigFiles();
  bool HasConfigFile = !ContainsError && (CfgOptions.get() != nullptr);

  // All arguments, from both config file and command line.
  InputArgList Args = std::move(HasConfigFile ? std::move(*CfgOptions)
                                              : std::move(*CommandLineOptions));

  if (HasConfigFile)
    for (auto *Opt : *CommandLineOptions) {
      if (Opt->getOption().matches(options::OPT_config))
        continue;
      const Arg *BaseArg = &Opt->getBaseArg();
      if (BaseArg == Opt)
        BaseArg = nullptr;
      addSingleArg(Args, Opt, BaseArg);
    }

  if (Arg *WD = Args.getLastArg(options::OPT_working_directory))
    if (VFS->setCurrentWorkingDirectory(WD->getValue()))
      Diag(diag::err_drv_unable_to_set_working_directory) << WD->getValue();

  bool CCCPrintPhases;

  // -canonical-prefixes, -no-canonical-prefixes are used very early in main.
  Args.ClaimAllArgs(options::OPT_canonical_prefixes);
  Args.ClaimAllArgs(options::OPT_no_canonical_prefixes);

  // Ignore -pipe.
  Args.ClaimAllArgs(options::OPT_pipe);

  CCCPrintPhases = Args.hasArg(options::OPT_ccc_print_phases);
  CCCPrintBindings = Args.hasArg(options::OPT_ccc_print_bindings);
  if (const Arg *A = Args.getLastArg(options::OPT_ccc_gcc_name))
    CCCGenericGCCName = A->getValue();

  if (const Arg *A = Args.getLastArg(options::OPT_target))
    TargetTriple = A->getValue();
  if (const Arg *A = Args.getLastArg(options::OPT_ccc_install_dir))
    Dir = InstalledDir = A->getValue();
  for (const Arg *A : Args.filtered(options::OPT_B)) {
    A->claim();
    PrefixDirs.push_back(A->getValue(0));
  }
  if (auto CompilerPathValue = llvm::sys::Process::GetEnv("COMPILER_PATH")) {
    llvm::StringRef CompilerPath = *CompilerPathValue;
    while (!CompilerPath.empty()) {
      std::pair<llvm::StringRef, llvm::StringRef> Split =
          CompilerPath.split(llvm::sys::EnvPathSeparator);
      PrefixDirs.push_back(std::string(Split.first));
      CompilerPath = Split.second;
    }
  }
  if (const Arg *A = Args.getLastArg(options::OPT__sysroot_EQ))
    SysRoot = A->getValue();
  if (const Arg *A = Args.getLastArg(options::OPT__dyld_prefix_EQ))
    DyldPrefix = A->getValue();

  if (const Arg *A = Args.getLastArg(options::OPT_resource_dir))
    ResourceDir = A->getValue();

  if (const Arg *A = Args.getLastArg(options::OPT_save_temps_EQ)) {
    SaveTemps = llvm::StringSwitch<SaveTempsMode>(A->getValue())
                    .Case("cwd", SaveTempsCwd)
                    .Case("obj", SaveTempsObj)
                    .Default(SaveTempsCwd);
  }

  setLTOMode(Args);

  if (Arg *A = Args.getLastArg(options::OPT_MJ))
    llvm::sys::fs::remove(A->getValue());

  std::unique_ptr<llvm::opt::InputArgList> UArgs =
      std::make_unique<InputArgList>(std::move(Args));

  // Perform the default argument translations.
  DerivedArgList *TranslatedArgs = TranslateInputArgs(*UArgs);

  // Owned by the host.
  llvm::StringRef RequestedTargetTriple = TargetTriple;
  if (const Arg *A = UArgs->getLastArg(options::OPT_target))
    RequestedTargetTriple = A->getValue();

  llvm::Triple ComputedTarget =
      computeTargetTriple(*this, TargetTriple, *UArgs);
  if (!isTargetArchSupported(ComputedTarget)) {
    if (ComputedTarget.getArch() == llvm::Triple::UnknownArch) {
      if (const Arg *A = UArgs->getLastArg(options::OPT_arch))
        Diag(diag::err_drv_invalid_arch_name) << A->getValue();
      else
        Diag(diag::err_drv_unsupported_target_arch)
            << ComputedTarget.getArchName();
    } else {
      Diag(diag::err_drv_unsupported_target_arch)
          << ComputedTarget.getArchName();
    }
    ContainsError = true;
  }
  if (!ContainsError) {
    llvm::StringRef CMakeTargetName;
    if (!isArchLinkedInLLVM(ComputedTarget, CMakeTargetName)) {
      Diag(diag::err_drv_target_backend_not_built)
          << ComputedTarget.str() << CMakeTargetName;
      ContainsError = true;
    }
  }

  const ToolChain &TC = getToolChain(*UArgs, ComputedTarget);

  // A common user mistake is specifying aarch64-none-eabi instead of
  // aarch64-none-elf.
  if (TC.getTriple().getOS() == llvm::Triple::UnknownOS &&
      TC.getTriple().getVendor() == llvm::Triple::UnknownVendor) {
    switch (TC.getTriple().getArch()) {
    case llvm::Triple::aarch64:
      if (TC.getTriple().getEnvironmentName().starts_with("eabi")) {
        Diag(diag::warn_target_unrecognized_env)
            << TargetTriple
            << (TC.getTriple().getArchName().str() + "-none-elf");
      }
      break;
    default:
      break;
    }
  }

  // The compilation takes ownership of Args.
  Compilation *C = new Compilation(*this, TC, UArgs.release(), TranslatedArgs,
                                   ContainsError);

  if (!ProcessImmediateArgs(*C))
    return C;

  // Construct the list of inputs.
  InputList Inputs;
  FormInputs(C->getDefaultToolChain(), *TranslatedArgs, Inputs);

  // Construct the list of abstract actions to perform for this compilation. On
  // MachO targets this uses the driver-driver and universal actions.
  if (TC.getTriple().isOSBinFormatMachO())
    FormUniversalActions(*C, C->getDefaultToolChain(), Inputs);
  else
    FormActions(*C, C->getArgs(), Inputs, C->getActions());

  if (CCCPrintPhases) {
    PrintActions(*C);
    return C;
  }

  GenerateJobs(*C);

  return C;
}

// ===----------------------------------------------------------------------===
// Crash diagnostics & error reporting
// ===----------------------------------------------------------------------===

namespace {
void printArgList(llvm::raw_ostream &OS, const llvm::opt::ArgList &Args) {
  llvm::opt::ArgStringList ASL;
  for (const auto *A : Args) {
    // Use user's original spelling of flags. For example, use
    // `/source-charset:utf-8` instead of `-finput-charset=utf-8` if the user
    // wrote the former.
    while (A->getAlias())
      A = A->getAlias();
    A->render(Args, ASL);
  }

  for (auto I = ASL.begin(), E = ASL.end(); I != E; ++I) {
    if (I != ASL.begin())
      OS << ' ';
    llvm::sys::printArg(OS, *I, true);
  }
  OS << '\n';
}
} // namespace

bool Driver::getCrashDiagnosticFile(llvm::StringRef ReproCrashFilename,
                                    llvm::SmallString<128> &CrashDiagDir) {
  using namespace llvm::sys;
  assert(llvm::Triple(llvm::sys::getProcessTriple()).isOSDarwin() &&
         "Only knows about .crash files on Darwin");

  // The .crash file can be found on at ~/Library/Logs/DiagnosticReports/
  // (or /Library/Logs/DiagnosticReports for root) and has the filename pattern
  // neverc-<VERSION>_<YYYY-MM-DD-HHMMSS>_<hostname>.crash.
  path::home_directory(CrashDiagDir);
  if (CrashDiagDir.starts_with("/var/root"))
    CrashDiagDir = "/";
  path::append(CrashDiagDir, "Library/Logs/DiagnosticReports");
  int PID =
#if LLVM_ON_UNIX
      getpid();
#else
      0;
#endif
  std::error_code EC;
  fs::file_status FileStatus;
  TimePoint<> LastAccessTime;
  llvm::SmallString<128> CrashFilePath;
  // Lookup the .crash files and get the one generated by a subprocess spawned
  // by this driver invocation.
  for (fs::directory_iterator File(CrashDiagDir, EC), FileEnd;
       File != FileEnd && !EC; File.increment(EC)) {
    llvm::StringRef FileName = path::filename(File->path());
    if (!FileName.starts_with(Name))
      continue;
    if (fs::status(File->path(), FileStatus))
      continue;
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> CrashFile =
        llvm::MemoryBuffer::getFile(File->path());
    if (!CrashFile)
      continue;
    // The first line should start with "Process:", otherwise this isn't a real
    // .crash file.
    llvm::StringRef Data = CrashFile.get()->getBuffer();
    if (!Data.starts_with("Process:"))
      continue;
    // Parse parent process pid line, e.g: "Parent Process: neverc [79141]"
    size_t ParentProcPos = Data.find("Parent Process:");
    if (ParentProcPos == llvm::StringRef::npos)
      continue;
    size_t LineEnd = Data.find_first_of("\n", ParentProcPos);
    if (LineEnd == llvm::StringRef::npos)
      continue;
    llvm::StringRef ParentProcess =
        Data.slice(ParentProcPos + 15, LineEnd).trim();
    int OpenBracket = -1, CloseBracket = -1;
    for (size_t i = 0, e = ParentProcess.size(); i < e; ++i) {
      if (ParentProcess[i] == '[')
        OpenBracket = i;
      if (ParentProcess[i] == ']')
        CloseBracket = i;
    }
    // Extract the parent process PID from the .crash file and check whether
    // it matches this driver invocation pid.
    int CrashPID;
    if (OpenBracket < 0 || CloseBracket < 0 ||
        ParentProcess.slice(OpenBracket + 1, CloseBracket)
            .getAsInteger(10, CrashPID) ||
        CrashPID != PID) {
      continue;
    }

    const auto FileAccessTime = FileStatus.getLastModificationTime();
    if (FileAccessTime > LastAccessTime) {
      CrashFilePath.assign(File->path());
      LastAccessTime = FileAccessTime;
    }
  }

  // If found, copy it over to the location of other reproducer files.
  if (!CrashFilePath.empty()) {
    EC = fs::copy_file(CrashFilePath, ReproCrashFilename);
    if (EC)
      return false;
    return true;
  }

  return false;
}

namespace {
const char BugReporMsg[] =
    "\n********************\n\n"
    "PLEASE ATTACH THE FOLLOWING FILES TO THE BUG REPORT:\n"
    "Preprocessed source(s) and associated run script(s) are located at:";
} // namespace

// When neverc crashes, produce diagnostic information including the fully
// preprocessed source file(s).  Request that the developer attach the
// diagnostic information to a bug report.
void Driver::generateCompilationDiagnostics(
    Compilation &C, const Command &FailingCommand,
    llvm::StringRef AdditionalInformation,
    CompilationDiagnosticReport *Report) {
  if (C.getArgs().hasArg(options::OPT_fno_crash_diagnostics))
    return;

  unsigned Level = 1;
  if (Arg *A = C.getArgs().getLastArg(options::OPT_fcrash_diagnostics_EQ)) {
    Level = llvm::StringSwitch<unsigned>(A->getValue())
                .Case("off", 0)
                .Case("compiler", 1)
                .Case("all", 2)
                .Default(1);
  }
  if (!Level)
    return;

  // Don't try to generate diagnostics for dsymutil jobs.
  if (FailingCommand.getCreator().isDsymutilJob())
    return;

  ArgStringList SavedTemps;
  if (FailingCommand.getCreator().isLinkJob()) {
    if (Level < 2)
      return;

    // If the linker crashed, we will re-run the same command with the input
    // it used to have. In that case we should not remove temp files in
    // initCompilationForDiagnostics yet. They will be added back and removed
    // later.
    SavedTemps = std::move(C.getTempFiles());
    assert(!C.getTempFiles().size());
  }

  // Print the version of the compiler.
  PrintVersion(C, llvm::errs());

  // Suppress driver output and emit preprocessor output to temp file.
  CCGenDiagnostics = true;

  // Save the original job command(s).
  Command Cmd = FailingCommand;

  // Keep track of whether we produce any errors while trying to produce
  // preprocessed sources.
  DiagnosticErrorTrap Trap(Diags);

  // Suppress tool output.
  C.initCompilationForDiagnostics();

  // Linker crash: no --reproduce support in the embedded linker.
  // Just emit the diagnostic note and bail out.
  Diag(neverc::diag::note_drv_command_failed_diag_msg) << BugReporMsg;
  return;

  // Construct the list of inputs.
  InputList Inputs;
  FormInputs(C.getDefaultToolChain(), C.getArgs(), Inputs);

  llvm::erase_if(Inputs, [&](const std::pair<types::ID, const Arg *> &Input) {
    if (types::getPreprocessedType(Input.first) == types::TY_INVALID)
      return true;
    if (!strcmp(Input.second->getValue(), "-")) {
      Diag(neverc::diag::note_drv_command_failed_diag_msg)
          << "Error generating preprocessed source(s) - "
             "ignoring input from stdin.";
      return true;
    }
    return false;
  });

  if (Inputs.empty()) {
    Diag(neverc::diag::note_drv_command_failed_diag_msg)
        << "Error generating preprocessed source(s) - "
           "no preprocessable inputs.";
    return;
  }

  // Don't attempt to generate preprocessed files if multiple -arch options are
  // used, unless they're all duplicates.
  llvm::StringSet<> ArchNames;
  for (const Arg *A : C.getArgs()) {
    if (A->getOption().matches(options::OPT_arch)) {
      llvm::StringRef ArchName = A->getValue();
      ArchNames.insert(ArchName);
    }
  }
  if (ArchNames.size() > 1) {
    Diag(neverc::diag::note_drv_command_failed_diag_msg)
        << "Error generating preprocessed source(s) - cannot generate "
           "preprocessed source with multiple -arch options.";
    return;
  }

  // Construct the list of abstract actions to perform for this compilation. On
  // Darwin OSes this uses the driver-driver and builds universal actions.
  const ToolChain &TC = C.getDefaultToolChain();
  if (TC.getTriple().isOSBinFormatMachO())
    FormUniversalActions(C, TC, Inputs);
  else
    FormActions(C, C.getArgs(), Inputs, C.getActions());

  GenerateJobs(C);

  // If there were errors building the compilation, quit now.
  if (Trap.hasErrorOccurred()) {
    Diag(neverc::diag::note_drv_command_failed_diag_msg)
        << "Error generating preprocessed source(s).";
    return;
  }

  // Generate preprocessed output.
  llvm::SmallVector<std::pair<int, const Command *>, 4> FailingCommands;
  C.ExecuteJobs(C.getJobs(), FailingCommands);

  // If any of the preprocessing commands failed, clean up and exit.
  if (!FailingCommands.empty()) {
    Diag(neverc::diag::note_drv_command_failed_diag_msg)
        << "Error generating preprocessed source(s).";
    return;
  }

  const ArgStringList &TempFiles = C.getTempFiles();
  if (TempFiles.empty()) {
    Diag(neverc::diag::note_drv_command_failed_diag_msg)
        << "Error generating preprocessed source(s).";
    return;
  }

  Diag(neverc::diag::note_drv_command_failed_diag_msg) << BugReporMsg;

  llvm::SmallString<128> VFS;
  llvm::SmallString<128> ReproCrashFilename;
  for (const char *TempFile : TempFiles) {
    Diag(neverc::diag::note_drv_command_failed_diag_msg) << TempFile;
    if (Report)
      Report->TemporaryFiles.push_back(TempFile);
    if (ReproCrashFilename.empty()) {
      ReproCrashFilename = TempFile;
      llvm::sys::path::replace_extension(ReproCrashFilename, ".crash");
    }
    if (llvm::StringRef(TempFile).ends_with(".cache")) {
      // In some cases (modules) we'll dump extra data to help with reproducing
      // the crash into a directory next to the output.
      VFS = llvm::sys::path::filename(TempFile);
      llvm::sys::path::append(VFS, "vfs", "vfs.yaml");
    }
  }

  for (const char *TempFile : SavedTemps)
    C.addTempFile(TempFile);

  // Assume associated files are based off of the first temporary file.
  CrashReportInfo CrashInfo(TempFiles[0], VFS);

  llvm::SmallString<128> Script(CrashInfo.Filename);
  llvm::sys::path::replace_extension(Script, "sh");
  std::error_code EC;
  llvm::raw_fd_ostream ScriptOS(Script, EC, llvm::sys::fs::CD_CreateNew,
                                llvm::sys::fs::FA_Write,
                                llvm::sys::fs::OF_Text);
  if (EC) {
    Diag(neverc::diag::note_drv_command_failed_diag_msg)
        << "Error generating run script: " << Script << " " << EC.message();
  } else {
    ScriptOS << "# Crash reproducer for " << getNeverCFullVersion() << "\n"
             << "# Driver args: ";
    printArgList(ScriptOS, C.getInputArgs());
    ScriptOS << "# Original command: ";
    Cmd.Print(ScriptOS, "\n", /*Quote=*/true);
    Cmd.Print(ScriptOS, "\n", /*Quote=*/true, &CrashInfo);
    if (!AdditionalInformation.empty())
      ScriptOS << "\n# Additional information: " << AdditionalInformation
               << "\n";
    if (Report)
      Report->TemporaryFiles.push_back(std::string(Script.str()));
    Diag(neverc::diag::note_drv_command_failed_diag_msg) << Script;
  }

  // On darwin, provide information about the .crash diagnostic report.
  if (llvm::Triple(llvm::sys::getProcessTriple()).isOSDarwin()) {
    llvm::SmallString<128> CrashDiagDir;
    if (getCrashDiagnosticFile(ReproCrashFilename, CrashDiagDir)) {
      Diag(neverc::diag::note_drv_command_failed_diag_msg)
          << ReproCrashFilename.str();
    } else { // Suggest a directory for the user to look for .crash files.
      llvm::sys::path::append(CrashDiagDir, Name);
      CrashDiagDir += "_<YYYY-MM-DD-HHMMSS>_<hostname>.crash";
      Diag(neverc::diag::note_drv_command_failed_diag_msg)
          << "Crash backtrace is located in";
      Diag(neverc::diag::note_drv_command_failed_diag_msg)
          << CrashDiagDir.str();
      Diag(neverc::diag::note_drv_command_failed_diag_msg)
          << "(choose the .crash file that corresponds to your crash)";
    }
  }

  Diag(neverc::diag::note_drv_command_failed_diag_msg)
      << "\n\n********************";
}

// ===----------------------------------------------------------------------===
// Compilation execution
// ===----------------------------------------------------------------------===

void Driver::setUpResponseFiles(Compilation &C, Command &Cmd) {
  // In-process commands (FrontendCommand, LinkerCommand) read getArguments()
  // directly and never consult the response file — skip the temp file
  // creation and the commandLineFitsWithinSystemLimits() syscall entirely.
  if (Cmd.getKind() == Command::CK_FrontendCommand ||
      Cmd.getKind() == Command::CK_LinkerCommand)
    return;

  // Since commandLineFitsWithinSystemLimits() may underestimate system's
  // capacity if the tool does not support response files, there is a chance/
  // that things will just work without a response file, so we silently just
  // skip it.
  if (Cmd.getResponseFileSupport().ResponseKind ==
          ResponseFileSupport::RF_None ||
      llvm::sys::commandLineFitsWithinSystemLimits(Cmd.getExecutable(),
                                                   Cmd.getArguments()))
    return;

  std::string TmpName = GetTemporaryPath("response", "txt");
  Cmd.setResponseFile(C.addTempFile(C.getArgs().MakeArgString(TmpName)));
}

int Driver::ExecuteCompilation(
    Compilation &C,
    llvm::SmallVectorImpl<std::pair<int, const Command *>> &FailingCommands) {
  if (C.getArgs().hasArg(options::OPT_fdriver_only)) {
    if (C.getArgs().hasArg(options::OPT_v))
      C.getJobs().Print(llvm::errs(), "\n", true);

    C.ExecuteJobs(C.getJobs(), FailingCommands, /*LogOnly=*/true);

    // If there were errors building the compilation, quit now.
    if (!FailingCommands.empty() || Diags.hasErrorOccurred())
      return 1;

    return 0;
  }

  // Just print if -### was present.
  if (C.getArgs().hasArg(options::OPT__HASH_HASH_HASH)) {
    C.getJobs().Print(llvm::errs(), "\n", true);
    return Diags.hasErrorOccurred() ? 1 : 0;
  }

  // If there were errors building the compilation, quit now.
  if (Diags.hasErrorOccurred())
    return 1;

  for (auto &Job : C.getJobs())
    setUpResponseFiles(C, Job);

  C.ExecuteJobs(C.getJobs(), FailingCommands);

  // If the command succeeded, we are done.
  if (FailingCommands.empty())
    return 0;

  // Otherwise, remove result files and print extra information about abnormal
  // failures.
  int Res = 0;
  for (const auto &CmdPair : FailingCommands) {
    int CommandRes = CmdPair.first;
    const Command *FailingCommand = CmdPair.second;

    if (!isSaveTempsEnabled()) {
      const JobAction *JA = cast<JobAction>(&FailingCommand->getSource());
      C.CleanupFileMap(C.getResultFiles(), JA, true);

      // Failure result files are valid unless we crashed.
      if (CommandRes < 0)
        C.CleanupFileMap(C.getFailureResultFiles(), JA, true);
    }

    // llvm/lib/Support/*/Signals.inc will exit with a special return code
    // for SIGPIPE. Do not print diagnostics for this case.
    if (CommandRes == EX_IOERR) {
      Res = CommandRes;
      continue;
    }

    // Print extra information about abnormal failures, if possible.
    //
    // This is ad-hoc, but we don't want to be excessively noisy. If the result
    // status was 1, assume the command failed normally. In particular, if it
    // was the compiler then assume it gave a reasonable error code. Failures
    // in other tools are less common, and they generally have worse
    // diagnostics, so always print the diagnostic there.
    const Tool &FailingTool = FailingCommand->getCreator();

    if (!FailingCommand->getCreator().hasGoodDiagnostics() || CommandRes != 1) {
      if (CommandRes < 0)
        Diag(neverc::diag::err_drv_command_signalled)
            << FailingTool.getShortName();
      else
        Diag(neverc::diag::err_drv_command_failed)
            << FailingTool.getShortName() << CommandRes;
    }
  }
  return Res;
}

// ===----------------------------------------------------------------------===
// Help, version & autocompletions
// ===----------------------------------------------------------------------===

void Driver::PrintHelp(bool ShowHidden) const {
  llvm::opt::Visibility VisibilityMask = getOptionVisibilityMask();

  std::string Usage = llvm::formatv("{0} [options] file...", Name).str();
  getOpts().printHelp(llvm::outs(), Usage.c_str(), DriverTitle.c_str(),
                      ShowHidden, /*ShowAllAliases=*/false, VisibilityMask);
}

void Driver::PrintVersion(const Compilation &C, llvm::raw_ostream &OS) const {
  OS << getNeverCFullVersion() << '\n';
  const ToolChain &TC = C.getDefaultToolChain();
  OS << "Target: " << TC.getTripleString() << '\n';

  // Print the threading model.
  if (Arg *A = C.getArgs().getLastArg(options::OPT_mthread_model)) {
    // Don't print if the ToolChain would have barfed on it already
    if (TC.isThreadModelSupported(A->getValue()))
      OS << "Thread model: " << A->getValue();
  } else
    OS << "Thread model: " << TC.getThreadModel();
  OS << '\n';

  // Print out the install directory.
  OS << "InstalledDir: " << InstalledDir << '\n';

  // If configuration files were used, print their paths.
  for (auto ConfigFile : ConfigFiles)
    OS << "Configuration file: " << ConfigFile << '\n';
}

namespace {
void printDiagCategories(llvm::raw_ostream &OS) {
  // Skip the empty category.
  for (unsigned i = 1, max = DiagnosticIDs::getNumberOfCategories(); i != max;
       ++i)
    OS << i << ',' << DiagnosticIDs::getCategoryNameFromID(i) << '\n';
}
} // namespace

void Driver::ProcessAutocompletions(llvm::StringRef PassedFlags) const {
  if (PassedFlags == "")
    return;
  // Print out all options that start with a given argument. This is used for
  // shell autocompletion.
  std::vector<std::string> SuggestedCompletions;
  std::vector<std::string> Flags;

  llvm::opt::Visibility VisibilityMask(options::NeverCOption);

  // Distinguish "--autocomplete=-someflag" and "--autocomplete=-someflag,"
  // because the latter indicates that the user put space before pushing tab
  // which should end up in a file completion.
  const bool HasSpace = PassedFlags.ends_with(",");

  // Parse PassedFlags by "," as all the command-line flags are passed to this
  // function separated by ","
  llvm::StringRef TargetFlags = PassedFlags;
  while (TargetFlags != "") {
    llvm::StringRef CurFlag;
    std::tie(CurFlag, TargetFlags) = TargetFlags.split(",");
    Flags.push_back(std::string(CurFlag));
  }

  // NeverC compiles and links in-process; all options share NeverCOption
  // visibility.
  const llvm::opt::OptTable &Opts = getOpts();
  llvm::StringRef Cur;
  Cur = Flags.at(Flags.size() - 1);
  llvm::StringRef Prev;
  if (Flags.size() >= 2) {
    Prev = Flags.at(Flags.size() - 2);
    SuggestedCompletions = Opts.suggestValueCompletions(Prev, Cur);
  }

  if (SuggestedCompletions.empty())
    SuggestedCompletions = Opts.suggestValueCompletions(Cur, "");

  // If Flags were empty, it means the user typed `neverc [tab]` where we should
  // list all possible flags. If there was no value completion and the user
  // pressed tab after a space, we should fall back to a file completion.
  // We're printing a newline to be consistent with what we print at the end of
  // this function.
  if (SuggestedCompletions.empty() && HasSpace && !Flags.empty()) {
    llvm::outs() << '\n';
    return;
  }

  // When flag ends with '=' and there was no value completion, return empty
  // string and fall back to the file autocompletion.
  if (SuggestedCompletions.empty() && !Cur.ends_with("=")) {
    // If the flag is in the form of "--autocomplete=-foo",
    // we were requested to print out all option names that start with "-foo".
    // For example, "--autocomplete=-fsyn" is expanded to "-fsyntax-only".
    SuggestedCompletions = Opts.findByPrefix(
        Cur, VisibilityMask,
        /*DisableFlags=*/options::Unsupported | options::Ignored);

    for (llvm::StringRef S : DiagnosticIDs::getDiagnosticFlags())
      if (S.starts_with(Cur))
        SuggestedCompletions.push_back(std::string(S));
  }

  // Sort the autocomplete candidates so that shells print them out in a
  // deterministic order. We could sort in any way, but we chose
  // case-insensitive sorting for consistency with the -help option
  // which prints out options in the case-insensitive alphabetical order.
  llvm::sort(SuggestedCompletions, [](llvm::StringRef A, llvm::StringRef B) {
    if (int X = A.compare_insensitive(B))
      return X < 0;
    return A.compare(B) > 0;
  });

  llvm::outs() << llvm::join(SuggestedCompletions, "\n") << '\n';
}

bool Driver::ProcessImmediateArgs(const Compilation &C) {
  // The order these options are handled in gcc is all over the place, but we
  // don't expect inconsistencies w.r.t. that to matter in practice.

  if (C.getArgs().hasArg(options::OPT_dumpmachine)) {
    llvm::outs() << C.getDefaultToolChain().getTripleString() << '\n';
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_dumpversion)) {
    // Since -dumpversion is only implemented for pedantic GCC compatibility, we
    // return an answer which matches our definition of __VERSION__.
    llvm::outs() << NEVERC_VERSION_STRING << "\n";
    return false;
  }

  if (C.getArgs().hasArg(options::OPT__print_diagnostic_categories)) {
    printDiagCategories(llvm::outs());
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_help) ||
      C.getArgs().hasArg(options::OPT__help_hidden)) {
    PrintHelp(C.getArgs().hasArg(options::OPT__help_hidden));
    return false;
  }

  if (C.getArgs().hasArg(options::OPT__version)) {
    // Follow gcc behavior and use stdout for --version and stderr for -v.
    PrintVersion(C, llvm::outs());
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_v) ||
      C.getArgs().hasArg(options::OPT__HASH_HASH_HASH) ||
      C.getArgs().hasArg(options::OPT_print_supported_cpus)) {
    PrintVersion(C, llvm::errs());
    SuppressMissingInputWarning = true;
  }

  if (C.getArgs().hasArg(options::OPT_v)) {
    if (!SystemConfigDir.empty())
      llvm::errs() << "System configuration file directory: " << SystemConfigDir
                   << "\n";
    if (!UserConfigDir.empty())
      llvm::errs() << "User configuration file directory: " << UserConfigDir
                   << "\n";
  }

  const ToolChain &TC = C.getDefaultToolChain();

  if (C.getArgs().hasArg(options::OPT_v))
    TC.printVerboseInfo(llvm::errs());

  if (C.getArgs().hasArg(options::OPT_print_resource_dir)) {
    llvm::outs() << ResourceDir << '\n';
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_search_dirs)) {
    llvm::outs() << "programs: =";
    bool separator = false;
    // Print -B and COMPILER_PATH.
    for (const std::string &Path : PrefixDirs) {
      if (separator)
        llvm::outs() << llvm::sys::EnvPathSeparator;
      llvm::outs() << Path;
      separator = true;
    }
    for (const std::string &Path : TC.getProgramPaths()) {
      if (separator)
        llvm::outs() << llvm::sys::EnvPathSeparator;
      llvm::outs() << Path;
      separator = true;
    }
    llvm::outs() << "\n";
    llvm::outs() << "libraries: =" << ResourceDir;

    llvm::StringRef sysroot = C.getSysRoot();

    for (const std::string &Path : TC.getFilePaths()) {
      // Always print a separator. ResourceDir was the first item shown.
      llvm::outs() << llvm::sys::EnvPathSeparator;
      if (Path[0] == '=')
        llvm::outs() << sysroot << Path.substr(1);
      else
        llvm::outs() << Path;
    }
    llvm::outs() << "\n";
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_runtime_dir)) {
    if (std::optional<std::string> RuntimePath = TC.getRuntimePath())
      llvm::outs() << *RuntimePath << '\n';
    else
      llvm::outs() << TC.getCompilerRTPath() << '\n';
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_diagnostic_options)) {
    std::vector<std::string> Flags = DiagnosticIDs::getDiagnosticFlags();
    for (std::size_t I = 0; I != Flags.size(); I += 2)
      llvm::outs() << "  " << Flags[I] << "\n  " << Flags[I + 1] << "\n\n";
    return false;
  }

  if (Arg *A = C.getArgs().getLastArg(options::OPT_print_file_name_EQ)) {
    llvm::outs() << GetFilePath(A->getValue(), TC) << "\n";
    return false;
  }

  if (Arg *A = C.getArgs().getLastArg(options::OPT_print_prog_name_EQ)) {
    llvm::StringRef ProgName = A->getValue();

    // Null program name cannot have a path.
    if (!ProgName.empty())
      llvm::outs() << GetProgramPath(ProgName, TC);

    llvm::outs() << "\n";
    return false;
  }

  if (Arg *A = C.getArgs().getLastArg(options::OPT_autocomplete)) {
    llvm::StringRef PassedFlags = A->getValue();
    ProcessAutocompletions(PassedFlags);
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_libgcc_file_name)) {
    ToolChain::RuntimeLibType RLT = TC.GetRuntimeLibType(C.getArgs());
    const llvm::Triple Triple(TC.ComputeEffectiveTriple(C.getArgs()));
    RegisterEffectiveTriple TripleRAII(TC, Triple);
    switch (RLT) {
    case ToolChain::RLT_CompilerRT:
      llvm::outs() << TC.getCompilerRT(C.getArgs(), "builtins") << "\n";
      break;
    case ToolChain::RLT_Libgcc:
      llvm::outs() << GetFilePath("libgcc.a", TC) << "\n";
      break;
    }
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_multi_lib)) {
    for (const Multilib &Multilib : TC.getMultilibs())
      llvm::outs() << Multilib << "\n";
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_multi_flags)) {
    Multilib::flags_list ArgFlags = TC.getMultilibFlags(C.getArgs());
    llvm::StringSet<> ExpandedFlags = TC.getMultilibs().expandFlags(ArgFlags);
    std::set<llvm::StringRef> SortedFlags;
    for (const auto &FlagEntry : ExpandedFlags)
      SortedFlags.insert(FlagEntry.getKey());
    for (auto Flag : SortedFlags)
      llvm::outs() << Flag << '\n';
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_multi_directory)) {
    for (const Multilib &Multilib : TC.getSelectedMultilibs()) {
      if (Multilib.gccSuffix().empty())
        llvm::outs() << ".\n";
      else {
        llvm::StringRef Suffix(Multilib.gccSuffix());
        assert(Suffix.front() == '/');
        llvm::outs() << Suffix.substr(1) << "\n";
      }
    }
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_target_triple)) {
    llvm::outs() << TC.getTripleString() << "\n";
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_effective_triple)) {
    const llvm::Triple Triple(TC.ComputeEffectiveTriple(C.getArgs()));
    llvm::outs() << Triple.getTriple() << "\n";
    return false;
  }

  if (C.getArgs().hasArg(options::OPT_print_targets)) {
    llvm::TargetRegistry::printRegisteredTargetsForVersion(llvm::outs());
    return false;
  }

  return true;
}

enum {
  TopLevelAction = 0,
  HeadSibAction = 1,
  OtherSibAction = 2,
};

// Display an action graph human-readably.  Action A is the "sink" node
// and latest-occuring action. Traversal is in pre-order, visiting the
// inputs to each action before printing the action itself.
namespace {
unsigned dumpActionNode(const Compilation &C, Action *A,
                        std::map<Action *, unsigned> &Ids,
                        llvm::Twine Indent = {}, int Kind = TopLevelAction) {
  if (auto It = Ids.find(A); It != Ids.end())
    return It->second;

  std::string str;
  llvm::raw_string_ostream os(str);

  auto getSibIndent = [](int K) -> llvm::Twine {
    return (K == HeadSibAction) ? "   " : (K == OtherSibAction) ? "|  " : "";
  };

  llvm::Twine SibIndent = Indent + getSibIndent(Kind);
  int SibKind = HeadSibAction;
  os << Action::getClassName(A->getKind()) << ", ";
  if (InputAction *IA = dyn_cast<InputAction>(A)) {
    os << "\"" << IA->getInputArg().getValue() << "\"";
  } else if (BindArchAction *BIA = dyn_cast<BindArchAction>(A)) {
    os << '"' << BIA->getArchName() << '"' << ", {"
       << dumpActionNode(C, *BIA->input_begin(), Ids, SibIndent, SibKind)
       << "}";
  } else {
    const ActionList *AL = &A->getInputs();

    if (AL->size()) {
      const char *Prefix = "{";
      for (Action *PreRequisite : *AL) {
        os << Prefix
           << dumpActionNode(C, PreRequisite, Ids, SibIndent, SibKind);
        Prefix = ", ";
        SibKind = OtherSibAction;
      }
      os << "}";
    } else
      os << "{}";
  }

  auto getSelfIndent = [](int K) -> llvm::Twine {
    return (K == HeadSibAction) ? "+- " : (K == OtherSibAction) ? "|- " : "";
  };

  unsigned Id = Ids.size();
  Ids[A] = Id;
  llvm::errs() << Indent + getSelfIndent(Kind) << Id << ": " << os.str() << ", "
               << types::getTypeName(A->getType()) << "\n";

  return Id;
}
} // namespace

// ===----------------------------------------------------------------------===
// Action & input construction
// ===----------------------------------------------------------------------===

void Driver::PrintActions(const Compilation &C) const {
  std::map<Action *, unsigned> Ids;
  for (Action *A : C.getActions())
    dumpActionNode(C, A, Ids);
}

namespace {
bool containsCompileOrAssemble(const Action *A) {
  if (isa<CompileJobAction>(A) || isa<BackendJobAction>(A) ||
      isa<AssembleJobAction>(A))
    return true;

  return llvm::any_of(A->inputs(), containsCompileOrAssemble);
}
} // namespace

void Driver::FormUniversalActions(Compilation &C, const ToolChain &TC,
                                  const InputList &BAInputs) const {
  DerivedArgList &Args = C.getArgs();
  ActionList &Actions = C.getActions();
  llvm::PrettyStackTraceString CrashInfo("Building universal build actions");
  // Collect the list of architectures. Duplicates are allowed, but should only
  // be handled once (in the order seen).
  llvm::StringSet<> ArchNames;
  llvm::SmallVector<const char *, 4> Archs;
  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT_arch)) {
      // Validate the option here; we don't save the type here because its
      // particular spelling may participate in other driver choices.
      llvm::Triple::ArchType Arch =
          tools::darwin::getArchTypeForMachOArchName(A->getValue());
      if (Arch == llvm::Triple::UnknownArch) {
        Diag(neverc::diag::err_drv_invalid_arch_name) << A->getAsString(Args);
        continue;
      }

      A->claim();
      if (ArchNames.insert(A->getValue()).second)
        Archs.push_back(A->getValue());
    }
  }

  // When there is no explicit arch for this platform, make sure we still bind
  // the architecture (to the default) so that -Xarch_ is handled correctly.
  if (!Archs.size())
    Archs.push_back(Args.MakeArgString(TC.getDefaultUniversalArchName()));

  ActionList SingleActions;
  FormActions(C, Args, BAInputs, SingleActions);

  // Add in arch bindings for every top level action, as well as lipo and
  // dsymutil steps if needed.
  for (Action *Act : SingleActions) {
    // Make sure we can lipo this kind of output. If not (and it is an actual
    // output) then we disallow, since we can't create an output file with the
    // right name without overwriting it. We could remove this oddity by just
    // changing the output names to include the arch, which would also fix
    // -save-temps. Compatibility wins for now.

    if (Archs.size() > 1 && !types::canLipoType(Act->getType()))
      Diag(neverc::diag::err_drv_invalid_output_with_multiple_archs)
          << types::getTypeName(Act->getType());

    ActionList Inputs;
    for (unsigned i = 0, e = Archs.size(); i != e; ++i)
      Inputs.push_back(C.MakeAction<BindArchAction>(Act, Archs[i]));

    // Lipo if necessary, we do it this way because we need to set the arch flag
    // so that -Xarch_ gets overwritten.
    if (Inputs.size() == 1 || Act->getType() == types::TY_Nothing)
      Actions.append(Inputs.begin(), Inputs.end());
    else
      Actions.push_back(C.MakeAction<LipoJobAction>(Inputs, Act->getType()));

    Arg *A = Args.getLastArg(options::OPT_g_Group);
    bool enablesDebugInfo = A && !A->getOption().matches(options::OPT_g0);
    if ((enablesDebugInfo || willEmitRemarks(Args)) &&
        containsCompileOrAssemble(Actions.back())) {

      // Skip dsymutil when the integrated compiler+linker pipeline keeps LTO
      // intermediates in memory.  dsymutil needs the original .o files on disk
      // to build the .dSYM bundle; with in-memory bitcode they don't exist.
      // Debug info is still embedded directly in the output binary's DWARF
      // sections by the linker.
      bool skipDsymutil = isUsingLTO() && !isSaveTempsEnabled();
      if (Act->getType() == types::TY_Image && !skipDsymutil) {
        ActionList Inputs;
        Inputs.push_back(Actions.back());
        Actions.pop_back();
        Actions.push_back(
            C.MakeAction<DsymutilJobAction>(Inputs, types::TY_dSYM));
      }
    }
  }
}

bool Driver::DiagnoseInputExistence(const DerivedArgList &Args,
                                    llvm::StringRef Value, types::ID Ty,
                                    bool TypoCorrect) const {
  if (!getCheckInputsExist())
    return true;

  // stdin always exists.
  if (Value == "-")
    return true;

  if (getVFS().exists(Value))
    return true;

  if (TypoCorrect) {
    // Check if the filename is a typo for an option flag. OptTable thinks
    // that all args that are not known options and that start with / are
    // filenames, but e.g. `/diagnostic:caret` is more likely a typo for
    // the option `/diagnostics:caret` than a reference to a file in the root
    // directory.
    std::string Nearest;
    if (getOpts().findNearest(Value, Nearest, getOptionVisibilityMask()) <= 1) {
      Diag(neverc::diag::err_drv_no_such_file_with_suggestion)
          << Value << Nearest;
      return false;
    }
  }

  // Don't error on apparently non-existent linker inputs on MSVC targets,
  // because they can be influenced by linker flags the driver might not
  // understand (e.g. /libpath:). Rely on the linker to diagnose missing
  // inputs instead.
  Diag(neverc::diag::err_drv_no_such_file) << Value;
  return false;
}

// Construct a the list of inputs and their types.
void Driver::FormInputs(const ToolChain &TC, DerivedArgList &Args,
                        InputList &Inputs) const {
  // Track the current user specified (-x) input. We also explicitly track the
  // argument used to set the type; we only want to claim the type when we
  // actually use it, so we warn about unused -x arguments.
  types::ID InputType = types::TY_Nothing;
  Arg *InputTypeArg = nullptr;

  // Warn -x after last input file has no effect
  {
    Arg *LastXArg = Args.getLastArgNoClaim(options::OPT_x);
    Arg *LastInputArg = Args.getLastArgNoClaim(options::OPT_INPUT);
    if (LastXArg && LastInputArg &&
        LastInputArg->getIndex() < LastXArg->getIndex())
      Diag(neverc::diag::warn_drv_unused_x) << LastXArg->getValue();
  }

  for (Arg *A : Args) {
    if (A->getOption().getKind() == Option::InputClass) {
      const char *Value = A->getValue();
      types::ID Ty = types::TY_INVALID;

      // Infer the input type if necessary.
      if (InputType == types::TY_Nothing) {
        // If there was an explicit arg for this, claim it.
        if (InputTypeArg)
          InputTypeArg->claim();

        // stdin must be handled specially.
        if (memcmp(Value, "-", 2) == 0) {
          assert(!CCGenDiagnostics && "stdin produces no crash reproducer");
          if (!Args.hasArgNoClaim(options::OPT_E))
            Diag(neverc::diag::err_drv_unknown_stdin_type);
          Ty = types::TY_C;
        } else {
          // Otherwise lookup by extension.
          // Fallback is C if CCGenDiagnostics, otherwise Object.
          if (const char *Ext = strrchr(Value, '.'))
            Ty = TC.LookupTypeForExtension(Ext + 1);

          if (Ty == types::TY_INVALID) {
            if (CCGenDiagnostics)
              Ty = types::TY_C;
            else
              Ty = types::TY_Object;
          }
        }

      } else {
        assert(InputTypeArg && "InputType set w/o InputTypeArg");
        if (Ty == types::TY_INVALID) {
          Ty = InputType;
          InputTypeArg->claim();
        }
      }

      if (DiagnoseInputExistence(Args, Value, Ty, /*TypoCorrect=*/true))
        Inputs.push_back(std::make_pair(Ty, A));

    } else if (A->getOption().hasFlag(options::LinkerInput)) {
      // Just treat as object type, we could make a special type for this if
      // necessary.
      Inputs.push_back(std::make_pair(types::TY_Object, A));

    } else if (A->getOption().matches(options::OPT_x)) {
      InputTypeArg = A;
      llvm::StringRef Val = A->getValue();
      InputType = types::lookupTypeForTypeSpecifier(Val.data());
      A->claim();

      if (!InputType) {
        Diag(neverc::diag::err_drv_unknown_language) << Val;
        // Do not leave a stale -x type: TY_Object would mis-type the next
        // input (e.g. .c as linker input) and produce spurious unused-arg
        // warnings after this error.
        InputType = types::TY_Nothing;
        InputTypeArg = nullptr;
      }
    } else if (A->getOption().getID() == options::OPT_U) {
      assert(A->getNumValues() == 1 && "The /U option has one value.");
      llvm::StringRef Val = A->getValue(0);
      if (Val.find_first_of("/\\") != llvm::StringRef::npos) {
        // Warn about e.g. "/Users/me/myfile.c".
        Diag(diag::warn_slash_u_filename) << Val;
        Diag(diag::note_use_dashdash);
      }
    }
  }
}

void Driver::handleArguments(Compilation &C, DerivedArgList &Args,
                             const InputList &Inputs,
                             ActionList &Actions) const {
  Arg *FinalPhaseArg;
  phases::ID FinalPhase = getFinalPhase(Args, &FinalPhaseArg);

  // NeverC always links in-process, so -fuse-ld=<name> has no effect.
  // Claim it silently: Kbuild and many build systems pass -fuse-ld=lld,
  // and an unclaimed flag triggers -Wunused-command-line-argument / -Werror.
  Args.ClaimAllArgs(options::OPT_fuse_ld_EQ);

  if (FinalPhase == phases::Link) {
    if (Args.hasArg(options::OPT_emit_llvm))
      Diag(neverc::diag::err_drv_emit_llvm_link);
    // If -dumpdir is not specified, give a default prefix derived from the link
    // output filename. For example, `neverc -g -gsplit-dwarf a.c -o x` passes
    // `-dumpdir x-` to the frontend. If -o is unspecified, use
    // stem(getDefaultImageName()) (usually stem("a.out") = "a").
    if (!Args.hasArg(options::OPT_dumpdir)) {
      Arg *FinalOutput = Args.getLastArg(options::OPT_o);
      Arg *Arg = Args.MakeSeparateArg(
          nullptr, getOpts().getOption(options::OPT_dumpdir),
          Args.MakeArgString(
              (FinalOutput ? FinalOutput->getValue()
                           : llvm::sys::path::stem(getDefaultImageName())) +
              "-"));
      Arg->claim();
      Args.append(Arg);
    }
  }

  unsigned LastPLSize = 0;
  for (auto &I : Inputs) {
    types::ID InputType = I.first;
    const Arg *InputArg = I.second;

    auto PL = types::getCompilationPhases(InputType);
    LastPLSize = PL.size();

    // If the first step comes after the final phase we are doing as part of
    // this compilation, warn the user about it.
    phases::ID InitialPhase = PL[0];
    if (InitialPhase > FinalPhase) {
      if (InputArg->isClaimed())
        continue;

      // Claim here to avoid the more general unused warning.
      InputArg->claim();

      // Suppress all unused style warnings with -Qunused-arguments
      if (Args.hasArg(options::OPT_Qunused_arguments))
        continue;

      // Special case '-E' warning on a previously preprocessed file to make
      // more sense.
      if (InitialPhase == phases::Compile &&
          (Args.getLastArg(options::OPT_msvc_preprocess_no_linemarkers,
                           options::OPT_msvc_preprocess_to_file) ||
           Args.getLastArg(options::OPT_E) ||
           Args.getLastArg(options::OPT_M, options::OPT_MM)) &&
          getPreprocessedType(InputType) == types::TY_INVALID)
        Diag(neverc::diag::warn_drv_preprocessed_input_file_unused)
            << InputArg->getAsString(Args) << !!FinalPhaseArg
            << (FinalPhaseArg ? FinalPhaseArg->getOption().getName() : "");
      else
        Diag(neverc::diag::warn_drv_input_file_unused)
            << InputArg->getAsString(Args) << getPhaseName(InitialPhase)
            << !!FinalPhaseArg
            << (FinalPhaseArg ? FinalPhaseArg->getOption().getName() : "");
      continue;
    }
  }

  // If we are linking, claim any options which are obviously only used for
  // compilation.
  if (FinalPhase == phases::Link && LastPLSize == 1) {
    Args.ClaimAllArgs(options::OPT_CompileOnly_Group);
  }
}

void Driver::FormActions(Compilation &C, DerivedArgList &Args,
                         const InputList &Inputs, ActionList &Actions) const {
  llvm::PrettyStackTraceString CrashInfo("Building compilation actions");

  if (!SuppressMissingInputWarning && Inputs.empty()) {
    Diag(neverc::diag::err_drv_no_input_files);
    return;
  }

  // Diagnose misuse of /Fo.
  if (Arg *A = Args.getLastArg(options::OPT_msvc_obj_output)) {
    llvm::StringRef V = A->getValue();
    if (Inputs.size() > 1 && !V.empty() &&
        !llvm::sys::path::is_separator(V.back())) {
      // Check whether /Fo tries to name an output file for multiple inputs.
      Diag(neverc::diag::err_drv_out_file_argument_with_multiple_sources)
          << A->getSpelling() << V;
      Args.eraseArg(options::OPT_msvc_obj_output);
    }
  }

  // Diagnose misuse of /Fa.
  if (Arg *A = Args.getLastArg(options::OPT_msvc_asm_output)) {
    llvm::StringRef V = A->getValue();
    if (Inputs.size() > 1 && !V.empty() &&
        !llvm::sys::path::is_separator(V.back())) {
      // Check whether /Fa tries to name an asm file for multiple inputs.
      Diag(neverc::diag::err_drv_out_file_argument_with_multiple_sources)
          << A->getSpelling() << V;
      Args.eraseArg(options::OPT_msvc_asm_output);
    }
  }

  // Diagnose misuse of /o.
  if (Arg *A = Args.getLastArg(options::OPT_o)) {
    if (A->getValue()[0] == '\0') {
      // It has to have a value.
      Diag(neverc::diag::err_drv_missing_argument) << A->getSpelling() << 1;
      Args.eraseArg(options::OPT_o);
    }
  }

  handleArguments(C, Args, Inputs, Actions);

  ActionList LinkerInputs;

  for (auto &I : Inputs) {
    types::ID InputType = I.first;
    const Arg *InputArg = I.second;

    auto PL = types::getCompilationPhases(*this, Args, InputType);
    if (PL.empty())
      continue;

    Action *Current = C.MakeAction<InputAction>(*InputArg, InputType);

    for (phases::ID Phase : PL) {
      if (Phase == phases::Link) {
        assert(Phase == PL.back() && "linking must be final compilation step.");
        LinkerInputs.push_back(Current);
        Current = nullptr;
        break;
      }

      Action *NewCurrent = ConstructPhaseAction(C, Args, Phase, Current);

      if (NewCurrent == Current)
        continue;

      Current = NewCurrent;

      if (Current->getType() == types::TY_Nothing)
        break;
    }

    if (Current)
      Actions.push_back(Current);
  }

  if (!LinkerInputs.empty()) {
    Action *LA;
    if (ShouldEmitStaticLibrary(Args)) {
      LA = C.MakeAction<StaticLibJobAction>(LinkerInputs, types::TY_Image);
    } else {
      LA = C.MakeAction<LinkJobAction>(LinkerInputs, types::TY_Image);
    }
    Actions.push_back(LA);
  }

  if (Arg *A = Args.getLastArg(options::OPT_print_supported_cpus)) {
    // If --print-supported-cpus, -mcpu=? or -mtune=? is specified, build a
    // custom Compile phase that prints out supported cpu models and quits.
    Actions.clear();
    Action *InputAc = C.MakeAction<InputAction>(*A, types::TY_C);
    Actions.push_back(
        C.MakeAction<CompileJobAction>(InputAc, types::TY_Nothing));
    for (auto &I : Inputs)
      I.second->claim();
  }
}

Action *Driver::ConstructPhaseAction(Compilation &C, const ArgList &Args,
                                     phases::ID Phase, Action *Input) const {
  llvm::PrettyStackTraceString CrashInfo("Constructing phase actions");

  // Some types skip the assembler phase (e.g., llvm-bc), but we can't
  // encode this in the steps because the intermediate type depends on
  // arguments. Just special case here.
  if (Phase == phases::Assemble && Input->getType() != types::TY_PP_Asm)
    return Input;

  switch (Phase) {
  case phases::Link:
    llvm_unreachable("link action invalid here.");
  case phases::Preprocess: {
    types::ID OutputTy;
    // -M and -MM specify the dependency file name by altering the output type,
    // -if -MD and -MMD are not specified.
    if (Args.hasArg(options::OPT_M, options::OPT_MM) &&
        !Args.hasArg(options::OPT_MD, options::OPT_MMD)) {
      OutputTy = types::TY_Dependencies;
    } else {
      OutputTy = Input->getType();
      // For these cases, the preprocessor is only translating forms, the Output
      // still needs preprocessing.
      if (!Args.hasFlag(options::OPT_fdirectives_only,
                        options::OPT_fno_directives_only, false) &&
          !CCGenDiagnostics)
        OutputTy = types::getPreprocessedType(OutputTy);
      assert(OutputTy != types::TY_INVALID &&
             "Cannot preprocess this input type!");
    }
    return C.MakeAction<PreprocessJobAction>(Input, OutputTy);
  }
  case phases::Compile: {
    if (Args.hasArg(options::OPT_fsyntax_only))
      return C.MakeAction<CompileJobAction>(Input, types::TY_Nothing);
    return C.MakeAction<CompileJobAction>(Input, types::TY_LLVM_BC);
  }
  case phases::Backend: {
    if (isUsingLTO()) {
      types::ID Output;
      if (Args.hasArg(options::OPT_S))
        Output = types::TY_LTO_IR;
      else
        Output = types::TY_LTO_BC;
      return C.MakeAction<BackendJobAction>(Input, Output);
    }
    if (Args.hasArg(options::OPT_emit_llvm)) {
      types::ID Output =
          Args.hasArg(options::OPT_S) ? types::TY_LLVM_IR : types::TY_LLVM_BC;
      return C.MakeAction<BackendJobAction>(Input, Output);
    }
    return C.MakeAction<BackendJobAction>(Input, types::TY_PP_Asm);
  }
  case phases::Assemble:
    return C.MakeAction<AssembleJobAction>(std::move(Input), types::TY_Object);
  }

  llvm_unreachable("invalid phase in ConstructPhaseAction");
}

// ===----------------------------------------------------------------------===
// Job generation
// ===----------------------------------------------------------------------===

void Driver::GenerateJobs(Compilation &C) const {
  llvm::PrettyStackTraceString CrashInfo("Building compilation jobs");

  Arg *FinalOutput = C.getArgs().getLastArg(options::OPT_o);

  // It is an error to provide a -o option if we are making multiple output
  // files.
  if (FinalOutput) {
    unsigned NumOutputs = 0;
    for (const Action *A : C.getActions()) {
      if (A->getType() != types::TY_Nothing)
        ++NumOutputs;
    }

    if (NumOutputs > 1) {
      Diag(neverc::diag::err_drv_output_argument_with_multiple_files);
      FinalOutput = nullptr;
    }
  }

  const llvm::Triple &RawTriple = C.getDefaultToolChain().getTriple();

  // Collect the list of architectures.
  llvm::StringSet<> ArchNames;
  if (RawTriple.isOSBinFormatMachO())
    for (const Arg *A : C.getArgs())
      if (A->getOption().matches(options::OPT_arch))
        ArchNames.insert(A->getValue());

  // Set of (Action, canonical ToolChain triple) pairs we've built jobs for.
  std::map<std::pair<const Action *, std::string>, InputInfoList> CachedResults;
  for (Action *A : C.getActions()) {
    // If we are linking an image for multiple archs then the linker wants
    // -arch_multiple and -final_output <final image name>.
    const char *LinkingOutput = nullptr;
    if (isa<LipoJobAction>(A)) {
      if (FinalOutput)
        LinkingOutput = FinalOutput->getValue();
      else
        LinkingOutput = getDefaultImageName();
    }

    GenerateJobsForAction(C, A, &C.getDefaultToolChain(),
                          /*BoundArch*/ llvm::StringRef(),
                          /*AtTopLevel*/ true,
                          /*MultipleArchs*/ ArchNames.size() > 1,
                          /*LinkingOutput*/ LinkingOutput, CachedResults);
  }

  // If the user passed -Qunused-arguments or there were errors, don't warn
  // about any unused arguments.
  if (Diags.hasErrorOccurred() ||
      C.getArgs().hasArg(options::OPT_Qunused_arguments))
    return;

  // Claim -fdriver-only here.
  (void)C.getArgs().hasArg(options::OPT_fdriver_only);
  // Claim -### here.
  (void)C.getArgs().hasArg(options::OPT__HASH_HASH_HASH);

  // Claim --rsp-quoting, it was handled earlier.
  (void)C.getArgs().hasArg(options::OPT_rsp_quoting);

  bool HasAssembleJob = llvm::any_of(C.getJobs(), [](auto &J) {
    // Match NeverCAs and other derived assemblers of Tool. NeverCAs uses a
    // longer ShortName "neverc integrated assembler" while other assemblers
    // just use "assembler".
    return strstr(J.getCreator().getShortName(), "assembler");
  });
  for (Arg *A : C.getArgs()) {
    if (!A->isClaimed()) {
      if (A->getOption().hasFlag(options::NoArgumentUnused))
        continue;

      // Suppress the warning automatically if this is just a flag, and it is an
      // instance of an argument we already claimed.
      const Option &Opt = A->getOption();
      if (Opt.getKind() == Option::FlagClass) {
        bool DuplicateClaimed = false;

        for (const Arg *AA : C.getArgs().filtered(&Opt)) {
          if (AA->isClaimed()) {
            DuplicateClaimed = true;
            break;
          }
        }

        if (DuplicateClaimed)
          continue;
      }

      {
        if (A->getOption().hasFlag(options::TargetSpecific) &&
            !A->isIgnoredTargetSpecific() && !HasAssembleJob &&
            // When for example -### or -v is used
            // without a file, target specific options are not
            // consumed/validated.
            // Instead emitting an error emit a warning instead.
            !C.getActions().empty()) {
          Diag(diag::err_drv_unsupported_opt_for_target)
              << A->getSpelling() << getTargetTriple();
        } else {
          Diag(neverc::diag::warn_drv_unused_argument)
              << A->getAsString(C.getArgs());
        }
      }
    }
  }
}

namespace {
class ToolSelector final {
  const ToolChain &TC;

  const Compilation &C;

  const JobAction *BaseAction;

  bool SaveTemps;

  const JobAction *getPrevDependentAction(const ActionList &Inputs,
                                          bool CanBeCollapsed = true) {
    if (Inputs.size() != 1)
      return nullptr;

    Action *CurAction = *Inputs.begin();
    if (CanBeCollapsed &&
        !CurAction->isCollapsingWithNextDependentActionLegal())
      return nullptr;

    return dyn_cast<JobAction>(CurAction);
  }

  bool canCollapseAssembleAction() const {
    return !SaveTemps && !C.getArgs().hasArg(options::OPT_via_file_asm) &&
           !C.getArgs().hasArg(options::OPT_msvc_asm_listing) &&
           !C.getArgs().hasArg(options::OPT_msvc_asm_output);
  }

  bool canCollapsePreprocessorAction() const {
    return !C.getArgs().hasArg(options::OPT_no_integrated_cpp) &&
           !C.getArgs().hasArg(options::OPT_traditional_cpp) && !SaveTemps;
  }

  struct JobActionInfo final {
    const JobAction *JA = nullptr;
  };

  const Tool *
  combineAssembleBackendCompile(llvm::ArrayRef<JobActionInfo> ActionInfo,
                                ActionList &Inputs) {
    if (ActionInfo.size() < 3 || !canCollapseAssembleAction())
      return nullptr;
    auto *AJ = dyn_cast<AssembleJobAction>(ActionInfo[0].JA);
    auto *BJ = dyn_cast<BackendJobAction>(ActionInfo[1].JA);
    auto *CJ = dyn_cast<CompileJobAction>(ActionInfo[2].JA);
    if (!AJ || !BJ || !CJ)
      return nullptr;

    const Tool *T = TC.SelectTool(*CJ);
    if (!T)
      return nullptr;

    // Can't collapse if we don't have codegen support unless we are
    // emitting LLVM IR.
    bool OutputIsLLVM = types::isLLVMIR(ActionInfo[0].JA->getType());
    if (!T->hasIntegratedBackend() && !(OutputIsLLVM && T->canEmitIR()))
      return nullptr;

    if (!T->hasIntegratedAssembler())
      return nullptr;

    Inputs = CJ->getInputs();
    return T;
  }
  const Tool *combineAssembleBackend(llvm::ArrayRef<JobActionInfo> ActionInfo,
                                     ActionList &Inputs) {
    if (ActionInfo.size() < 2 || !canCollapseAssembleAction())
      return nullptr;
    auto *AJ = dyn_cast<AssembleJobAction>(ActionInfo[0].JA);
    auto *BJ = dyn_cast<BackendJobAction>(ActionInfo[1].JA);
    if (!AJ || !BJ)
      return nullptr;

    const Tool *T = TC.SelectTool(*BJ);
    if (!T)
      return nullptr;

    if (!T->hasIntegratedAssembler())
      return nullptr;

    Inputs = BJ->getInputs();
    return T;
  }
  const Tool *combineBackendCompile(llvm::ArrayRef<JobActionInfo> ActionInfo,
                                    ActionList &Inputs) {
    if (ActionInfo.size() < 2)
      return nullptr;
    auto *BJ = dyn_cast<BackendJobAction>(ActionInfo[0].JA);
    auto *CJ = dyn_cast<CompileJobAction>(ActionInfo[1].JA);
    if (!BJ || !CJ)
      return nullptr;

    // Check if the initial input (to the compile job or its predessor if one
    // exists) is LLVM bitcode. In that case, no preprocessor step is required
    // and we can still collapse the compile and backend jobs when we have
    // -save-temps. I.e. there is no need for a separate compile job just to
    // emit unoptimized bitcode.
    bool InputIsBitcode = true;
    for (size_t i = 1; i < ActionInfo.size(); i++)
      if (ActionInfo[i].JA->getType() != types::TY_LLVM_BC &&
          ActionInfo[i].JA->getType() != types::TY_LTO_BC) {
        InputIsBitcode = false;
        break;
      }
    if (!InputIsBitcode && !canCollapsePreprocessorAction())
      return nullptr;

    const Tool *T = TC.SelectTool(*CJ);
    if (!T)
      return nullptr;

    // Can't collapse if we don't have codegen support unless we are
    // emitting LLVM IR.
    bool OutputIsLLVM = types::isLLVMIR(ActionInfo[0].JA->getType());
    if (!T->hasIntegratedBackend() && !(OutputIsLLVM && T->canEmitIR()))
      return nullptr;

    if (T->canEmitIR() && (SaveTemps && !InputIsBitcode))
      return nullptr;

    Inputs = CJ->getInputs();
    return T;
  }

  void combineWithPreprocessor(const Tool *T, ActionList &Inputs) {
    if (!T || !canCollapsePreprocessorAction() || !T->hasIntegratedCPP())
      return;

    ActionList NewInputs;
    for (Action *A : Inputs) {
      auto *PJ = getPrevDependentAction({A});
      if (!PJ || !isa<PreprocessJobAction>(PJ)) {
        NewInputs.push_back(A);
        continue;
      }

      NewInputs.append(PJ->input_begin(), PJ->input_end());
    }
    Inputs = NewInputs;
  }

public:
  ToolSelector(const JobAction *BaseAction, const ToolChain &TC,
               const Compilation &C, bool SaveTemps)
      : TC(TC), C(C), BaseAction(BaseAction), SaveTemps(SaveTemps) {
    assert(BaseAction && "Invalid base action.");
  }

  const Tool *getTool(ActionList &Inputs) {
    llvm::SmallVector<JobActionInfo, 5> ActionChain(1);
    ActionChain.back().JA = BaseAction;
    while (ActionChain.back().JA) {
      const Action *CurAction = ActionChain.back().JA;

      // Grow the chain by one element.
      ActionChain.resize(ActionChain.size() + 1);
      JobActionInfo &AI = ActionChain.back();

      AI.JA = getPrevDependentAction(CurAction->getInputs());
    }

    // Pop the last action info as it could not be filled.
    ActionChain.pop_back();

    //
    // Attempt to combine actions. If all combining attempts failed, just return
    // the tool of the provided action. At the end we attempt to combine the
    // action with any preprocessor action it may depend on.
    //

    const Tool *T = combineAssembleBackendCompile(ActionChain, Inputs);
    if (!T)
      T = combineAssembleBackend(ActionChain, Inputs);
    if (!T)
      T = combineBackendCompile(ActionChain, Inputs);
    if (!T) {
      Inputs = BaseAction->getInputs();
      T = TC.SelectTool(*BaseAction);
    }

    combineWithPreprocessor(T, Inputs);
    return T;
  }
};
} // namespace

namespace {
std::string formatTripleWithArch(const ToolChain *TC,
                                 llvm::StringRef BoundArch) {
  std::string TriplePlusArch = TC->getTriple().normalize();
  if (!BoundArch.empty()) {
    TriplePlusArch += "-";
    TriplePlusArch += BoundArch;
  }
  return TriplePlusArch;
}
} // namespace

InputInfoList Driver::GenerateJobsForAction(
    Compilation &C, const Action *A, const ToolChain *TC,
    llvm::StringRef BoundArch, bool AtTopLevel, bool MultipleArchs,
    const char *LinkingOutput,
    std::map<std::pair<const Action *, std::string>, InputInfoList>
        &CachedResults) const {
  std::pair<const Action *, std::string> ActionTC = {
      A, formatTripleWithArch(TC, BoundArch)};
  auto CachedResult = CachedResults.find(ActionTC);
  if (CachedResult != CachedResults.end()) {
    return CachedResult->second;
  }
  InputInfoList Result =
      GenerateJobsForActionNoCache(C, A, TC, BoundArch, AtTopLevel,
                                   MultipleArchs, LinkingOutput, CachedResults);
  CachedResults[ActionTC] = Result;
  return Result;
}

namespace {
void handleTimeTrace(Compilation &C, const ArgList &Args, const JobAction *JA,
                     const char *BaseInput, const InputInfo &Result) {
  Arg *A =
      Args.getLastArg(options::OPT_ftime_trace, options::OPT_ftime_trace_EQ);
  if (!A)
    return;
  llvm::SmallString<128> Path;
  if (A->getOption().matches(options::OPT_ftime_trace_EQ)) {
    Path = A->getValue();
    if (llvm::sys::fs::is_directory(Path)) {
      llvm::SmallString<128> Tmp(Result.getFilename());
      llvm::sys::path::replace_extension(Tmp, "json");
      llvm::sys::path::append(Path, llvm::sys::path::filename(Tmp));
    }
  } else {
    if (Arg *DumpDir = Args.getLastArgNoClaim(options::OPT_dumpdir)) {
      // The trace file is ${dumpdir}${basename}.json. Note that dumpdir may not
      // end with a path separator.
      Path = DumpDir->getValue();
      Path += llvm::sys::path::filename(BaseInput);
    } else {
      Path = Result.getFilename();
    }
    llvm::sys::path::replace_extension(Path, "json");
  }
  const char *ResultFile = C.getArgs().MakeArgString(Path);
  C.addTimeTraceFile(ResultFile, JA);
  C.addResultFile(ResultFile, JA);
}
} // namespace

InputInfoList Driver::GenerateJobsForActionNoCache(
    Compilation &C, const Action *A, const ToolChain *TC,
    llvm::StringRef BoundArch, bool AtTopLevel, bool MultipleArchs,
    const char *LinkingOutput,
    std::map<std::pair<const Action *, std::string>, InputInfoList>
        &CachedResults) const {
  llvm::PrettyStackTraceString CrashInfo("Building compilation jobs");

  if (const InputAction *IA = dyn_cast<InputAction>(A)) {
    const Arg &Input = IA->getInputArg();
    Input.claim();
    if (Input.getOption().matches(options::OPT_INPUT)) {
      const char *Name = Input.getValue();
      return {InputInfo(A, Name, /* _BaseInput = */ Name)};
    }
    return {InputInfo(A, &Input, /* _BaseInput = */ "")};
  }

  if (const BindArchAction *BAA = dyn_cast<BindArchAction>(A)) {
    const ToolChain *TC;
    llvm::StringRef ArchName = BAA->getArchName();

    if (!ArchName.empty())
      TC = &getToolChain(
          C.getArgs(),
          computeTargetTriple(*this, TargetTriple, C.getArgs(), ArchName));
    else
      TC = &C.getDefaultToolChain();

    return GenerateJobsForAction(C, *BAA->input_begin(), TC, ArchName,
                                 AtTopLevel, MultipleArchs, LinkingOutput,
                                 CachedResults);
  }

  ActionList Inputs = A->getInputs();

  const JobAction *JA = cast<JobAction>(A);

  ToolSelector TS(JA, *TC, C, isSaveTempsEnabled());
  const Tool *T = TS.getTool(Inputs);

  if (!T)
    return {InputInfo()};

  // Only use pipes when there is exactly one input.
  InputInfoList InputInfos;
  for (const Action *Input : Inputs) {
    bool SubJobAtTopLevel = AtTopLevel && isa<DsymutilJobAction>(A);
    InputInfos.append(GenerateJobsForAction(C, Input, TC, BoundArch,
                                            SubJobAtTopLevel, MultipleArchs,
                                            LinkingOutput, CachedResults));
  }

  // Always use the first file input as the base input.
  const char *BaseInput = InputInfos[0].getBaseInput();
  for (auto &Info : InputInfos) {
    if (Info.isFilename()) {
      BaseInput = Info.getBaseInput();
      break;
    }
  }

  // ... except dsymutil actions, which use their actual input as the base
  // input.
  if (JA->getType() == types::TY_dSYM)
    BaseInput = InputInfos[0].getFilename();

  llvm::Triple EffectiveTriple;
  const ToolChain &ToolTC = T->getToolChain();
  const ArgList &Args = C.getArgsForToolChain(TC, BoundArch);
  if (InputInfos.size() != 1) {
    EffectiveTriple = llvm::Triple(ToolTC.ComputeEffectiveTriple(Args));
  } else {
    // Pass along the input type if it can be unambiguously determined.
    EffectiveTriple = llvm::Triple(
        ToolTC.ComputeEffectiveTriple(Args, InputInfos[0].getType()));
  }
  RegisterEffectiveTriple TripleRAII(ToolTC, EffectiveTriple);

  InputInfo Result;
  if (JA->getType() == types::TY_Nothing) {
    Result = {InputInfo(A, BaseInput)};
  } else {
    Result = InputInfo(A,
                       GetNamedOutputPath(C, *JA, BaseInput, BoundArch,
                                          AtTopLevel, MultipleArchs),
                       BaseInput);
    if (T->canEmitIR())
      handleTimeTrace(C, Args, JA, BaseInput, Result);
  }

  if (CCCPrintBindings && !CCGenDiagnostics) {
    llvm::errs() << "# \"" << T->getToolChain().getTripleString() << '"'
                 << " - \"" << T->getName() << "\", inputs: [";
    for (unsigned i = 0, e = InputInfos.size(); i != e; ++i) {
      llvm::errs() << InputInfos[i].getAsString();
      if (i + 1 != e)
        llvm::errs() << ", ";
    }
    llvm::errs() << "], output: " << Result.getAsString() << "\n";
  } else {
    T->ConstructJob(C, *JA, Result, InputInfos,
                    C.getArgsForToolChain(TC, BoundArch), LinkingOutput);
  }
  return {Result};
}

// ===----------------------------------------------------------------------===
// Output naming & path resolution
// ===----------------------------------------------------------------------===

const char *Driver::getDefaultImageName() const {
  llvm::Triple Target(llvm::Triple::normalize(TargetTriple));
  return Target.isOSWindows() ? "a.exe" : "a.out";
}

namespace {
const char *FormCLOutputFilename(const ArgList &Args, llvm::StringRef ArgValue,
                                 llvm::StringRef BaseName, types::ID FileType) {
  llvm::SmallString<128> Filename = ArgValue;

  if (ArgValue.empty()) {
    // If the argument is empty, output to BaseName in the current dir.
    Filename = BaseName;
  } else if (llvm::sys::path::is_separator(Filename.back())) {
    // If the argument is a directory, output to BaseName in that dir.
    llvm::sys::path::append(Filename, BaseName);
  }

  if (!llvm::sys::path::has_extension(ArgValue)) {
    // If the argument didn't provide an extension, then set it.
    const char *Extension = types::getTypeTempSuffix(FileType, true);

    if (FileType == types::TY_Image &&
        Args.hasArg(options::OPT_create_dll, options::OPT_create_dll_debug)) {
      // The output file is a dll.
      Extension = "dll";
    }

    llvm::sys::path::replace_extension(Filename, Extension);
  }

  return Args.MakeArgString(Filename.c_str());
}

bool outputsPreprocessedCode(const Action &JA) {
  return isa<PreprocessJobAction>(JA);
}
} // namespace

const char *Driver::CreateTempFile(Compilation &C, llvm::StringRef Prefix,
                                   llvm::StringRef Suffix, bool MultipleArchs,
                                   llvm::StringRef BoundArch,
                                   bool NeedUniqueDirectory) const {
  llvm::SmallString<128> TmpName;

  // Integrated compiler+linker with auto-LTO: intermediate bitcode stays in
  // InMemoryFileStore, so no disk file is needed.  Use a cheap synthetic path
  // (no system_temp_directory syscall) and skip TempFiles registration —
  // CleanupFileList would just waste syscalls on non-existent paths.
  if (isUsingLTO() && !CCGenDiagnostics && !isSaveTempsEnabled()) {
    static std::atomic<unsigned> InMemSeq{0};
    unsigned N = InMemSeq.fetch_add(1, std::memory_order_relaxed);
    TmpName = "<inmem>/";
    llvm::raw_svector_ostream(TmpName) << Prefix << '-' << N << '.' << Suffix;
    return C.getArgs().MakeArgString(TmpName);
  }

  Arg *A = C.getArgs().getLastArg(options::OPT_fcrash_diagnostics_dir);
  auto CrashDirEnv = llvm::sys::Process::GetEnv("NEVERC_CRASH_DIAGNOSTICS_DIR");
  std::optional<llvm::SmallString<256>> CrashDirectory =
      CCGenDiagnostics && A ? llvm::SmallString<256>(A->getValue())
                            : CrashDirEnv;
  if (CrashDirectory) {
    if (!getVFS().exists(*CrashDirectory))
      llvm::sys::fs::create_directories(*CrashDirectory);
    llvm::SmallString<128> Path(*CrashDirectory);
    llvm::sys::path::append(Path, Prefix);
    const char *Middle = !Suffix.empty() ? "-%%%%%%." : "-%%%%%%";
    if (std::error_code EC =
            llvm::sys::fs::createUniqueFile(Path + Middle + Suffix, TmpName)) {
      Diag(neverc::diag::err_unable_to_make_temp) << EC.message();
      return "";
    }
  } else {
    if (MultipleArchs && !BoundArch.empty()) {
      if (NeedUniqueDirectory) {
        TmpName = GetTemporaryDirectory(Prefix);
        llvm::sys::path::append(TmpName, llvm::Twine(Prefix) + "-" + BoundArch +
                                             "." + Suffix);
      } else {
        TmpName = GetTemporaryPath(
            (llvm::Twine(Prefix) + "-" + BoundArch).str(), Suffix);
      }

    } else {
      TmpName = GetTemporaryPath(Prefix, Suffix);
    }
  }
  return C.addTempFile(C.getArgs().MakeArgString(TmpName));
}

const char *Driver::GetNamedOutputPath(Compilation &C, const JobAction &JA,
                                       const char *BaseInput,
                                       llvm::StringRef OrigBoundArch,
                                       bool AtTopLevel,
                                       bool MultipleArchs) const {
  std::string BoundArch = OrigBoundArch.str();
  if (is_style_windows(llvm::sys::path::Style::native)) {
    // BoundArch may contains ':', which is invalid in file names on Windows,
    // therefore replace it with '%'.
    std::replace(BoundArch.begin(), BoundArch.end(), ':', '@');
  }

  llvm::PrettyStackTraceString CrashInfo("Computing output path");
  // Output to a user requested destination?
  if (AtTopLevel && !isa<DsymutilJobAction>(JA)) {
    if (Arg *FinalOutput = C.getArgs().getLastArg(options::OPT_o))
      return C.addResultFile(FinalOutput->getValue(), &JA);
  }

  // For /P, preprocess to file named after BaseInput.
  if (C.getArgs().hasArg(options::OPT_msvc_preprocess_to_file)) {
    assert(AtTopLevel && isa<PreprocessJobAction>(JA));
    llvm::StringRef BaseName = llvm::sys::path::filename(BaseInput);
    llvm::StringRef NameArg;
    if (Arg *A = C.getArgs().getLastArg(options::OPT_msvc_pp_output))
      NameArg = A->getValue();
    return C.addResultFile(
        FormCLOutputFilename(C.getArgs(), NameArg, BaseName, types::TY_PP_C),
        &JA);
  }

  // Default to writing to stdout?
  if (AtTopLevel && !CCGenDiagnostics && outputsPreprocessedCode(JA)) {
    return "-";
  }

  // Is this the assembly listing for /FA?
  if (JA.getType() == types::TY_PP_Asm &&
      (C.getArgs().hasArg(options::OPT_msvc_asm_listing) ||
       C.getArgs().hasArg(options::OPT_msvc_asm_output))) {
    // Use /Fa and the input filename to determine the asm file name.
    llvm::StringRef BaseName = llvm::sys::path::filename(BaseInput);
    llvm::StringRef FaValue =
        C.getArgs().getLastArgValue(options::OPT_msvc_asm_output);
    return C.addResultFile(
        FormCLOutputFilename(C.getArgs(), FaValue, BaseName, JA.getType()),
        &JA);
  }

  // Output to a temporary file?
  if ((!AtTopLevel && !isSaveTempsEnabled() &&
       !C.getArgs().hasArg(options::OPT_msvc_obj_output)) ||
      CCGenDiagnostics) {
    llvm::StringRef Name = llvm::sys::path::filename(BaseInput);
    std::pair<llvm::StringRef, llvm::StringRef> Split = Name.split('.');
    const char *Suffix = types::getTypeTempSuffix(
        JA.getType(),
        llvm::Triple(getTargetTriple()).isWindowsMSVCEnvironment());
    // On Darwin, deterministic binaries require deterministic input paths,
    // therefore use a unique directory for temporaries.
    llvm::Triple Triple(C.getDriver().getTargetTriple());
    bool NeedUniqueDirectory = Triple.isOSDarwin();
    return CreateTempFile(C, Split.first, Suffix, MultipleArchs, BoundArch,
                          NeedUniqueDirectory);
  }

  llvm::SmallString<128> BasePath(BaseInput);
  llvm::SmallString<128> ExternalPath("");
  llvm::StringRef BaseName;

  // Dsymutil actions should use the full path.
  if (isa<DsymutilJobAction>(JA) && C.getArgs().hasArg(options::OPT_dsym_dir)) {
    ExternalPath += C.getArgs().getLastArg(options::OPT_dsym_dir)->getValue();
    // We use posix style here because the tests (specifically
    // darwin-dsymutil.c) demonstrate that posix style paths are acceptable
    // even on Windows and if we don't then the similar test covering this
    // fails.
    llvm::sys::path::append(ExternalPath, llvm::sys::path::Style::posix,
                            llvm::sys::path::filename(BasePath));
    BaseName = ExternalPath;
  } else if (isa<DsymutilJobAction>(JA))
    BaseName = BasePath;
  else
    BaseName = llvm::sys::path::filename(BasePath);

  // Determine what the derived output name should be.
  const char *NamedOutput;

  if ((JA.getType() == types::TY_Object || JA.getType() == types::TY_LTO_BC) &&
      C.getArgs().hasArg(options::OPT_msvc_obj_output, options::OPT_o)) {
    // The /Fo or /o flag decides the object filename.
    llvm::StringRef Val =
        C.getArgs()
            .getLastArg(options::OPT_msvc_obj_output, options::OPT_o)
            ->getValue();
    NamedOutput =
        FormCLOutputFilename(C.getArgs(), Val, BaseName, types::TY_Object);
  } else if (JA.getType() == types::TY_Image &&
             C.getArgs().hasArg(options::OPT_msvc_exe_output, options::OPT_o)) {
    // The /Fe or /o flag names the linked file.
    llvm::StringRef Val =
        C.getArgs()
            .getLastArg(options::OPT_msvc_exe_output, options::OPT_o)
            ->getValue();
    NamedOutput =
        FormCLOutputFilename(C.getArgs(), Val, BaseName, types::TY_Image);
  } else if (JA.getType() == types::TY_Image) {
    if (llvm::Triple(getTargetTriple()).isWindowsMSVCEnvironment()) {
      NamedOutput =
          FormCLOutputFilename(C.getArgs(), "", BaseName, types::TY_Image);
    } else {
      llvm::SmallString<128> Output(getDefaultImageName());
      if (MultipleArchs && !BoundArch.empty()) {
        Output += "-";
        Output.append(BoundArch);
      }
      NamedOutput = C.getArgs().MakeArgString(Output.c_str());
    }
  } else {
    const char *Suffix = types::getTypeTempSuffix(
        JA.getType(),
        llvm::Triple(getTargetTriple()).isWindowsMSVCEnvironment());
    assert(Suffix && "All types used for output should have a suffix.");

    std::string::size_type End = std::string::npos;
    if (!types::appendSuffixForType(JA.getType()))
      End = BaseName.rfind('.');
    llvm::SmallString<128> Suffixed(BaseName.substr(0, End));
    if (MultipleArchs && !BoundArch.empty()) {
      Suffixed += "-";
      Suffixed.append(BoundArch);
    }
    // When using both -save-temps and -emit-llvm, use a ".tmp.bc" suffix for
    // the unoptimized bitcode so that it does not get overwritten by the ".bc"
    // optimized bitcode output.
    if (!AtTopLevel && JA.getType() == types::TY_LLVM_BC &&
        C.getArgs().hasArg(options::OPT_emit_llvm))
      Suffixed += ".tmp";
    Suffixed += '.';
    Suffixed += Suffix;
    NamedOutput = C.getArgs().MakeArgString(Suffixed.c_str());
  }

  // Prepend object file path if -save-temps=obj
  if (!AtTopLevel && isSaveTempsObj() && C.getArgs().hasArg(options::OPT_o)) {
    Arg *FinalOutput = C.getArgs().getLastArg(options::OPT_o);
    llvm::SmallString<128> TempPath(FinalOutput->getValue());
    llvm::sys::path::remove_filename(TempPath);
    llvm::StringRef OutputFileName = llvm::sys::path::filename(NamedOutput);
    llvm::sys::path::append(TempPath, OutputFileName);
    NamedOutput = C.getArgs().MakeArgString(TempPath.c_str());
  }

  // If we're saving temps and the temp file conflicts with the input file,
  // then avoid overwriting input file.
  if (!AtTopLevel && isSaveTempsEnabled() && NamedOutput == BaseName) {
    bool SameFile = false;
    llvm::SmallString<256> Result;
    llvm::sys::fs::current_path(Result);
    llvm::sys::path::append(Result, BaseName);
    llvm::sys::fs::equivalent(BaseInput, Result.c_str(), SameFile);
    // Must share the same path to conflict.
    if (SameFile) {
      llvm::StringRef Name = llvm::sys::path::filename(BaseInput);
      std::pair<llvm::StringRef, llvm::StringRef> Split = Name.split('.');
      std::string TmpName = GetTemporaryPath(
          Split.first,
          types::getTypeTempSuffix(
              JA.getType(),
              llvm::Triple(getTargetTriple()).isWindowsMSVCEnvironment()));
      return C.addTempFile(C.getArgs().MakeArgString(TmpName));
    }
  }

  return C.addResultFile(NamedOutput, &JA);
}

std::string Driver::GetFilePath(llvm::StringRef Name,
                                const ToolChain &TC) const {
  // Search for Name in a list of paths.
  auto SearchPaths = [&](const llvm::SmallVectorImpl<std::string> &P)
      -> std::optional<std::string> {
    // Respect a limited subset of the '-Bprefix' functionality in GCC by
    // attempting to use this prefix when looking for file paths.
    for (const auto &Dir : P) {
      if (Dir.empty())
        continue;
      llvm::SmallString<128> P(Dir[0] == '=' ? SysRoot + Dir.substr(1) : Dir);
      llvm::sys::path::append(P, Name);
      if (llvm::sys::fs::exists(llvm::Twine(P)))
        return std::string(P);
    }
    return std::nullopt;
  };

  if (auto P = SearchPaths(PrefixDirs))
    return *P;

  llvm::SmallString<128> R(ResourceDir);
  llvm::sys::path::append(R, Name);
  if (llvm::sys::fs::exists(llvm::Twine(R)))
    return std::string(R.str());

  llvm::SmallString<128> P(TC.getCompilerRTPath());
  llvm::sys::path::append(P, Name);
  if (llvm::sys::fs::exists(llvm::Twine(P)))
    return std::string(P.str());

  llvm::SmallString<128> D(Dir);
  llvm::sys::path::append(D, "..", Name);
  if (llvm::sys::fs::exists(llvm::Twine(D)))
    return std::string(D.str());

  if (auto P = SearchPaths(TC.getLibraryPaths()))
    return *P;

  if (auto P = SearchPaths(TC.getFilePaths()))
    return *P;

  return std::string(Name);
}

void Driver::generatePrefixedToolNames(
    llvm::StringRef Tool, const ToolChain &TC,
    llvm::SmallVectorImpl<std::string> &Names) const {
  Names.emplace_back((TargetTriple + "-" + Tool).str());
  Names.emplace_back(Tool);
}

namespace {
bool findExecutableInDir(llvm::SmallString<128> &Dir, llvm::StringRef Name) {
  llvm::sys::path::append(Dir, Name);
  if (llvm::sys::fs::can_execute(llvm::Twine(Dir)))
    return true;
  llvm::sys::path::remove_filename(Dir);
  return false;
}
} // namespace

std::string Driver::GetProgramPath(llvm::StringRef Name,
                                   const ToolChain &TC) const {
  llvm::SmallVector<std::string, 2> TargetSpecificExecutables;
  generatePrefixedToolNames(Name, TC, TargetSpecificExecutables);

  // Respect a limited subset of the '-Bprefix' functionality in GCC by
  // attempting to use this prefix when looking for program paths.
  for (const auto &PrefixDir : PrefixDirs) {
    if (llvm::sys::fs::is_directory(PrefixDir)) {
      llvm::SmallString<128> P(PrefixDir);
      if (findExecutableInDir(P, Name))
        return std::string(P.str());
    } else {
      llvm::SmallString<128> P((PrefixDir + Name).str());
      if (llvm::sys::fs::can_execute(llvm::Twine(P)))
        return std::string(P.str());
    }
  }

  const ToolChain::path_list &List = TC.getProgramPaths();
  for (const auto &TargetSpecificExecutable : TargetSpecificExecutables) {
    // For each possible name of the tool look for it in
    // program paths first, then the path.
    // Higher priority names will be first, meaning that
    // a higher priority name in the path will be found
    // instead of a lower priority name in the program path.
    // E.g. <triple>-gcc on the path will be found instead
    // of gcc in the program path
    for (const auto &Path : List) {
      llvm::SmallString<128> P(Path);
      if (findExecutableInDir(P, TargetSpecificExecutable))
        return std::string(P.str());
    }

    // Fall back to the path
    if (auto P = llvm::sys::findProgramByName(TargetSpecificExecutable))
      return std::string(P->data(), P->size());
  }

  return std::string(Name);
}

std::string Driver::GetTemporaryPath(llvm::StringRef Prefix,
                                     llvm::StringRef Suffix) const {
  llvm::SmallString<128> Path;
  std::error_code EC = llvm::sys::fs::createTemporaryFile(Prefix, Suffix, Path);
  if (EC) {
    Diag(neverc::diag::err_unable_to_make_temp) << EC.message();
    return "";
  }

  return std::string(Path.str());
}

std::string Driver::GetTemporaryDirectory(llvm::StringRef Prefix) const {
  llvm::SmallString<128> Path;
  std::error_code EC = llvm::sys::fs::createUniqueDirectory(Prefix, Path);
  if (EC) {
    Diag(neverc::diag::err_unable_to_make_temp) << EC.message();
    return "";
  }

  return std::string(Path.str());
}

// ===----------------------------------------------------------------------===
// ToolChain management & utilities
// ===----------------------------------------------------------------------===

const ToolChain &Driver::getToolChain(const ArgList &Args,
                                      const llvm::Triple &Target) const {

  auto &TC = ToolChains[Target.str()];
  if (!TC) {
    switch (Target.getOS()) {
    case llvm::Triple::Darwin:
    case llvm::Triple::MacOSX:
    case llvm::Triple::IOS:
      TC = std::make_unique<toolchains::DarwinNeverC>(*this, Target, Args);
      break;
    case llvm::Triple::Linux:
      TC = std::make_unique<toolchains::Linux>(*this, Target, Args);
      break;
    case llvm::Triple::Win32:
      switch (Target.getEnvironment()) {
      default:
        if (Target.isOSBinFormatELF())
          TC = std::make_unique<toolchains::Generic_ELF>(*this, Target, Args);
        else if (Target.isOSBinFormatMachO())
          TC = std::make_unique<toolchains::MachO>(*this, Target, Args);
        else
          TC = std::make_unique<toolchains::Generic_GCC>(*this, Target, Args);
        break;
      case llvm::Triple::MSVC:
      case llvm::Triple::UnknownEnvironment:
        TC = std::make_unique<toolchains::MSVCToolChain>(*this, Target, Args);
        break;
      }
      break;
    default:
      switch (Target.getArch()) {
      default:
        if (Target.isOSBinFormatELF())
          TC = std::make_unique<toolchains::Generic_ELF>(*this, Target, Args);
        else if (Target.isOSBinFormatMachO())
          TC = std::make_unique<toolchains::MachO>(*this, Target, Args);
        else
          TC = std::make_unique<toolchains::Generic_GCC>(*this, Target, Args);
      }
    }
  }

  return *TC;
}

bool Driver::ShouldUseNeverCCompiler(const JobAction &JA) const {
  // Say "no" if there is not exactly one input of a type NeverC understands.
  if (JA.size() != 1 ||
      !types::isAcceptedByNeverC((*JA.input_begin())->getType()))
    return false;

  // And say "no" if this is not a kind of action NeverC understands.
  if (!isa<PreprocessJobAction>(JA) && !isa<CompileJobAction>(JA) &&
      !isa<BackendJobAction>(JA))
    return false;

  return true;
}

bool Driver::ShouldEmitStaticLibrary(const ArgList &Args) const {
  // Only emit static library if the flag is set explicitly.
  return Args.hasArg(options::OPT_emit_static_lib);
}

bool Driver::GetReleaseVersion(llvm::StringRef Str, unsigned &Major,
                               unsigned &Minor, unsigned &Micro,
                               bool &HadExtra) {
  HadExtra = false;

  Major = Minor = Micro = 0;
  if (Str.empty())
    return false;

  if (Str.consumeInteger(10, Major))
    return false;
  if (Str.empty())
    return true;
  if (Str[0] != '.')
    return false;

  Str = Str.drop_front(1);

  if (Str.consumeInteger(10, Minor))
    return false;
  if (Str.empty())
    return true;
  if (Str[0] != '.')
    return false;
  Str = Str.drop_front(1);

  if (Str.consumeInteger(10, Micro))
    return false;
  if (!Str.empty())
    HadExtra = true;
  return true;
}

bool Driver::GetReleaseVersion(llvm::StringRef Str,
                               llvm::MutableArrayRef<unsigned> Digits) {
  if (Str.empty())
    return false;

  unsigned CurDigit = 0;
  while (CurDigit < Digits.size()) {
    unsigned Digit;
    if (Str.consumeInteger(10, Digit))
      return false;
    Digits[CurDigit] = Digit;
    if (Str.empty())
      return true;
    if (Str[0] != '.')
      return false;
    Str = Str.drop_front(1);
    CurDigit++;
  }

  // More digits than requested, bail out...
  return false;
}

llvm::opt::Visibility Driver::getOptionVisibilityMask() const {
  return llvm::opt::Visibility(options::NeverCOption);
}

bool neverc::driver::isOptimizationLevelFast(const ArgList &Args) {
  return Args.hasFlag(options::OPT_Ofast, options::OPT_O_Group, false);
}

bool neverc::driver::willEmitRemarks(const ArgList &Args) {
  // -fsave-optimization-record enables it.
  if (Args.hasFlag(options::OPT_fsave_optimization_record,
                   options::OPT_fno_save_optimization_record, false))
    return true;

  // -fsave-optimization-record=<format> enables it as well.
  if (Args.hasFlag(options::OPT_fsave_optimization_record_EQ,
                   options::OPT_fno_save_optimization_record, false))
    return true;

  // -foptimization-record-file alone enables it too.
  if (Args.hasFlag(options::OPT_foptimization_record_file_EQ,
                   options::OPT_fno_save_optimization_record, false))
    return true;

  // -foptimization-record-passes alone enables it too.
  if (Args.hasFlag(options::OPT_foptimization_record_passes_EQ,
                   options::OPT_fno_save_optimization_record, false))
    return true;
  return false;
}

llvm::Error driver::expandResponseFiles(
    llvm::SmallVectorImpl<const char *> &Args, llvm::StringRef ProgName,
    llvm::StringRef DefaultTargetTriple, llvm::BumpPtrAllocator &Alloc,
    llvm::vfs::FileSystem *FS) {
  auto isWindowsMSVCTarget = [](llvm::StringRef TripleString) {
    return !TripleString.empty() &&
           llvm::Triple(TripleString).isWindowsMSVCEnvironment();
  };

  enum { Default, POSIX, Windows } RSPQuoting = Default;
  for (const char *F : Args) {
    if (strcmp(F, "--rsp-quoting=posix") == 0)
      RSPQuoting = POSIX;
    else if (strcmp(F, "--rsp-quoting=windows") == 0)
      RSPQuoting = Windows;
  }

  bool UseWindowsQuoting = false;
  if (RSPQuoting == Default) {
    UseWindowsQuoting = isWindowsMSVCTarget(DefaultTargetTriple);
    if (!UseWindowsQuoting && !ProgName.empty()) {
      ParsedDriverName NameParts =
          ToolChain::getTargetPrefixFromProgramName(ProgName);
      UseWindowsQuoting = NameParts.TargetIsValid &&
                          isWindowsMSVCTarget(NameParts.TargetPrefix);
    }
    for (size_t I = 0, E = Args.size(); !UseWindowsQuoting && I != E; ++I) {
      llvm::StringRef Arg(Args[I]);
      if ((Arg == "-target" || Arg == "--target") && I + 1 < E) {
        UseWindowsQuoting = isWindowsMSVCTarget(Args[I + 1]);
        ++I;
        continue;
      }
      if (Arg.consume_front("-target=") || Arg.consume_front("--target="))
        UseWindowsQuoting = isWindowsMSVCTarget(Arg);
    }
  }

  llvm::cl::TokenizerCallback Tokenizer;
  if (RSPQuoting == Windows || (RSPQuoting == Default && UseWindowsQuoting))
    Tokenizer = &llvm::cl::TokenizeWindowsCommandLine;
  else
    Tokenizer = &llvm::cl::TokenizeGNUCommandLine;

  llvm::cl::ExpansionContext ECtx(Alloc, Tokenizer);
  ECtx.setMarkEOLs(false);
  if (FS)
    ECtx.setVFS(FS);

  if (llvm::Error Err = ECtx.expandResponseFiles(Args))
    return Err;

  return llvm::Error::success();
}
