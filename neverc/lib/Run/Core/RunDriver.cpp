#include "neverc/Run/RunDriver.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>
#include <utility>

using namespace llvm;

namespace neverc {
namespace run {
namespace {

bool isRunSource(StringRef Argument) {
  if (Argument.starts_with("-"))
    return false;
  StringRef Extension = sys::path::extension(Argument);
  return Extension == ".c" || Extension == ".nc";
}

void copyArguments(ArrayRef<StringRef> Arguments, size_t Begin, size_t End,
                   std::vector<std::string> &Destination) {
  Destination.reserve(Destination.size() + End - Begin);
  for (size_t I = Begin; I != End; ++I)
    Destination.push_back(Arguments[I].str());
}

class TemporaryRunDirectory {
public:
  explicit TemporaryRunDirectory(StringRef Path) : Path(Path) {}

  TemporaryRunDirectory(const TemporaryRunDirectory &) = delete;
  TemporaryRunDirectory &operator=(const TemporaryRunDirectory &) = delete;

  ~TemporaryRunDirectory() {
    if (Active)
      (void)sys::fs::remove_directories(Path, /*IgnoreErrors=*/false);
  }

  StringRef path() const { return Path; }

  Error cleanup() {
    if (!Active)
      return Error::success();
    if (std::error_code EC =
            sys::fs::remove_directories(Path, /*IgnoreErrors=*/false))
      return errorCodeToError(EC);
    Active = false;
    return Error::success();
  }

private:
  SmallString<256> Path;
  bool Active = true;
};

void printRunHelp() {
  outs() << "Usage:\n"
         << "  neverc run [compiler flags] file.c [file2.c ...] "
            "[program arguments...]\n"
         << "  neverc run [compiler arguments...] -- "
            "[program arguments...]\n\n"
         << "Compile a C program to a temporary executable, run it locally, "
            "and remove it.\n\n"
         << "Without '--', compiler flags must precede the first .c or .nc "
            "file; arguments\n"
         << "after the consecutive source list are passed to the program. "
            "Use '--' to\n"
         << "separate advanced compiler invocations unambiguously.\n\n"
         << "Examples:\n"
         << "  neverc run -O2 hello.c Alice\n"
         << "  neverc run hello.c helper.o -O2 -- Alice\n";
}

int executeProcess(StringRef Program, ArrayRef<StringRef> Arguments,
                   StringRef Description) {
  SmallVector<char, 256> Message;
  bool ExecutionFailed = false;
  int RC = sys::ExecuteAndWait(Program, Arguments, /*Env=*/std::nullopt,
                               /*Redirects=*/{}, /*SecondsToWait=*/0,
                               /*MemoryLimit=*/0, &Message, &ExecutionFailed);
  if (!ExecutionFailed && RC >= 0)
    return RC;

  errs() << "neverc run: error: " << Description;
  if (ExecutionFailed)
    errs() << " could not be started";
  else
    errs() << " terminated abnormally";
  StringRef Detail(Message.data(), Message.size());
  if (!Detail.empty())
    errs() << ": " << Detail;
  errs() << "\n";
  return 1;
}

int finishWithCleanup(TemporaryRunDirectory &Directory, int ExitCode) {
  if (Error E = Directory.cleanup()) {
    errs() << "neverc run: error: cannot remove temporary directory '"
           << Directory.path() << "': " << toString(std::move(E)) << "\n";
    return 1;
  }
  return ExitCode;
}

Expected<std::string> resolveCompilerPath(StringRef ExecutablePath) {
  SmallString<256> Resolved(ExecutablePath);
  if (sys::path::filename(Resolved) == Resolved) {
    ErrorOr<SmallString<256>> Found = sys::findProgramByName(Resolved);
    if (!Found)
      return errorCodeToError(Found.getError());
    Resolved = std::move(*Found);
  }
  if (std::error_code EC = sys::fs::make_absolute(Resolved))
    return errorCodeToError(EC);
  sys::path::remove_dots(Resolved, /*remove_dot_dot=*/true);
  return Resolved.str().str();
}

} // namespace

Expected<RunInvocation> parseRunArguments(ArrayRef<StringRef> Arguments) {
  RunInvocation Invocation;

  size_t Separator = Arguments.size();
  for (size_t I = 0; I != Arguments.size(); ++I) {
    if (Arguments[I] == "--") {
      Separator = I;
      break;
    }
  }

  if (Separator != Arguments.size()) {
    copyArguments(Arguments, 0, Separator, Invocation.CompilerArguments);
    copyArguments(Arguments, Separator + 1, Arguments.size(),
                  Invocation.ProgramArguments);
  } else {
    size_t FirstSource = Arguments.size();
    for (size_t I = 0; I != Arguments.size(); ++I) {
      if (isRunSource(Arguments[I])) {
        FirstSource = I;
        break;
      }
    }

    size_t CompilerEnd = Arguments.size();
    if (FirstSource != Arguments.size()) {
      CompilerEnd = FirstSource + 1;
      while (CompilerEnd != Arguments.size() &&
             isRunSource(Arguments[CompilerEnd]))
        ++CompilerEnd;
    }

    copyArguments(Arguments, 0, CompilerEnd, Invocation.CompilerArguments);
    copyArguments(Arguments, CompilerEnd, Arguments.size(),
                  Invocation.ProgramArguments);
  }

  if (Invocation.CompilerArguments.empty())
    return createStringError(inconvertibleErrorCode(),
                             "neverc run: no compiler input provided");

  return Invocation;
}

int runCommand(int Argc, const char **Argv, const char *ExecutablePath,
               const char *PrependArg) {
  if (Argc == 2 &&
      (StringRef(Argv[1]) == "-h" || StringRef(Argv[1]) == "--help")) {
    printRunHelp();
    return 0;
  }

  SmallVector<StringRef, 32> Arguments;
  for (int I = 1; I < Argc; ++I)
    Arguments.push_back(Argv[I]);

  Expected<RunInvocation> Parsed = parseRunArguments(Arguments);
  if (!Parsed) {
    errs() << toString(Parsed.takeError()) << "\n";
    return 1;
  }

  Expected<std::string> ResolvedCompiler = resolveCompilerPath(ExecutablePath);
  if (!ResolvedCompiler) {
    errs() << "neverc run: error: cannot resolve the NeverC executable: "
           << toString(ResolvedCompiler.takeError()) << "\n";
    return 1;
  }
  std::string Compiler = std::move(*ResolvedCompiler);

  SmallString<256> TemporaryPath;
  if (std::error_code EC =
          sys::fs::createUniqueDirectory("neverc-run", TemporaryPath)) {
    errs() << "neverc run: error: cannot create temporary directory: "
           << EC.message() << "\n";
    return 1;
  }
  TemporaryRunDirectory TemporaryDirectory(TemporaryPath);

  SmallString<256> TemporaryExecutable(TemporaryDirectory.path());
  bool UsesExeSuffix =
      sys::path::extension(Compiler).equals_insensitive(".exe");
  sys::path::append(TemporaryExecutable,
                    UsesExeSuffix ? "neverc-run.exe" : "neverc-run");

  SmallVector<StringRef, 32> CompilerArguments;
  CompilerArguments.push_back(Compiler);
  if (PrependArg != nullptr)
    CompilerArguments.push_back(PrependArg);
  for (const std::string &Argument : Parsed->CompilerArguments)
    CompilerArguments.push_back(Argument);
  CompilerArguments.push_back("-o");
  CompilerArguments.push_back(TemporaryExecutable);

  int CompilerExitCode =
      executeProcess(Compiler, CompilerArguments, "compiler");
  if (CompilerExitCode != 0)
    return finishWithCleanup(TemporaryDirectory, CompilerExitCode);

  if (!sys::fs::exists(TemporaryExecutable) ||
      !sys::fs::can_execute(TemporaryExecutable)) {
    errs() << "neverc run: error: compilation produced no runnable "
              "executable\n";
    return finishWithCleanup(TemporaryDirectory, 1);
  }

  SmallVector<StringRef, 16> ProgramArguments;
  ProgramArguments.push_back(TemporaryExecutable);
  for (const std::string &Argument : Parsed->ProgramArguments)
    ProgramArguments.push_back(Argument);

  int ProgramExitCode = executeProcess(TemporaryExecutable, ProgramArguments,
                                       "temporary program");
  return finishWithCleanup(TemporaryDirectory, ProgramExitCode);
}

} // namespace run
} // namespace neverc
