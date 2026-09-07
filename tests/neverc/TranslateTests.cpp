#include "../../neverc/lib/Translate/ArtifactWriter.h"
#include "../../neverc/lib/Translate/FrontendProcess.h"
#include "NeverCTestFixture.h"
#include "neverc/Translate/TranslateDriver.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Program.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

class TranslateTest : public NeverCTest {
protected:
  static std::string referenceCompiler() {
    const char *Value = std::getenv("NEVERC_CPP_REFERENCE_COMPILER");
    return Value ? Value : "";
  }

  fs::path fixture(const std::string &Name) {
    return testDir() / "Inputs" / "translate" / "cpp" / Name;
  }

  fs::path source() {
    const auto Source = tmpFile("input.cpp");
    writeFile(Source, "int main() { return 0; }\n");
    return Source;
  }

  std::vector<std::string> args(const fs::path &Source,
                                const std::vector<std::string> &Output) {
    std::vector<std::string> Args = {"translate", "--from", "cpp",
                                     Source.string()};
    Args.insert(Args.end(), Output.begin(), Output.end());
    return Args;
  }

  CmdResult translate(const fs::path &Source,
                      const std::vector<std::string> &Output) {
    auto Args = args(Source, Output);
    Args.insert(Args.end(), {"--", "-std=c++17"});
    return ncc(Args);
  }

  // Exercise corrupt/failed subprocess handling through the existing C++ API.
  // Production CLI always supplies its own executable; it exposes no override.
#ifndef _WIN32
  llvm::sys::ProcessInfo
  spawnControlledDriver(const fs::path &Source,
                        const std::vector<std::string> &Output,
                        const fs::path &Compiler, const fs::path &Stdout,
                        const fs::path &Stderr) {
    auto Storage = args(Source, Output);
    std::vector<const char *> Arguments;
    for (const auto &Argument : Storage)
      Arguments.push_back(Argument.c_str());
    const auto Executable = Compiler.string();
    const int OutFD =
        ::open(Stdout.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const int ErrFD =
        ::open(Stderr.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    llvm::sys::ProcessInfo Child;
    if (OutFD < 0 || ErrFD < 0) {
      if (OutFD >= 0)
        ::close(OutFD);
      if (ErrFD >= 0)
        ::close(ErrFD);
      return Child;
    }
    llvm::outs().flush();
    llvm::errs().flush();
    // The translator test binary runs these fixtures sequentially. Fork keeps
    // signal/error tests isolated from gtest without adding a production hook.
    Child.Pid = ::fork();
    if (Child.Pid == 0) {
      if (::dup2(OutFD, STDOUT_FILENO) < 0 || ::dup2(ErrFD, STDERR_FILENO) < 0)
        ::_exit(125);
      ::close(OutFD);
      ::close(ErrFD);
      const int Status = neverc::translate::runTranslate(
          Arguments.size(), Arguments.data(), Executable.c_str());
      llvm::outs().flush();
      llvm::errs().flush();
      ::_exit(Status);
    }
    ::close(OutFD);
    ::close(ErrFD);
    return Child;
  }
#endif

  CmdResult controlledDriver(const fs::path &Source,
                             const std::vector<std::string> &Output,
                             const fs::path &Compiler) {
    CmdResult Result;
#ifndef _WIN32
    const auto Stdout = tmpFile("controlled.stdout");
    const auto Stderr = tmpFile("controlled.stderr");
    auto Child =
        spawnControlledDriver(Source, Output, Compiler, Stdout, Stderr);
    if (Child.Pid <= 0)
      return Result;
    auto Ended = llvm::sys::Wait(Child, 3);
    Result.exitCode = Ended.ReturnCode;
    Result.out = readFile(Stdout);
    Result.err = readFile(Stderr);
#else
    auto Storage = args(Source, Output);
    std::vector<const char *> Arguments;
    for (const auto &Argument : Storage)
      Arguments.push_back(Argument.c_str());
    const auto Executable = Compiler.string();
    llvm::outs().flush();
    llvm::errs().flush();
    ::testing::internal::CaptureStdout();
    ::testing::internal::CaptureStderr();
    Result.exitCode = neverc::translate::runTranslate(
        Arguments.size(), Arguments.data(), Executable.c_str());
    llvm::outs().flush();
    llvm::errs().flush();
    Result.out = ::testing::internal::GetCapturedStdout();
    Result.err = ::testing::internal::GetCapturedStderr();
#endif
    return Result;
  }

  void expectCode(const CmdResult &Result, const std::string &Code) {
    EXPECT_NE(Result.exitCode, 0) << Result.out << Result.err;
    EXPECT_TRUE(Result.stderrContains(Code)) << Result.out << Result.err;
  }

  void expectNoArtifacts(const fs::path &Output) {
    for (const auto &Path : {Output, fs::path(Output.string() + ".map.json"),
                             fs::path(Output.string() + ".manifest.json")})
      EXPECT_FALSE(fs::exists(Path) || fs::is_symlink(Path)) << Path;
  }

  void expectJSONObject(const fs::path &Path) {
    ASSERT_TRUE(fs::is_regular_file(Path)) << Path;
    auto Parsed = llvm::json::parse(readFile(Path));
    ASSERT_TRUE(static_cast<bool>(Parsed))
        << llvm::toString(Parsed.takeError()).str().str();
    EXPECT_NE(Parsed->getAsObject(), nullptr);
  }

  void expectReport(const fs::path &Path, const std::string &Status) {
    auto Parsed = llvm::json::parse(readFile(Path));
    ASSERT_TRUE(static_cast<bool>(Parsed))
        << llvm::toString(Parsed.takeError()).str().str();
    const auto *Object = Parsed->getAsObject();
    ASSERT_NE(Object, nullptr);
    EXPECT_TRUE(Object->getString("schema") == "neverc.translate.report");
    EXPECT_TRUE(Object->getString("profile") == "cpp-core-v1");
    EXPECT_TRUE(Object->getString("status") == Status);
    int64_t Version = 0;
    ASSERT_TRUE(Object->getInteger("version", Version));
    EXPECT_EQ(Version, 1);
    EXPECT_FALSE(Object->getString("stage").empty());
    const auto *Mappings = Object->getArray("mappings");
    ASSERT_NE(Mappings, nullptr);
    EXPECT_TRUE(Mappings->empty());
    const auto *Diagnostics = Object->getArray("diagnostics");
    ASSERT_NE(Diagnostics, nullptr);
    EXPECT_EQ(Diagnostics->empty(), Status == "success");
    for (const auto &Value : *Diagnostics) {
      const auto *Diagnostic = Value.getAsObject();
      ASSERT_NE(Diagnostic, nullptr);
      for (const auto *Field :
           {"code", "file", "construct", "reason", "guidance"})
        EXPECT_FALSE(Diagnostic->getString(Field).empty()) << Field;
      for (const auto *Field : {"line", "column"}) {
        int64_t Location = 0;
        EXPECT_TRUE(Diagnostic->getInteger(Field, Location));
        EXPECT_GT(Location, 0);
      }
    }
  }

  void expectMetadata(const fs::path &Output, const fs::path &Source,
                      fs::path MapPath = {}, fs::path ManifestPath = {}) {
    ASSERT_TRUE(fs::is_regular_file(Output)) << Output;
    EXPECT_FALSE(fs::is_symlink(Output));
    if (MapPath.empty())
      MapPath = Output.string() + ".map.json";
    if (ManifestPath.empty())
      ManifestPath = Output.string() + ".manifest.json";
    ASSERT_TRUE(fs::is_regular_file(MapPath));
    ASSERT_TRUE(fs::is_regular_file(ManifestPath));
    EXPECT_FALSE(fs::is_symlink(MapPath));
    EXPECT_FALSE(fs::is_symlink(ManifestPath));
    const auto SourceHash = neverc::translate::sha256(readFile(Output));
    auto MapValue = llvm::json::parse(readFile(MapPath));
    ASSERT_TRUE(static_cast<bool>(MapValue))
        << llvm::toString(MapValue.takeError()).str().str();
    const auto *Map = MapValue->getAsObject();
    ASSERT_NE(Map, nullptr);
    EXPECT_TRUE(Map->getString("schema") == "neverc.translate.source-map");
    EXPECT_TRUE(Map->getString("generated_file") == Output.filename().string());
    EXPECT_TRUE(Map->getString("generated_sha256") == SourceHash);
    const auto *Entries = Map->getArray("entries");
    ASSERT_NE(Entries, nullptr);
    EXPECT_FALSE(Entries->empty());

    auto ManifestValue = llvm::json::parse(readFile(ManifestPath));
    ASSERT_TRUE(static_cast<bool>(ManifestValue))
        << llvm::toString(ManifestValue.takeError()).str().str();
    const auto *Manifest = ManifestValue->getAsObject();
    ASSERT_NE(Manifest, nullptr);
    EXPECT_TRUE(Manifest->getString("schema") == "neverc.translate.manifest");
    EXPECT_TRUE(Manifest->getString("profile") == "cpp-core-v1");
    int64_t Version = 0;
    ASSERT_TRUE(Manifest->getInteger("version", Version));
    EXPECT_EQ(Version, 1);
    for (const auto *Field :
         {"mappings", "required_headers", "required_modules"}) {
      const auto *Values = Manifest->getArray(Field);
      ASSERT_NE(Values, nullptr) << Field;
      EXPECT_TRUE(Values->empty()) << Field;
    }
    for (const auto *Field : {"frontend", "neverc", "target"})
      EXPECT_NE(Manifest->getObject(Field), nullptr) << Field;
    for (const auto *Field : {"dependencies", "exports", "source_options",
                              "compiler_options", "compilation_recipe"}) {
      const auto *Values = Manifest->getArray(Field);
      ASSERT_NE(Values, nullptr) << Field;
      EXPECT_FALSE(Values->empty()) << Field;
    }
    const auto *Dependencies = Manifest->getArray("dependencies");
    ASSERT_NE(Dependencies, nullptr);
    ASSERT_EQ(Dependencies->size(), 1u);
    const auto *Dependency = Dependencies->front().getAsObject();
    ASSERT_NE(Dependency, nullptr);
    EXPECT_TRUE(Dependency->getString("path") == Source.filename().string());
    EXPECT_TRUE(Dependency->getString("sha256") ==
                neverc::translate::sha256(readFile(Source)));
    const auto *Generated = Manifest->getArray("generated_files");
    ASSERT_NE(Generated, nullptr);
    ASSERT_EQ(Generated->size(), 2u);
    for (const auto &Value : *Generated) {
      const auto *File = Value.getAsObject();
      ASSERT_NE(File, nullptr);
      const auto Name = File->getString("path").str();
      EXPECT_TRUE(Name == Output.filename().string() ||
                  Name == MapPath.filename().string());
      EXPECT_TRUE(
          File->getString("sha256") ==
          neverc::translate::sha256(readFile(Output.parent_path() / Name)));
    }
  }

  CmdResult compileGenerated(const fs::path &Source, const fs::path &Output,
                             const std::string &Optimization,
                             const std::vector<std::string> &Extra = {}) {
    std::vector<std::string> Args = {
        Source.string(),         Optimization, "-fno-lto",
        "-fno-builtin-mimalloc", "-o",         Output.string()};
    Args.insert(Args.end(), Extra.begin(), Extra.end());
    return ncc(Args);
  }

  std::string nativeTarget() {
    auto Result = ncc({"-dumpmachine"});
    if (!Result.ok()) {
      ADD_FAILURE() << Result.err;
      return "";
    }
    while (!Result.out.empty() &&
           (Result.out.back() == '\n' || Result.out.back() == '\r'))
      Result.out.pop_back();
    return Result.out;
  }
};

TEST_F(TranslateTest, HelpDescribesTheBuiltInFrontend) {
  auto Result = ncc({"translate", "--help"});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(Result.contains("--from")) << Result.out;
  EXPECT_TRUE(Result.contains("--check")) << Result.out;
  EXPECT_TRUE(Result.contains("--profile")) << Result.out;
  EXPECT_TRUE(Result.contains("built into")) << Result.out;
  EXPECT_FALSE(Result.contains("--frontend")) << Result.out;
  EXPECT_FALSE(Result.contains("--cpp-sdk")) << Result.out;
}

TEST_F(TranslateTest, RequiresExplicitLanguageAndKnownProfile) {
  const auto Source = source();
  expectCode(ncc({"translate", Source.string(), "--check"}), "TR0001");
  expectCode(ncc({"translate", "--from", "python", Source.string(), "--check"}),
             "TR0002");
  expectCode(ncc({"translate", "--from", "cpp", Source.string(), "--check",
                  "--profile", "cpp-future-v99"}),
             "TR0003");
}

TEST_F(TranslateTest, RejectsConflictingOutputFormsBeforeStartingFrontend) {
  const auto Source = source();
  const auto Output = tmpFile("output.nc");
  const auto Directory = tmpFile("output");
  const std::vector<std::vector<std::string>> Cases = {
      {},
      {"-o", Output.string(), "--check"},
      {"-o", Output.string(), "--out-dir", Directory.string()},
      {"--check", "--out-dir", Directory.string()}};
  for (const auto &OutputArgs : Cases) {
    SCOPED_TRACE(OutputArgs.size());
    expectCode(ncc(args(Source, OutputArgs)), "TR0001");
    expectNoArtifacts(Output);
    EXPECT_FALSE(fs::exists(Directory));
  }
}

TEST_F(TranslateTest, RejectsUnsupportedSourceOptionsBeforeStartingFrontend) {
  const auto Source = source();
  for (const std::string &Flag : {"--target=x86_64-linux-gnu", "-ffast-math",
                                  "-fpack-struct=1", "-std=c++20"}) {
    SCOPED_TRACE(Flag);
    auto Args = args(Source, {"--check"});
    Args.insert(Args.end(), {"--", Flag});
    expectCode(ncc(Args), "TR0004");
  }
}

TEST_F(TranslateTest, RejectsExternalFrontendSelection) {
  const auto Source = source(), Output = tmpFile("output.nc");
  auto Args = args(Source, {"-o", Output.string()});
  Args.insert(Args.end(),
              {"--frontend", tmpFile("unavailable-helper").string()});
  expectCode(ncc(Args), "TR0001");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, MissingSelfExecutableFailsWithoutGeneratedOutput) {
  const auto Output = tmpFile("output.nc");
  expectCode(controlledDriver(source(), {"-o", Output.string()},
                              tmpFile("missing-neverc")),
             "TR0402");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, UsesRunningExecutableWhenArgvZeroIsUnrelated) {
  const auto Source = source(), Output = tmpFile("output.nc");
  const auto Unrelated = tmpFile("not-the-running-compiler");
  ASSERT_FALSE(fs::exists(Unrelated));
  auto Storage = args(Source, {"-o", Output.string()});
  Storage.insert(Storage.begin(), Unrelated.string());
  llvm::SmallVector<llvm::StringRef, 16> Arguments;
  for (const auto &Argument : Storage)
    Arguments.push_back(Argument);
  const auto Stdout = tmpFile("argv-zero.stdout").string();
  const auto Stderr = tmpFile("argv-zero.stderr").string();
  const llvm::StringRef Redirects[] = {"", Stdout, Stderr};
  llvm::SmallVector<char, 128> ExecutionError;
  // Program selects the real process; the first argument is only its name.
  const int Status = llvm::sys::ExecuteAndWait(
      neverc().string(), Arguments, {}, Redirects, 120, 0, &ExecutionError);
  ASSERT_EQ(Status, 0)
      << llvm::StringRef(ExecutionError.data(), ExecutionError.size()).str()
      << readFile(Stdout) << readFile(Stderr);
  expectMetadata(Output, Source);
}

TEST_F(TranslateTest, ExistingSourceMapIsNeverOverwritten) {
  const auto Output = tmpFile("output.nc");
  const auto Map = fs::path(Output.string() + ".map.json");
  writeFile(Map, "existing map\n");
  expectCode(ncc(args(source(), {"-o", Output.string()})), "TR0501");
  EXPECT_EQ(readFile(Map), "existing map\n");
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_FALSE(fs::exists(Output.string() + ".manifest.json"));
}

TEST_F(TranslateTest, ExistingManifestAndReportAreNeverOverwritten) {
  const auto Source = source();
  const auto Output = tmpFile("output.nc");
  const auto Manifest = fs::path(Output.string() + ".manifest.json");
  writeFile(Manifest, "existing manifest\n");
  expectCode(ncc(args(Source, {"-o", Output.string()})), "TR0501");
  EXPECT_EQ(readFile(Manifest), "existing manifest\n");

  const auto Report = tmpFile("existing-report.json");
  writeFile(Report, "existing report\n");
  expectCode(ncc(args(Source, {"--check", "--report", Report.string()})),
             "TR0501");
  EXPECT_EQ(readFile(Report), "existing report\n");
}

TEST_F(TranslateTest, ReportCannotAliasOutputOrInput) {
  const auto Source = source();
  const auto Original = readFile(Source);
  const auto Output = tmpFile("output.nc");
  expectCode(
      ncc(args(Source, {"-o", Output.string(), "--report", Output.string()})),
      "TR0501");
  expectCode(ncc(args(Source, {"--check", "--report", Source.string()})),
             "TR0501");
  EXPECT_EQ(readFile(Source), Original);
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, OutputCannotAliasInputAndExistingDirectoriesAreRefused) {
  // --from supplies the language, so a .nc-named input can still be C++ source.
  const auto Source = tmpFile("input.nc");
  writeFile(Source, "int main() { return 0; }\n");
  const auto Original = readFile(Source);
  expectCode(ncc(args(Source, {"-o", Source.string()})), "TR0501");
  const auto Directory = tmpFile("already-exists");
  fs::create_directory(Directory);
  expectCode(ncc(args(Source, {"--out-dir", Directory.string()})), "TR0501");
  EXPECT_TRUE(fs::is_empty(Directory));
  EXPECT_EQ(readFile(Source), Original);
}

TEST_F(TranslateTest,
       ExistingPrimaryAndSymlinkDestinationsAreNeverOverwritten) {
  const auto Source = source();
  const auto Output = tmpFile("output.nc");
  writeFile(Output, "existing generated source\n");
  expectCode(ncc(args(Source, {"-o", Output.string()})), "TR0501");
  EXPECT_EQ(readFile(Output), "existing generated source\n");

  const auto Alias = tmpFile("alias.nc");
  std::error_code Error;
  fs::create_symlink(Source, Alias, Error);
  if (Error)
    GTEST_SKIP() << "file symlinks unavailable: " << Error.message();
  const auto Original = readFile(Source);
  expectCode(ncc(args(Source, {"-o", Alias.string()})), "TR0501");
  EXPECT_TRUE(fs::is_symlink(Alias));
  EXPECT_EQ(readFile(Source), Original);
}

TEST_F(TranslateTest, MalformedAndIncompatibleFrontendResponsesAreRejected) {
  const auto Source = source();
  for (const auto *Response : {"{malformed", "{\\\"protocol\\\":99}"}) {
    SCOPED_TRACE(Response);
    const auto HelperSource = tmpFile("protocol-helper.c");
    const auto Helper = tmpFile("protocol-helper");
    writeFile(HelperSource,
              "#include <stdio.h>\nint main(int argc, char **argv) {\n"
              "if (argc != 6) return 2; FILE *file = fopen(argv[5], \"wb\");\n"
              "if (!file) return 3; fputs(\"" +
                  std::string(Response) +
                  "\", file); return fclose(file); }\n");
    auto Build = compileGenerated(HelperSource, Helper, "-O0");
    ASSERT_EQ(Build.exitCode, 0) << Build.err;
    const auto Output = tmpFile("output.nc");
    auto Result = controlledDriver(Source, {"-o", Output.string()}, Helper);
    expectCode(Result, "TR0103");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, FrontendProcessFailureIsDistinctFromInvalidSource) {
  const auto HelperSource = tmpFile("failed-helper.c");
  const auto Helper = tmpFile("failed-helper");
  writeFile(HelperSource, "int main(void) { return 17; }\n");
  auto Build = compileGenerated(HelperSource, Helper, "-O0");
  ASSERT_EQ(Build.exitCode, 0) << Build.err;
  const auto Source = source();
  const auto Output = tmpFile("output.nc");
  const auto Report = tmpFile("failed.json");
  auto Result = controlledDriver(
      Source, {"-o", Output.string(), "--report", Report.string()}, Helper);
  expectCode(Result, "TR0102");
  expectNoArtifacts(Output);
  expectReport(Report, "failed");
}

#ifndef _WIN32
TEST_F(TranslateTest, FifoSourceFailsPromptlyWithoutFrontendExecution) {
  const auto Source = tmpFile("input.cpp"), Output = tmpFile("output.nc");
  const auto Report = tmpFile("failed.json");
  ASSERT_EQ(::mkfifo(Source.c_str(), 0600), 0);
  const auto Stdout = tmpFile("stdout"), Stderr = tmpFile("stderr");
  neverc::translate::TranslationCancellation Cancellation;
  auto Result = neverc::translate::runProcess(
      {neverc().string(), "translate", "--from", "cpp", Source.string(), "-o",
       Output.string(), "--report", Report.string()},
      Stdout.string(), Stderr.string(), 3);
  ASSERT_NE(Result.ExitCode, -2) << "FIFO input blocked until the test timeout";
  EXPECT_NE(Result.ExitCode, 0);
  EXPECT_NE(readFile(Stderr).find("TR0502"), std::string::npos)
      << readFile(Stderr);
  EXPECT_NE(readFile(Stderr).find("regular file"), std::string::npos)
      << readFile(Stderr);
  expectNoArtifacts(Output);
  expectReport(Report, "failed");
  for (const auto &Entry : fs::directory_iterator(tmp()))
    EXPECT_NE(Entry.path().filename().string().find(".neverc-translate"), 0u);
}

TEST_F(TranslateTest, FifoFrontendResponseFailsPromptlyAndCleansStaging) {
  const auto Source = source(), Output = tmpFile("output.nc");
  const auto Report = tmpFile("failed.json"), Helper = tmpFile("fifo-helper");
  writeFile(Helper, "#!/bin/sh\nmkfifo \"$5\"\n");
  fs::permissions(Helper, fs::perms::owner_read | fs::perms::owner_write |
                              fs::perms::owner_exec);
  auto Result = controlledDriver(
      Source, {"-o", Output.string(), "--report", Report.string()}, Helper);
  ASSERT_NE(Result.exitCode, -2)
      << "FIFO response blocked until the test timeout";
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("TR0103"), std::string::npos);
  EXPECT_NE(Result.err.find("regular file"), std::string::npos);
  expectNoArtifacts(Output);
  expectReport(Report, "failed");
  for (const auto &Entry : fs::directory_iterator(tmp()))
    EXPECT_NE(Entry.path().filename().string().find(".neverc-translate"), 0u);
}

TEST_F(TranslateTest, DeepFrontendResponsesAreRejectedBeforeJSONRecursion) {
  const auto Source = source(), Output = tmpFile("output.nc");
  const auto Helper = tmpFile("deep-helper");
  writeFile(Helper.string() + ".json",
            std::string(20000, '[') + "0" + std::string(20000, ']'));
  for (int ExitStatus : {0, 1}) {
    SCOPED_TRACE(ExitStatus);
    writeFile(Helper, "#!/bin/sh\ncp \"$0.json\" \"$5\"\nexit " +
                          std::to_string(ExitStatus) + "\n");
    fs::permissions(Helper, fs::perms::owner_read | fs::perms::owner_write |
                                fs::perms::owner_exec);
    const auto Report =
        tmpFile("failed-" + std::to_string(ExitStatus) + ".json");
    auto Result = controlledDriver(
        Source, {"-o", Output.string(), "--report", Report.string()}, Helper);
    ASSERT_GT(Result.exitCode, 0) << "deep response must produce a diagnostic, "
                                     "not crash or reach timeout";
    EXPECT_NE(Result.err.find(ExitStatus ? "TR0102" : "TR0103"),
              std::string::npos)
        << Result.err;
    expectNoArtifacts(Output);
    expectReport(Report, "failed");
    for (const auto &Entry : fs::directory_iterator(tmp()))
      EXPECT_NE(Entry.path().filename().string().find(".neverc-translate"), 0u);
  }
}

TEST_F(TranslateTest,
       CancellationTerminatesTheInternalProcessAndCleansStaging) {
  const auto HelperSource = tmpFile("waiting-helper.c");
  const auto Helper = tmpFile("waiting-helper");
  writeFile(HelperSource,
            "#include <stdio.h>\n#include <unistd.h>\n"
            "int main(int argc, char **argv) {\n"
            "if (argc != 6) return 2; char path[4096];\n"
            "snprintf(path, sizeof(path), \"%s.pid\", argv[5]);\n"
            "FILE *file = fopen(path, \"w\"); if (!file) return 3;\n"
            "fprintf(file, \"%ld\\n\", (long)getpid()); fclose(file);\n"
            "for (;;) sleep(1); }\n");
  auto Build = compileGenerated(HelperSource, Helper, "-O0");
  ASSERT_EQ(Build.exitCode, 0) << Build.err;
  const auto Source = source();

  // Signal the isolated driver process, never the gtest process. Cleanup also
  // bounds a failing test: no controlled child is left running after assertion.
  struct ChildCleanup {
    pid_t Driver = 0;
    pid_t Helper = 0;
    ~ChildCleanup() {
      if (Helper > 0)
        (void)::kill(Helper, SIGKILL);
      if (Driver > 0) {
        (void)::kill(Driver, SIGKILL);
        while (::waitpid(Driver, nullptr, 0) == -1 && errno == EINTR) {
        }
      }
    }
  };

  for (int Signal : {SIGTERM, SIGINT}) {
    SCOPED_TRACE(Signal);
    const auto Output = tmpFile("cancelled.nc");
    const auto Report =
        tmpFile("cancelled-" + std::to_string(Signal) + ".json");
    const auto Stdout = tmpFile("cancel-stdout.txt").string();
    const auto Stderr = tmpFile("cancel-stderr.txt").string();
    llvm::SmallVector<char, 256> Message;
    auto Driver = spawnControlledDriver(
        Source, {"-o", Output.string(), "--report", Report.string()}, Helper,
        Stdout, Stderr);
    ASSERT_GT(Driver.Pid, 0);
    ChildCleanup Cleanup;
    Cleanup.Driver = Driver.Pid;

    fs::path Stage;
    const auto Deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < Deadline && Cleanup.Helper == 0) {
      for (const auto &Entry : fs::directory_iterator(tmp())) {
        if (Entry.is_directory() &&
            Entry.path().filename().string().find(".neverc-translate") == 0) {
          const auto PidFile = Entry.path() / "response.json.pid";
          if (fs::is_regular_file(PidFile)) {
            Cleanup.Helper = static_cast<pid_t>(
                std::strtol(readFile(PidFile).c_str(), nullptr, 10));
            Stage = Entry.path();
          }
        }
      }
      if (Cleanup.Helper == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GT(Cleanup.Helper, 0) << readFile(Stderr);
    ASSERT_EQ(::kill(Cleanup.Driver, Signal), 0);
    auto Ended = llvm::sys::Wait(Driver, 5, &Message, nullptr, true);
    ASSERT_EQ(Ended.Pid, Driver.Pid)
        << "cancelled driver did not exit promptly";
    Cleanup.Driver = 0;
    EXPECT_NE(Ended.ReturnCode, 0);
    EXPECT_NE(readFile(Stderr).find("TR0005"), std::string::npos);
    EXPECT_NE(readFile(Stderr).find("cancelled"), std::string::npos);
    errno = 0;
    const int HelperAlive = ::kill(Cleanup.Helper, 0);
    const int HelperError = errno;
    EXPECT_EQ(HelperAlive, -1) << "helper remains after cancellation";
    EXPECT_EQ(HelperError, ESRCH);
    if (HelperAlive == -1 && HelperError == ESRCH)
      Cleanup.Helper = 0;
    expectNoArtifacts(Output);
    EXPECT_FALSE(fs::exists(Stage));
    expectReport(Report, "failed");
  }
}
#endif

TEST_F(TranslateTest,
       ScalarProgramPublishesSidecarsAndRunsAtBothOptimizations) {
  const auto Output = tmpFile("program.nc");
  auto Result = translate(fixture("program.cpp"), {"-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  expectMetadata(Output, fixture("program.cpp"));
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("program" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CheckModeRunsValidationAndPublishesOnlyItsReport) {
  const auto Source = source();
  const auto Report = tmpFile("check-report.json");
  auto Result = translate(Source, {"--check", "--report", Report.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  expectReport(Report, "success");
  expectNoArtifacts(tmpFile("input.nc"));
  for (const auto &Entry : fs::directory_iterator(tmp()))
    EXPECT_EQ(Entry.path().filename().string().find(".neverc-translate"),
              std::string::npos)
        << Entry.path();
}

TEST_F(TranslateTest,
       HandlesSpacesAndUnicodeAndPublishesACleanOutputDirectory) {
  const auto Source = tmpFile(u8"源 input.cpp");
  writeFile(Source, "int main() { return 0; }\n");
  const auto Directory = tmpFile(u8"生成 output");
  const auto Report = tmpFile(u8"检查 report.json");
  auto Result = translate(
      Source, {"--out-dir", Directory.string(), "--report", Report.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::is_regular_file(Directory / u8"源 input.nc"));
  expectMetadata(Directory / u8"源 input.nc", Source,
                 Directory / "translate.map.json",
                 Directory / "translate-manifest.json");
  expectReport(Report, "success");
  EXPECT_EQ(std::distance(fs::directory_iterator(Directory),
                          fs::directory_iterator()),
            3);
}

TEST_F(TranslateTest, RejectsUnsupportedOwnedCodeWithoutPublishingArtifacts) {
  struct Rejection {
    const char *Name;
    const char *Source;
    const char *Code;
  };
  const Rejection Cases[] = {
      {"pointer", "int *f(int *p) { return p; }", "TR0201"},
      {"reference", "int f(int &p) { return p; }", "TR0201"},
      {"floating", "double f(double p) { return p; }", "TR0201"},
      {"array", "int f() { int p[2] = {1,2}; return p[0]; }", "TR0201"},
      {"enum", "enum Kind { First }; int main() { return 0; }", "TR0201"},
      {"unused-template",
       "template<class T> T f(T x) { return x; } int main() { return 0; }",
       "TR0201"},
      {"destructor", "struct S { ~S() {} }; int main() { return 0; }",
       "TR0201"},
      {"static-local", "int f() { static int value = 0; return ++value; }",
       "TR0201"},
      {"volatile", "int f() { volatile int value = 0; return value; }",
       "TR0201"},
      {"include", "#include \"owned.h\"\nint main() { return 0; }", "TR0201"},
      {"date-macro", "int main() { return sizeof(__DATE__) == 12 ? 0 : 1; }",
       "TR0201"},
      {"time-macro", "int main() { return sizeof(__TIME__) == 9 ? 0 : 1; }",
       "TR0201"},
      {"timestamp-macro",
       "int main() { return sizeof(__TIMESTAMP__) == 25 ? 0 : 1; }", "TR0201"},
      {"external-call", "int foreign(int); int f() { return foreign(1); }",
       "TR0203"},
      {"invalid-cpp", "int f( {", "TR0202"}};
  writeFile(tmpFile("owned.h"), "int hidden() { return 1; }\n");
  for (const auto &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    const auto Source = tmpFile(std::string(Case.Name) + ".cpp");
    const auto Output = tmpFile(std::string(Case.Name) + ".nc");
    const auto Report = tmpFile(std::string(Case.Name) + ".json");
    writeFile(Source, Case.Source);
    auto Result =
        translate(Source, {"-o", Output.string(), "--report", Report.string()});
    expectCode(Result, Case.Code);
    expectNoArtifacts(Output);
    expectReport(Report, "failed");
    EXPECT_NE(readFile(Report).find(Case.Code), std::string::npos);
  }
}

TEST_F(TranslateTest,
       ScalarModuleMatchesFullReferenceValuesAtBothOptimizations) {
  if (referenceCompiler().empty())
    GTEST_SKIP()
        << "set NEVERC_CPP_REFERENCE_COMPILER for independent comparison";
  const auto Output = tmpFile("module.nc");
  auto Result = translate(fixture("module.cpp"), {"-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_EQ(readFile(Output).find("int main("), std::string::npos);
  const auto Target = nativeTarget();
  ASSERT_FALSE(Target.empty());
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto ReferenceObject = tmpFile("reference" + Optimization + ".o");
    auto ReferenceCompile =
        exec(referenceCompiler(),
             {"--target=" + Target, "-std=c++17", Optimization, "-c",
              fixture("module.cpp").string(), "-o", ReferenceObject.string()});
    ASSERT_EQ(ReferenceCompile.exitCode, 0) << ReferenceCompile.err;
    const auto ReferenceExecutable = tmpFile("reference" + Optimization);
    auto ReferenceLink =
        compileGenerated(fixture("module-harness.c"), ReferenceExecutable,
                         Optimization, {ReferenceObject.string()});
    ASSERT_EQ(ReferenceLink.exitCode, 0) << ReferenceLink.err;

    const auto GeneratedObject = tmpFile("generated" + Optimization + ".o");
    auto GeneratedCompile =
        compileGenerated(Output, GeneratedObject, Optimization, {"-c"});
    ASSERT_EQ(GeneratedCompile.exitCode, 0) << GeneratedCompile.err;
    const auto GeneratedExecutable = tmpFile("generated" + Optimization);
    auto GeneratedLink =
        compileGenerated(fixture("module-harness.c"), GeneratedExecutable,
                         Optimization, {GeneratedObject.string()});
    ASSERT_EQ(GeneratedLink.exitCode, 0) << GeneratedLink.err;
    auto ReferenceRun = exec(ReferenceExecutable.string(), {});
    auto GeneratedRun = exec(GeneratedExecutable.string(), {});
    ASSERT_EQ(ReferenceRun.exitCode, 0) << ReferenceRun.err;
    ASSERT_EQ(GeneratedRun.exitCode, 0) << GeneratedRun.err;
    EXPECT_EQ(
        std::count(ReferenceRun.out.begin(), ReferenceRun.out.end(), '\n'),
        269);
    EXPECT_TRUE(ReferenceRun.contains("seed="));
    EXPECT_TRUE(ReferenceRun.contains(
        "signed 0 -2147483648\nsigned 1 -1073741824\nsigned 2 -4\n"
        "signed 3 -1\nsigned 4 -2147483648\n"))
        << ReferenceRun.out;
    EXPECT_EQ(GeneratedRun.out, ReferenceRun.out)
        << "logged seed and input pairs identify each failing case";
  }
}

TEST_F(TranslateTest,
       UnspecifiedArgumentOrderStaysInsideThePermittedResultSet) {
  const auto Output = tmpFile("order.nc");
  auto Result =
      translate(fixture("unspecified-order.cpp"), {"-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  const auto Harness = tmpFile("order-harness.c");
  writeFile(
      Harness,
      "#include <stdio.h>\nunsigned int translate_order(void);\n"
      "int main(void) { printf(\"%u\\n\", translate_order()); return 0; }\n");
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("order" + Optimization);
    auto Compile =
        compileGenerated(Output, Executable, Optimization, {Harness.string()});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.err;
    auto Run = exec(Executable.string(), {});
    ASSERT_EQ(Run.exitCode, 0) << Run.err;
    EXPECT_TRUE(Run.out == "12\n" || Run.out == "21\n") << Run.out;
  }
}

TEST_F(TranslateTest, RelocatingTheInputRootPreservesNormalizedArtifacts) {
  const auto FirstRoot = tmpFile("first root");
  const auto SecondRoot = tmpFile("second root");
  fs::create_directory(FirstRoot);
  fs::create_directory(SecondRoot);
  for (const auto &Root : {FirstRoot, SecondRoot}) {
    writeFile(Root / "module.cpp", readFile(fixture("module.cpp")));
    auto Result =
        translate(Root / "module.cpp", {"-o", (Root / "module.nc").string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
  }
  for (const std::string &Name :
       {"module.nc", "module.nc.map.json", "module.nc.manifest.json"}) {
    SCOPED_TRACE(Name);
    ASSERT_TRUE(fs::is_regular_file(FirstRoot / Name));
    ASSERT_TRUE(fs::is_regular_file(SecondRoot / Name));
    EXPECT_EQ(readFile(FirstRoot / Name), readFile(SecondRoot / Name));
  }
}

} // namespace
