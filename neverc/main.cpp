#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/Session.h"
#include "neverc/Compiler/CompilerInvocation.h"
#include "neverc/Compiler/FrontendTool.h"
#include "neverc/Compiler/TextDiagnosticPrinter.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Config/config.h"
#include "neverc/Foundation/Core/Stack.h"
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.h"
#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/DirectInvocationOpts.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/Options.h"
#include "neverc/Invoke/ToolChain.h"
#include "neverc/Shellcode/Pipeline/DriverIntegration.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <set>

using llvm::ArrayRef;
using llvm::IntrusiveRefCntPtr;
using llvm::SmallString;
using llvm::SmallVector;
using llvm::SmallVectorImpl;
using llvm::StringRef;

using namespace neverc;
using namespace neverc::driver;
using namespace llvm::opt;

// ===----------------------------------------------------------------------===
// Linker backend registration
// ===----------------------------------------------------------------------===

#if defined(LINKER_ENABLE_DRIVER_COFF)
LINKER_HAS_DRIVER(coff)
#endif
#if defined(LINKER_ENABLE_DRIVER_ELF)
LINKER_HAS_DRIVER(elf)
#endif
#if defined(LINKER_ENABLE_DRIVER_MACHO)
LINKER_HAS_DRIVER(macho)
#endif

/// Resolve the executable path from argv[0], with optional canonicalization.
std::string GetExecutablePath(const char *Argv0, bool CanonicalPrefixes) {
  if (!CanonicalPrefixes) {
    SmallString<128> ExecutablePath(Argv0);
    if (!llvm::sys::fs::exists(ExecutablePath))
      if (auto P = llvm::sys::findProgramByName(ExecutablePath))
        ExecutablePath = *P;
    return std::string(ExecutablePath.str());
  }

  void *P = (void *)(intptr_t)GetExecutablePath;
  return llvm::sys::fs::getMainExecutable(Argv0, P);
}

// ===----------------------------------------------------------------------===
// Internal helpers (file-local)
// ===----------------------------------------------------------------------===

namespace {

// --- Linker driver table ---------------------------------------------------

const linker::DriverDef EnabledLinkerDrivers[] = {
#if defined(LINKER_ENABLE_DRIVER_COFF)
    {linker::Flavor::WinLink, &linker::coff::link},
#endif
#if defined(LINKER_ENABLE_DRIVER_ELF)
    {linker::Flavor::Gnu, &linker::elf::link},
#endif
#if defined(LINKER_ENABLE_DRIVER_MACHO)
    {linker::Flavor::Darwin, &linker::macho::link},
#endif
};

// --- String / path utilities ------------------------------------------------

const char *getStableCStr(std::set<std::string> &SavedStrings, StringRef S) {
  return SavedStrings.insert(std::string(S)).first->c_str();
}

void insertTargetPrefixArgs(const ParsedDriverName &NameParts,
                            SmallVectorImpl<const char *> &ArgVector,
                            std::set<std::string> &SavedStrings) {
  if (!NameParts.TargetIsValid)
    return;

  int InsertionPoint = ArgVector.size() > 0 ? 1 : 0;
  const char *arr[] = {"-target",
                       getStableCStr(SavedStrings, NameParts.TargetPrefix)};
  ArgVector.insert(ArgVector.begin() + InsertionPoint, std::begin(arr),
                   std::end(arr));
}

void fixupDiagPrefixExeName(TextDiagnosticPrinter *DiagClient,
                            const std::string &Path) {
  StringRef ExeBasename(llvm::sys::path::stem(Path));
  DiagClient->setPrefix(std::string(ExeBasename));
}

void setInstallDir(SmallVectorImpl<const char *> &argv, Driver &TheDriver,
                   bool CanonicalPrefixes) {
  SmallString<128> InstalledPath(argv[0]);

  if (llvm::sys::path::filename(InstalledPath) == InstalledPath)
    if (auto Tmp = llvm::sys::findProgramByName(
            llvm::sys::path::filename(InstalledPath.str())))
      InstalledPath = *Tmp;

  if (CanonicalPrefixes) {
    [[maybe_unused]] auto EC = llvm::sys::fs::make_absolute(InstalledPath);
  }

  StringRef InstalledPathParent(llvm::sys::path::parent_path(InstalledPath));
  if (llvm::sys::fs::exists(InstalledPathParent))
    TheDriver.setInstalledDir(InstalledPathParent);
}

// --- LLVM target initialization ---------------------------------------------

void initializeLLVMTargets() {
  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
  LLVMInitializeX86TargetMC();
  LLVMInitializeX86AsmPrinter();
  LLVMInitializeX86AsmParser();
  LLVMInitializeAArch64TargetInfo();
  LLVMInitializeAArch64Target();
  LLVMInitializeAArch64TargetMC();
  LLVMInitializeAArch64AsmPrinter();
  LLVMInitializeAArch64AsmParser();
}

// --- Argument processing ----------------------------------------------------

void handlePrintArguments(ArrayRef<const char *> Args) {
  for (auto Arg : Args) {
    if (StringRef(Arg) == "-fprint-arguments") {
      llvm::outs() << "compiler arguments:\n";
      for (auto A : Args)
        llvm::outs() << "\"" << A << "\",\n";
      return;
    }
  }
}

bool scanCanonicalPrefixes(ArrayRef<const char *> Args) {
  bool CanonicalPrefixes = true;
  for (int i = 1, size = Args.size(); i < size; ++i) {
    if (Args[i] == nullptr)
      continue;
    if (StringRef(Args[i]) == "-canonical-prefixes")
      CanonicalPrefixes = true;
    else if (StringRef(Args[i]) == "-no-canonical-prefixes")
      CanonicalPrefixes = false;
  }
  return CanonicalPrefixes;
}

// --- Driver configuration ---------------------------------------------------

/// Wire up the in-process frontend and linker callbacks.
void configureDriverCallbacks(Driver &TheDriver) {
  TheDriver.FrontendMain = [](SmallVectorImpl<const char *> &ArgV,
                              const DirectInvocationOpts *DirectOpts) {
    void *VP = (void *)(intptr_t)GetExecutablePath;
    return neverc::ExecuteFrontendDirect(ArrayRef(ArgV).slice(1), ArgV[0], VP,
                                         DirectOpts);
  };

  TheDriver.LinkerMain = [](SmallVectorImpl<const char *> &ArgV,
                            LinkerFlavor Flavor,
                            const linker::LinkerDriverConfig &DriverCfg) {
    auto It =
        llvm::find_if(EnabledLinkerDrivers, [=](const linker::DriverDef &D) {
          return D.f == Flavor;
        });
    if (It == std::end(EnabledLinkerDrivers)) {
      llvm::errs() << "neverc: error: linker backend for this target "
                      "was not enabled at build time\n";
      return 1;
    }
    ArrayRef<const char *> Args(ArgV.data(), ArgV.size());
    bool Ok = It->d(Args, llvm::outs(), llvm::errs(),
                    /*exitEarly=*/false, /*disableOutput=*/false, DriverCfg);
    linker::CommonLinkerContext::destroy();
    return Ok ? 0 : 1;
  };
}

// --- Compilation execution --------------------------------------------------

/// Parse --gen-reproducer=<level>.  Returns nullopt on unrecognised value.
std::optional<Driver::ReproLevel> parseReproLevel(Compilation &C) {
  Driver::ReproLevel Level = Driver::ReproLevel::OnCrash;

  if (Arg *A = C.getArgs().getLastArg(options::OPT_gen_reproducer_eq)) {
    auto Parsed =
        llvm::StringSwitch<std::optional<Driver::ReproLevel>>(A->getValue())
            .Case("off", Driver::ReproLevel::Off)
            .Case("crash", Driver::ReproLevel::OnCrash)
            .Case("error", Driver::ReproLevel::OnError)
            .Case("always", Driver::ReproLevel::Always)
            .Default(std::nullopt);
    if (!Parsed) {
      llvm::errs() << "Unknown value for " << A->getSpelling() << ": '"
                   << A->getValue() << "'\n";
      return std::nullopt;
    }
    Level = *Parsed;
  }

  return Level;
}

struct CompilationResult {
  int ExitCode = 1;
  bool IsCrash = false;
  Driver::CommandStatus Status = Driver::CommandStatus::Ok;
  const Command *FailingCommand = nullptr;
};

CompilationResult runCompilation(Driver &TheDriver, Compilation &C) {
  CompilationResult Result;

  if (!C.getJobs().empty())
    Result.FailingCommand = &*C.getJobs().begin();

  if (C.containsError())
    return Result;

  SmallVector<std::pair<int, const Command *>, 4> FailingCommands;
  Result.ExitCode = TheDriver.ExecuteCompilation(C, FailingCommands);

  for (const auto &[CommandRes, Cmd] : FailingCommands) {
    Result.FailingCommand = Cmd;
    if (!Result.ExitCode)
      Result.ExitCode = CommandRes;

    Result.IsCrash = CommandRes < 0 || CommandRes == 70;
#ifdef _WIN32
    Result.IsCrash |= CommandRes == 3;
#endif
#if LLVM_ON_UNIX
    Result.IsCrash |= CommandRes > 128;
#endif
    Result.Status = Result.IsCrash ? Driver::CommandStatus::Crash
                                   : Driver::CommandStatus::Error;
    if (Result.IsCrash)
      break;
  }
  return Result;
}

} // namespace

// ===----------------------------------------------------------------------===
// Entry point
// ===----------------------------------------------------------------------===

int neverc_main(int Argc, char **Argv, const llvm::ToolContext &ToolContext) {
  noteBottomOfStack();
  llvm::InitLLVM X(Argc, Argv);
  llvm::setBugReportMsg("PLEASE submit a bug report to " BUG_REPORT_URL
                        " and include the crash backtrace, preprocessed "
                        "source, and associated run script.\n");
  SmallVector<const char *, 256> Args(Argv, Argv + Argc);

  if (llvm::sys::Process::FixupStandardFileDescriptors())
    return 1;

  initializeLLVMTargets();

  if (Argc > 1 && StringRef(Argv[1]) == "-cc1") {
    void *VP = (void *)(intptr_t)GetExecutablePath;
    ArrayRef<const char *> CC1Args(Argv + 2, Argv + Argc);
    return neverc::ExecuteFrontendDirect(CC1Args, Argv[0], VP);
  }

  llvm::BumpPtrAllocator A;
  llvm::StringSaver Saver(A);
  const char *ProgName =
      ToolContext.NeedsPrependArg ? ToolContext.PrependArg : ToolContext.Path;

  if (llvm::Error Err = expandResponseFiles(
          Args, ProgName, llvm::sys::getDefaultTargetTriple(), A)) {
    llvm::errs() << toString(std::move(Err)) << '\n';
    return 1;
  }

  handlePrintArguments(Args);
  bool CanonicalPrefixes = scanCanonicalPrefixes(Args);
  std::set<std::string> SavedStrings;

  // --- Diagnostics ---
  std::string Path = GetExecutablePath(ToolContext.Path, CanonicalPrefixes);
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts =
      CreateAndPopulateDiagOpts(Args);
  auto *DiagClient = new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);
  fixupDiagPrefixExeName(DiagClient, ProgName);
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  DiagnosticsEngine Diags(DiagID, &*DiagOpts, DiagClient);
  ProcessWarningOptions(Diags, *DiagOpts, /*ReportDiags=*/false);

  // --- Driver ---
  Driver TheDriver(Path, llvm::sys::getDefaultTargetTriple(), Diags);
  setInstallDir(Args, TheDriver, CanonicalPrefixes);
  auto TargetPrefix = ToolChain::getTargetPrefixFromProgramName(ProgName);
  TheDriver.setTargetPrefixFromProgramName(TargetPrefix);
  if (ToolContext.NeedsPrependArg || CanonicalPrefixes)
    TheDriver.setPrependArg(ToolContext.PrependArg);
  insertTargetPrefixArgs(TargetPrefix, Args, SavedStrings);
  configureDriverCallbacks(TheDriver);
  llvm::CrashRecoveryContext::Enable();

  // --- Shellcode pipeline ---
  neverc::shellcode::CompilationState Shellcode;
  if (int Rc = neverc::shellcode::configureCompilation(Args, Shellcode))
    return Rc;

  // --- Build & execute ---
  std::unique_ptr<Compilation> C(TheDriver.CreateCompilation(Args));

  auto MaybeReproLevel = parseReproLevel(*C);
  if (!MaybeReproLevel)
    return 1;
  Driver::ReproLevel ReproLevel = *MaybeReproLevel;

  CompilationResult CR = runCompilation(TheDriver, *C);

  if (Shellcode.enabled()) {
    CR.ExitCode =
        neverc::shellcode::finalizeCompilation(Shellcode, CR.ExitCode);
    if (CR.ExitCode == 0)
      return 0;
  }

  if (CR.ExitCode == 0 && !CR.IsCrash) {
    Diags.getClient()->finish();
    llvm::outs().flush();
    llvm::errs().flush();
#ifdef NEVERC_PGO_TRAINING
    return 0;
#else
    _exit(0);
#endif
  }

  // --- Post-compilation (error/crash path only) ---
  if (CR.FailingCommand != nullptr &&
      TheDriver.maybeGenerateCompilationDiagnostics(CR.Status, ReproLevel, *C,
                                                    *CR.FailingCommand))
    CR.ExitCode = 1;

  Diags.getClient()->finish();

  if (CR.IsCrash) {
    llvm::BuryPointer(llvm::TimerGroup::aquireDefaultGroup());
  } else {
    llvm::TimerGroup::printAll(llvm::errs());
    llvm::TimerGroup::clearAll();
  }

#ifdef _WIN32
  if (CR.ExitCode < 0)
    CR.ExitCode = 1;
#endif

  return CR.ExitCode;
}
