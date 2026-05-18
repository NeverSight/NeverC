#include "neverc/Invoke/Job.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/InputInfo.h"
#include "neverc/Invoke/Tool.h"
#include "neverc/Invoke/ToolChain.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Config/config.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <string>
#include <system_error>
#include <utility>

using namespace neverc;
using namespace driver;

using llvm::ArrayRef;
using llvm::raw_ostream;
using llvm::SmallVectorImpl;
using llvm::StringRef;

// ===----------------------------------------------------------------------===
// Command
// ===----------------------------------------------------------------------===

Command::Command(const Action &Source, const Tool &Creator,
                 ResponseFileSupport ResponseSupport, const char *Executable,
                 const llvm::opt::ArgStringList &Arguments,
                 llvm::ArrayRef<InputInfo> Inputs,
                 llvm::ArrayRef<InputInfo> Outputs, const char *PrependArg)
    : Source(Source), Creator(Creator), ResponseSupport(ResponseSupport),
      Executable(Executable), PrependArg(PrependArg), Arguments(Arguments) {
  for (const auto &II : Inputs)
    if (II.isFilename())
      InputInfoList.push_back(II);
  for (const auto &II : Outputs)
    if (II.isFilename())
      OutputFilenames.push_back(II.getFilename());
}

namespace {
bool skipArgs(const char *Flag, bool HaveCrashVFS, int &SkipNum,
              bool &IsInclude) {
  SkipNum = 2;
  // These flags are all of the form -Flag <Arg> and are treated as two
  // arguments.  Therefore, we need to skip the flag and the next argument.
  bool ShouldSkip = llvm::StringSwitch<bool>(Flag)
                        .Cases("-MF", "-MT", "-MQ", true)
                        .Cases("-o", "-dependency-file", true)
                        .Case("-fdebug-compilation-dir", true)
                        .Cases("-dwarf-debug-flags", "-ivfsoverlay", true)
                        .Default(false);
  if (ShouldSkip)
    return true;

  // Some include flags shouldn't be skipped if we have a crash VFS
  IsInclude =
      llvm::StringSwitch<bool>(Flag)
          .Cases("-include", "-header-include-file", true)
          .Cases("-idirafter", "-internal-isystem", "-iwithprefix", true)
          .Cases("-internal-externc-isystem", "-iprefix", true)
          .Cases("-iwithprefixbefore", "-isystem", "-iquote", true)
          .Cases("-isysroot", "-I", "-F", "-resource-dir", true)
          .Case("-iframework", true)
          .Default(false);
  if (IsInclude)
    return !HaveCrashVFS;

  // The remaining flags are treated as a single argument.

  // These flags are all of the form -Flag and have no second argument.
  ShouldSkip = llvm::StringSwitch<bool>(Flag)
                   .Cases("-M", "-MM", "-MG", "-MP", "-MD", true)
                   .Case("-MMD", true)
                   .Default(false);

  // Match found.
  SkipNum = 1;
  if (ShouldSkip)
    return true;

  // These flags are treated as a single argument (e.g., -F<Dir>).
  llvm::StringRef FlagRef(Flag);
  IsInclude = FlagRef.starts_with("-F") || FlagRef.starts_with("-I");
  if (IsInclude)
    return !HaveCrashVFS;

  SkipNum = 0;
  return false;
}
} // namespace

void Command::writeResponseFile(llvm::raw_ostream &OS) const {
  // In a file list, we only write the set of inputs to the response file
  if (ResponseSupport.ResponseKind == ResponseFileSupport::RF_FileList) {
    for (const auto *Arg : InputFileList) {
      OS << Arg << '\n';
    }
    return;
  }

  // In regular response files, we send all arguments to the response file.
  // Wrapping all arguments in double quotes ensures that both Unix tools and
  // Windows tools understand the response file.
  for (const auto *Arg : Arguments) {
    OS << '"';

    for (; *Arg != '\0'; Arg++) {
      if (*Arg == '\"' || *Arg == '\\') {
        OS << '\\';
      }
      OS << *Arg;
    }

    OS << "\" ";
  }
}

void Command::buildArgvForResponseFile(
    llvm::SmallVectorImpl<const char *> &Out) const {
  // When not a file list, all arguments are sent to the response file.
  // This leaves us to set the argv to a single parameter, requesting the tool
  // to read the response file.
  if (ResponseSupport.ResponseKind != ResponseFileSupport::RF_FileList) {
    Out.push_back(Executable);
    Out.push_back(ResponseFileFlag.c_str());
    return;
  }

  llvm::StringSet<> Inputs;
  for (const auto *InputName : InputFileList)
    Inputs.insert(InputName);
  Out.push_back(Executable);

  if (PrependArg)
    Out.push_back(PrependArg);

  // In a file list, build args vector ignoring parameters that will go in the
  bool FirstInput = true;
  for (const auto *Arg : Arguments) {
    if (!Inputs.contains(Arg)) {
      Out.push_back(Arg);
    } else if (FirstInput) {
      FirstInput = false;
      Out.push_back(ResponseSupport.ResponseFlag);
      Out.push_back(ResponseFile);
    }
  }
}

namespace {
void rewriteIncludes(const llvm::ArrayRef<const char *> &Args, size_t Idx,
                     size_t NumArgs,
                     llvm::SmallVectorImpl<llvm::SmallString<128>> &IncFlags) {
  using namespace llvm;
  using namespace sys;

  auto getAbsPath = [](llvm::StringRef InInc,
                       llvm::SmallVectorImpl<char> &OutInc) -> bool {
    if (path::is_absolute(InInc)) // Nothing to do here...
      return false;
    std::error_code EC = fs::current_path(OutInc);
    if (EC)
      return false;
    path::append(OutInc, InInc);
    return true;
  };

  llvm::SmallString<128> NewInc;
  if (NumArgs == 1) {
    llvm::StringRef FlagRef(Args[Idx + NumArgs - 1]);
    assert((FlagRef.starts_with("-F") || FlagRef.starts_with("-I")) &&
           "Expecting -I or -F");
    llvm::StringRef Inc = FlagRef.slice(2, llvm::StringRef::npos);
    if (getAbsPath(Inc, NewInc)) {
      llvm::SmallString<128> NewArg(FlagRef.slice(0, 2));
      NewArg += NewInc;
      IncFlags.push_back(std::move(NewArg));
    }
    return;
  }

  assert(NumArgs == 2 && "Not expecting more than two arguments");
  llvm::StringRef Inc(Args[Idx + NumArgs - 1]);
  if (!getAbsPath(Inc, NewInc))
    return;
  IncFlags.push_back(llvm::SmallString<128>(Args[Idx]));
  IncFlags.push_back(std::move(NewInc));
}
} // namespace

void Command::Print(llvm::raw_ostream &OS, const char *Terminator, bool Quote,
                    CrashReportInfo *CrashInfo) const {
  // Always quote the exe.
  OS << ' ';
  llvm::sys::printArg(OS, Executable, /*Quote=*/true);

  llvm::ArrayRef<const char *> Args = Arguments;
  llvm::SmallVector<const char *, 128> ArgsRespFile;
  if (ResponseFile != nullptr) {
    buildArgvForResponseFile(ArgsRespFile);
    Args = llvm::ArrayRef<const char *>(ArgsRespFile)
               .slice(1); // no executable name
  } else if (PrependArg) {
    OS << ' ';
    llvm::sys::printArg(OS, PrependArg, /*Quote=*/true);
  }

  bool HaveCrashVFS = CrashInfo && !CrashInfo->VFSPath.empty();
  for (size_t i = 0, e = Args.size(); i < e; ++i) {
    const char *const Arg = Args[i];

    if (CrashInfo) {
      int NumArgs = 0;
      bool IsInclude = false;
      if (skipArgs(Arg, HaveCrashVFS, NumArgs, IsInclude)) {
        i += NumArgs - 1;
        continue;
      }

      // Relative includes need to be expanded to absolute paths.
      if (HaveCrashVFS && IsInclude) {
        llvm::SmallVector<llvm::SmallString<128>, 2> NewIncFlags;
        rewriteIncludes(Args, i, NumArgs, NewIncFlags);
        if (!NewIncFlags.empty()) {
          for (auto &F : NewIncFlags) {
            OS << ' ';
            llvm::sys::printArg(OS, F.c_str(), Quote);
          }
          i += NumArgs - 1;
          continue;
        }
      }

      auto Found = llvm::find_if(InputInfoList, [&Arg](const InputInfo &II) {
        return II.getFilename() == Arg;
      });
      if (Found != InputInfoList.end() &&
          (i == 0 || llvm::StringRef(Args[i - 1]) != "-main-file-name")) {
        // Replace the input file name with the crashinfo's file name.
        OS << ' ';
        llvm::StringRef ShortName =
            llvm::sys::path::filename(CrashInfo->Filename);
        llvm::sys::printArg(OS, ShortName.str(), Quote);
        continue;
      }
    }

    OS << ' ';
    llvm::sys::printArg(OS, Arg, Quote);
  }

  if (CrashInfo && HaveCrashVFS) {
    OS << ' ';
    llvm::sys::printArg(OS, "-ivfsoverlay", Quote);
    OS << ' ';
    llvm::sys::printArg(OS, CrashInfo->VFSPath.str(), Quote);
  }

  if (ResponseFile != nullptr) {
    OS << "\n Arguments passed via response file:\n";
    writeResponseFile(OS);
    // Avoiding duplicated newline terminator, since FileLists are
    // newline-separated.
    if (ResponseSupport.ResponseKind != ResponseFileSupport::RF_FileList)
      OS << "\n";
    OS << " (end of response file)";
  }

  OS << Terminator;
}

void Command::setResponseFile(const char *FileName) {
  ResponseFile = FileName;
  ResponseFileFlag = ResponseSupport.ResponseFlag;
  ResponseFileFlag += FileName;
}

void Command::setEnvironment(llvm::ArrayRef<const char *> NewEnvironment) {
  Environment.reserve(NewEnvironment.size() + 1);
  Environment.assign(NewEnvironment.begin(), NewEnvironment.end());
  Environment.push_back(nullptr);
}

void Command::setRedirectFiles(
    const std::vector<std::optional<std::string>> &Redirects) {
  RedirectFiles = Redirects;
}

void Command::PrintFileNames() const {
  if (PrintInputFilenames) {
    for (const auto &Arg : InputInfoList)
      llvm::outs() << llvm::sys::path::filename(Arg.getFilename()) << "\n";
    llvm::outs().flush();
  }
}

int Command::Execute(llvm::ArrayRef<llvm::StringRef> Redirects,
                     llvm::SmallVectorImpl<char> *ErrMsg, bool *ExecutionFailed,
                     llvm::sys::ProcessInfo &PI) const {
  PrintFileNames();

  llvm::SmallVector<const char *, 128> Argv;
  if (ResponseFile == nullptr) {
    Argv.push_back(Executable);
    if (PrependArg)
      Argv.push_back(PrependArg);
    Argv.append(Arguments.begin(), Arguments.end());
    Argv.push_back(nullptr);
  } else {
    // If the command is too large, we need to put arguments in a response file.
    std::string RespContents;
    llvm::raw_string_ostream SS(RespContents);

    // Write file contents and build the Argv vector
    writeResponseFile(SS);
    buildArgvForResponseFile(Argv);
    Argv.push_back(nullptr);
    SS.flush();

    // Save the response file in the appropriate encoding
    if (int EC = writeFileWithEncoding(ResponseFile, RespContents,
                                       ResponseSupport.ResponseEncoding)) {
      if (ErrMsg) {
        auto msg = std::error_code(EC, std::generic_category()).message();
        ErrMsg->assign(msg.begin(), msg.end());
      }
      if (ExecutionFailed)
        *ExecutionFailed = true;
      // Return -1 by convention (see llvm/include/llvm/Support/Program.h) to
      // indicate the requested executable cannot be started.
      PI.ReturnCode = -1;
      return -1;
    }
  }

  llvm::ArrayRef<llvm::StringRef> Env;
  std::vector<llvm::StringRef> ArgvVectorStorage;
  if (!Environment.empty()) {
    assert(Environment.back() == nullptr &&
           "Environment vector should be null-terminated by now");
    ArgvVectorStorage = llvm::toStringRefArray(Environment.data());
    Env = llvm::ArrayRef(ArgvVectorStorage);
  }

  auto Args = llvm::toStringRefArray(Argv.data());

  // Use Job-specific redirect files if they are present.
  llvm::ArrayRef<llvm::StringRef> EffectiveRedirects = Redirects;
  llvm::SmallVector<llvm::StringRef, 3> RedirectFilesVec;
  if (!RedirectFiles.empty()) {
    for (const auto &Ele : RedirectFiles)
      RedirectFilesVec.push_back(Ele ? llvm::StringRef(*Ele)
                                     : llvm::StringRef());
    EffectiveRedirects = RedirectFilesVec;
  }

  int R = llvm::sys::ExecuteAndWait(Executable, Args, Env, EffectiveRedirects,
                                    /*secondsToWait=*/0, /*memoryLimit=*/0,
                                    ErrMsg, ExecutionFailed, &ProcStat, nullptr,
                                    &PI, false);
  HasProcStat = true;
  return R;
}

FrontendCommand::FrontendCommand(const Action &Source, const Tool &Creator,
                                 ResponseFileSupport ResponseSupport,
                                 const char *Executable,
                                 const llvm::opt::ArgStringList &Arguments,
                                 llvm::ArrayRef<InputInfo> Inputs,
                                 llvm::ArrayRef<InputInfo> Outputs,
                                 const char *PrependArg)
    : Command(Source, Creator, ResponseSupport, Executable, Arguments, Inputs,
              Outputs, PrependArg) {
  InProcess = true;
}

// ===----------------------------------------------------------------------===
// FrontendCommand
// ===----------------------------------------------------------------------===

void FrontendCommand::Print(llvm::raw_ostream &OS, const char *Terminator,
                            bool Quote, CrashReportInfo *CrashInfo) const {
  OS << " (in-process)\n";
  Command::Print(OS, Terminator, Quote, CrashInfo);
}

int FrontendCommand::Execute(llvm::ArrayRef<llvm::StringRef> Redirects,
                             llvm::SmallVectorImpl<char> *ErrMsg,
                             bool *ExecutionFailed,
                             llvm::sys::ProcessInfo &PI) const {
  PrintFileNames();

  llvm::SmallVector<const char *, 128> Argv;
  Argv.push_back(getExecutable());
  Argv.append(getArguments().begin(), getArguments().end());

  if (ExecutionFailed)
    *ExecutionFailed = false;

  const Driver &D = getCreator().getToolChain().getDriver();
  assert(D.FrontendMain && "FrontendMain callback must be set before Execute");

  const DirectInvocationOpts &DO = getDirectOpts();
  const DirectInvocationOpts *Opts = hasAnyDirectOpts(DO) ? &DO : nullptr;

#if ENABLE_CRASH_OVERRIDES
  llvm::CrashRecoveryContext CRC;
  CRC.DumpStackAndCleanupOnFailure = true;
  const void *PrettyState = llvm::SavePrettyStackState();
  int R = 0;
  if (!CRC.RunSafely([&]() { R = D.FrontendMain(Argv, Opts); })) {
    llvm::RestorePrettyStackState(PrettyState);
    return CRC.RetCode;
  }
  return R;
#else
  return D.FrontendMain(Argv, Opts);
#endif
}

void FrontendCommand::setEnvironment(
    llvm::ArrayRef<const char *> NewEnvironment) {
  // We don't support setting a new environment for in-process frontend
  // execution
  llvm_unreachable(
      "The FrontendCommand doesn't support changing the environment vars!");
}

LinkerCommand::LinkerCommand(const Action &Source, const Tool &Creator,
                             ResponseFileSupport ResponseSupport,
                             const char *Executable,
                             const llvm::opt::ArgStringList &Arguments,
                             llvm::ArrayRef<InputInfo> Inputs,
                             LinkerFlavor Flavor,
                             llvm::ArrayRef<InputInfo> Outputs,
                             const char *PrependArg)
    : Command(Source, Creator, ResponseSupport, Executable, Arguments, Inputs,
              Outputs, PrependArg),
      Flavor(Flavor) {
  InProcess = true;
}

// ===----------------------------------------------------------------------===
// LinkerCommand
// ===----------------------------------------------------------------------===

void LinkerCommand::Print(llvm::raw_ostream &OS, const char *Terminator,
                          bool Quote, CrashReportInfo *CrashInfo) const {
  OS << " (in-process)\n";
  Command::Print(OS, Terminator, Quote, CrashInfo);
}

int LinkerCommand::Execute(llvm::ArrayRef<llvm::StringRef> Redirects,
                           llvm::SmallVectorImpl<char> *ErrMsg,
                           bool *ExecutionFailed,
                           llvm::sys::ProcessInfo &PI) const {
  PrintFileNames();

  llvm::SmallVector<const char *, 128> Argv;
  Argv.push_back(getExecutable());
  Argv.append(getArguments().begin(), getArguments().end());

  if (ExecutionFailed)
    *ExecutionFailed = false;

  const Driver &D = getCreator().getToolChain().getDriver();
  assert(D.LinkerMain && "LinkerMain callback must be set before Execute");

#if ENABLE_CRASH_OVERRIDES
  llvm::CrashRecoveryContext CRC;
  CRC.DumpStackAndCleanupOnFailure = true;
  const void *PrettyState = llvm::SavePrettyStackState();
  int R = 0;
  if (!CRC.RunSafely([&]() { R = D.LinkerMain(Argv, Flavor, DriverCfg); })) {
    llvm::RestorePrettyStackState(PrettyState);
    return CRC.RetCode;
  }
  return R;
#else
  return D.LinkerMain(Argv, Flavor, DriverCfg);
#endif
}

void LinkerCommand::setEnvironment(
    llvm::ArrayRef<const char *> NewEnvironment) {
  llvm_unreachable(
      "The LinkerCommand doesn't support changing the environment vars!");
}

ArchiveCommand::ArchiveCommand(const Action &Source, const Tool &Creator,
                               ResponseFileSupport ResponseSupport,
                               const char *Executable,
                               const llvm::opt::ArgStringList &Arguments,
                               llvm::ArrayRef<InputInfo> Inputs,
                               llvm::ArrayRef<InputInfo> Outputs)
    : Command(Source, Creator, ResponseSupport, Executable, Arguments, Inputs,
              Outputs) {
  InProcess = true;
}

// ===----------------------------------------------------------------------===
// ArchiveCommand
// ===----------------------------------------------------------------------===

void ArchiveCommand::Print(llvm::raw_ostream &OS, const char *Terminator,
                           bool Quote, CrashReportInfo *CrashInfo) const {
  OS << " (in-process archive)\n";
  Command::Print(OS, Terminator, Quote, CrashInfo);
}

int ArchiveCommand::Execute(llvm::ArrayRef<llvm::StringRef> Redirects,
                            llvm::SmallVectorImpl<char> *ErrMsg,
                            bool *ExecutionFailed,
                            llvm::sys::ProcessInfo &PI) const {
  PrintFileNames();

  if (ExecutionFailed)
    *ExecutionFailed = false;

  llvm::SmallVector<llvm::NewArchiveMember> Members;
  for (const auto &II : getInputInfos()) {
    if (!II.isFilename())
      continue;
    auto MemberOrErr = llvm::NewArchiveMember::getFile(II.getFilename(),
                                                       /*Deterministic=*/true);
    if (!MemberOrErr) {
      if (ErrMsg) {
        llvm::raw_svector_ostream OS(*ErrMsg);
        OS << llvm::toString(MemberOrErr.takeError());
      }
      if (ExecutionFailed)
        *ExecutionFailed = true;
      return 1;
    }
    Members.push_back(std::move(*MemberOrErr));
  }

  const auto &Outputs = getOutputFilenames();
  if (Outputs.empty()) {
    if (ExecutionFailed)
      *ExecutionFailed = true;
    return 1;
  }

  const auto &Triple = getCreator().getToolChain().getTriple();
  auto Kind = Triple.isOSDarwin()    ? llvm::object::Archive::K_DARWIN
              : Triple.isOSWindows() ? llvm::object::Archive::K_COFF
                                     : llvm::object::Archive::K_GNU;

  if (auto Err = llvm::writeArchive(Outputs[0], Members,
                                    llvm::SymtabWritingMode::NormalSymtab, Kind,
                                    /*Deterministic=*/true, /*Thin=*/false)) {
    if (ErrMsg) {
      llvm::raw_svector_ostream OS(*ErrMsg);
      OS << llvm::toString(std::move(Err));
    }
    if (ExecutionFailed)
      *ExecutionFailed = true;
    return 1;
  }
  return 0;
}

void ArchiveCommand::setEnvironment(
    llvm::ArrayRef<const char *> NewEnvironment) {
  llvm_unreachable(
      "ArchiveCommand doesn't support changing the environment vars!");
}

void JobList::Print(llvm::raw_ostream &OS, const char *Terminator, bool Quote,
                    CrashReportInfo *CrashInfo) const {
  for (const auto &Job : *this)
    Job.Print(OS, Terminator, Quote, CrashInfo);
}

void JobList::clear() { Jobs.clear(); }
