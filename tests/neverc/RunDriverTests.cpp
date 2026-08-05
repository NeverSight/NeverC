#include "neverc/Run/RunDriver.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

#include <string>
#include <system_error>
#include <vector>

using namespace llvm;
using namespace neverc;

namespace {

std::string errorText(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return std::string(Message.begin(), Message.end());
}

template <typename T> T takeValue(Expected<T> Value) {
  if (!Value) {
    ADD_FAILURE() << errorText(Value.takeError());
    return {};
  }
  return std::move(*Value);
}

class ScratchDirectory {
public:
  SmallString<256> Path;

  ScratchDirectory() {
    EXPECT_FALSE(sys::fs::createUniqueDirectory("neverc-run-test", Path));
  }

  ~ScratchDirectory() { (void)sys::fs::remove_directories(Path); }

  SmallString<256> child(StringRef Name) const {
    SmallString<256> Result(Path);
    sys::path::append(Result, Name);
    return Result;
  }
};

class RemoveFileGuard {
public:
  explicit RemoveFileGuard(StringRef Path) : Path(Path) {}
  ~RemoveFileGuard() { (void)sys::fs::remove(Path); }

private:
  SmallString<256> Path;
};

void writeFile(StringRef Path, StringRef Contents) {
  std::error_code EC;
  raw_fd_ostream Output(Path, EC);
  ASSERT_FALSE(EC) << EC.message();
  Output << Contents;
}

std::string readFile(StringRef Path) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
      MemoryBuffer::getFile(Path, /*IsText=*/true);
  EXPECT_TRUE(Buffer) << Buffer.getError().message();
  return Buffer ? Buffer.get()->getBuffer().str() : std::string();
}

int executeNeverC(ArrayRef<StringRef> Tail,
                  ArrayRef<StringRef> Redirects = {}) {
  SmallVector<StringRef, 16> Args;
  Args.push_back(NEVERC_BINARY);
  Args.append(Tail.begin(), Tail.end());
  SmallVector<char, 256> Message;
  bool ExecutionFailed = false;
  int RC = sys::ExecuteAndWait(NEVERC_BINARY, Args, /*Env=*/std::nullopt,
                               Redirects, /*SecondsToWait=*/120,
                               /*MemoryLimit=*/0, &Message, &ExecutionFailed);
  EXPECT_FALSE(ExecutionFailed)
      << StringRef(Message.data(), Message.size()).str();
  return RC;
}

TEST(RunArgumentTest, SplitsGoStyleConsecutiveSourcesFromProgramArguments) {
  SmallVector<StringRef, 8> Args = {"-O2", "main.c", "helper.nc",
                                    "--program-flag", "value"};
  run::RunInvocation Invocation = takeValue(run::parseRunArguments(Args));

  EXPECT_EQ(Invocation.CompilerArguments,
            (std::vector<std::string>{"-O2", "main.c", "helper.nc"}));
  EXPECT_EQ(Invocation.ProgramArguments,
            (std::vector<std::string>{"--program-flag", "value"}));
}

TEST(RunArgumentTest, ExplicitSeparatorKeepsAdvancedCompilerArguments) {
  SmallVector<StringRef, 8> Args = {"main.c", "helper.o", "-lm",
                                    "--",     "arg.c",    "-x"};
  run::RunInvocation Invocation = takeValue(run::parseRunArguments(Args));

  EXPECT_EQ(Invocation.CompilerArguments,
            (std::vector<std::string>{"main.c", "helper.o", "-lm"}));
  EXPECT_EQ(Invocation.ProgramArguments,
            (std::vector<std::string>{"arg.c", "-x"}));
}

TEST(RunArgumentTest, LeavesNonFileCompilerInvocationIntact) {
  SmallVector<StringRef, 4> Args = {"-x", "c", "-"};
  run::RunInvocation Invocation = takeValue(run::parseRunArguments(Args));

  EXPECT_EQ(Invocation.CompilerArguments,
            (std::vector<std::string>{"-x", "c", "-"}));
  EXPECT_TRUE(Invocation.ProgramArguments.empty());
}

TEST(RunArgumentTest, DoesNotTreatDashPrefixedFlagAsSourceFile) {
  SmallVector<StringRef, 5> Args = {"-DGENERATED=.c", "-O2", "main.c",
                                    "argument"};
  run::RunInvocation Invocation = takeValue(run::parseRunArguments(Args));

  EXPECT_EQ(Invocation.CompilerArguments,
            (std::vector<std::string>{"-DGENERATED=.c", "-O2", "main.c"}));
  EXPECT_EQ(Invocation.ProgramArguments,
            (std::vector<std::string>{"argument"}));
}

TEST(RunArgumentTest, RejectsMissingCompilerInput) {
  SmallVector<StringRef, 1> Empty;
  Expected<run::RunInvocation> Missing = run::parseRunArguments(Empty);
  EXPECT_FALSE(Missing);
  consumeError(Missing.takeError());

  SmallVector<StringRef, 1> SeparatorOnly = {"--"};
  Expected<run::RunInvocation> MissingBeforeSeparator =
      run::parseRunArguments(SeparatorOnly);
  EXPECT_FALSE(MissingBeforeSeparator);
  consumeError(MissingBeforeSeparator.takeError());
}

TEST(NeverCRunIntegrationTest, ForwardsGoStyleArgumentsAndProgramExitCode) {
  ScratchDirectory Scratch;
  SmallString<256> Source = Scratch.child("exit_code.c");
  SmallString<256> Stdout = Scratch.child("stdout.txt");
  SmallString<256> Stderr = Scratch.child("stderr.txt");
  writeFile(Source, R"c(
static int equal(const char *a, const char *b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}

int main(int argc, char **argv) {
  return argc == 3 && equal(argv[1], "alpha") &&
                 equal(argv[2], "two words")
             ? 23
             : 91;
}
)c");

  SmallVector<StringRef, 8> Args = {"run", Source, "alpha", "two words"};
  StringRef Redirects[] = {StringRef(), Stdout, Stderr};
  EXPECT_EQ(executeNeverC(Args, Redirects), 23) << readFile(Stderr);
}

TEST(NeverCRunIntegrationTest, ExplicitSeparatorAllowsTrailingCompilerFlags) {
  ScratchDirectory Scratch;
  SmallString<256> Source = Scratch.child("explicit_separator.c");
  SmallString<256> Stdout = Scratch.child("stdout.txt");
  SmallString<256> Stderr = Scratch.child("stderr.txt");
  writeFile(Source, R"c(
int main(int argc, char **argv) {
  return argc == 2 && argv[1][0] == 'x' && argv[1][1] == 0 ? 17 : 92;
}
)c");

  SmallVector<StringRef, 8> Args = {"run", Source, "-O1", "--", "x"};
  StringRef Redirects[] = {StringRef(), Stdout, Stderr};
  EXPECT_EQ(executeNeverC(Args, Redirects), 17) << readFile(Stderr);
}

TEST(NeverCRunIntegrationTest, CompilationFailureNeverRunsProgram) {
  ScratchDirectory Scratch;
  SmallString<256> Source = Scratch.child("invalid.c");
  SmallString<256> Marker = Scratch.child("must-not-exist");
  SmallString<256> Stdout = Scratch.child("stdout.txt");
  SmallString<256> Stderr = Scratch.child("stderr.txt");
  writeFile(Source, R"c(
#error expected compile failure
#include <stdio.h>
int main(int argc, char **argv) {
  FILE *marker = fopen(argv[1], "wb");
  if (marker) fclose(marker);
  return 0;
}
)c");

  SmallVector<StringRef, 8> Args = {"run", Source, Marker};
  StringRef Redirects[] = {StringRef(), Stdout, Stderr};
  EXPECT_NE(executeNeverC(Args, Redirects), 0);
  EXPECT_FALSE(sys::fs::exists(Marker));
}

TEST(NeverCRunIntegrationTest, HelpIsRunSpecificAndDoesNotCompile) {
  ScratchDirectory Scratch;
  SmallString<256> Stdout = Scratch.child("stdout.txt");
  SmallString<256> Stderr = Scratch.child("stderr.txt");
  SmallVector<StringRef, 2> Args = {"run", "--help"};
  StringRef Redirects[] = {StringRef(), Stdout, Stderr};

  EXPECT_EQ(executeNeverC(Args, Redirects), 0) << readFile(Stderr);
  std::string Help = readFile(Stdout);
  EXPECT_NE(Help.find("neverc run"), std::string::npos);
  EXPECT_NE(Help.find("neverc run -O2 -fbuiltin-string hello.c"),
            std::string::npos);
}

TEST(NeverCRunIntegrationTest, InheritsWorkingDirectoryEnvironmentAndStdio) {
  ScratchDirectory Scratch;
  SmallString<256> Source = Scratch.child("inheritance.c");
  SmallString<256> Input = Scratch.child("stdin.txt");
  SmallString<256> Stdout = Scratch.child("stdout.txt");
  SmallString<256> Stderr = Scratch.child("stderr.txt");
  writeFile(Source, R"c(
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc != 2) return 81;
  FILE *marker = fopen(argv[1], "rb");
  if (!marker) return 82;
  fclose(marker);
  const char *path = getenv("PATH");
  if (!path || !*path) return 83;
  int input = getchar();
  printf("inherited:%c\n", input);
  return input == 'Q' ? 0 : 84;
}
)c");
  writeFile(Input, "Q");

  SmallString<256> CurrentDirectory;
  ASSERT_FALSE(sys::fs::current_path(CurrentDirectory));
  SmallString<256> MarkerModel(CurrentDirectory);
  sys::path::append(MarkerModel, "neverc-run-cwd-%%%%%%.marker");
  SmallString<256> Marker;
  int MarkerFD = -1;
  ASSERT_FALSE(sys::fs::createUniqueFile(MarkerModel, MarkerFD, Marker));
  ASSERT_EQ(sys::Process::SafelyCloseFileDescriptor(MarkerFD), 0);
  RemoveFileGuard MarkerCleanup(Marker);
  StringRef RelativeMarker = sys::path::filename(Marker);

  SmallVector<StringRef, 8> Args = {"run", Source, RelativeMarker};
  StringRef Redirects[] = {Input, Stdout, Stderr};
  EXPECT_EQ(executeNeverC(Args, Redirects), 0) << readFile(Stderr);
  EXPECT_NE(readFile(Stdout).find("inherited:Q"), std::string::npos);
}

} // namespace
