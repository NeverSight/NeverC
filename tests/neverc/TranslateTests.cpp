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
#include <tuple>
#include <utility>

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
    if (MapPath.empty()) {
      MapPath = Output;
      MapPath += ".map.json";
    }
    if (ManifestPath.empty()) {
      ManifestPath = Output;
      ManifestPath += ".manifest.json";
    }
    ASSERT_TRUE(fs::is_regular_file(MapPath));
    ASSERT_TRUE(fs::is_regular_file(ManifestPath));
    EXPECT_FALSE(fs::is_symlink(MapPath));
    EXPECT_FALSE(fs::is_symlink(ManifestPath));
    // Metadata filenames are UTF-8, independent of the Windows ANSI code page.
    // Keep the checked reader's default 32 MiB bound for these shared callers.
    auto OutputBytes = neverc::translate::readFile(Output.u8string());
    ASSERT_TRUE(static_cast<bool>(OutputBytes))
        << llvm::toString(OutputBytes.takeError()).str().str();
    const auto SourceHash = neverc::translate::sha256(*OutputBytes);
    auto MapBytes = neverc::translate::readFile(MapPath.u8string());
    ASSERT_TRUE(static_cast<bool>(MapBytes))
        << llvm::toString(MapBytes.takeError()).str().str();
    auto MapValue = llvm::json::parse(*MapBytes);
    ASSERT_TRUE(static_cast<bool>(MapValue))
        << llvm::toString(MapValue.takeError()).str().str();
    const auto *Map = MapValue->getAsObject();
    ASSERT_NE(Map, nullptr);
    EXPECT_TRUE(Map->getString("schema") == "neverc.translate.source-map");
    EXPECT_EQ(Map->getString("generated_file").str(),
              Output.filename().u8string());
    EXPECT_TRUE(Map->getString("generated_sha256") == SourceHash);
    const auto *Entries = Map->getArray("entries");
    ASSERT_NE(Entries, nullptr);
    EXPECT_FALSE(Entries->empty());

    auto ManifestBytes = neverc::translate::readFile(ManifestPath.u8string());
    ASSERT_TRUE(static_cast<bool>(ManifestBytes))
        << llvm::toString(ManifestBytes.takeError()).str().str();
    auto ManifestValue = llvm::json::parse(*ManifestBytes);
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
    EXPECT_EQ(Dependency->getString("path").str(),
              Source.filename().u8string());
    auto InputBytes = neverc::translate::readFile(Source.u8string());
    ASSERT_TRUE(static_cast<bool>(InputBytes))
        << llvm::toString(InputBytes.takeError()).str().str();
    EXPECT_EQ(Dependency->getString("sha256").str(),
              neverc::translate::sha256(*InputBytes));
    const auto *Generated = Manifest->getArray("generated_files");
    ASSERT_NE(Generated, nullptr);
    ASSERT_EQ(Generated->size(), 2u);
    for (const auto &Value : *Generated) {
      const auto *File = Value.getAsObject();
      ASSERT_NE(File, nullptr);
      const auto Name = File->getString("path").str();
      ASSERT_TRUE(Name == Output.filename().u8string() ||
                  Name == MapPath.filename().u8string())
          << "unexpected generated filename: " << Name
          << "; expected " << Output.filename().u8string() << " or "
          << MapPath.filename().u8string();
      const auto GeneratedPath = Output.parent_path() / fs::u8path(Name);
      ASSERT_TRUE(fs::is_regular_file(GeneratedPath)) << GeneratedPath;
      EXPECT_FALSE(fs::is_symlink(GeneratedPath)) << GeneratedPath;
      auto GeneratedBytes = neverc::translate::readFile(GeneratedPath.u8string());
      ASSERT_TRUE(static_cast<bool>(GeneratedBytes))
          << llvm::toString(GeneratedBytes.takeError()).str().str();
      EXPECT_EQ(File->getString("sha256").str(),
                neverc::translate::sha256(*GeneratedBytes));
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

TEST_F(TranslateTest, EmptySelfExecutableFailsWithoutGeneratedOutput) {
  const auto Output = tmpFile("output.nc");
  const auto Result =
      controlledDriver(source(), {"-o", Output.string()}, fs::path{});
  expectCode(Result, "TR0402");
  EXPECT_TRUE(Result.stderrContains(
      "cannot determine the running NeverC executable path"))
      << Result.out << Result.err;
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
  const std::string SourceName = u8"\u6e90 input.cpp";
  const std::string DirectoryName = u8"\u751f\u6210 output";
  const std::string ReportName = u8"\u68c0\u67e5 report.json";
  const std::string OutputName = u8"\u6e90 input.nc";
  const auto Source = tmp() / fs::u8path(SourceName);
  const auto Directory = tmp() / fs::u8path(DirectoryName);
  const auto Report = tmp() / fs::u8path(ReportName);
  const auto Output = Directory / fs::u8path(OutputName);
  ASSERT_EQ(Source.filename().u8string(), SourceName);
  ASSERT_EQ(Directory.filename().u8string(), DirectoryName);
  ASSERT_EQ(Report.filename().u8string(), ReportName);
  ASSERT_EQ(Output.filename().u8string(), OutputName);
  for (const auto &Path : {Source, Directory, Report, Output})
    ASSERT_EQ(fs::u8path(Path.u8string()), Path);
  writeFile(Source, "int main() { return 0; }\n");

  // This test supplies UTF-8 argv to the existing runner (W APIs on Windows).
  // The shared fixture's native-narrow command contract is unchanged.
  const auto Stdout = tmpFile("unicode.stdout");
  const auto Stderr = tmpFile("unicode.stderr");
  const auto StdoutUtf8 = Stdout.u8string(), StderrUtf8 = Stderr.u8string();
  const std::vector<std::string> Arguments{
      neverc().u8string(), "translate", "--from", "cpp", Source.u8string(),
      "--out-dir", Directory.u8string(), "--report", Report.u8string(),
      "--", "-std=c++17"};
  neverc::translate::ProcessResult Result;
  {
    neverc::translate::TranslationCancellation Cancellation;
    Result = neverc::translate::runProcess(Arguments, StdoutUtf8, StderrUtf8, 30);
  }
  std::string Out, Err, CaptureErrors;
  auto readCapture = [&](const fs::path &Path, std::string &Contents) {
    auto Bytes = neverc::translate::readFile(Path.u8string(), 1024 * 1024);
    if (!Bytes) {
      CaptureErrors += Path.filename().u8string() + ": " +
                       llvm::toString(Bytes.takeError()).str().str() + "\n";
      return false;
    }
    Contents = std::move(*Bytes);
    return true;
  };
  // Read both captures before asserting, retaining evidence from either one
  // even if launch/timeout or the other read failed. Every Error is consumed.
  const bool OutRead = readCapture(Stdout, Out);
  const bool ErrRead = readCapture(Stderr, Err);
  const auto Detail =
      "exit=" + std::to_string(Result.ExitCode) +
      "; cancelled=" + std::to_string(Result.Cancelled) +
      "; process error=" + Result.Error + "\n" + CaptureErrors +
      "stdout:\n" + Out + "\nstderr:\n" + Err;
  EXPECT_FALSE(Result.Cancelled) << Detail;
  EXPECT_TRUE(Result.Error.empty()) << Detail;
  ASSERT_EQ(Result.ExitCode, 0) << Detail;
  ASSERT_TRUE(OutRead) << Detail;
  ASSERT_TRUE(ErrRead) << Detail;
  EXPECT_NE(Out.find(SourceName), std::string::npos) << Detail;
  EXPECT_TRUE(fs::is_regular_file(Output));
  expectMetadata(Output, Source, Directory / "translate.map.json",
                 Directory / "translate-manifest.json");
  expectReport(Report, "success");
  EXPECT_EQ(std::distance(fs::directory_iterator(Directory),
                          fs::directory_iterator()),
            3);
}

TEST_F(TranslateTest, AggregateInitializationStoresFieldsInSourceOrder) {
  const auto Source = tmpFile("aggregate-order.cpp");
  writeFile(Source, R"cpp(
struct Pair { int x; int y; };
struct Nested { Pair pair; int last; };
int main() {
  Pair direct{3, direct.x + 4};
  if (direct.x != 3 || direct.y != 7) return 1;
  Nested nested{{11, nested.pair.x + 2}, nested.pair.y + 4};
  if (nested.pair.x != 11 || nested.pair.y != 13 || nested.last != 17)
    return 2;
  Pair assigned{5, 6};
  assigned = {7, assigned.x + 10};
  if (assigned.x != 7 || assigned.y != 15) return 3;
  Pair copied = direct;
  if (copied.x != 3 || copied.y != 7) return 4;
  return 0;
}
)cpp");
  for (const std::string &Profile : {"cpp-core-v1", "cpp-core-v2"}) {
    SCOPED_TRACE(Profile);
    const auto Output = tmpFile("aggregate-order-" + Profile + ".nc");
    auto Result = translate(Source, {"--profile", Profile, "-o", Output.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
    for (const std::string &Optimization : {"-O0", "-O2"}) {
      SCOPED_TRACE(Optimization);
      const auto Executable = tmpFile("aggregate-order-" + Profile + Optimization);
      auto Compile = compileGenerated(Output, Executable, Optimization,
                                      {"-fno-inline"});
      ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
      auto Run = exec(Executable.string(), {});
      EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
    }
  }
}

TEST_F(TranslateTest, CoreV2SwitchPreservesDispatchFallthroughAndLoopControl) {
  const auto Source = tmpFile("switch.cpp");
  const auto Output = tmpFile("switch.nc");
  writeFile(Source, R"cpp(
int pick(int &calls, int value) { ++calls; return value; }
int selection(int value, int &calls) {
  int result = 0;
  switch (int selected = pick(calls, value); selected) {
  case 0: result = 1; [[fallthrough]];
  case 1: result += 2; break;
  default: result = 9;
  case 4: result += 4; break;
  }
  return result;
}
int complete(int value) {
  switch (value) {case 1: return 7; default: return 11;}
}
int constant_match() {
  switch (1) {case 1: return 13;}
}
int nested_entries(int value) {
  switch (value) {
    return 99;
    int storage;
    {case 1: storage = 17; return storage;
     case 2: return 19;}
    default: return 23;
  }
}
struct Record { int first; int second; };
int declared_storage(int value) {
  switch (value) {
    return 99;
    Record record;
    Record records[2];
    case 1:
      record.first = 29;
      records[1].second = 31;
      return record.first + records[1].second;
    default: return 37;
  }
}
int embedded_case(int value) {
  int effects = 0;
  switch (value) {
    if (++effects == 0) {
      case 1: effects += 41; break;
    }
    while (++effects < 5) {
      case 2: effects += 43; break;
    }
    effects += 47;
    break;
    default: effects = 53;
  }
  return effects;
}
int main() {
  int calls = 0;
  if (selection(0, calls) != 3 || calls != 1) return 1;
  if (selection(1, calls) != 2 || calls != 2) return 2;
  if (selection(4, calls) != 4 || calls != 3) return 3;
  if (selection(8, calls) != 13 || calls != 4) return 4;
  int total = 0;
  for (int i = 0; i < 5; ++i) {
    switch (i) {
      case 1: continue;
      case 3: break;
      default: total += i;
    }
    total += 10;
  }
  if (total != 46) return 5;
  switch (1) {
    case 1:
      for (int j = 0; j < 3; ++j) {
        if (j == 1) break;
        total += 100;
      }
      total += 7;
      break;
    default: return 6;
  }
  if (total != 153) return 7;
  switch (1) {
    case 1:
      switch (2) {case 2: total += 11; break; default: return 8;}
      total += 13;
      break;
    default: return 9;
  }
  if (total != 177) return 10;
  int n = 0, sum = 0;
  while (n < 4) {
    ++n;
    switch (n) {case 1: continue; case 2: break; default: sum += n;}
    sum += 10;
  }
  if (sum != 37) return 11;
  n = 0; sum = 0;
  do {
    ++n;
    switch (n) {case 1: continue; default: sum += n;}
  } while (n < 3);
  if (sum != 5) return 12;
  if (complete(1) != 7 || complete(2) != 11 || constant_match() != 13)
    return 13;
  if (nested_entries(1) != 17 || nested_entries(2) != 19 ||
      nested_entries(3) != 23) return 14;
  if (declared_storage(1) != 60 || declared_storage(2) != 37) return 15;
  if (embedded_case(1) != 41 || embedded_case(2) != 90 ||
      embedded_case(3) != 53) return 16;
  enum class Mode : unsigned int { high = 0xffffffffu, low = 0u };
  Mode mode = Mode::high;
  switch (mode) {case Mode::high: total = 59; break; case Mode::low: return 17;}
  if (total != 59) return 18;
  switch (0xffffffffu) {case 0xffffffffu: total = 61; break; default: return 19;}
  if (total != 61) return 20;
  switch (int condition = pick(calls, 1)) {
    case 1: total += condition; break;
    default: return 21;
  }
  if (total != 62 || calls != 5) return 22;
  switch (pick(calls, 0)) { total = 99; }
  if (calls != 6 || total != 62) return 23;
  switch (7) {case 9: return 24;}
  switch (7) {case 9: return 25; default: total = 67;}
  if (total != 67) return 26;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("switch" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2SwitchRejectsUnsupportedCasesAndInvalidEntries) {
  struct Rejection { const char *Name; const char *Source; const char *Code; };
  const Rejection Cases[] = {
      {"switch-case-range",
       "int f(int n){switch(n){case 1 ... 3:return 7;default:return 9;}}", "TR0201"},
      {"switch-dead-range",
       "int f(int n){if(false){switch(n){case 1 ... 3:return 7;}}return 0;}", "TR0201"},
      {"switch-other-attribute",
       "int f(int n){switch(n){case 0:[[likely]];case 1:return 7;default:return 9;}}", "TR0201"},
      {"switch-folded-cast",
       "int f(int n){switch(n){case (void(0),1):return 7;default:return 9;}}", "TR0201"},
      {"switch-pointer-selector",
       "int f(int*p){switch(p){default:return 0;}}", "TR0202"},
      {"switch-nonconstant-case",
       "int f(int n,int v){switch(n){case v:return 7;default:return 9;}}", "TR0202"},
      {"switch-duplicate-case",
       "int f(int n){switch(n){case 1:return 7;case 1:return 9;}}", "TR0202"},
      {"switch-skipped-initialization",
       "int f(int n){switch(n){int value=3;case 1:return value;default:return 0;}}", "TR0202"},
  };
  for (const auto &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    const auto Source = tmpFile(std::string(Case.Name) + ".cpp");
    const auto Output = tmpFile(std::string(Case.Name) + ".nc");
    writeFile(Source, Case.Source);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Case.Code);
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("switch-v1.cpp");
  const auto Output = tmpFile("switch-v1.nc");
  writeFile(Source, "int f(int n){switch(n){default:return 0;}}");
  auto Result = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
  expectCode(Result, "TR0201");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2FixedArraysPreserveStorageAndInitialization) {
  const auto Source = tmpFile("arrays.cpp");
  const auto Output = tmpFile("arrays.nc");
  writeFile(Source, R"cpp(
using Row = int[3];
struct Record { int values[3]; int marker; };
struct Links { int *values[2]; };
struct Wrapper { Record record; int marker; };
struct Pair { int x; int y; };
int second(int values[3]) { return values[1]; }
Record make_record(int &calls) { ++calls; return {{47, 53}, 59}; }
Row &row(Row &value) { return value; }
Row *row_pointer(Row &value) { return &value; }
int redirect(int *&pointer, int *replacement) {
  pointer = replacement;
  return 1;
}
int main() {
  int values[3] = {1};
  if (values[0] != 1 || values[1] != 0 || values[2] != 0) return 1;
  int matrix[2][3] = {{1, 2}, {3}};
  if (matrix[0][0] != 1 || matrix[0][1] != 2 || matrix[0][2] != 0 ||
      matrix[1][0] != 3 || matrix[1][1] != 0 || matrix[1][2] != 0) return 2;
  row(values)[1] = 7;
  (*row_pointer(values))[2] = 9;
  if (values[1] != 7 || values[2] != 9) return 3;
  const Row &view = values;
  if (view[1] != 7) return 4;
  int other[3] = {80, 90, 100};
  int *pointer = values;
  int selected = pointer[redirect(pointer, other)];
  if (selected != 7 || pointer != other) return 5;
  pointer = values;
  int reversed = redirect(pointer, other)[pointer];
  if (reversed != 90) return 13;
  Record partial;
  Record *record = &partial;
  record->values[1] = 11;
  if (record->values[1] != 11) return 6;
  Record complete{{13, 17}, 19};
  Record copy = complete;
  copy.values[0] = 23;
  if (complete.values[0] != 13 || copy.values[0] != 23 ||
      copy.values[1] != 17 || copy.values[2] != 0 || copy.marker != 19) return 7;
  const Record *readonly = &copy;
  const int *element_view = readonly->values;
  if (element_view[0] != 23) return 8;
  int left = 29, right = 31;
  Links links{{&left, &right}};
  *links.values[1] = 37;
  if (right != 37) return 9;
  int *const fixed[2] = {&left, &right};
  int *const *fixed_view = fixed;
  **fixed_view = 41;
  if (left != 41) return 10;
  bool flags[3] = {true};
  int *pointers[2] = {};
  if (!flags[0] || flags[1] || flags[2] || pointers[0] || pointers[1]) return 11;
  enum class E:int { value=43 };
  E enums[2] = {E::value};
  if (static_cast<int>(enums[0]) != 43 || static_cast<int>(enums[1]) != 0)
    return 12;
  Record observed{{5, observed.values[0] + 2}, observed.values[1] + 3};
  if (observed.values[0] != 5 || observed.values[1] != 7 ||
      observed.values[2] != 0 || observed.marker != 10) return 14;
  Record records[2] = {{{2, 3}, 4}, {{5}, 6}};
  records[1].values[2] = 7;
  if (records[0].values[0] != 2 || records[0].values[1] != 3 ||
      records[0].values[2] != 0 || records[0].marker != 4 ||
      records[1].values[0] != 5 || records[1].values[1] != 0 ||
      records[1].values[2] != 7 || records[1].marker != 6) return 15;
  const int constants[2][3] = {{11}, {13, 17}};
  const int (*constant_rows)[3] = constants;
  if (constant_rows[0][0] != 11 || constant_rows[0][1] != 0 ||
      constant_rows[1][0] != 13 || constant_rows[1][1] != 17 ||
      constant_rows[1][2] != 0) return 16;
  int calls = 0;
  if (make_record(calls).values[1] != 53 || calls != 1) return 17;
  if (Record{{61, 67}, 71}.values[0] != 61) return 18;
  int self[3] = {5, self[0] + 2, self[1] + 4};
  if (self[0] != 5 || self[1] != 7 || self[2] != 11) return 19;
  Record assigned{{1}, 2};
  assigned = {{3, assigned.values[0] + 4}, assigned.marker + 5};
  if (assigned.values[0] != 3 || assigned.values[1] != 5 ||
      assigned.values[2] != 0 || assigned.marker != 7) return 20;
  Pair pair = Pair{3, pair.x + 4};
  if (pair.x != 3 || pair.y != 7) return 21;
  Record typed = Record{{5, typed.values[0] + 2}, typed.values[1] + 3};
  if (typed.values[0] != 5 || typed.values[1] != 7 || typed.marker != 10)
    return 22;
  Wrapper nested{Record{{7, nested.record.values[0] + 4},
                        nested.record.values[1] + 2}, nested.record.marker + 4};
  if (nested.record.values[0] != 7 || nested.record.values[1] != 11 ||
      nested.record.marker != 13 || nested.marker != 17) return 23;
  if (second(values) != 7) return 24;
  for (int choose = 0; choose < 2; ++choose) {
    int effects = 0;
    Pair selected = choose ? Pair{3, selected.x + 4}
                           : Pair{5, selected.x + 6};
    if (selected.x != (choose ? 3 : 5) ||
        selected.y != (choose ? 7 : 11)) return 25;
    Pair sequenced = (++effects, Pair{13, sequenced.x + 4});
    if (effects != 1 || sequenced.x != 13 || sequenced.y != 17) return 26;
  }
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("arrays" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ArrayScopeRejectsUnsupportedStorage) {
  struct Rejection {
    const char *Name;
    const char *Source;
    const char *Code;
  };
  const Rejection Cases[] = {
      {"zero-array",
       "int f(){int a[0]; return 0;}", "TR0201"},
      {"variable-array",
       "int f(int n){int a[n]; return 0;}", "TR0201"},
      {"dead-variable-array",
       "int f(int n){if(false){int a[n];} return 0;}", "TR0201"},
      {"unsupported-element",
       "int f(){float a[2]; return 0;}", "TR0201"},
      {"global-array",
       "const int a[2]={1,2};", "TR0201"},
      {"global-array-field",
       "struct R{int a[2];}; constexpr R r{{1,2}};", "TR0201"},
      {"array-alias-bound",
       "using TooLarge=int[65537]; int main(){}", "TR0201"},
      {"nested-array-expansion",
       "using TooLarge=int[65536][65536]; int main(){}", "TR0201"},
      {"const-array-write",
       "void f(){const int a[2]={1,2}; a[0]=3;}", "TR0202"},
      {"const-record-array-write",
       "struct R{int a[2];}; void f(const R*p){p->a[0]=3;}", "TR0202"},
      {"array-temporary-dereference",
       "struct R{int a[2];}; int f(){const int &r=*R{{1,2}}.a; return r;}", "TR0201"},
      {"array-initialization-budget",
       "int f(){int a[65536]={}; return a[0];}", "TR0201"}
  };
  for (const auto &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    const auto Source = tmpFile(std::string(Case.Name) + ".cpp");
    const auto Output = tmpFile(std::string(Case.Name) + ".nc");
    writeFile(Source, Case.Source);
    auto Result = translate(
        Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Case.Code);
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2PointersAndReferencesPreserveAliasedStorage) {
  const auto Source = tmpFile("references.cpp");
  const auto Output = tmpFile("references.nc");
  writeFile(Source, R"cpp(
const int constant = 17;
struct Box { int *value; int marker; };
struct Self { int value; int *alias; int observed; };
int &select(bool first, int &left, int &right) {
  return first ? left : right;
}
int &increment(int &value) {
  ++value;
  return value;
}
int *&identity(int *&pointer) { return pointer; }
const int *qualified_alias(int **p, const int *const *q, int *replacement) {
  *p = replacement;
  return *q;
}
void update(int *pointer, int &alias) {
  *pointer += 2;
  alias += 3;
}
int main() {
  int left = 1;
  int right = 7;
  int *pointer = &left;
  int &alias = left;
  update(pointer, alias);
  if (left != 6 || *pointer != 6 || alias != 6) return 1;
  increment(left) = 20;
  if (left != 20) return 2;
  select(false, left, right) = 31;
  if (left != 20 || right != 31) return 3;
  int *selected = &select(true, left, right);
  *selected = 23;
  if (left != 23) return 4;
  Box partial;
  Box *box = &partial;
  box->value = pointer;
  *box->value = 29;
  if (left != 29) return 5;
  const int *view = &constant;
  if (*view != 17) return 6;
  int *empty = ((nullptr));
  if (empty || empty != nullptr || !pointer) return 7;
  empty = 0;
  if (!(empty == nullptr)) return 8;
  int *zero{};
  if (zero != empty) return 9;
  int *const fixed = pointer;
  int *const *nested = &fixed;
  **nested = 37;
  if (left != 37) return 10;
  const int *readonly = pointer;
  int *writable = const_cast<int *>(readonly);
  *writable = 41;
  void *erased = pointer;
  int *restored = static_cast<int *>(erased);
  if (*restored != 41) return 11;
  Box complete{pointer, 9};
  Box copy = complete;
  *copy.value = 43;
  if (left != 43 || copy.marker != 9) return 12;
  identity(pointer) = &right;
  if (pointer != &right || *pointer != 31) return 13;
  const Box *constbox = &copy;
  *constbox->value = 47;
  if (left != 47) return 14;
  int &assigned = (left = 49);
  int &comma = (right = 51, left);
  int &prefix = ++left;
  if (&assigned != &left || &comma != &left || &prefix != &left || left != 50)
    return 15;
  const int &qualified = left;
  const_cast<int &>(qualified) = 53;
  if (left != 53) return 16;
  enum class E:unsigned int { value=0xffffffffu };
  E e=E::value;
  E *ep=&e;
  E &er=*ep;
  er=E{3u};
  if (static_cast<unsigned int>(e)!=3u) return 17;
  pointer = &left;
  const int *const *qualified_pointer = &pointer;
  if (qualified_alias(&pointer, qualified_pointer, &right) != &right)
    return 18;
  pointer = &left;
  *(pointer = &right) = *pointer;
  if (right != 53) return 19;
  Self self{59, &self.value, *self.alias};
  if (self.value != 59 || self.alias != &self.value || self.observed != 59)
    return 20;
  return 0;
}
)cpp");
  auto Result = translate(
      Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("references" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization,
                                    {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PointerArithmeticPreservesStridesAndSequencing) {
  const auto Source = tmpFile("pointer-arithmetic.cpp");
  const auto Output = tmpFile("pointer-arithmetic.nc");
  writeFile(Source, R"cpp(
using Distance = decltype(static_cast<int *>(nullptr) - static_cast<int *>(nullptr));
using Size = decltype(sizeof(int));
struct Cell { signed char tag; long long value; };
struct Holder { Cell *pointer; };
int *offset(int *p, long long n) { return p + n; }
int *back(int *p, long long n) { return p - n; }
Distance distance(const int *a, const int *b) { return a - b; }
int &tick(int &trace, int digit) { trace = trace * 10 + digit; return trace; }
int choose(int &which, int &trace) { tick(trace, 1); which = 1; return 2; }
int *&slot(int *&first, int *&second, int &which, int &trace) {
  tick(trace, 2);
  return which == 0 ? first : second;
}
int *called_pointer(int *p, int &trace) { tick(trace, 1); return p; }
int called_index(int &trace) { tick(trace, 2); return 1; }
int main() {
  int *null = nullptr;
  if (offset(null, 0) != nullptr || back(null, 0) != nullptr ||
      0 + null != nullptr || distance(null, null) != 0) return 1;
  null += 0;
  null -= 0ull;
  if (null != nullptr) return 2;
  int scalar = 41;
  int *one_past = &scalar + 1;
  if (*(one_past - 1) != 41 || one_past - &scalar != 1 ||
      &scalar - one_past != -1) return 3;
  int values[5] = {2, 3, 5, 7, 11};
  int *begin = values;
  int *end = values + 5;
  int total = 0;
  for (int *it = begin; it != end; ++it) total += *it;
  if (total != 28 || end - begin != 5 || begin - end != -5) return 4;
  int *p = begin;
  int *old = p++;
  if (old != begin || p != begin + 1) return 5;
  old = p--;
  if (old != begin + 1 || p != begin) return 6;
  int *&same = ++p;
  same += static_cast<unsigned char>(2);
  if (p != begin + 3) return 7;
  --p;
  p -= static_cast<short>(1);
  if (p != begin + 1) return 8;
  if (offset(p, -1) != begin || back(p, -1) != begin + 2) return 9;
  unsigned long long wide = 4;
  if (*(begin + wide) != 11 || *(wide + begin) != 11 ||
      *(end - static_cast<Size>(1)) != 11) return 10;
  const int *constant = end;
  if (constant - begin != 5 || begin - constant != -5 ||
      *(constant - 2) != 7 || (constant + 0) != end) return 11;
  int matrix[2][3] = {{1, 2, 3}, {5, 7, 11}};
  int (*row)[3] = matrix;
  int (*next_row)[3] = row + 1;
  if ((*next_row)[2] != 11 || next_row - row != 1 ||
      row - next_row != -1) return 12;
  const int (*const_row)[3] = matrix;
  if ((const_row + 2) - row != 2 || (*(const_row + 1))[1] != 7) return 13;
  Cell cells[2] = {{1, 17}, {2, 23}};
  Cell *cell = cells;
  cell++;
  if (cell->value != 23 || cell - cells != 1 || (cell - 1)->tag != 1) return 14;
  int *pointers[2] = {begin, end};
  int **iterator = pointers;
  if (*(iterator + 1) != end || (iterator + 2) - iterator != 2) return 15;
  int *first = begin;
  int *second = begin;
  int which = 0, trace = 0;
  slot(first, second, which, trace) += choose(which, trace);
  if (trace != 12 || first != begin || second != begin + 2) return 16;
  trace = 0;
  old = slot(first, second, which, trace)++;
  if (trace != 2 || old != begin + 2 || second != begin + 3) return 17;
  trace = 0;
  int *from_calls = called_pointer(begin, trace) + called_index(trace);
  if (from_calls != begin + 1 || (trace != 12 && trace != 21)) return 18;
  int index = 0;
  int *reversed = &(index = 1)[(index = 2, values)];
  if (reversed != begin + 1 || index != 2) return 19;
  int *formed = &values[index++];
  if (formed != begin + 2 || index != 3) return 20;
  int *formed_end = &values[5];
  if (formed_end != end || &*end != end || formed_end - begin != 5) return 21;
  int *const *qualified_iterator = pointers;
  if (qualified_iterator + 1 != &pointers[1]) return 22;
  // Address cancellation is the documented selected null behavior. No load,
  // store, member access or invalid element is evaluated through these values.
  if (&null[0] != nullptr || &*null != nullptr || &0[null] != nullptr) return 23;
  int (*null_row)[3] = nullptr;
  if (null_row + 0 != nullptr || null_row - null_row != 0) return 24;
  if (distance(constant, constant) != 0 || (end - 5) != begin) return 25;
  long long &live = Holder{&cells[0]}.pointer->value;
  live += 14;
  if (cells[0].value != 31) return 26;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  auto Text = readFile(Output);
  EXPECT_NE(Text.find("translated ptrdiff width mismatch"), std::string::npos);
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("pointer-arithmetic" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PointerArithmeticKeepsUnsupportedOperationsRejected) {
  const std::vector<std::string> Sources = {
      "bool f(int*a,int*b){return a<b;}",
      "bool f(int*a,int*b){return a<=b;}",
      "bool f(int*a,int*b){return a>b;}",
      "bool f(int*a,int*b){return a>=b;}",
      "int f(int*p){if(false){bool b=p<p;}return 0;}",
      "void*f(void*p){return p+1;}",
      "void f(void*p){++p;}",
      "auto f(void*a,void*b){return a-b;}",
      "struct R; R*f(R*p){return p+1;}",
      "auto f(int*a,unsigned int*b){return a-b;}",
      "auto f(int(*a)[2],int(*b)[3]){return a-b;}",
      "unsigned long long f(int*p){return (unsigned long long)p;}",
      "int*f(int x){return (int*)x;}",
      "struct R{int x;}; auto f(){return &R::x;}",
      "int g(){return 0;} auto f(){return &g+1;}",
      "int*f(int*p,__int128 n){return p+n;}",
      "struct R{int a[2];};int f(){const int&r=*(R{{1,2}}.a+0);return r;}",
      "struct R{int a[2];};int f(){const int&r=(0+R{{1,2}}.a)[0];return r;}",
      "struct R{int a[2];};int f(){const int&r=*(R{{1,2}}.a-0);return r;}",
      "struct E{int n;};struct R{E a[1];};int f(){const int&r=R{{{1}}}.a->n;return r;}",
      "struct E{int n;};struct R{E a[1];};int f(){const int&r=(R{{{1}}}.a+0)->n;return r;}",
      "struct R{int a[2];};int f(){if(false){const int&r=*(R{{1,2}}.a+0);}return 0;}"};
  for (size_t I = 0; I < Sources.size(); ++I) {
    SCOPED_TRACE(Sources[I]);
    const auto Source = tmpFile("pointer-reject-" + std::to_string(I) + ".cpp");
    const auto Output = tmpFile("pointer-reject-" + std::to_string(I) + ".nc");
    writeFile(Source, Sources[I]);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    EXPECT_NE(Result.exitCode, 0);
    EXPECT_TRUE(Result.stderrContains("TR0201") || Result.stderrContains("TR0202"))
        << Result.out << Result.err;
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("v1-pointer-offset.cpp");
  const auto Output = tmpFile("v1-pointer-offset.nc");
  writeFile(Source, "int*f(int*p){return p+1;}");
  auto Result = translate(Source, {"-o", Output.string()});
  expectCode(Result, "TR0201");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2PointerScopeDiagnosesUnsupportedBindings) {
  struct Rejection {
    const char *Name;
    const char *Source;
    const char *Code;
  };
  const Rejection Cases[] = {
      {"reference-field", "struct R{int &value;};", "TR0201"},
      {"pointer-global", "int *const value=nullptr;", "TR0201"},
      {"reference-global", "const int value=1; const int &alias=value;",
       "TR0201"},
      {"pointer-ordering", "bool f(int *a,int *b){return a<b;}", "TR0201"},
      {"function-pointer", "int f(int (*call)()){return call();}", "TR0201"},
      {"unsupported-pointee", "float *f(float *p){return p;}", "TR0201"},
      {"const-write", "void f(const int *p){*p=1;}", "TR0202"}};
  for (const auto &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    const auto Source = tmpFile(std::string(Case.Name) + ".cpp");
    const auto Output = tmpFile(std::string(Case.Name) + ".nc");
    writeFile(Source, Case.Source);
    auto Result = translate(
        Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Case.Code);
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2UserCopyingPreservesSelectedOperationsAndOperandOrder) {
  const auto Source = tmpFile("record-user-copy.cpp");
  const auto Output = tmpFile("record-user-copy.nc");
  writeFile(Source, R"cpp(
// Source fixture prepared before user-copy admission; native execution is CI-only.
struct Stats { int copies,assignments,destructions,trace; };
struct Item {
  int value;
  Stats *stats;
  Item *self;
  Item(int n,Stats &s):value(n),stats(&s),self(this){}
  Item(const Item &source):value(source.value+1),stats(source.stats),self(this){++stats->copies;}
  Item &operator=(const Item &source){++stats->assignments;value=source.value+2;return *this;}
  ~Item(){++stats->destructions;}
};
struct Choice {
  int value;
  Choice(int n):value(n){}
  Choice(Choice &source):value(source.value+10){}
  Choice(const Choice &source):value(source.value+20){}
  Choice &operator=(Choice &source){value=source.value+30;return *this;}
  Choice &operator=(const Choice &source){value=source.value+40;return *this;}
};
struct ExplicitCopy {
  int value;
  ExplicitCopy(int n):value(n){}
  explicit ExplicitCopy(const ExplicitCopy &other):value(other.value+1){}
};
struct OutOfLine {
  int value;
  OutOfLine(int n):value(n){}
  OutOfLine(const OutOfLine &);
  OutOfLine &operator=(const OutOfLine &);
};
OutOfLine::OutOfLine(const OutOfLine &other):value(other.value+1){}
OutOfLine &OutOfLine::operator=(const OutOfLine &other){value=other.value+2;return *this;}
struct Partial {
  int initialized,spare;
  Partial(int n):initialized(n){}
  Partial(const Partial &other):initialized(other.initialized+1){}
  Partial &operator=(const Partial &other){initialized=other.initialized+2;return *this;}
};
struct Redirect {
  int value;
  Redirect *result;
  Redirect &operator=(const Redirect &other){value=other.value;return *result;}
};
struct Box { Item value; explicit Box(const Item &item):value(item){} };
struct Aggregate { Item value; };
Item make(int n,Stats &stats){return Item(n,stats);}
Item forward(int n,Stats &stats){return make(n,stats);}
int byValue(Item value,const Item &source){return value.value==source.value+1 && value.self==&value && &value!=&source;}
int prvalue(Item value){return value.self==&value?value.value:-1;}
Item returnSource(const Item &source){return source;}
Item named(Stats &stats){Item local(20,stats);return local;}
Item identity(Item value){return value;}
Item &left(Item &item,Stats &stats){stats.trace=stats.trace*10+1;return item;}
Item &right(Item &item,Stats &stats){stats.trace=stats.trace*10+2;return item;}
Item &reseat(Item *&target,Item &replacement,Item &source){target=&replacement;return source;}
int main(){
  Stats stats{0,0,0,0};
  {
    Item source(3,stats);
    Item copied=source;
    Item direct(source);
    Item braced{source};
    if(stats.copies!=3 || copied.value!=4 || direct.value!=4 || braced.value!=4 ||
       copied.self!=&copied || direct.self!=&direct || braced.self!=&braced)return 1;
    copied=source;
    if(copied.value!=5 || copied.self!=&copied || stats.assignments!=1)return 2;
    copied=copied;
    if(copied.value!=7 || stats.assignments!=2)return 3;
    direct=braced=source;
    if(braced.value!=5 || direct.value!=7 || stats.assignments!=4)return 4;
    if(!byValue(source,source) || stats.copies!=4 || source.value!=3 || stats.destructions!=1)return 5;
    if(prvalue(Item(11,stats))!=11 || stats.copies!=4 || stats.destructions!=2)return 6;
    Item result=forward(13,stats);
    if(result.value!=13 || result.self!=&result || stats.copies!=4)return 7;
    Item returned=returnSource(source);
    if(returned.value!=4 || returned.self!=&returned || stats.copies!=5)return 8;
    Box box(source);
    Item elements[2]={source,source};
    Aggregate aggregate{source};
    if(box.value.value!=4 || box.value.self!=&box.value || elements[0].value!=4 ||
       elements[1].self!=&elements[1] || aggregate.value.self!=&aggregate.value || stats.copies!=9)return 9;
    stats.trace=0;
    left(direct,stats)=right(source,stats);
    if(stats.trace!=21 || direct.value!=5)return 10;
    stats.trace=0;
    left(direct,stats).operator=(right(source,stats));
    if(stats.trace!=12 || direct.value!=5)return 11;
    Item *target=&direct;
    direct.value=7;braced.value=9;
    *target=reseat(target,braced,source);
    if(target!=&braced || braced.value!=5 || direct.value!=7)return 12;
    target=&direct;direct.value=7;braced.value=9;
    target->operator=(reseat(target,braced,source));
    if(target!=&braced || braced.value!=9 || direct.value!=5)return 13;
  }
  if(stats.destructions!=12)return 14;
  Choice source(1);const Choice constant(2);
  Choice mutable_copy(source),const_copy(constant);
  if(mutable_copy.value!=11 || const_copy.value!=22)return 15;
  mutable_copy=source;const_copy=constant;
  if(mutable_copy.value!=31 || const_copy.value!=42)return 16;
  ExplicitCopy explicit_source(3);ExplicitCopy explicit_copy(explicit_source);
  if(explicit_copy.value!=4)return 17;
  OutOfLine out_source(3);OutOfLine out_copy=out_source;out_source=out_copy;
  if(out_copy.value!=4 || out_source.value!=6)return 18;
  Partial partial_source(3);Partial partial_copy=partial_source;partial_source=partial_copy;
  if(partial_copy.initialized!=4 || partial_source.initialized!=6)return 19;
  Redirect third{7,nullptr},first{1,&third},second{2,&third},result{0,&third};
  if(&(first=second)!=&third || first.value!=2)return 20;
  result=(first=second);
  if(result.value!=7)return 21;
  Stats named_stats{0,0,0,0};
  {Item named_result=named(named_stats);
   if(named_result.value!=21 || named_result.self!=&named_result ||
      named_stats.copies!=1 || named_stats.destructions!=1)return 22;}
  if(named_stats.destructions!=2)return 23;
  Stats parameter_stats{0,0,0,0};
  {Item source(3,parameter_stats);{Item result=identity(source);
    if(result.value!=5 || result.self!=&result || parameter_stats.copies!=2 ||
       parameter_stats.destructions!=1)return 24;}}
  if(parameter_stats.destructions!=3)return 25;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("record-user-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UserCopyingDiagnosesUnsupportedSelectedSpecialMembers) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"deleted-constructor", "struct R{int n;R(const R&)=delete;};"},
      {"deleted-assignment", "struct R{int n;R&operator=(const R&)=delete;};"},
      {"volatile-constructor", "struct R{int n;R(const volatile R&r):n(r.n){}};"},
      {"volatile-assignment", "struct R{int n;R&operator=(const volatile R&r){n=r.n;return *this;}};"},
      {"default-argument", "struct R{int n;R(const R&r,int extra=0):n(r.n+extra){}};"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("user-copy-reject-" + Name + ".cpp");
    const auto Output = tmpFile("user-copy-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("user-copy-definition.cpp");
  const auto Output = tmpFile("user-copy-definition.nc");
  for (const std::string &Code : {
      "struct R{int n;R(const R&);};",
      "struct R{int n;R&operator=(const R&);};"}) {
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0203");
    expectNoArtifacts(Output);
  }
  writeFile(Source, "struct R{int n;R&operator=(const R&r){n=r.n;return *this;}};void f(){const R r{1};R s{2};r=s;}");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  expectCode(Result, "TR0202");
  expectNoArtifacts(Output);
  writeFile(Source, "struct R{int n;R(const R&r):n(r.n){}};");
  Result = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
  expectCode(Result, "TR0201");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2OperatorsPreserveSequencingAliasesAndLifetimes) {
  const auto Source = tmpFile("operators.cpp");
  const auto Output = tmpFile("operators.nc");
  writeFile(Source, R"cpp(
struct Item {
  int n;int*alive;Item*self=this;
  Item(int v,int&count)noexcept:n(v),alive(&count){++*alive;}
  Item(const Item&s)noexcept:n(s.n),alive(s.alive){++*alive;}
  Item(Item&&s)noexcept:n(s.n),alive(s.alive){++*alive;s.n=-1;}
  ~Item()noexcept{--*alive;}
};
struct Target{int n;};
struct R {
  int n;Target*target;R*alias=this;
  R(int v,Target&t):n(v),target(&t){}
  int operator+(int v)const noexcept{return n+v;}
  int operator-(int v)const{return n-v;}
  int operator*(int v)const{return n*v;}
  int operator/(int v)const{return n/v;}
  int operator%(int v)const{return n%v;}
  int operator^(int v)const{return n^v;}
  int operator|(int v)const{return n|v;}
  int operator&(int v)const{return n&v;}
  int operator<<(int v)const{return n<<v;}
  int operator>>(int v)const{return n>>v;}
  int operator+()const{return n;}
  int operator-()const{return -n;}
  int operator~()const{return ~n;}
  bool operator!()const{return !n;}
  int&operator*(){return n;}
  int*operator&(){return &n;}
  Target*operator->(){return target;}
  int operator->*(int v)const{return target->n+v;}
  int&operator[](int){return n;}
  int&operator()(){return n;}
  R&operator++(){++n;return *this;}
  R operator++(int){R old=*this;++n;return old;}
  R&operator--(){--n;return *this;}
  R operator--(int){R old=*this;--n;return old;}
  R&operator+=(const R&r){n=r.n;return *alias;}
  R&operator-=(int v){n-=v;return *this;}
  R&operator*=(int v){n*=v;return *this;}
  R&operator/=(int v){n/=v;return *this;}
  R&operator%=(int v){n%=v;return *this;}
  R&operator^=(int v){n^=v;return *this;}
  R&operator|=(int v){n|=v;return *this;}
  R&operator&=(int v){n&=v;return *this;}
  R&operator<<=(int v){n<<=v;return *this;}
  R&operator>>=(int v){n>>=v;return *this;}
  R&operator,(R&r){return r;}
  bool operator&&(const R&r)const{return n&&r.n;}
  bool operator||(const R&r)const{return n||r.n;}
};
int operator+(int v,const R&r){return v+r.n;}
bool operator==(const R&a,const R&b){return a.n==b.n;}
bool operator!=(const R&a,const R&b){return a.n!=b.n;}
bool operator<(const R&a,const R&b){return a.n<b.n;}
bool operator>(const R&a,const R&b){return a.n>b.n;}
bool operator<=(const R&a,const R&b){return a.n<=b.n;}
bool operator>=(const R&a,const R&b){return a.n>=b.n;}
R&left(R&r,int&t){t=t*10+1;return r;}
R&right(R&r,int&t){t=t*10+2;return r;}
int index(int&t){t=t*10+3;return 0;}
R&reseat(R*&p,R&r){p=r.alias;r.n=17;return r;}
struct Free{int n;};
Free&operator+=(Free&a,const Free&b){a.n=b.n;return a;}
Free&freeLeft(Free&r,int&t){t=t*10+1;return r;}
Free&freeRight(Free&r,int&t){t=t*10+2;return r;}
struct Factory{int*alive;Item operator()(int v)const noexcept{return Item(v,*alive);}};
Item operator+(Item value,const Factory&f){return Item(value.n+1,*f.alive);}
struct Assign {
  int n;
  int&operator=(int v){n=v;return n;}
  void operator=(const Assign&r){n=r.n;}
  const Assign&operator=(Assign&&r){n=r.n;r.n=-1;return *this;}
};
struct ValueAssign{int n;ValueAssign operator=(ValueAssign v){n=v.n;return {n+1};}};
struct Qualified {
  int n;
  int operator()() & {return n;}
  int operator()()const & {return n+1;}
  int operator()() && {return n+2;}
  const Qualified&operator=(int)const{return *this;}
};
struct Ordered {
  int id;int*events;
  Ordered(int n,int&t):id(n),events(&t){*events=*events*10+id;}
  Ordered(const Ordered&s):id(s.id),events(s.events){*events=*events*10+id;}
  ~Ordered(){*events=*events*10+id+2;}
};
struct Result {
  int n;int*events;Result*self=this;
  Result(int v,int&t):n(v),events(&t){*events=*events*10+6;}
  ~Result(){*events=*events*10+7;}
};
void operator+=(Ordered lhs,Ordered rhs){*lhs.events=*lhs.events*10+5;if(lhs.id==1)return;*rhs.events=0;}
Result operator-=(Ordered lhs,Ordered rhs){*lhs.events=*lhs.events*10+5;return Result(lhs.id+rhs.id,*lhs.events);}
struct VoidMembers{Assign items[2];};
struct ReturnAssign{int n;int*alive;Item operator=(const ReturnAssign&r){n=r.n;return Item(n,*alive);}};
struct ResultMembers{ReturnAssign items[2];};
int main(){
  Target target{21};R a(6,target),b(3,target);int trace=0;
  if(a+2!=8 || a-2!=4 || a*2!=12 || a/2!=3 || a%4!=2 || 2+a!=8)return 1;
  if((a^3)!=5 || (a|1)!=7 || (a&3)!=2 || (a<<1)!=12 || (a>>1)!=3)return 2;
  if(+a!=6 || -a!=-6 || ~a!=~6 || !a || !noexcept(a+1))return 3;
  if(a==b || !(a!=b) || a<b || !(a>b) || a<=b || !(a>=b))return 4;
  *a=7;a[0]=8;a()=9;*(&a)=10;
  if(a.n!=10 || a->n!=21 || (a->*2)!=23)return 5;
  R old=a++;if(old.n!=10 || a.n!=11 || (++a).n!=12)return 6;
  R previous=a--;if(previous.n!=12 || a.n!=11 || (--a).n!=10)return 7;
  a-=2;a*=3;a/=2;a%=7;a^=3;a|=8;a&=14;a<<=1;a>>=2;
  if(a.n!=7)return 8;
  left(a,trace)+=right(b,trace);if(trace!=21 || a.n!=3)return 9;
  trace=0;left(a,trace).operator+=(right(b,trace));if(trace!=12)return 10;
  trace=0;left(a,trace)[index(trace)]=4;if(trace!=13 || a.n!=4)return 11;
  trace=0;(left(a,trace),right(b,trace)).n=5;if(trace!=12 || b.n!=5)return 12;
  a.n=0;trace=0;bool both=left(a,trace)&&right(b,trace);if(both || trace!=12)return 13;
  a.n=1;trace=0;bool either=left(a,trace)||right(b,trace);if(!either || trace!=12)return 14;
  trace=0;bool builtin=false&&index(trace);if(builtin || trace)return 15;
  trace=0;int shifted=left(b,trace)<<index(trace);if(trace!=13 || shifted!=5)return 16;
  Free x{1},y{2};trace=0;freeLeft(x,trace)+=freeRight(y,trace);if(trace!=21 || x.n!=2)return 17;
  trace=0;operator+=(freeLeft(x,trace),freeRight(y,trace));if((trace!=12 && trace!=21) || x.n!=2)return 18;
  R c(12,target),d(13,target);R*p=c.alias;
  reseat(p,d)+=*p;if(p!=d.alias || d.n!=12)return 19;
  int alive=0;Factory factory{&alive};
  {Item first=factory(30);if(alive!=1 || first.self!=&first || first.n!=30)return 20;
   Item second=first+factory;if(alive!=2 || second.self!=&second || second.n!=31)return 21;
   factory(40);if(alive!=2)return 22;
   if(!noexcept(factory(1)) || alive!=2)return 23;}
  if(alive)return 24;
  Assign to{1},from{8};int&alias=(to=3);alias=4;if(to.n!=4)return 25;
  to=from;if(to.n!=8)return 26;
  const Assign&returned=(to=static_cast<Assign&&>(from));if(&returned!=&to || from.n!=-1)return 27;
  ValueAssign va{1},vb{9};ValueAssign value=(va=vb);if(va.n!=9 || vb.n!=9 || value.n!=10)return 28;
  Qualified q{5};const Qualified cq{7};
  if(q()!=5 || cq()!=8 || static_cast<Qualified&&>(q)()!=7 || &(cq=1)!=&cq)return 29;
  if(a.operator+(3)!=4 || operator+(3,a)!=4)return 30;
  int events=0;
  {Ordered lhs(1,events),rhs(2,events);events=0;
   lhs+=rhs;if(events!=21534)return 31;
   events=0;operator+=(lhs,rhs);if(events!=21534)return 32;
   events=0;{Result result=(lhs-=rhs);if(events!=215634 || result.n!=3 || result.self!=&result)return 33;}
   if(events!=2156347)return 34;
   events=0;{Result result=operator-=(lhs,rhs);if(events!=215634 || result.n!=3 || result.self!=&result)return 35;}
   if(events!=2156347)return 36;events=0;}
  if(events!=43)return 37;
  VoidMembers vm1{{{1},{2}}},vm2{{{3},{4}}};vm1=vm2;
  if(vm1.items[0].n!=3 || vm1.items[1].n!=4 || vm2.items[1].n!=4)return 38;
  ResultMembers rm1{{{1,&alive},{2,&alive}}},rm2{{{3,&alive},{4,&alive}}};rm1=rm2;
  if(rm1.items[0].n!=3 || rm1.items[1].n!=4 || alive)return 39;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("operators" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2OperatorsAcceptOrdinaryAssignmentSignatures) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"user_copy_rejected-const-assignment", "struct R{int n;R&operator=(const R&r)const{return const_cast<R&>(*this);}};"},
      {"user_copy_rejected-rvalue-assignment", "struct R{int n;R&operator=(const R&r)&&{n=r.n;return *this;}};"},
      {"user_copy_rejected-by-value-assignment", "struct R{int n;R&operator=(R r){n=r.n;return *this;}};"},
      {"user_copy_rejected-void-assignment", "struct R{int n;void operator=(const R&r){n=r.n;}};"},
      {"user_copy_rejected-other-assignment-result", "struct R{int n;int&operator=(const R&r){n=r.n;return n;}};"},
      {"user_copy_rejected-arbitrary-operator", "struct R{int n;R operator+(const R&r){return {n+r.n};}};"},
      {"user_move_rejected-const-assignment-receiver", "struct R{int n;R&operator=(R&&)const{return const_cast<R&>(*this);}};"},
      {"user_move_rejected-assignment-void-result", "struct R{int n;void operator=(R&&r){n=r.n;}};"},
      {"user_move_rejected-assignment-const-result", "struct R{int n;const R&operator=(R&&r){n=r.n;return *this;}};"},
      {"user_move_rejected-assignment-other-result", "struct R{int n;int&operator=(R&&r){n=r.n;return n;}};"},
      {"user_move_rejected-assignment-value-source", "struct R{int n;R&operator=(R r){n=r.n;return *this;}};"},
      {"user_move_rejected-arbitrary-operator", "struct R{int n;R operator+(R&&r){return {n+r.n};}};"},
      {"method-call", "struct R{int n;int operator()()const{return n;}};"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("operators-positive-" + Name + ".cpp");
    const auto Output = tmpFile("operators-positive-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    EXPECT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  }
}

TEST_F(TranslateTest, CoreV2OperatorsRetainSourceAndLifetimeBoundaries) {
  const std::vector<std::tuple<std::string, std::string, std::string>> Cases = {
      {"deleted", "struct R{int n;int operator+(int)const=delete;};", "TR0201"},
      {"volatile-receiver", "struct R{int n;int operator()()volatile{return n;}};", "TR0201"},
      {"volatile-argument", "struct R{int n;int operator+(volatile R&r)const{return r.n;}};", "TR0201"},
      {"template", "struct R{int n;template<class T>int operator()(T v){return n;}};", "TR0201"},
      {"default-argument", "struct R{int n;int operator()(int v=1){return n+v;}};", "TR0201"},
      {"friend", "struct R{int n;friend int operator+(const R&r,int v){return r.n+v;}};", "TR0201"},
      {"member-pointer", "struct R{int n;int operator+(int v)const{return n+v;}};void f(){auto p=&R::operator+;}", "TR0201"},
      {"free-pointer", "struct R{int n;};int operator+(R r,int v){return r.n+v;}void f(){auto p=&operator+;}", "TR0201"},
      {"new-member", "using Size=decltype(sizeof(0));struct R{int n;static void*operator new(Size){return nullptr;}};", "TR0201"},
      {"delete-member", "struct R{int n;static void operator delete(void*){}};", "TR0201"},
      {"new-free", "using Size=decltype(sizeof(0));void*operator new(Size){return nullptr;}", "TR0201"},
      {"attribute", "struct R{int n;[[deprecated]] int operator()()const{return n;}};", "TR0201"},
      {"virtual", "struct R{int n;virtual int operator()()const{return n;}};", "TR0201"},
      {"binary-arity", "struct R{int n;int operator+(int,int){return n;}};", "TR0202"},
      {"prefix-postfix-type", "struct R{int n;R&operator++(long){return *this;}};", "TR0202"},
      {"free-assignment", "struct R{int n;};R&operator=(R&a,const R&b){return a;}", "TR0202"},
      {"static-member", "struct R{int n;static int operator+(int){return 1;}};", "TR0202"},
      {"free-call", "struct R{int n;};int operator()(R r){return r.n;}", "TR0202"},
      {"member", "struct R{int n;int operator()()const;};int f(R&r){return r();}", "TR0203"},
      {"free", "struct R{int n;};int operator+(R r,int v);int f(R&r){return r+1;}", "TR0203"},
  };
  for (const auto &[Name, Code, Diagnostic] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("operators-reject-" + Name + ".cpp");
    const auto Output = tmpFile("operators-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Diagnostic);
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("operators-v1.cpp");
  const auto Output = tmpFile("operators-v1.nc");
  for (const std::string &Code : {
      "struct R{int n;int operator()()const{return n;}};",
      "struct R{int n;};int operator+(R r,int n){return r.n+n;}int f(){R r{1};return r+2;}"}) {
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2ConversionsPreserveCallsAliasesAndLifetimes) {
  const auto Source = tmpFile("conversions.cpp");
  const auto Output = tmpFile("conversions.nc");
  writeFile(Source, R"cpp(
struct Counts { int converted; int made; int copied; int moved; int destroyed; };
struct Source {
  int n; Counts *counts;
  operator int() & { ++counts->converted; return n; }
  operator int() const & { ++counts->converted; return n+1; }
  operator int() && { ++counts->converted; return n+2; }
  explicit operator bool() const noexcept { ++counts->converted; return n!=0; }
};
struct Explicit { int n; explicit operator int() const; };
Explicit::operator int() const { return n; }
struct Ordered {
  int n; int *order;
  operator int() { *order=*order*10+2; return n; }
};
Ordered &receiver(Ordered &r) { *r.order=*r.order*10+1; return r; }
int consume(int n,int &order) { order=order*10+3; return n; }
struct Pointer { int *p; int calls; operator int*() { ++calls; return p; } };
struct Reference {
  int n; int *p; int calls;
  operator int&() { ++calls; return n; }
  operator int*&() { ++calls; return p; }
};
struct ConstReference { int n; operator const int&() const { return n; } };
struct XReference { int n; operator int&&() { return static_cast<int&&>(n); } };
enum class Code : unsigned short { ok=65000 };
struct EnumSource { operator Code() const { return Code::ok; } };
struct Target {
  int n; Counts *counts; Target *self=this;
  Target(int value,Counts &c):n(value),counts(&c) { ++counts->made; }
  Target(const Target &r):n(r.n),counts(r.counts) { ++counts->copied; }
  Target(Target &&r):n(r.n),counts(r.counts) { ++counts->moved; r.n=-1; }
  ~Target() { ++counts->destroyed; }
};
struct RecordSource {
  int n; Counts *counts;
  operator Target() const noexcept { ++counts->converted; return Target(n,*counts); }
};
struct RecordReference { Target *p; operator Target&() { return *p; } };
struct RecordXReference { Target *p; operator Target&&() { return static_cast<Target&&>(*p); } };
struct Wrapper { Target field; };
Target returned(RecordSource &source) { return source; }
int take(Target value) { return value.self==&value ? value.n : -99; }
struct Pure { int n; constexpr operator int() const noexcept { return n; } };
constexpr Pure pure{23};
constexpr int converted=pure;
static_assert(converted==23,"conversion constant");
static_assert(noexcept(pure.operator int()),"conversion specification");
struct Risky { int n; operator int() const noexcept(false) { return n; } };
int main() {
  Counts counts{0,0,0,0,0}; Source source{4,&counts};
  int n=source;
  if(n!=4||counts.converted!=1)return 1;
  long long wide=source;
  if(wide!=4||counts.converted!=2)return 2;
  const Source fixed{4,&counts}; n=fixed;
  if(n!=5||counts.converted!=3)return 3;
  n=static_cast<Source&&>(source);
  if(n!=6||source.n!=4||counts.converted!=4)return 4;
  counts.converted=0;
  if(source){}else return 5;
  if(counts.converted!=1)return 6;
  Source zero{0,&counts}; counts.converted=0;
  bool both=zero&&source;
  if(both||counts.converted!=1)return 7;
  counts.converted=0; bool either=source||zero;
  if(!either||counts.converted!=1)return 8;
  counts.converted=0; bool needed=source&&source;
  if(!needed||counts.converted!=2)return 9;
  counts.converted=0; int rounds=0;
  while(source){++rounds;source.n=0;}
  if(rounds!=1||counts.converted!=2)return 10;
  if(!noexcept(static_cast<bool>(source))||counts.converted!=2)return 11;
  Explicit explicitValue{9}; int direct(explicitValue);
  if(direct!=9||static_cast<int>(explicitValue)!=9||int(explicitValue)!=9)return 12;
  int order=0; Ordered ordered{7,&order};
  if(consume(receiver(ordered),order)!=7||order!=123)return 13;
  int first=2,second=8; Pointer pointer{&first,0}; int *p=pointer;
  if(p!=&first||pointer.calls!=1)return 14;
  if(pointer){}else return 15;
  if(pointer.calls!=2)return 16;
  pointer.p=nullptr;
  if(pointer!=nullptr||pointer.calls!=3)return 17;
  Reference references{3,&first,0}; int &alias=references; alias=11;
  const int &constAlias=references;
  if(references.n!=11||constAlias!=11||references.calls!=2)return 18;
  int *&pointerAlias=references; pointerAlias=&second;
  if(references.p!=&second||references.calls!=3)return 19;
  const ConstReference constReference{13}; const int &readonly=constReference;
  if(&readonly!=&constReference.n)return 20;
  XReference xreference{5}; int &&xalias=xreference; xalias=17;
  if(xreference.n!=17||&xalias!=&xreference.n)return 21;
  EnumSource enumSource{}; Code code=enumSource;
  if(code!=Code::ok)return 22;
  if(converted!=23||static_cast<int>(pure)!=23)return 23;
  Risky risky{6};
  if(noexcept(static_cast<int>(risky))||static_cast<int>(risky)!=6)return 24;
  Counts objects{0,0,0,0,0};
  {
    RecordSource factory{19,&objects}; Target a=factory;
    if(a.n!=19||a.self!=&a||objects.made!=1||objects.copied||objects.moved)return 25;
    Target array[2]={factory,factory}; Wrapper wrapper{factory}; Target result=returned(factory);
    if(array[0].self!=&array[0]||array[1].self!=&array[1]||wrapper.field.self!=&wrapper.field||result.self!=&result)return 26;
    if(objects.made!=5||objects.converted!=5||objects.destroyed)return 27;
    if(take(factory)!=19||objects.made!=6||objects.destroyed!=1)return 28;
    static_cast<Target>(factory);
    if(objects.made!=7||objects.converted!=7||objects.destroyed!=2||objects.copied||objects.moved)return 29;
    if(!noexcept(static_cast<Target>(factory))||objects.converted!=7)return 30;
    RecordReference reference{&a}; Target &recordAlias=reference; recordAlias.n=29;
    Target copied=reference;
    if(a.n!=29||copied.n!=29||copied.self!=&copied||objects.copied!=1)return 31;
    RecordXReference xref{&a}; Target moved=xref;
    if(a.n!=-1||moved.n!=29||moved.self!=&moved||objects.moved!=1)return 32;
    if(objects.made!=7||objects.destroyed!=2)return 33;
  }
  if(objects.destroyed!=9)return 34;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("conversions" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ConversionsAcceptOrdinaryDefinitions) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"conversion", "struct R{int n;operator int()const{return n;}};"},
      {"method-conversion", "struct R{int n;operator int()const{return n;}};"},
      {"constructor-conversion-function", "struct R{int n;R():n(1){} operator int()const{return n;}};int f(){R r;return r;}"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("conversions-positive-" + Name + ".cpp");
    const auto Output = tmpFile("conversions-positive-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    EXPECT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  }
}

TEST_F(TranslateTest, CoreV2ConversionsRetainSourceAndLifetimeBoundaries) {
  const std::vector<std::tuple<std::string, std::string, std::string>> Cases = {
      {"float", "struct R{operator double()const{return 1.0;}};", "TR0201"},
      {"string", "struct R{operator const char*()const{return \"x\";}};", "TR0201"},
      {"volatile", "struct R{operator int()volatile{return 1;}};", "TR0201"},
      {"restrict", "struct R{operator int()__restrict{return 1;}};", "TR0201"},
      {"template", "struct R{template<class T>operator T()const{return T{};}};", "TR0201"},
      {"member-address", "struct R{operator int()const{return 1;}};auto f(){return &R::operator int;}", "TR0201"},
      {"function-pointer", "using F=int(*)();int g(){return 1;}struct R{operator F()const{return g;}};", "TR0201"},
      {"empty-record-conversion", "struct T{int n;};struct R{operator T()const{return {1};}};int f(){R r;const T&t=r;return t.n;}", "TR0201"},
      {"unused-throw", "struct R{operator int()const{throw 1;}};", "TR0201"},
      {"query-throw", "struct R{operator int()const noexcept(false){throw 1;}};bool f(R&r){return noexcept(static_cast<int>(r));}", "TR0201"},
      {"folded-float", "struct R{constexpr operator int()const{return static_cast<int>(1.0);}};constexpr R r{};static_assert(int(r)==1,\"value\");", "TR0201"},
      {"default-argument", "int g(int n=1){return n;}struct R{operator int()const{return g();}};", "TR0201"},
      {"virtual", "struct R{virtual operator int()const{return 1;}};", "TR0201"},
      {"explicit-copy", "struct R{explicit operator int()const{return 1;}};int f(){R r;int n=r;return n;}", "TR0202"},
      {"parameters", "struct R{operator int(int n){return n;}};", "TR0202"},
      {"written-result", "struct R{int operator int(){return 1;}};", "TR0202"},
      {"ambiguous", "struct R{operator long(){return 1;}operator unsigned long(){return 1;}};int f(){R r;return r;}", "TR0202"},
      {"deleted-use", "struct R{operator int()const=delete;};int f(){R r;return r;}", "TR0202"},
      {"ref-qualifier", "struct R{operator int()&&{return 1;}};int f(){R r;return r;}", "TR0202"},
      {"implicit", "struct R{operator int()const;};int f(R&r){return r;}", "TR0203"},
      {"explicit", "struct R{explicit operator bool()const;};bool f(R&r){return static_cast<bool>(r);}", "TR0203"},
  };
  for (const auto &[Name, Code, Diagnostic] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("conversions-reject-" + Name + ".cpp");
    const auto Output = tmpFile("conversions-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Diagnostic);
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("conversions-v1.cpp");
  const auto Output = tmpFile("conversions-v1.nc");
  for (const std::string &Code : {
      "struct R{int n;operator int()const{return n;}};",
      "struct R{explicit operator bool()const{return true;}};bool f(R&r){return static_cast<bool>(r);}"}) {
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2ArrayTemporariesPreserveStorageAndCleanup) {
  const auto Source = tmpFile("array-temporaries.cpp");
  const auto Output = tmpFile("array-temporaries.nc");
  writeFile(Source, R"cpp(
int alive=0,made=0,dead=0,copies=0,moves=0,bad=0,log=0;
void reset(){alive=made=dead=copies=moves=bad=log=0;}
struct R {
 int n;R*self;
 R():n(0),self(this){++alive;++made;}
 R(int value):n(value),self(this){++alive;++made;}
 R(const R&r):n(r.n),self(this){++alive;++made;++copies;}
 R(R&&r):n(r.n),self(this){r.n=0;++alive;++made;++moves;}
 ~R(){if(self!=this)++bad;log=log*10+n;--alive;++dead;}
 int get()const{return self==this?n:-1;}
};
using Items=R[2];using Triple=R[3];using Grid=R[2][2];using Numbers=int[3];
enum class E:unsigned char{one=1,two=2};using Enums=E[2];using Pointers=int*[2];
int read(const R(&a)[2]){if(alive<2||a[0].self!=&a[0]||a[1].self!=&a[1])++bad;return a[0].n+a[1].n;}
int mutate(R(&&a)[2]){++a[0].n;return read(a);}
int sum(const int(&a)[3]){return a[0]+a[1]+a[2];}
int pointer(const int*p){return p[1];}
int bump(int&n){return ++n;}
struct Holder{int n;Holder(const R(&a)[2]):n(read(a)){}};
struct Default{int n=read(Items{R(1),R(2)});};
int early(){const Items&r={R(3),R(4)};return read(r);}
int selected(bool b){const R&r=b?Items{R(1),R(2)}[0]:Items{R(3),R(4)}[1];return r.get();}
constexpr int folded(){const Numbers&r={1,2,3};return r[1];}
constexpr int constant=folded();static_assert(constant==2);
int main(){
 {const Numbers&r={1,2,3};Numbers&&s={4,5,6};++s[0];
  const int&e=Numbers{7,8,9}[1];const Numbers&same{r};
  int a=10,b=11;const Pointers&p={&a,&b};*p[1]=12;
  const Enums&enums={E::one,E::two};
  if(sum(r)!=6||sum(s)!=16||e!=8||&same!=&r||b!=12||static_cast<int>(enums[1])!=2||constant!=2)return 1;}
 int n=0;int value=sum(Numbers{bump(n),bump(n),bump(n)});
 if(value!=6||n!=3||pointer(Numbers{4,5,6})!=5)return 2;
 reset();value=read(Items{R(1),R(2)});
 if(value!=3||alive||made!=2||dead!=2||copies||moves||bad||log!=21)return 3;
 reset();value=read({R(3),R(4)});
 if(value!=7||alive||made!=2||dead!=2||bad||log!=43)return 4;
 reset();value=mutate(Items{R(1),R(2)});
 if(value!=4||alive||made!=2||dead!=2||copies||moves||bad||log!=22)return 5;
 reset();value=Items{R(5),R(6)}[1].get();
 if(value!=6||alive||made!=2||dead!=2||bad||log!=65)return 6;
 reset();
 {Holder h(Items{R(1),R(2)});if(h.n!=3||alive||dead!=2||bad)return 7;}
 reset();
 {Default d{};if(d.n!=3||alive||made!=2||dead!=2||bad)return 8;}
 reset();Items{R(1),R(2)};
 if(alive||made!=2||dead!=2||bad||log!=21)return 9;
 reset();Triple{R(3)};
 if(alive||made!=3||dead!=3||bad||log!=3)return 10;
 reset();
 {const Items&r{R(1),R(2)};const Items&same={r};
  if(read(r)!=3||alive!=2||dead||&same!=&r||copies||moves||bad)return 11;}
 if(alive||dead!=2||bad||log!=21)return 12;
 reset();
 {Items&&r={R(3),R(4)};++r[0].n;
  if(read(r)!=8||alive!=2||dead||r[0].self!=&r[0]||r[1].self!=&r[1])return 13;}
 if(alive||dead!=2||bad||log!=44)return 14;
 reset();
 {const R&r=Items{R(1),R(2)}[1];R copy(r);
  if(r.get()!=2||copy.n!=2||alive!=3||dead||copies!=1||moves||bad)return 15;}
 if(alive||dead!=3||bad||log!=221)return 16;
 reset();
 {const R(&row)[2]=Grid{{R(1),R(2)},{R(3),R(4)}}[1];
  if(read(row)!=7||alive!=4||dead||bad)return 17;}
 if(alive||made!=4||dead!=4||bad||log!=4321)return 18;
 reset();
 {Grid&&grid={{R(1),R(2)},{R(3),R(4)}};
  if(grid[1][1].get()!=4||grid[0][0].self!=&grid[0][0]||alive!=4||dead)return 19;}
 if(alive||dead!=4||bad||log!=4321)return 20;
 reset();
 {const Triple&r={R(5)};if(alive!=3||dead||r[0].get()!=5||r[1].get()!=0||r[2].get()!=0||&r[1]==&r[2])return 21;}
 if(alive||made!=3||dead!=3||bad||log!=5)return 22;
 reset();
 {const Items&r=Items{R(read(Items{R(1),R(2)})),R(4)};
  if(read(r)!=7||alive!=2||made!=4||dead!=2||bad||log!=21)return 23;}
 if(alive||dead!=4||bad||log!=2143)return 24;
 reset();
 {const R&first=R(1);const Items&middle={R(2),R(3)};R last(4);
  if(alive!=4||dead)return 25;}
 if(alive||dead!=4||bad||log!=4321)return 26;
 reset();value=early();
 if(value!=7||alive||dead!=2||bad||log!=43)return 27;
 reset();value=selected(true);int other=selected(false);
 if(value!=1||other!=4||alive||made!=4||dead!=4||bad||log!=2143)return 28;
 reset();n=0;
 {const Items&r=(++n,Items{R(1),R(2)});if(n!=1||read(r)!=3||alive!=2||dead)return 29;}
 if(alive||dead!=2||bad||log!=21)return 30;
 reset();
 for(int i=0;i<3;++i){const Items&r={R(1),R(2)};if(alive!=2||dead!=i*2)return 31;if(i==1)continue;}
 if(alive||made!=6||dead!=6||bad||log!=212121)return 32;
 reset();
 for(int i=0;i<3;++i){const Items&r={R(3),R(4)};if(alive!=2)return 33;if(i==1)break;}
 if(alive||made!=4||dead!=4||bad||log!=4343)return 34;
 reset();n=0;
 for(int i=0;i<2;++i)n+=read(Items{R(1),R(2)});
 if(n!=6||alive||made!=4||dead!=4||bad||log!=2121)return 35;
 reset();
 bool quiet=noexcept(Items{R(1),R(2)});unsigned long long size=sizeof(Items{R(1),R(2)});
 if(quiet||size!=sizeof(Items)||alive||made||dead||bad)return 36;
 return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("array-temporaries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ArrayTemporariesAcceptBoundedLifetimes) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"promoted-array-argument", "using A=int[2];void take(const int(&)[2]){}void f(){take(A{1,2});}"},
      {"promoted-dead-array-argument", "using A=int[2];void take(const int(&)[2]){}void f(){if(false)take(A{1,2});}"},
      {"promoted-query-array-argument", "using A=int[2];void take(const int(&)[2])noexcept{}bool f(){return noexcept(take(A{1,2}));}"},
      {"promoted-query-array-expression", "using A=int[2];bool f(){return noexcept(A{1,2}[0]);}"},
      {"promoted-array-owner", "void f(){const int(&r)[2]={1,2};}"},
      {"promoted-dead-array-owner", "void f(){if(false){const int(&r)[2]={1,2};}}"},
      {"promoted-constexpr-array-owner", "constexpr int f(){const int(&r)[2]={1,2};return r[0];}constexpr int n=f();"},
      {"promoted-query-braced-array", "void take(const int(&)[2])noexcept{}bool f(){return noexcept(take({1,2}));}"},
      {"promoted-braced-array-argument", "void take(const int(&)[2]){}void f(){take({1,2});}"},
      {"scalar-discard", "using A=int[2];void f(){A{1,2};}"},
      {"record-discard", "struct R{int n;~R(){}};using A=R[2];void f(){A{{1},{2}};}"},
      {"scalar-decay", "using A=int[2];int read(const int*p){return p[0]+p[1];}int f(){return read(A{1,2});}"},
      {"scalar-index", "using A=int[2];int f(){return A{1,2}[1];}"},
      {"record-element-receiver", "struct R{int n;int get()const{return n;}};using A=R[2];int f(){return A{{1},{2}}[1].get();}"},
      {"rvalue-array-reference", "using A=int[2];int f(){A&&r={1,2};return ++r[1];}"},
      {"direct-element-extension", "using A=int[2];int f(){const int&r=A{1,2}[1];return r;}"},
      {"row-extension", "using A=int[2][2];int f(){const int(&r)[2]=A{{1,2},{3,4}}[1];return r[0];}"},
      {"array-comma", "using A=int[2];int f(){int n=0;const A&r=(++n,A{1,2});return n+r[0];}"},
      {"braced-live-array", "using A=int[2];int f(A&a){A&r{a};return ++r[0];}"},
      {"array-rvalue-parameter", "using A=int[2];int read(A&&a){return ++a[0];}int f(){return read(A{1,2});}"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("array-temporaries-positive-" + Name + ".cpp");
    const auto Output = tmpFile("array-temporaries-positive-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    EXPECT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  }
}

TEST_F(TranslateTest, CoreV2ArrayTemporariesRetainTypeAndLifetimeBoundaries) {
  const std::vector<std::tuple<std::string, std::string, std::string>> Cases = {
      {"static-array", "int f(){static const int(&r)[2]={1,2};return r[0];}", "TR0201"},
      {"global-array", "const int(&r)[2]={1,2};", "TR0201"},
      {"tls-array", "int f(){thread_local const int(&r)[2]={1,2};return r[0];}", "TR0201"},
      {"reference-field", "struct R{const int(&a)[2];};void f(){R r{{1,2}};}", "TR0201"},
      {"fresh-array-return", "using A=int[2];const A&f(){return A{1,2};}", "TR0201"},
      {"fresh-element-return", "using A=int[2];const int&f(){return A{1,2}[0];}", "TR0201"},
      {"pointer-offset-reference", "using A=int[2];int f(){const int&r=*(A{1,2}+1);return r;}", "TR0201"},
      {"dereference-reference", "using A=int[2];int f(){const int&r=*A{1,2};return r;}", "TR0201"},
      {"float-element", "void f(){const double(&r)[2]={1.0,2.0};}", "TR0201"},
      {"volatile-element", "void f(){const volatile int(&&r)[2]={1,2};}", "TR0201"},
      {"vla", "void f(int n){int a[n];}", "TR0201"},
      {"unknown-bound", "extern int a[];", "TR0201"},
      {"zero-bound", "using A=int[0];void f(){const A&r={};}", "TR0201"},
      {"extent-limit", "using A=int[65537];void f(){const A&r={};}", "TR0201"},
      {"storage-limit", "using A=int[512][512];void f(){const A&r={};}", "TR0201"},
      {"expanded-storage-budget", "using A=int[32768];void f(){const A&r={};}", "TR0201"},
      {"query-unsupported", "using A=double[2];bool f(){return noexcept(A{1.0,2.0}[0]);}", "TR0201"},
      {"mutable-array-reference", "using A=int[2];void f(){A&r={1,2};}", "TR0202"},
      {"const-array-write", "using A=int[2];void f(){const A&r={1,2};r[0]=3;}", "TR0202"},
      {"array-assignment", "using A=int[2];void f(){A&&a={1,2};A&&b={3,4};a=b;}", "TR0202"},
      {"narrow-element", "void f(){const unsigned char(&r)[2]={1,300};}", "TR0202"},
      {"too-many", "void f(){const int(&r)[2]={1,2,3};}", "TR0202"},
      {"deleted-move", "struct R{int n;R(int v):n(v){}R(R&&)=delete;};using A=R[2];void f(){R r(1);const A&a={static_cast<R&&>(r),R(2)};}", "TR0202"},
      {"constructor", "struct R{int n;R(int);};using A=R[2];void f(){const A&r={R(1),R(2)};}", "TR0203"},
      {"destructor", "struct R{int n;~R();};using A=R[2];void f(){const A&r={{1},{2}};}", "TR0203"},
  };
  for (const auto &[Name, Code, Diagnostic] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("array-temporaries-reject-" + Name + ".cpp");
    const auto Output = tmpFile("array-temporaries-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Diagnostic);
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("array-temporaries-v1.cpp");
  const auto Output = tmpFile("array-temporaries-v1.nc");
  for (const std::string &Code : {
      "using A=int[2];int f(){const A&r={1,2};return r[0];}",
      "using A=int[2];int f(){return A{1,2}[0];}"}) {
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2AutomaticReferencesPreserveStorageAndCleanup) {
  const auto Source = tmpFile("automatic-references.cpp");
  const auto Output = tmpFile("automatic-references.nc");
  writeFile(Source, R"cpp(
int alive=0,made=0,dead=0,copies=0,moves=0,bad=0;
void reset(){alive=made=dead=copies=moves=bad=0;}
struct R {
 int n; R*self;
 R(int v):n(v),self(this){++alive;++made;}
 R(const R&r):n(r.n),self(this){++alive;++made;++copies;}
 R(R&&r):n(r.n),self(this){r.n=-1;++alive;++made;++moves;}
 ~R(){if(self!=this)++bad;--alive;++dead;}
 explicit operator bool()const{return n!=0;}
};
struct Group{R array[2];};
struct Source{int n;operator int()const{return n;}operator R()const{return R(n);}};
struct Literal{int n;};
constexpr int folded(){const Literal&r{Literal{9}};const int&n={r.n};return n;}
constexpr int constant=folded();
static_assert(constant==9);
enum class E:unsigned char{value=7};
int read(const R&r){if(alive<1||r.self!=&r)++bad;return r.n;}
int number(const int&n){return n;}
const R&alias(const R&r){return r;}
R factory(int n){return R(n);}
R copyResult(){const R&r{R(8)};return r;}
R moveResult(){R&&r={R(9)};return static_cast<R&&>(r);}
int early(){const R&r=R(10);R ordinary(11);return read(r)+ordinary.n;}
int select(bool b){const R&r=b?R(12):R(13);return read(r);}
int selectMember(bool b){const int&r=b?R(14).n:R(15).n;return r;}
int log=0;
struct Tag{int n;Tag(int v):n(v){}~Tag(){log=log*10+n;}};
int main(){
 {const int&a{1};int&&b={2};b+=3;const int&&c=6;
  int x=8;int*const&p{&x};E&&e={E::value};const unsigned&u=x;
  Source s{4};const int&converted{s};
  if(a!=1||b!=5||c!=6||*p!=8||static_cast<int>(e)!=7||u!=8||converted!=4||constant!=9)return 1;}
 reset();
 {const R&a{R(1)};R&&b={R(2)};R&again=b;const R&same={a};++again.n;
  if(alive!=2||dead||copies||moves||a.self!=&a||b.self!=&b||&same!=&a||b.n!=3)return 2;}
 if(alive||dead!=2||bad)return 3;
 reset();
 {const int&a{R(3).n};int&&b={R(4).n};++b;
  if(a!=3||b!=5||alive!=2||dead)return 4;}
 if(alive||dead!=2||bad)return 5;
 reset();
 {const R(&array)[2]=Group{{R(5),R(6)}}.array;const R&same{array[1]};
  if(alive!=2||dead||read(same)!=6||array[0].self!=&array[0])return 6;}
 if(alive||dead!=2||bad)return 7;
 reset();
 {const int&a={(Group{{R(7),R(8)}}.array)[1].n};
  if(a!=8||alive!=2||dead)return 8;}
 if(alive||dead!=2||bad)return 9;
 reset();
 {const R&outer{R(read(R(4)))};
  if(read(outer)!=4||alive!=1||made!=2||dead!=1||copies||moves)return 10;}
 if(alive||dead!=2||bad)return 11;
 reset();
 {const R&a=factory(5);Source s{6};R&&b={static_cast<R>(s)};
  if(read(a)!=5||read(b)!=6||alive!=2||dead||copies||moves)return 12;}
 if(alive||dead!=2||bad)return 13;
 reset();
 {const R&a=R(7);R copy(a);R&&b=R(8);R moved(static_cast<R&&>(b));
  if(alive!=4||copies!=1||moves!=1||copy.n!=7||moved.n!=8||b.n!=-1)return 14;}
 if(alive||dead!=4||bad)return 15;
 reset();
 {R copy=copyResult();R moved=moveResult();
  if(alive!=2||made!=4||dead!=2||copies!=1||moves!=1||copy.n!=8||moved.n!=9)return 16;}
 if(alive||dead!=4||bad)return 17;
 reset();
 int result=early();if(result!=21||alive||dead!=2||bad)return 18;
 reset();
 int a=select(true);int b=select(false);int c=selectMember(true);int d=selectMember(false);
 if(a!=12||b!=13||c!=14||d!=15||alive||made!=4||dead!=4||bad)return 19;
 reset();
 {int n=0;R&&r=(++n,static_cast<R&&>(R(16)));
  if(n!=1||r.n!=16||alive!=1||dead||copies||moves)return 20;}
 if(alive||dead!=1||bad)return 21;
 {const Tag&a=Tag(1);Tag ordinary(2);const Tag&b={Tag(3)};if(log)return 22;}
 if(log!=321)return 23;
 reset();
 if(const R&r{R(17)};r){if(read(r)!=17||alive!=1||dead)return 24;}
 if(alive||dead!=1||bad)return 25;
 switch(const R&r=R(18);r.n){case 18:if(alive!=1||dead!=1)return 26;break;default:return 27;}
 if(alive||dead!=2||bad)return 28;
 reset();
 int n=3,sum=0;
 while(const R&r=R(n--)){if(alive!=1||dead!=3-r.n)return 29;sum+=r.n;if(r.n==2)continue;}
 if(sum!=6||alive||made!=4||dead!=4||bad)return 30;
 reset();n=3;
 while(const R&r{R(n--)}){const R&body=R(20);if(alive!=2)return 31;if(r.n==2)break;}
 if(alive||made!=4||dead!=4||bad)return 32;
 reset();n=2;sum=0;
 for(const R&outer=R(21);const R&condition=R(n--);sum+=alive){
  if(alive!=2||read(outer)!=21)return 33;const R&body=R(22);if(alive!=3)return 34;continue;
 }
 if(sum!=4||alive||made!=6||dead!=6||bad)return 35;
 reset();
 for(int i=0;i<3;++i){const R&r=R(i);if(alive!=1||dead!=i)return 36;}
 if(alive||made!=3||dead!=3||bad)return 37;
 reset();
 {const R&expired=alias({R(23)});if(alive||dead!=1||made!=1)return 38;}
 if(alive||dead!=1||bad)return 39;
 reset();
 int braced=number({24})+read({R(25)});
 if(braced!=49||alive||made!=1||dead!=1||bad)return 40;
 return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("automatic-references" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AutomaticReferencesAcceptLocalExtensions) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"promoted-1", "struct I{int n;I(const I&s):n(s.n){}};struct R{I i;R(const R&)=default;};void f(const R&s){const R&r=R(s);}"},
      {"promoted-2", "void f(){int&&r=1;}"},
      {"promoted-3", "void f(){int&&r=static_cast<int&&>(1);}"},
      {"promoted-4", "void f(){const int&&r=1;}"},
      {"promoted-5", "struct R{int n;};void f(){R&&r=R{1};}"},
      {"promoted-6", "struct R{int n;};void f(){R&&r=static_cast<R&&>(R{1});}"},
      {"promoted-7", "struct R{int n;};void f(){int&&r=R{1}.n;}"},
      {"promoted-8", "struct R{int a[2];};void f(){int&&r=R{{1,2}}.a[0];}"},
      {"promoted-9", "struct R{int n;};void f(bool b,R&live){R&&r=b?static_cast<R&&>(live):R{1};}"},
      {"promoted-10", "struct R{int n;};void f(){int n=0;R&&r=(++n,R{1});}"},
      {"promoted-11", "struct R{int n;operator int()const{return n;}};int f(){R r{1};const int&n=r;return n;}"},
      {"promoted-12", "int f(){const int&r=1;return r;}"},
      {"promoted-13", "int f(){int&&r=1;return r;}"},
      {"promoted-14", "struct R{int n;};int f(){const R&r=R{1};return r.n;}"},
      {"promoted-15", "struct R{int n;};int f(){R&&r=R{1};return r.n;}"},
      {"promoted-16", "struct R{int n;};int f(){const int&r=R{1}.n;return r;}"},
      {"promoted-17", "struct R{int a[2];};int f(){const int&r=R{{1,2}}.a[0];return r;}"},
      {"promoted-18", "struct R{int n;explicit R()=default;};int f(){const R&r=R{};return r.n;}"},
      {"promoted-19", "struct R{int n;~R(){}};int f(){const R&r=R{1};return r.n;}"},
      {"promoted-20", "int f(){const int&r=1; return r;}"},
      {"promoted-21", "int f(){if(false){const int&r=1;} return 0;}"},
      {"promoted-22", "int f(){int x=1; const unsigned int&r=x; return r;}"},
      {"promoted-23", "struct R{int x;}; int f(){const int&r=R{1}.x; return r;}"},
      {"promoted-24", "struct R{int a[2];}; int f(){int n=0; const int&r=(++n,R{{1,2}}.a)[0]; return r;}"},
      {"promoted-25", "struct R{int a[2];}; int f(){const int&r=R{{1,2}}.a[0]; return r;}"},
      {"promoted-26", "struct R{int a[2];}; int f(){const int &r=R{{1,2}}.a[0]; return r;}"},
      {"promoted-27", "struct R{int a[2];}; int f(){int n=0; const int &r=(++n,R{{1,2}}.a)[0]; return r;}"},
      {"promoted-28", "int f(){const int &value=42; return value;}"},
      {"promoted-29", "int f(){if(false){const int &value=42;} return 0;}"},
      {"promoted-30", "int f(){int x=1; const unsigned int &r=x; return r;}"},
      {"promoted-31", "struct R{int x;}; int f(){const int &r=R{1}.x; return r;}"},
      {"brace-scalar", "int f(){const int&r{1};return r;}"},
      {"equal-brace-record", "struct R{int n;};int f(){const R&r={R{2}};return r.n;}"},
      {"brace-subobject", "struct R{int n;};int f(){int&&r{R{3}.n};return ++r;}"},
      {"brace-live", "int f(int&n){int&r{n};return ++r;}"},
      {"brace-parameter", "int take(const int&n){return n;}int f(){return take({4});}"},
      {"brace-constructor-parameter", "struct R{int n;R(const int&r):n(r){}};int f(){R r({5});return r.n;}"},
      {"brace-record-conversion", "struct V{int n;};struct R{int n;operator V()const{return V{n};}};int f(){R r{6};const V&v={r};return v.n;}"},
      {"constexpr-reference", "struct R{int n;};constexpr int f(){const R&r{R{7}};return r.n;}constexpr int n=f();static_assert(n==7);"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("automatic-references-positive-" + Name + ".cpp");
    const auto Output = tmpFile("automatic-references-positive-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    EXPECT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  }
}

TEST_F(TranslateTest, CoreV2AutomaticReferencesRetainLifetimeBoundaries) {
  const std::vector<std::tuple<std::string, std::string, std::string>> Cases = {
      {"fresh-brace-scalar-return", "const int&f(){return {1};}", "TR0201"},
      {"fresh-equal-record-return", "struct R{int n;};const R&f(){return {R{1}};}", "TR0201"},
      {"fresh-brace-member-return", "struct R{int n;};const int&f(){return {R{1}.n};}", "TR0201"},
      {"static-brace", "int f(){static const int&r{1};return r;}", "TR0201"},
      {"tls-brace", "int f(){thread_local const int&r{1};return r;}", "TR0201"},
      {"global-brace", "const int&r{1};", "TR0201"},
      {"reference-field", "struct R{const int&r;};int f(){R r{1};return r.r;}", "TR0201"},
      {"braced-offset", "struct R{int a[2];};int f(){const int&r{*(R{{1,2}}.a+1)};return r;}", "TR0201"},
      {"braced-arrow", "struct I{int n;};struct R{I a[2];};int f(){const int&r{(R{{{1},{2}}}.a+1)->n};return r;}", "TR0201"},
      {"unused-throw", "struct R{int n;~R(){throw 1;}};void f(){const R&r{R{1}};}", "TR0201"},
      {"constexpr-unsupported", "constexpr int f(){const double&r{1.0};return 1;}constexpr int n=f();", "TR0201"},
      {"volatile-owner", "void f(){const volatile int&&r=1;}", "TR0201"},
      {"mutable-scalar", "void f(){int&r{1};}", "TR0202"},
      {"mutable-record", "struct R{int n;};void f(){R&r={R{1}};}", "TR0202"},
      {"const-mutation", "void f(){const int&r{1};++r;}", "TR0202"},
      {"deleted-copy", "struct R{int n;R(int v):n(v){}R(const R&)=delete;};void f(){const R&r=R(1);R copy(r);}", "TR0202"},
      {"deleted-move", "struct R{int n;R(int v):n(v){}R(R&&)=delete;};void f(){R&&r=R(1);R moved(static_cast<R&&>(r));}", "TR0202"},
      {"narrow-brace", "void f(){const unsigned char&r{300};}", "TR0202"},
      {"constructor", "struct R{int n;R(int);};void f(){const R&r=R(1);}", "TR0203"},
      {"destructor", "struct R{int n;~R();};void f(){const R&r{R{1}};}", "TR0203"},
  };
  for (const auto &[Name, Code, Diagnostic] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("automatic-references-reject-" + Name + ".cpp");
    const auto Output = tmpFile("automatic-references-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Diagnostic);
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("automatic-references-v1.cpp");
  const auto Output = tmpFile("automatic-references-v1.nc");
  for (const std::string &Code : {
      "int f(){const int&r{1};return r;}",
      "struct R{int n;};int f(){const R&r={R{1}};return r.n;}"}) {
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2TemporaryCallsPreserveStorageAndCleanup) {
  const auto Source = tmpFile("temporary-calls.cpp");
  const auto Output = tmpFile("temporary-calls.nc");
  writeFile(Source, R"cpp(
struct Counts { int made; int copied; int moved; int alive; int destroyed; };
struct R {
  int n; Counts *counts; R *self=this;
  R(int value,Counts &c):n(value),counts(&c){++counts->made;++counts->alive;}
  R(const R&r):n(r.n),counts(r.counts){++counts->copied;++counts->alive;}
  R(R&&r):n(r.n),counts(r.counts){++counts->moved;++counts->alive;r.n=-1;}
  ~R(){--counts->alive;++counts->destroyed;}
  int get()const & {return self==this?n:-99;}
  int get()&& {return self==this?n+1:-99;}
  int operator()(const int &v)const {return n+v;}
  int &ref()&& {return n;}
  operator int()const {return n;}
  explicit operator bool()const noexcept {return n!=0;}
  R make()&& {return R(n+1,*counts);}
};
int read(const R&r){return r.self==&r?r.n:-99;}
int readX(R&&r){r.n+=2;return r.n;}
int number(const int&n){return n;}
int increase(int&&n){return ++n;}
unsigned unsignedNumber(const unsigned&n){return n;}
bool nullPointer(int*const&p){return p==nullptr;}
enum class Code:unsigned short{ok=65000};
bool enumValue(const Code&c){return c==Code::ok;}
int operator+(const R&a,const R&b){return a.n+b.n;}
struct Box{R member;};
struct Holder{R array[2];};
struct Tag {
  int id; int *trace;
  Tag(int n,int&t):id(n),trace(&t){t=t*10+n;}
  ~Tag(){*trace=*trace*10+id+5;}
  void call(const Tag&){*trace=*trace*10+3;}
  Tag&operator+=(const Tag&){*trace=*trace*10+3;return *this;}
};
void mixed(Tag value,const Tag&ref){*value.trace=*value.trace*10+3;}
struct FromReference { int n; FromReference(const R&r):n(r.n){} };
struct DefaultSource { Counts *counts; int value=R(11,*counts).get(); };
struct MemberInit { int value; MemberInit(Counts&c):value(R(13,c).get()){} };
struct Item {
  int n; Counts *counts;
  Item(int value,Counts&c):n(value),counts(&c){++counts->made;++counts->alive;}
  Item(Item&&r):n(r.n),counts(r.counts){++counts->moved;++counts->alive;r.n=-1;}
  Item&operator=(Item&&r){n=r.n;r.n=-1;++counts->moved;return *this;}
  ~Item(){--counts->alive;++counts->destroyed;}
};
struct Group { Item item; };
struct Pure {int n;constexpr int get()const noexcept{return n;}};
constexpr int pure=Pure{5}.get();
static_assert(pure==5,"temporary receiver constant");
static_assert(noexcept(Pure{1}.get()),"temporary receiver query");
int scalarResult(Counts&c){return R(12,c).get();}
int main(){
  Counts c{0,0,0,0,0};
  int observed=R(3,c).get();
  if(observed!=4||c.made!=1||c.destroyed!=1||c.alive)return 1;
  observed=static_cast<const R&&>(R(5,c)).get();
  if(observed!=5||c.made!=2||c.destroyed!=2)return 2;
  observed=read(R(7,c));
  if(observed!=7||c.made!=3||c.destroyed!=3)return 3;
  observed=readX(R(7,c));
  if(observed!=9||c.made!=4||c.destroyed!=4)return 4;
  if(number(12)!=12||increase(12)!=13)return 5;
  int value=9;
  if(unsignedNumber(value)!=9||value!=9||!nullPointer(nullptr)||!enumValue(Code::ok))return 6;
  observed=R(4,c)(3);
  if(observed!=7||c.made!=5||c.destroyed!=5)return 7;
  observed=number(R(10,c).ref());
  if(observed!=10||c.made!=6||c.destroyed!=6)return 8;
  int converted=R(6,c);
  if(converted!=6||c.made!=7||c.destroyed!=7)return 9;
  observed=R(1,c)+R(2,c);
  if(observed!=3||c.made!=9||c.destroyed!=9)return 10;
  observed=Box{R(3,c)}.member.get();
  if(observed!=4||c.made!=10||c.destroyed!=10)return 11;
  observed=(Holder{{R(3,c),R(4,c)}}.array+1)->get();
  if(observed!=4||c.made!=12||c.destroyed!=12)return 12;
  observed=(0+Holder{{R(5,c),R(6,c)}}.array)->get();
  if(observed!=5||c.made!=14||c.destroyed!=14)return 13;
  observed=number(Holder{{R(7,c),R(8,c)}}.array[0].n);
  if(observed!=7||c.made!=16||c.destroyed!=16)return 14;
  if(c.copied||c.moved||c.alive)return 15;
  int trace=0; Tag(1,trace).call(Tag(2,trace));
  if(trace!=12376)return 16;
  trace=0;Tag(1,trace)+=Tag(2,trace);
  if(trace!=21367)return 17;
  trace=0;Tag(1,trace).operator+=(Tag(2,trace));
  if(trace!=12376)return 18;
  trace=0;mixed(Tag(1,trace),Tag(2,trace));
  if(trace!=12367)return 19;
  Counts returned{0,0,0,0,0};
  {
    R result=R(20,returned).make();
    if(result.n!=21||result.self!=&result||returned.made!=2||returned.alive!=1||returned.destroyed!=1||returned.copied||returned.moved)return 20;
  }
  if(returned.destroyed!=2||returned.alive)return 21;
  Counts calls{0,0,0,0,0};
  FromReference from(R(3,calls));DefaultSource defaults{&calls};MemberInit members(calls);
  if(from.n!=3||defaults.value!=12||members.value!=14||calls.made!=3||calls.destroyed!=3||calls.alive)return 22;
  int total=0;
  for(int i=0;i<3;++i){total+=R(i,calls).get();if(calls.alive)return 23;}
  if(total!=6||calls.made!=6||calls.destroyed!=6)return 24;
  bool off=false;int selected=off?R(9,calls).get():R(10,calls).get();
  if(selected!=11||calls.made!=7||calls.destroyed!=7)return 25;
  bool first=R(0,calls)&&R(1,calls);
  if(first||calls.made!=8||calls.destroyed!=8)return 26;
  bool second=R(1,calls)||R(0,calls);
  if(!second||calls.made!=9||calls.destroyed!=9)return 27;
  bool both=R(1,calls)&&R(2,calls);
  if(!both||calls.made!=11||calls.destroyed!=11||calls.alive)return 28;
  if(noexcept(static_cast<bool>(R(1,calls)))||calls.made!=11)return 29;
  Counts moves{0,0,0,0,0};
  {
    Item moved(static_cast<Item&&>(Item(8,moves)));
    if(moved.n!=8||moves.made!=1||moves.moved!=1||moves.destroyed!=1||moves.alive!=1)return 30;
    moved=Item(9,moves);
    if(moved.n!=9||moves.made!=2||moves.moved!=2||moves.destroyed!=2||moves.alive!=1)return 31;
    Group group(static_cast<Group&&>(Group{Item(10,moves)}));
    if(group.item.n!=10||moves.made!=3||moves.moved!=3||moves.destroyed!=3||moves.alive!=2)return 32;
    group=Group{Item(12,moves)};
    if(group.item.n!=12||moves.made!=4||moves.moved!=4||moves.destroyed!=4||moves.alive!=2)return 33;
  }
  if(moves.destroyed!=6||moves.alive)return 34;
  if(pure!=5)return 35;
  Counts conditions{0,0,0,0,0};
  if(R(1,conditions)){if(conditions.alive)return 36;}
  int remaining=2,rounds=0;
  while(R(remaining--,conditions)){if(conditions.alive)return 37;++rounds;}
  if(rounds!=2||conditions.made!=4||conditions.destroyed!=4)return 38;
  observed=scalarResult(conditions);
  if(observed!=13||conditions.made!=5||conditions.destroyed!=5||conditions.alive)return 39;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("temporary-calls" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2TemporaryCallsAcceptFullExpressionBindings) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"user-copy-temporary-receiver", "struct R{int n;R(int v):n(v){}R&operator=(const R&r){n=r.n;return *this;}};void f(){R r(1);R(2)=r;}"},
      {"940-temporary-source", "struct R{int n;R&operator=(const R&)=default;};void f(R&r){r=R{1};}"},
      {"941-temporary-receiver", "struct R{int n;R&operator=(const R&)=default;};void f(const R&s){R{1}=s;}"},
      {"1088-temporary-reference", "int take(const int&n){return n;}struct R{int n=take(1);};"},
      {"1089-temporary-receiver", "struct A{int n;int get(){return n;}};struct R{int n=A{1}.get();};"},
      {"1206-temporary-method", "struct R{int n;int get()&&{return n;}};int f(){return R{1}.get();}"},
      {"1207-temporary-cast-method", "struct R{int n;int get()&&{return n;}};int f(){return static_cast<R&&>(R{1}).get();}"},
      {"1208-temporary-callee-argument", "struct R{int n;};R&&id(R&&r){return static_cast<R&&>(r);}void f(){R&&r=id(R{1});}"},
      {"1341-temporary-constructor-source", "struct R{int n;R(int v):n(v){}R(R&&r):n(r.n){}};void f(){R r(static_cast<R&&>(R(1)));}"},
      {"1342-temporary-assignment-source", "struct R{int n;R&operator=(R&&r){n=r.n;return *this;}};void f(R&r){r=R{1};}"},
      {"1343-temporary-assignment-receiver", "struct R{int n;R&operator=(R&&r){n=r.n;return *this;}};void f(R&r){R{1}=static_cast<R&&>(r);}"},
      {"1524-temporary-defaulted-constructor", "struct R{int n;R(R&&)=default;};void f(){R r(static_cast<R&&>(R{1}));}"},
      {"1525-temporary-defaulted-assignment-source", "struct R{int n;R&operator=(R&&)=default;};void f(R&r){r=R{1};}"},
      {"1526-temporary-defaulted-assignment-receiver", "struct R{int n;R&operator=(R&&)=default;};void f(R&r){R{1}=static_cast<R&&>(r);}"},
      {"1527-temporary-implicit-member-source", "struct R{int n;};void f(R&r){r.operator=(R{1});}"},
      {"1528-temporary-implicit-member-receiver", "struct R{int n;};void f(R&r){R{1}.operator=(static_cast<R&&>(r));}"},
      {"1530-temporary-nontrivial-implicit-constructor", "struct I{int n;I(int v):n(v){}I(I&&r):n(r.n){}};struct R{I i;};void f(){R r(static_cast<R&&>(R{I(1)}));}"},
      {"1531-temporary-nontrivial-implicit-assignment", "struct I{int n;I&operator=(I&&r){n=r.n;return *this;}};struct R{I i;};void f(R&r){r=R{{1}};}"},
      {"1677-temporary-receiver", "struct R{int n;int get()noexcept{return n;}};bool f(){return noexcept(R{1}.get());}"},
      {"1678-temporary-reference", "int take(const int&n)noexcept{return n;}bool f(){return noexcept(take(1));}"},
      {"1853-temporary-receiver", "struct R{int n;int operator()()const{return n;}};int f(){return R{1}();}"},
      {"1854-temporary-reference", "struct R{int n;};int operator+(const R&a,const R&b){return a.n+b.n;}int f(R&r){return r+R{1};}"},
      {"1855-unevaluated-temporary", "struct R{int n;int operator()()const noexcept{return n;}};bool f(){return noexcept(R{1}());}"},
      {"2047-fresh-receiver", "struct R{int n;operator int()const{return n;}};int f(){return R{1};}"},
      {"2053-query-fresh-receiver", "struct R{operator int()const noexcept{return 1;}};bool f(){return noexcept(static_cast<int>(R{}));}"},
      {"2270-temporary-receiver", "struct R{int n;~R(){}int get(){return n;}};int f(){return R{1}.get();}"},
      {"2449-temporary-reference-argument", "int f(const int&r){return r;} int main(){return f(1);}"},
      {"2505-method-temporary-dot", "struct R{int n;int get()const{return n;}};int f(){return R{1}.get();}"},
      {"2506-method-temporary-arrow", "struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return H{{{1}}}.a->get();}"},
      {"2507-method-temporary-arrow-offset", "struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return (H{{{1}}}.a+0)->get();}"},
      {"2508-method-dead-temporary-receiver", "struct R{int n;int get()const{return n;}};int f(){if(false)return R{1}.get();return 0;}"},
      {"2509-method-folded-temporary-receiver", "struct R{int n;constexpr int get()const{return n;}};static_assert(R{1}.get()==1);"},
      {"2523-method-method-temporary-reference-argument", "struct R{int n;int get(const int&v){return n+v;}};int f(){R r{1};return r.get(2);}"},
      {"2524-method-static-temporary-reference-argument", "struct R{int n;static int get(const int&v){return v;}};int f(){return R::get(2);}"},
      {"2526-method-temporary-reverse-arrow-offset", "struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return (0+H{{{1}}}.a)->get();}"},
      {"2546-constructor-temporary-reference-argument", "struct R{int n;R(const int &v):n(v){}};int f(){R r(1);return r.n;}"},
      {"2547-constructor-dead-temporary-reference-argument", "struct R{int n;R(const int &v):n(v){}};int f(){if(false){R r(1);return r.n;}return 0;}"},
      {"2548-constructor-temporary-method-receiver", "struct R{int n;R(int v):n(v){} int get()const{return n;}};int f(){return R(1).get();}"},
      {"2549-constructor-temporary-subobject-receiver", "struct I{int n;int get()const{return n;}};struct R{I i;R():i{1}{}};int f(){return R().i.get();}"},
      {"2550-constructor-temporary-array-receiver", "struct I{int n;int get()const{return n;}};struct R{I i[1];R():i{{1}}{}};int f(){return (R().i+0)->get();}"},
      {"620-temporary-assignment-source", "struct R{int n;R(int v):n(v){}R&operator=(const R&r){n=r.n;return *this;}};void f(){R r(1);r=R(2);}"},
      {"1198-reference-argument", "int f(int&&n){return n;}int main(){return f(1);}"},
      {"cpp-scalar-reference-argument", "int f(const int &x){return x;} int main(){return f(42);}"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("temporary-calls-positive-" + Name + ".cpp");
    const auto Output = tmpFile("temporary-calls-positive-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    EXPECT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  }
}

TEST_F(TranslateTest, CoreV2TemporaryCallsRetainLifetimeExtensionBoundaries) {
  const std::vector<std::tuple<std::string, std::string, std::string>> Cases = {
      {"empty-conversion-record", "struct R{operator int()const{return 1;}};int f(){R r;const int&n=r;return n;}", "TR0201"},
      {"fresh-return", "const int&f(){return 1;}", "TR0201"},
      {"fresh-record-return", "struct R{int n;};const R&f(){return R{1};}", "TR0201"},
      {"static-extension", "int f(){static const int&r=1;return r;}", "TR0201"},
      {"global-extension", "const int&r=1;", "TR0201"},
      {"unused-throw", "struct R{int get()const{throw 1;}};int f(){return R{}.get();}", "TR0201"},
      {"query-float", "struct R{double get()const noexcept{return 1.0;}};bool f(){return noexcept(R{}.get());}", "TR0201"},
      {"mutable-reference", "void take(int&){}void f(){take(1);}", "TR0202"},
      {"mutable-record-reference", "struct R{int n;};void take(R&){}void f(){take(R{1});}", "TR0202"},
      {"lvalue-qualified", "struct R{int get()&{return 1;}};int f(){return R{}.get();}", "TR0202"},
      {"deleted-move", "struct R{R(){}R(R&&)=delete;};void f(){R r(static_cast<R&&>(R{}));}", "TR0202"},
      {"temporary-receiver", "struct R{int get()const;};int f(){return R{}.get();}", "TR0203"},
      {"temporary-argument", "int take(const int&);int f(){return take(1);}", "TR0203"},
  };
  for (const auto &[Name, Code, Diagnostic] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("temporary-calls-reject-" + Name + ".cpp");
    const auto Output = tmpFile("temporary-calls-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Diagnostic);
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("temporary-calls-v1.cpp");
  const auto Output = tmpFile("temporary-calls-v1.nc");
  for (const std::string &Code : {
      "int take(const int&n){return n;}int f(){return take(1);}",
      "struct R{int get()const{return 1;}};int f(){return R{}.get();}"}) {
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2NoexceptPreservesQueriesAndNormalExecution) {
  const auto Source = tmpFile("noexcept.cpp");
  const auto Output = tmpFile("noexcept.nc");
  writeFile(Source, R"cpp(
struct Counts { int made,copied,moved,assigned,destroyed; };
constexpr bool yes(){return true;}
int certain(int&n)noexcept(yes()){return ++n;}
int propagated(int&n)noexcept(noexcept(certain(n))){return certain(n);}
int potential(int&n)noexcept(false){return ++n;}
int unspecified(int&n){return ++n;}
int legacy(int&n)throw(){return ++n;}
bool query(int&n){return noexcept(++n);}
constexpr bool arithmetic=noexcept(1+2);
static_assert(arithmetic);
struct Sure {
  int n;Counts*counts;Sure*self=this;
  Sure(Counts&c,int value)noexcept:n(value),counts(&c){++counts->made;}
  Sure(const Sure&s)noexcept:n(s.n+1),counts(s.counts){++counts->copied;}
  Sure(Sure&&s)noexcept(noexcept(++s.n)):n(s.n+2),counts(s.counts){++counts->moved;s.n=-1;}
  Sure&operator=(const Sure&s)noexcept(false){n=s.n+3;++counts->assigned;return *this;}
  Sure&operator=(Sure&&s)noexcept{n=s.n+4;s.n=-2;++counts->assigned;return *this;}
  int read()const & noexcept{return n;}
  int read()&& noexcept(false){return n;}
  static int bump(int&n)throw(){return ++n;}
  ~Sure()noexcept{++counts->destroyed;}
};
struct Risky {
  Counts*counts;
  Risky(Counts&c)noexcept:counts(&c){++counts->made;}
  ~Risky()noexcept(false){++counts->destroyed;}
};
struct Flags { Counts*counts;bool safe=noexcept(++counts->made);bool risky=noexcept(Risky(*counts)); };
struct DefaultSure {
  Sure values[2];
  DefaultSure(DefaultSure&&)noexcept=default;
  DefaultSure&operator=(DefaultSure&&)noexcept=default;
  ~DefaultSure()noexcept=default;
};
struct PotentialLeaf {
  int n;PotentialLeaf(int v)noexcept(false):n(v){}
  PotentialLeaf(PotentialLeaf&&s)noexcept(false):n(s.n+1){s.n=-1;}
  PotentialLeaf&operator=(PotentialLeaf&&s)noexcept(false){n=s.n+2;s.n=-2;return *this;}
};
struct AutoException { PotentialLeaf leaf; };
struct Defaulted { int n=7;Defaulted()noexcept=default;~Defaulted()noexcept=default; };
struct DefaultFalse { int n=9;DefaultFalse()noexcept(false)=default; };
struct Outside {
  int n=1;Outside()noexcept;Outside(Outside&&)noexcept;
  Outside&operator=(Outside&&)noexcept;~Outside()noexcept;
};
Outside::Outside()noexcept=default;
Outside::Outside(Outside&&)noexcept=default;
Outside&Outside::operator=(Outside&&)noexcept=default;
Outside::~Outside()noexcept=default;
int redeclared(int&n)noexcept(noexcept(certain(n)));
int redeclared(int&n)noexcept(true){return ++n;}
int main(){
  int n=0;Counts counts{};
  if(!noexcept(++n) || !query(n) || !noexcept(certain(n)) || !noexcept(propagated(n)))return 1;
  if(noexcept(potential(n)) || noexcept(unspecified(n)) || !noexcept(legacy(n)))return 2;
  if(!noexcept(noexcept(potential(++n))) || n)return 3;
  if(!noexcept(Sure(counts,1)) || noexcept(Risky(counts)) || counts.made || counts.destroyed)return 4;
  Flags flags{&counts};
  if(!flags.safe || flags.risky || counts.made || counts.destroyed)return 5;
  {
    Sure a(counts,3),b(counts,5);
    if(!noexcept(Sure(a)) || !noexcept(Sure(static_cast<Sure&&>(a))))return 6;
    if(noexcept(b=a) || !noexcept(b=static_cast<Sure&&>(a)))return 7;
    if(!noexcept(a.read()) || noexcept(static_cast<Sure&&>(a).read()) || !noexcept(Sure::bump(n)))return 8;
    if(counts.made!=2 || counts.copied || counts.moved || counts.assigned || counts.destroyed || a.n!=3 || b.n!=5 || n)return 9;
    Sure copied(a);Sure moved(static_cast<Sure&&>(a));b=static_cast<Sure&&>(copied);
    if(counts.copied!=1 || counts.moved!=1 || counts.assigned!=1 || a.n!=-1 || copied.n!=-2 || b.n!=8 || moved.n!=5)return 10;
    if(copied.self!=&copied || moved.self!=&moved || b.self!=&b)return 11;
    if(noexcept(b=copied))return 12;
    b=copied;
    if(b.n!=1 || counts.assigned!=2)return 13;
  }
  if(counts.destroyed!=4)return 14;
  {Risky object(counts);if(counts.made!=3 || counts.destroyed!=4)return 15;}
  if(counts.destroyed!=5)return 16;
  {
    DefaultSure source{{Sure(counts,1),Sure(counts,2)}};
    if(!noexcept(DefaultSure(static_cast<DefaultSure&&>(source))) || !noexcept(source=static_cast<DefaultSure&&>(source)))return 17;
    if(counts.made!=5 || counts.copied!=1 || counts.moved!=1 || counts.assigned!=2 || counts.destroyed!=5)return 18;
    DefaultSure target(static_cast<DefaultSure&&>(source));
    if(target.values[0].n!=3 || source.values[1].n!=-1 || target.values[1].self!=&target.values[1] || counts.moved!=3)return 19;
  }
  if(counts.destroyed!=9)return 20;
  AutoException source{PotentialLeaf(1)};
  if(noexcept(AutoException(static_cast<AutoException&&>(source))) || noexcept(source=static_cast<AutoException&&>(source)) || source.leaf.n!=1)return 21;
  AutoException target(static_cast<AutoException&&>(source));
  if(target.leaf.n!=2 || source.leaf.n!=-1)return 22;
  source=static_cast<AutoException&&>(target);
  if(source.leaf.n!=4 || target.leaf.n!=-2)return 23;
  constexpr bool local=noexcept(Defaulted());static_assert(local);
  if(!local || noexcept(DefaultFalse()) || !noexcept(Outside()))return 24;
  Defaulted def;DefaultFalse bad;Outside outside;
  if(def.n!=7 || bad.n!=9 || outside.n!=1)return 25;
  if(!noexcept(Outside(static_cast<Outside&&>(outside))) || !noexcept(outside=static_cast<Outside&&>(outside)))return 26;
  Outside second(static_cast<Outside&&>(outside));outside=static_cast<Outside&&>(second);
  if(outside.n!=1 || second.n!=1)return 27;
  bool selected=n?noexcept(potential(n)):noexcept(certain(n));
  if(!selected || !noexcept(redeclared(n)) || n)return 28;
  if(propagated(n)!=1 || legacy(n)!=2 || Sure::bump(n)!=3 || redeclared(n)!=4 || n!=4)return 29;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("noexcept" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NoexceptAcceptsStandardSpecifications) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"562-noexcept-constructor", "struct R{int n;R(const R&r)noexcept:n(r.n){}};"},
      {"562-noexcept-assignment", "struct R{int n;R&operator=(const R&r)noexcept{n=r.n;return *this;}};"},
      {"761-copy-noexcept", "struct R{int n;R(const R&)noexcept=default;};"},
      {"761-copy-noexcept-false", "struct R{int n;R(const R&)noexcept(false)=default;};"},
      {"761-copy-throw", "struct R{int n;R(const R&)throw()=default;};"},
      {"761-out-of-line-noexcept", "struct R{int n;R(const R&)noexcept;};R::R(const R&)noexcept=default;"},
      {"886-noexcept", "struct R{int n;R&operator=(const R&)noexcept=default;};"},
      {"886-noexcept-false", "struct R{int n;R&operator=(const R&)noexcept(false)=default;};"},
      {"886-out-of-line-noexcept", "struct R{int n;R&operator=(const R&)noexcept;};R&R::operator=(const R&)noexcept=default;"},
      {"1147-method-noexcept", "struct R{int n;int get()&&noexcept{return n;}};"},
      {"1286-constructor-noexcept", "struct R{int n;R(R&&r)noexcept:n(r.n){}};"},
      {"1286-assignment-noexcept", "struct R{int n;R&operator=(R&&r)noexcept{n=r.n;return *this;}};"},
      {"1476-constructor-noexcept", "struct R{int n;R(R&&)noexcept=default;};"},
      {"1476-constructor-noexcept-false", "struct R{int n;R(R&&)noexcept(false)=default;};"},
      {"1476-constructor-throw", "struct R{int n;R(R&&)throw()=default;};"},
      {"1476-assignment-noexcept", "struct R{int n;R&operator=(R&&)noexcept=default;};"},
      {"1476-assignment-noexcept-false", "struct R{int n;R&operator=(R&&)noexcept(false)=default;};"},
      {"1476-out-of-line-noexcept", "struct R{int n;R(R&&)noexcept;};R::R(R&&)noexcept=default;"},
      {"1476-out-of-line-assignment-noexcept", "struct R{int n;R&operator=(R&&)noexcept;};R&R::operator=(R&&)noexcept=default;"},
      {"1592-constructor-noexcept", "struct R{int n;R()noexcept=default;};"},
      {"1592-constructor-noexcept-false", "struct R{int n;R()noexcept(false)=default;};"},
      {"1592-destructor-noexcept", "struct R{int n;~R()noexcept=default;};"},
      {"1592-destructor-throw", "struct R{int n;~R()throw()=default;};"},
      {"1592-out-of-line-noexcept", "struct R{int n;R()noexcept;};R::R()noexcept=default;"},
      {"1592-out-of-line-destructor-noexcept", "struct R{int n;~R()noexcept;};R::~R()noexcept=default;"},
      {"1710-explicit-noexcept", "struct R{int n;~R()noexcept{}};"},
      {"1710-explicit-noexcept-false", "struct R{int n;~R()noexcept(false){}};"},
      {"1710-explicit-empty-throw", "struct R{int n;~R()throw(){}};"},
      {"1958-method-noexcept-method", "struct R{int n;int get()const noexcept{return n;}};"},
      {"1985-constructor-noexcept-constructor", "struct R{int n;R() noexcept:n(1){}};"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("noexcept-positive-" + Name + ".cpp");
    const auto Output = tmpFile("noexcept-positive-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    EXPECT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  }
}

TEST_F(TranslateTest, CoreV2NoexceptInspectsUnevaluatedSource) {
  const std::vector<std::tuple<std::string, std::string, std::string>> Cases = {
      {"query-size-double", "bool f(){return noexcept(sizeof(double));}", "TR0201"},
      {"unused-query", "void f(){noexcept(sizeof(double));}", "TR0201"},
      {"spec-size-double", "int f()noexcept(sizeof(double)>0){return 1;}", "TR0201"},
      {"short-circuit-spec", "int f()noexcept(true || noexcept(sizeof(double))){return 1;}", "TR0201"},
      {"nested-query", "bool f(){return noexcept(noexcept(sizeof(double)));}", "TR0201"},
      {"defaulted-spec", "struct R{int n;R()noexcept(sizeof(double)>0)=default;};", "TR0201"},
      {"out-of-line-spec", "struct R{int n;R()noexcept(sizeof(double)>0);};R::R()noexcept(true)=default;", "TR0201"},
      {"redeclaration-spec", "int f()noexcept(sizeof(double)>0);int f()noexcept(true){return 1;}", "TR0201"},
      {"default-field-query", "struct R{bool b=noexcept(sizeof(double));};", "TR0201"},
      {"throw-query", "bool f(){return noexcept(throw 1);}", "TR0201"},
      {"throw-body", "int f()noexcept{throw 1;}", "TR0201"},
      {"catch-body", "int f()noexcept(false){try{return 1;}catch(...){return 2;}}", "TR0201"},
      {"vendor-nothrow", "__attribute__((nothrow)) int f(){return 1;}", "TR0201"},
      {"dependent-spec", "template<class T>int f(T&t)noexcept(noexcept(t.get())){return 1;}", "TR0201"},
      {"explicit-destruction-query", "struct R{int n;~R()noexcept{}};bool f(R&r){return noexcept(r.~R());}", "TR0201"},
      {"nonconstant-spec", "void f(int n)noexcept(n){}", "TR0202"},
      {"incompatible-redeclaration", "int f()noexcept;int f()noexcept(false){return 1;}", "TR0202"},
      {"typed-dynamic-spec", "int f()throw(int){return 1;}", "TR0202"},
      {"invalid-query-operand", "bool f(){return noexcept(unknown());}", "TR0202"},
      {"query-call", "int missing()noexcept;bool f(){return noexcept(missing());}", "TR0203"},
      {"query-constructor", "struct R{int n;R()noexcept;};bool f(){return noexcept(R());}", "TR0203"},
  };
  for (const auto &[Name, Code, Diagnostic] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("noexcept-reject-" + Name + ".cpp");
    const auto Output = tmpFile("noexcept-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Diagnostic);
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("noexcept-v1.cpp");
  const auto Output = tmpFile("noexcept-v1.nc");
  for (const std::string &Code : {"int f()noexcept{return 1;}",
                                  "bool f(){return noexcept(1+2);}"}) {
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2GeneratedMovesPreserveMembersSelectionAndLifetimes) {
  const auto Source = tmpFile("generated-moves.cpp");
  const auto Output = tmpFile("generated-moves.nc");
  writeFile(Source, R"cpp(
struct Log { int copies,moves,copyAssignments,moveAssignments,destroyed,defaults,used;int order[128]; };
void mark(Log&log,int n){log.order[log.used++]=n;}
struct Leaf {
  int n;Log*log;Leaf*self=this;Leaf*alias=this;
  Leaf(int value,Log&l):n(value),log(&l){}
  Leaf(const Leaf&s):n(s.n+100),log(s.log){++log->copies;mark(*log,s.n+100);}
  Leaf(Leaf&&s):n(s.n+10),log(s.log){++log->moves;mark(*log,s.n);s.n=-s.n;}
  Leaf&operator=(const Leaf&s){n=s.n+30;++log->copyAssignments;mark(*log,s.n+100);return *alias;}
  Leaf&operator=(Leaf&&s){int old=s.n;n=old+20;if(this!=&s)s.n=-old;++log->moveAssignments;mark(*log,old);return *alias;}
  ~Leaf(){++log->destroyed;}
};
struct CopyOnly {
  int n;Log*log;CopyOnly*self=this;
  CopyOnly(int value,Log&l):n(value),log(&l){}
  CopyOnly(const CopyOnly&s):n(s.n+100),log(s.log){++log->copies;mark(*log,s.n+100);}
  CopyOnly&operator=(const CopyOnly&s){n=s.n+30;++log->copyAssignments;mark(*log,s.n+100);return *this;}
  ~CopyOnly(){++log->destroyed;}
};
struct Box {
  int scalar[2];Leaf grid[2][2];CopyOnly fallback[2];Box*self=this;
  int cookie=++grid[0][0].log->defaults;
  Box(Box&&)=default;
  Box&operator=(Box&&)=default;
  Box&operator=(const Box&)=default;
  ~Box()=default;
};
Box make(Log&log){return {{7,8},{{Leaf(1,log),Leaf(2,log)},{Leaf(3,log),Leaf(4,log)}},{CopyOnly(5,log),CopyOnly(6,log)}};}
Box forward(Log&log){return make(log);}
Box moved(Box&&source){return static_cast<Box&&>(source);}
Box parameter(Box source){return source;}
Box&&right(Box&r,int&t){t=t*10+2;return static_cast<Box&&>(r);}
Box&left(Box&target,Box&source,int&t){t=t*10+1;source.grid[0][0].n=77;return target;}
struct Implicit { Leaf items[2]; };
struct Outside {
  Leaf leaf;Outside*self=this;
  Outside(int n,Log&l):leaf(n,l){}
  Outside(Outside&&);
  Outside&operator=(Outside&&);
};
Outside::Outside(Outside&&)=default;
Outside&Outside::operator=(Outside&&)=default;
struct Explicit {
  Leaf leaf;Explicit(int n,Log&l):leaf(n,l){}
  explicit Explicit(Explicit&&)=default;
};
struct Qualified { Leaf leaf;Qualified&operator=(Qualified&&) & =default; };
struct RvalueQualified { Leaf leaf;RvalueQualified&operator=(RvalueQualified&&) && =default; };
struct Trivial {
  int values[2];Trivial*self=this;
  Trivial(Trivial&&)=default;Trivial&operator=(Trivial&&)=default;
};
Trivial&mutate(Trivial&target,Trivial&source,int&t){t=t*10+1;source.values[0]=77;return target;}
Trivial&&select(Trivial&source,int&t){t=t*10+2;return static_cast<Trivial&&>(source);}
Trivial&reseat(Trivial&target,Trivial*&pointer,Trivial&other){pointer=&other;return target;}
struct Plain {
  int n;Log*log;Plain*self=this;
  Plain(int value,Log&l):n(value),log(&l){}
  Plain(const Plain&s):n(s.n),log(s.log){++log->copies;}
  ~Plain(){++log->destroyed;}
};
struct MovePlain {
  int n;Log*log;MovePlain*self=this;
  MovePlain(int value,Log&l):n(value),log(&l){}
  MovePlain&operator=(MovePlain&&)=default;
  ~MovePlain(){++log->destroyed;}
};
struct AssignArrays { Plain copied[2];MovePlain moved[2];Leaf leaf; };
struct Cleanup { int n;Log*log;~Cleanup(){++log->destroyed;} };
struct Wrapper { Cleanup member; };
struct Simple { int n; };
int main(){
  Log construction{};
  {
    Box source=forward(construction);Box target(static_cast<Box&&>(source));
    if(construction.moves!=4 || construction.copies!=2 || construction.defaults!=1 || construction.destroyed)return 1;
    if(target.grid[0][0].n!=11 || target.grid[1][1].n!=14 || source.grid[0][1].n!=-2 || source.grid[1][0].n!=-3)return 2;
    if(target.fallback[0].n!=105 || target.fallback[1].n!=106 || source.fallback[0].n!=5)return 3;
    if(target.grid[1][0].self!=&target.grid[1][0] || target.fallback[1].self!=&target.fallback[1])return 4;
    if(target.self!=&source || target.cookie!=1 || target.scalar[0]!=7 || target.scalar[1]!=8)return 5;
    if(construction.used!=6 || construction.order[0]!=1 || construction.order[1]!=2 || construction.order[2]!=3 || construction.order[3]!=4 || construction.order[4]!=105 || construction.order[5]!=106)return 6;
  }
  if(construction.destroyed!=12)return 7;
  Log assignment{};
  {
    Box source=make(assignment),target=make(assignment);
    target.grid[0][0].alias=&source.grid[1][1];
    if(&(target=static_cast<Box&&>(source))!=&target)return 8;
    if(target.grid[0][0].n!=21 || target.grid[1][1].n!=24 || source.grid[1][1].n!=-4 || target.fallback[0].n!=35 || source.fallback[0].n!=5)return 9;
    if(assignment.moveAssignments!=4 || assignment.copyAssignments!=2 || assignment.moves || assignment.copies || assignment.destroyed)return 10;
    if(assignment.defaults!=2 || target.cookie!=1 || target.self!=&source || target.grid[0][0].self!=&target.grid[0][0])return 11;
    if(assignment.order[0]!=1 || assignment.order[3]!=4 || assignment.order[4]!=105 || assignment.order[5]!=106)return 12;
    if(&(target=static_cast<Box&&>(target))!=&target || target.grid[0][0].n!=41 || target.fallback[0].n!=65)return 13;
    Box outer=make(assignment);
    outer=target=static_cast<Box&&>(source);
    if(target.grid[0][0].n!=19 || source.grid[0][0].n!=1 || outer.grid[0][0].n!=49 || outer.fallback[0].n!=65)return 14;
    int trace=0;
    left(target,source,trace)=right(source,trace);
    if(trace!=21 || target.grid[0][0].n!=97 || source.grid[0][0].n!=-77)return 15;
    trace=0;
    left(target,source,trace).operator=(right(source,trace));
    if(trace!=12 || target.grid[0][0].n!=97 || source.grid[0][0].n!=-77 || assignment.defaults!=3)return 16;
  }
  if(assignment.destroyed!=18)return 17;
  Log calls{};
  {
    Box source=make(calls);Box result=moved(static_cast<Box&&>(source));
    if(calls.moves!=4 || calls.copies!=2 || result.grid[0][0].n!=11 || result.grid[0][0].self!=&result.grid[0][0])return 18;
    Box returned=parameter(static_cast<Box&&>(result));
    if(calls.moves!=12 || calls.copies!=6 || calls.destroyed!=6 || calls.defaults!=1 || returned.grid[0][0].n!=31 || returned.fallback[0].n!=305 || returned.grid[0][0].self!=&returned.grid[0][0])return 19;
  }
  if(calls.destroyed!=24)return 20;
  Log variants{};
  {
    Implicit source{{Leaf(1,variants),Leaf(2,variants)}};
    Implicit target(static_cast<Implicit&&>(source));
    if(target.items[1].n!=12 || target.items[0].self!=&target.items[0] || variants.moves!=2)return 21;
    target=static_cast<Implicit&&>(source);
    if(target.items[0].n!=19 || source.items[0].n!=1 || variants.moveAssignments!=2)return 22;
    Outside a(3,variants);Outside b(static_cast<Outside&&>(a));
    if(b.leaf.n!=13 || b.leaf.self!=&b.leaf || b.self!=&a)return 23;
    if(&(a=static_cast<Outside&&>(b))!=&a || a.leaf.n!=33 || b.leaf.n!=-13 || a.leaf.self!=&a.leaf)return 24;
    Explicit x(4,variants);Explicit y(static_cast<Explicit&&>(x));
    if(y.leaf.n!=14 || x.leaf.n!=-4 || y.leaf.self!=&y.leaf)return 25;
    Qualified qa{Leaf(5,variants)},qb{Leaf(6,variants)};
    if(&(qa=static_cast<Qualified&&>(qb))!=&qa || qa.leaf.n!=26 || qb.leaf.n!=-6)return 26;
    RvalueQualified ra{Leaf(7,variants)},rb{Leaf(8,variants)};
    if(&(static_cast<RvalueQualified&&>(ra)=static_cast<RvalueQualified&&>(rb))!=&ra || ra.leaf.n!=28 || rb.leaf.n!=-8)return 27;
  }
  if(variants.destroyed!=12)return 28;
  Trivial source{{3,4}},target(static_cast<Trivial&&>(source)),other{{5,6}};
  if(target.values[0]!=3 || target.self!=&source || source.values[0]!=3)return 29;
  int trace=0;
  mutate(target,source,trace)=select(source,trace);
  if(trace!=21 || target.values[0]!=77 || target.self!=&source)return 30;
  source.values[0]=3;trace=0;
  mutate(target,source,trace).operator=(select(source,trace));
  if(trace!=12 || target.values[0]!=77)return 31;
  Trivial*pointer=&source;
  reseat(target,pointer,other)=static_cast<Trivial&&>(*pointer);
  if(pointer!=&other || target.values[0]!=77 || target.self!=&source)return 32;
  if(&(target=static_cast<Trivial&&>(target))!=&target || target.values[0]!=77)return 33;
  Log arrays{};
  {
    AssignArrays a{{Plain(1,arrays),Plain(2,arrays)},{MovePlain(3,arrays),MovePlain(4,arrays)},Leaf(5,arrays)};
    AssignArrays b{{Plain(6,arrays),Plain(7,arrays)},{MovePlain(8,arrays),MovePlain(9,arrays)},Leaf(10,arrays)};
    if(&(a=static_cast<AssignArrays&&>(b))!=&a || a.copied[1].n!=7 || a.moved[0].n!=8 || a.leaf.n!=30 || b.leaf.n!=-10)return 34;
    if(a.copied[0].self!=&b.copied[0] || a.moved[1].self!=&b.moved[1] || a.leaf.self!=&a.leaf)return 35;
    if(arrays.moves || arrays.copies || arrays.copyAssignments || arrays.moveAssignments!=1 || arrays.destroyed)return 36;
  }
  if(arrays.destroyed!=10)return 37;
  Simple simple{1};simple=Simple{2};Simple constructed(static_cast<Simple&&>(Simple{3}));
  if(simple.n!=2 || constructed.n!=3 || (Simple{4}=Simple{5}).n!=5)return 38;
  Log compatibility{};
  {
    Wrapper value(static_cast<Wrapper&&>(Wrapper{{1,&compatibility}}));
    if(value.member.n!=1 || compatibility.destroyed!=1)return 39;
    value=Wrapper{{2,&compatibility}};
    if(value.member.n!=2 || compatibility.destroyed!=2)return 40;
    int n=(Wrapper{{3,&compatibility}}=Wrapper{{4,&compatibility}}).member.n;
    if(n!=4 || compatibility.destroyed!=4)return 41;
  }
  if(compatibility.destroyed!=5)return 42;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("generated-moves" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2GeneratedMovesRetainSourceAndLifetimeBoundaries) {
  const std::vector<std::tuple<std::string, std::string, std::string>> Cases = {
      {"deleted-constructor", "struct R{int n;R(R&&)=delete;};", "TR0201"},
      {"deleted-assignment", "struct R{int n;R&operator=(R&&)=delete;};", "TR0201"},
      {"defaulted-deleted-constructor", "struct I{int n;I(I&&)=delete;};struct R{I i;R(R&&)=default;};", "TR0201"},
      {"defaulted-deleted-assignment", "struct I{int n;I&operator=(I&&)=delete;};struct R{I i;R&operator=(R&&)=default;};", "TR0201"},
      {"attribute", "struct R{int n;[[deprecated]] R(R&&)=default;};", "TR0201"},
      {"reference-field", "struct R{int&n;R(R&&)=default;};", "TR0201"},
      {"const-field", "struct R{const int n;R(R&&)=default;};", "TR0201"},
      {"private-field", "class R{int n;public:R(R&&)=default;};", "TR0201"},
      {"base", "struct B{int n;};struct R:B{int value;R(R&&)=default;};", "TR0201"},
      {"source-builtin", "struct R{int n[2];};void f(R&a,R&b){__builtin_memcpy(&a,&b,sizeof(R));}", "TR0201"},
      {"lambda-array", "int f(){int a[2]={1,2};auto capture=[a](){return a[0];};return capture();}", "TR0201"},
      {"expansion", "struct I{int n;I(I&&s):n(s.n){}};struct R{I items[65536];R(R&&)=default;};R f(R&&s){return static_cast<R&&>(s);}", "TR0201"},
      {"const-constructor-source", "struct R{int n;R(const R&&)=default;};", "TR0202"},
      {"volatile-constructor-source", "struct R{int n;R(volatile R&&)=default;};", "TR0202"},
      {"const-assignment-source", "struct R{int n;R&operator=(const R&&)=default;};", "TR0202"},
      {"volatile-assignment-source", "struct R{int n;R&operator=(volatile R&&)=default;};", "TR0202"},
      {"const-assignment-receiver", "struct R{int n;R&operator=(R&&)const=default;};", "TR0202"},
      {"volatile-assignment-receiver", "struct R{int n;R&operator=(R&&)volatile=default;};", "TR0202"},
      {"const-assignment-result", "struct R{int n;const R&operator=(R&&)=default;};", "TR0202"},
      {"value-assignment-result", "struct R{int n;R operator=(R&&)=default;};", "TR0202"},
      {"lvalue-binding", "struct R{int n;R(R&&)=default;};void f(R&r){R s(r);}", "TR0202"},
      {"invalid-ref-receiver", "struct R{int n;R&operator=(R&&)&&=default;};void f(R&a,R&b){a=static_cast<R&&>(b);}", "TR0202"},
  };
  for (const auto &[Name, Code, Diagnostic] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("generated-moves-reject-" + Name + ".cpp");
    const auto Output = tmpFile("generated-moves-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Diagnostic);
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("generated-moves-definition.cpp");
  const auto Output = tmpFile("generated-moves-definition.nc");
  for (const std::string &Code : {
      "struct I{int n;I(I&&);};struct R{I i;R(R&&)=default;};R f(R&&r){return static_cast<R&&>(r);}",
      "struct I{int n;I&operator=(I&&);};struct R{I i;R&operator=(R&&)=default;};void f(R&a,R&b){a=static_cast<R&&>(b);}"}) {
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0203");
    expectNoArtifacts(Output);
  }
  writeFile(Source, "struct R{int n;R(R&&)=default;};");
  auto Old = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
  expectCode(Old, "TR0201");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2UserMovesPreserveSelectedCallsAliasesAndCleanup) {
  const auto Source = tmpFile("user-moves.cpp");
  const auto Output = tmpFile("user-moves.nc");
  writeFile(Source, R"cpp(
struct Stats { int copies,moves,constMoves,copyAssignments,moveAssignments,constAssignments,destroyed; };
struct R {
  int n;Stats*stats;R*self=this;R*alias=this;
  R(int value,Stats&s):n(value),stats(&s){}
  R(const R&s):n(s.n+1),stats(s.stats){++stats->copies;}
  explicit R(R&&s):n(s.n+10),stats(s.stats){++stats->moves;s.n=-s.n;}
  R(const R&&s):n(s.n+20),stats(s.stats){++stats->constMoves;}
  R&operator=(const R&s){n=s.n+1;++stats->copyAssignments;return *alias;}
  R&operator=(R&&s){int old=s.n;n=old+10;if(this!=&s)s.n=-old;++stats->moveAssignments;return *alias;}
  R&operator=(const R&&s){n=s.n+20;++stats->constAssignments;return *alias;}
  ~R(){++stats->destroyed;}
};
R make(Stats&s,int n){return R(n,s);}
R forward(Stats&s,int n){return make(s,n);}
R moved(R&&r){return R(static_cast<R&&>(r));}
int take(R r){return r.n;}
R&&source(R&r,int&trace){trace=trace*10+2;return static_cast<R&&>(r);}
R&receiver(R&target,R&original,int&trace){trace=trace*10+1;original.n=77;return target;}
R&reseat(R&target,R*&pointer,R&other){pointer=&other;return target;}
struct Pair {
  R first;R values[2];
  Pair(R&a,R&b,R&c):first(static_cast<R&&>(a)),values{R(static_cast<R&&>(b)),R(static_cast<R&&>(c))}{}
  ~Pair()=default;
};
struct Outside {
  int n;Outside*self=this;
  Outside(int value):n(value){}
  Outside(Outside&&);
  Outside&operator=(Outside&&);
};
Outside::Outside(Outside&&r):n(r.n+1){r.n=-1;}
Outside&Outside::operator=(Outside&&r){n=r.n+2;r.n=-2;return *this;}
Outside localResult(){Outside source(4);return source;}
Outside parameterResult(Outside source){return source;}
struct Qualified {
  int n;Qualified*self;
  Qualified&operator=(Qualified&&r)& {n=r.n+1;self=this;r.n=-1;return *this;}
};
struct RvalueQualified {
  int n;RvalueQualified*self;
  RvalueQualified&operator=(RvalueQualified&&r)&& {n=r.n+2;self=this;r.n=-2;return *this;}
};
struct ConstexprMove {
  int n;constexpr ConstexprMove(int x):n(x){}
  constexpr ConstexprMove(ConstexprMove&&r):n(r.n){}
};
int main(){
  Stats stats{};
  {
    R original(3,stats);const R constant(7,stats);
    R copy(original);R move(static_cast<R&&>(original));R constMove(static_cast<const R&&>(constant));
    if(copy.n!=4 || move.n!=13 || original.n!=-3 || constMove.n!=27 || constant.n!=7)return 1;
    if(copy.self!=&copy || move.self!=&move || constMove.self!=&constMove || move.alias!=&move)return 2;
    R fallback=static_cast<R&&>(original);
    if(fallback.n!=17 || fallback.self!=&fallback || original.n!=-3)return 28;
    if(stats.copies!=1 || stats.moves!=1 || stats.constMoves!=2 || stats.destroyed)return 3;
    R&&named=static_cast<R&&>(original);R namedCopy(named);
    if(namedCopy.n!=-2 || stats.copies!=2 || stats.moves!=1 || namedCopy.self!=&namedCopy)return 4;
    R returned=moved(static_cast<R&&>(move));
    if(returned.n!=23 || move.n!=-13 || returned.self!=&returned || stats.moves!=2)return 5;
    // Copy initialization does not select an explicit move constructor.
    if(take(R(static_cast<R&&>(returned)))!=33 || returned.n!=-23 || stats.moves!=3 || stats.destroyed!=1)return 6;
    R direct=forward(stats,31);
    if(direct.n!=31 || direct.self!=&direct || stats.moves!=3 || stats.copies!=2)return 7;
  }
  if(stats.destroyed!=10)return 8;
  Stats assigned{};
  {
    R a(3,assigned),b(7,assigned),other(11,assigned);const R constant(13,assigned);
    b.alias=&other;
    if(&(b=static_cast<R&&>(a))!=&other || b.n!=13 || a.n!=-3 || assigned.moveAssignments!=1)return 9;
    if(&(a=static_cast<const R&&>(constant))!=&a || a.n!=33 || constant.n!=13 || assigned.constAssignments!=1)return 10;
    if(&(a=b)!=&a || a.n!=14 || assigned.copyAssignments!=1)return 11;
    if(&(a=static_cast<R&&>(a))!=&a || a.n!=24 || assigned.moveAssignments!=2)return 12;
    a = b = static_cast<R&&>(other);
    if(b.n!=21 || other.n!=-11 || a.n!=-10 || assigned.moveAssignments!=3 || assigned.copyAssignments!=2)return 13;
    int trace=0;
    receiver(b,a,trace)=source(a,trace);
    if(trace!=21 || b.n!=87 || a.n!=-77 || assigned.moveAssignments!=4)return 14;
    trace=0;
    receiver(b,a,trace).operator=(source(a,trace));
    if(trace!=12 || b.n!=87 || a.n!=-77 || assigned.moveAssignments!=5)return 15;
    R*pointer=&a;
    reseat(b,pointer,other)=static_cast<R&&>(*pointer);
    if(pointer!=&other || b.n!=-67 || a.n!=77 || other.n!=-11 || assigned.moveAssignments!=6)return 16;
    if(a.self!=&a || b.self!=&b || assigned.moves || assigned.copies || assigned.destroyed)return 17;
  }
  if(assigned.destroyed!=4)return 18;
  Stats members{};
  {
    R a(1,members),b(2,members),c(3,members);
    Pair pair(a,b,c);
    if(members.moves!=3 || members.copies || pair.first.n!=11 || pair.values[0].n!=12 || pair.values[1].n!=13)return 19;
    if(pair.first.self!=&pair.first || pair.values[0].self!=&pair.values[0] || pair.values[1].self!=&pair.values[1])return 20;
    if(a.n!=-1 || b.n!=-2 || c.n!=-3 || members.destroyed)return 21;
  }
  if(members.destroyed!=6)return 22;
  Outside outside(3);Outside second(static_cast<Outside&&>(outside));
  if(second.n!=4 || outside.n!=-1 || second.self!=&second)return 23;
  if(&(outside=static_cast<Outside&&>(second))!=&outside || outside.n!=6 || second.n!=-2)return 24;
  Qualified qa{3,nullptr},qb{7,nullptr};
  if(&(qa=static_cast<Qualified&&>(qb))!=&qa || qa.n!=8 || qb.n!=-1 || qa.self!=&qa)return 25;
  RvalueQualified ra{3,nullptr},rb{7,nullptr};
  if(&(static_cast<RvalueQualified&&>(ra)=static_cast<RvalueQualified&&>(rb))!=&ra || ra.n!=9 || rb.n!=-2 || ra.self!=&ra)return 26;
  ConstexprMove cv(9);ConstexprMove result(static_cast<ConstexprMove&&>(cv));
  if(result.n!=9 || cv.n!=9)return 27;
  Outside local=localResult();
  if(local.n!=5 || local.self!=&local)return 29;
  Outside param=parameterResult(static_cast<Outside&&>(local));
  if(param.n!=7 || param.self!=&param || local.n!=-1)return 30;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("user-moves" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2UserMovesRetainSourceLifetimeAndGeneratedBoundaries) {
  const std::vector<std::tuple<std::string, std::string, std::string>> Cases = {
      {"deleted-constructor", "struct R{int n;R(R&&)=delete;};", "TR0201"},
      {"volatile-constructor", "struct R{int n;R(volatile R&&r):n(r.n){}};", "TR0201"},
      {"const-volatile-constructor", "struct R{int n;R(const volatile R&&r):n(r.n){}};", "TR0201"},
      {"constructor-default-argument", "struct R{int n;R(R&&r,int extra=0):n(r.n+extra){}};", "TR0201"},
      {"deleted-assignment", "struct R{int n;R&operator=(R&&)=delete;};", "TR0201"},
      {"volatile-assignment-source", "struct R{int n;R&operator=(volatile R&&r){n=r.n;return *this;}};", "TR0201"},
      {"volatile-assignment-receiver", "struct R{int n;R&operator=(R&&)volatile{return const_cast<R&>(*this);}};", "TR0201"},
      {"attribute", "struct R{int n;[[deprecated]] R(R&&r):n(r.n){}};", "TR0201"},
      {"base", "struct B{int n;};struct R:B{int value;R(R&&r):value(r.value){}};", "TR0201"},
      {"deleted-implicit-copy", "struct R{int n;R(R&&r):n(r.n){}};R f(R&r){return R(r);}", "TR0202"},
      {"lvalue-to-move-only", "struct R{int n;R&operator=(R&&r){n=r.n;return *this;}};void f(R&a,R&b){a=b;}", "TR0202"},
      {"invalid-rvalue-receiver", "struct R{int n;R&operator=(R&&r)&&{n=r.n;return *this;}};void f(R&a,R&b){a=static_cast<R&&>(b);}", "TR0202"},
      {"write-const-source", "struct R{int n;R(const R&&r):n(r.n){r.n=1;}};", "TR0202"},
  };
  for (const auto &[Name, Code, Diagnostic] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("user-moves-reject-" + Name + ".cpp");
    const auto Output = tmpFile("user-moves-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Diagnostic);
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("user-moves-definition.cpp");
  const auto Output = tmpFile("user-moves-definition.nc");
  for (const std::string &Code : {
      "struct R{int n;R(R&&);};",
      "struct R{int n;R&operator=(R&&);};"}) {
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0203");
    expectNoArtifacts(Output);
  }
  writeFile(Source, "struct R{int n;R(R&&r):n(r.n){}};");
  auto Old = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
  expectCode(Old, "TR0201");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2LiveRvalueReferencesPreserveAliasesAndSelectedCalls) {
  const auto Source = tmpFile("live-rvalue.cpp");
  const auto Output = tmpFile("live-rvalue.nc");
  writeFile(Source, R"cpp(
struct Counts { int copies,destroyed; };
struct Record {
  int n;Counts*counts;Record*self;
  Record(int v,Counts&c):n(v),counts(&c),self(this){}
  Record(const Record&s):n(s.n+1),counts(s.counts),self(this){++counts->copies;}
  ~Record(){++counts->destroyed;}
  int category() & {return 1;}
  int category() const & {return 2;}
  int category() && {return 3;}
  int category() const && {return 4;}
  Record&&again() && {return static_cast<Record&&>(*this);}
  int update(int value)&& {n=value;return n;}
};
int kind(int&){return 1;}
int kind(const int&){return 2;}
int kind(int&&){return 3;}
int kind(const int&&){return 4;}
int&&scalar(int&&n){return static_cast<int&&>(n);}
Record&&identity(Record&&r){return static_cast<Record&&>(r);}
Record&&chosen(bool yes,Record&a,Record&b){return yes?static_cast<Record&&>(a):static_cast<Record&&>(b);}
Record&&source(Record&r,int&trace){trace=trace*10+1;return static_cast<Record&&>(r);}
int argument(int&trace){trace=trace*10+2;return 19;}
int take(Record r){return r.n;}
Record copied(Record&&r){return static_cast<Record&&>(r);}
struct Plain { int n;Plain*self; };
struct Holder { Plain p;int values[2]; };
using Row=int[2];
Row&&rowIdentity(Row&&r){return static_cast<Row&&>(r);}
int*&&pointerIdentity(int*&&p){return static_cast<int*&&>(p);}
struct Defaults { int n=4;int read()&& {return n+1;}int next=static_cast<Defaults&&>(*this).read(); };
using RRef=int&&;
using CollapsedL=RRef&;
using CollapsedR=RRef&&;
int main(){
  int n=3;const int constant=4;
  int&&ref=static_cast<int&&>(n);ref=7;
  if(&ref!=&n || n!=7 || kind(ref)!=1 || kind(static_cast<int&&>(ref))!=3)return 1;
  const int&&cref=static_cast<const int&&>(constant);
  if(&cref!=&constant || kind(cref)!=2 || kind(static_cast<const int&&>(cref))!=4)return 2;
  int&&returned=scalar(static_cast<int&&>(n));returned=9;
  if(&returned!=&n || n!=9)return 3;
  CollapsedL l=n;CollapsedR r=static_cast<RRef>(n);l=11;
  if(&l!=&n || &r!=&n || r!=11)return 4;
  int other=2;int*p=&n;
  int*&&pr=pointerIdentity(static_cast<int*&&>(p));pr=&other;
  if(&pr!=&p || p!=&other)return 5;
  Counts counts{0,0};
  {
    Record a(3,counts),b(7,counts);const Record c(11,counts);
    Record&&ar=static_cast<Record&&>(a);
    if(&ar!=&a || ar.category()!=1 || static_cast<Record&&>(ar).category()!=3)return 6;
    if(c.category()!=2 || static_cast<const Record&&>(c).category()!=4)return 7;
    Record&&chain=identity(static_cast<Record&&>(a)).again();
    if(&chain!=&a || chain.self!=&a || counts.copies || counts.destroyed)return 8;
    Record&&yes=chosen(true,a,b);Record&&no=chosen(false,a,b);yes.n=13;no.n=17;
    if(&yes!=&a || &no!=&b || a.n!=13 || b.n!=17)return 9;
    int trace=0;
    if(source(a,trace).update(argument(trace))!=19 || trace!=12 || a.n!=19)return 10;
    trace=0;
    Record&&comma=(++trace,static_cast<Record&&>(b));
    if(&comma!=&b || trace!=1)return 11;
    int&&field=static_cast<Record&&>(a).n;field=21;
    if(&field!=&a.n || a.n!=21)return 12;
    Record copy=static_cast<Record&&>(a);
    if(copy.n!=22 || copy.self!=&copy || counts.copies!=1 || counts.destroyed)return 13;
    if(take(static_cast<Record&&>(a))!=22 || counts.copies!=2 || counts.destroyed!=1)return 14;
    Record result=copied(static_cast<Record&&>(a));
    if(result.n!=22 || result.self!=&result || counts.copies!=3 || counts.destroyed!=1)return 15;
    if(a.n!=21 || a.self!=&a || b.self!=&b)return 16;
  }
  if(counts.copies!=3 || counts.destroyed!=6)return 17;
  Holder holder{{3,nullptr},{5,7}};holder.p.self=&holder.p;
  Plain&&part=static_cast<Holder&&>(holder).p;part.n=11;
  if(&part!=&holder.p || part.self!=&holder.p || holder.p.n!=11)return 18;
  Plain copy=static_cast<Plain&&>(holder.p);
  if(copy.n!=11 || copy.self!=&holder.p)return 19;
  Row&&array=rowIdentity(static_cast<Row&&>(holder.values));array[1]=13;
  if(&array!=&holder.values || holder.values[1]!=13)return 20;
  int&&element=static_cast<Row&&>(holder.values)[0];element=17;
  if(&element!=&holder.values[0] || holder.values[0]!=17)return 21;
  Row grid[2]={{1,2},{3,4}};
  Row&&chosenRow=(n>0?static_cast<Row&&>(grid[0]):static_cast<Row&&>(grid[1]));chosenRow[1]=19;
  if(&chosenRow!=&grid[0] || grid[0][1]!=19 || grid[1][1]!=4)return 22;
  const int&&readOnly=static_cast<const Row&&>(holder.values)[1];
  if(&readOnly!=&holder.values[1] || readOnly!=13)return 23;
  Defaults defaults{};
  if(defaults.n!=4 || defaults.next!=5)return 24;
  Plain receiver{2,nullptr};
  if(&(static_cast<Plain&&>(receiver)=holder.p)!=&receiver || receiver.n!=11 || receiver.self!=&holder.p)return 25;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("live-rvalue" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2LiveRvalueReferencesRetainTemporaryAndMoveBoundaries) {
  const std::vector<std::tuple<std::string, std::string, std::string>> Cases = {
      {"reference-return", "int&&f(){return 1;}", "TR0201"},
      {"volatile-reference", "int f(volatile int&&n){return n;}", "TR0201"},
      {"reference-field", "struct R{int&&n;};", "TR0201"},
      {"global-reference", "int n;int&&r=static_cast<int&&>(n);", "TR0201"},
      {"function-reference", "int f(){return 1;}using Fn=int();Fn&&g(){return static_cast<Fn&&>(f);}", "TR0201"},
      {"direct-lvalue-binding", "void f(){int n=1;int&&r=n;}", "TR0202"},
      {"lvalue-method-on-xvalue", "struct R{int n;int get()&{return n;}};int f(R&r){return static_cast<R&&>(r).get();}", "TR0202"},
      {"rvalue-method-on-lvalue", "struct R{int n;int get()&&{return n;}};int f(R&r){return r.get();}", "TR0202"},
      {"const-mutation", "void f(const int&&n){n=1;}", "TR0202"},
  };
  for (const auto &[Name, Code, Diagnostic] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("rvalue-reject-" + Name + ".cpp");
    const auto Output = tmpFile("rvalue-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Diagnostic);
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("rvalue-v1.cpp");
  const auto Output = tmpFile("rvalue-v1.nc");
  writeFile(Source, "int f(int&&r){return r;}");
  auto Old = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
  expectCode(Old, "TR0201");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2DefaultMembersPreserveDestinationsOverridesAndCleanup) {
  const auto Source = tmpFile("default-members.cpp");
  const auto Output = tmpFile("default-members.nc");
  writeFile(Source, R"cpp(
struct Plain {
  int n=3;
  int next=n+4;
  Plain*self=this;
  int values[2][2]={{n,next},{next+1,n+9}};
};
struct Defaulted { int n=7;Defaulted*self=this;Defaulted()=default; };
struct Log { int used;int events[64]; };
int mark(Log&log,int n){log.events[log.used++]=n;return n;}
struct Counted {
  Log*log;
  int first=mark(*log,1);
  int second=first+mark(*log,2);
  Counted*self=this;
};
struct Constructed {
  Log*log;
  int first=mark(*log,1);
  int second=first+mark(*log,2);
  Constructed*self=this;
  Constructed(Log&l):log(&l){}
  Constructed(Log&l,int n):second(n),log(&l),first(n+1){}
};
struct UserCopy {
  Log*log;
  int n=mark(*log,4);
  UserCopy*self=this;
  UserCopy(Log&l):log(&l){}
  UserCopy(const UserCopy&s):log(s.log){}
};
struct Outer;
struct Inner { Outer*outer;Inner*self=this;int n=6; };
struct Outer {
  int n=5;
  Inner inner={this};
  Outer*last=this;
  int after=inner.n+n;
};
struct Caller;
struct Target { Caller*caller;Target*self=this;int n=9; };
struct Caller { int n;Target make(){return {this};} };
struct MemberCall { int n=8;int read(){return n+1;}int next=read(); };
struct Leaf {
  int n=7;
  Leaf*self=this;
  Leaf()=default;
  Leaf(int x):n(x){}
};
struct Nest { Leaf first=Leaf(3);Leaf items[2][2]={{Leaf(4)},{}}; };
struct Temp {
  Log*log;int n;
  Temp(Log&l,int value):log(&l),n(value){}
  ~Temp(){mark(*log,n+10);}
};
struct AggregateCleanup {
  Log*log;
  int first=(Temp(*log,3),mark(*log,4));
  int second=mark(*log,5);
};
struct ConstructorCleanup {
  Log*log;
  int first=(Temp(*log,3),mark(*log,4));
  int second=mark(*log,5);
  ConstructorCleanup(Log&l):log(&l){}
};
struct Lazy { int n=11;Lazy*self=this;Lazy()=default; };
int query(){return sizeof(Lazy{});}
int main(){
  Plain p;
  if(p.n!=3 || p.next!=7 || p.self!=&p || p.values[1][1]!=12)return 1;
  Plain value{};
  if(value.n!=3 || value.next!=7 || value.self!=&value)return 2;
  const Plain constant{};
  if(constant.n!=3 || constant.self!=&constant)return 3;
  Plain override{20,21};
  if(override.n!=20 || override.next!=21 || override.self!=&override || override.values[0][0]!=20 || override.values[1][0]!=22)return 4;
  Plain partial[3]={{},{8}};
  if(partial[0].next!=7 || partial[1].next!=12 || partial[2].next!=7)return 5;
  for(int i=0;i<3;++i)if(partial[i].self!=&partial[i])return 6;
  Plain copied=p;
  if(copied.self!=&p || copied.next!=7)return 7;
  copied=override;
  if(copied.self!=&override || copied.next!=21)return 8;
  Defaulted first;Defaulted second{};
  if(first.n!=7 || first.self!=&first || second.n!=7 || second.self!=&second)return 9;
  Log log{0,{}};
  Counted counted{&log};
  if(log.used!=2 || log.events[0]!=1 || log.events[1]!=2 || counted.first!=1 || counted.second!=3 || counted.self!=&counted)return 10;
  Counted copy=counted;
  if(log.used!=2 || copy.self!=&counted)return 11;
  copy=counted;
  if(log.used!=2 || copy.self!=&counted)return 12;
  log.used=0;
  Counted oneOverride{&log,9};
  if(log.used!=1 || log.events[0]!=2 || oneOverride.second!=11 || oneOverride.self!=&oneOverride)return 13;
  log.used=0;
  Counted allOverride{&log,9,10,nullptr};
  if(log.used || allOverride.self!=nullptr)return 14;
  Constructed constructed(log);
  if(log.used!=2 || log.events[0]!=1 || log.events[1]!=2 || constructed.second!=3 || constructed.self!=&constructed)return 15;
  log.used=0;
  Constructed explicitMembers(log,9);
  if(log.used || explicitMembers.first!=10 || explicitMembers.second!=9 || explicitMembers.self!=&explicitMembers)return 16;
  UserCopy original(log);UserCopy userCopy=original;
  if(log.used!=2 || log.events[0]!=4 || log.events[1]!=4 || original.self!=&original || userCopy.self!=&userCopy)return 17;
  Outer outer{};
  if(outer.inner.outer!=&outer || outer.inner.self!=&outer.inner || outer.last!=&outer || outer.after!=11)return 18;
  Outer outerDefault;
  if(outerDefault.inner.outer!=&outerDefault || outerDefault.inner.self!=&outerDefault.inner || outerDefault.last!=&outerDefault)return 19;
  Caller caller{1};Target target=caller.make();
  if(target.caller!=&caller || target.self!=&target || target.n!=9)return 20;
  MemberCall member{};
  if(member.n!=8 || member.next!=9)return 21;
  Nest nest{};
  if(nest.first.n!=3 || nest.first.self!=&nest.first || nest.items[0][0].n!=4 || nest.items[0][1].n!=7 || nest.items[1][1].n!=7)return 22;
  for(int i=0;i<2;++i)for(int j=0;j<2;++j)if(nest.items[i][j].self!=&nest.items[i][j])return 23;
  log.used=0;
  AggregateCleanup aggregate{&log};
  if(log.used!=3 || log.events[0]!=4 || log.events[1]!=5 || log.events[2]!=13 || aggregate.first!=4 || aggregate.second!=5)return 24;
  log.used=0;
  ConstructorCleanup constructor(log);
  if(log.used!=3 || log.events[0]!=4 || log.events[1]!=13 || log.events[2]!=5 || constructor.first!=4 || constructor.second!=5)return 25;
  log.used=0;
  AggregateCleanup skip{&log,8,9};
  if(log.used || skip.first!=8 || skip.second!=9 || query()!=sizeof(Lazy))return 26;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("default-members" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DefaultMembersCheckWrittenAndSelectedExpressions) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"const-field", "struct R{const int n=1;};"},
      {"reference-field", "struct R{int value;int&ref=value;};"},
      {"mutable-field", "struct R{mutable int n=1;};"},
      {"private-field", "class R{int n=1;};"},
      {"bitfield", "struct R{int bits:2;int n=1;};"},
      {"static-member", "struct R{static int x;int n=1;};int R::x=0;"},
      {"base", "struct B{int n=1;};struct R:B{int next=2;};"},
      {"attribute", "struct R{[[maybe_unused]] int n=1;};"},
      {"floating-default", "struct R{int n=static_cast<int>(1.5);};"},
      {"lambda-unused", "struct R{int n=[](){return 1;}();};"},
      {"lambda-overridden", "struct R{int n=[](){return 1;}();};void f(){R r{7};}"},
      {"throw-unused", "struct R{int n=(throw 1,2);};"},
      {"throw-overridden", "struct R{int n=(throw 1,2);R():n(7){}};"},
      {"new-default", "struct R{int*p=new int(1);};"},
      {"reinterpret-default", "struct R{int*p=reinterpret_cast<int*>(1);};"},
      {"template-default", "template<class T>struct R{T n=1;};"},
      {"excessive-array", "struct R{int n[65537]={1};};"},
      {"address-of-member", "struct R{int n=1;int R::*p=&R::n;};"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("default-member-reject-" + Name + ".cpp");
    const auto Output = tmpFile("default-member-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("default-member-boundary.cpp");
  const auto Output = tmpFile("default-member-boundary.nc");
  writeFile(Source, "int missing();struct R{int n=missing();};void f(){R r{7};}");
  auto Missing = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  expectCode(Missing, "TR0203");
  expectNoArtifacts(Output);
  writeFile(Source, "struct R{int n=1;};");
  auto Old = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
  expectCode(Old, "TR0201");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2GeneratedAssignmentPreservesMemberEffectsAndSequencing) {
  const auto Source = tmpFile("generated-assignment.cpp");
  const auto Output = tmpFile("generated-assignment.nc");
  writeFile(Source, R"cpp(
struct Stats { int assigned,copied,destroyed,used,trace[64]; };
struct Leaf {
  int value;Stats*stats;Leaf*self;Leaf*alias;
  Leaf(int n,Stats&s):value(n),stats(&s),self(this),alias(this){}
  Leaf(const Leaf&s):value(s.value),stats(s.stats),self(this),alias(this){++stats->copied;}
  Leaf&operator=(const Leaf&s){
    ++stats->assigned;stats->trace[stats->used++]=s.value;
    value=s.value+10;self=this;return *alias;
  }
  ~Leaf(){++stats->destroyed;}
};
struct Inner { Leaf items[2];~Inner()=default; };
struct Box {
  int before;Leaf first;int numbers[2];Inner inner;Leaf grid[2][2];int after;
  Box&operator=(const Box&)=default;
  ~Box()=default;
};
Box make(Stats&s,int n){
  return {n,Leaf(n,s),{n+1,n+2},{{Leaf(n+1,s),Leaf(n+2,s)}},
          {{Leaf(n+3,s),Leaf(n+4,s)},{Leaf(n+5,s),Leaf(n+6,s)}},n+7};
}
bool own(const Box&b){
  if(b.first.self!=&b.first)return false;
  for(int i=0;i<2;++i)if(b.inner.items[i].self!=&b.inner.items[i])return false;
  for(int i=0;i<2;++i)for(int j=0;j<2;++j)if(b.grid[i][j].self!=&b.grid[i][j])return false;
  return true;
}
Box&sourceEffect(Box&source,int&trace,int&count){trace=trace*10+2;++count;return source;}
Box&receiverEffect(Box&target,Box&source,int&trace,int&count){
  trace=trace*10+1;++count;source.first.value=77;return target;
}
Box&reseatReceiver(Box&target,Box*&pointer,Box&other){pointer=&other;return target;}
struct Plain {
  int values[2];Plain*self;Stats*stats;
  Plain(int n,Stats&s):values{n,n+1},self(this),stats(&s){}
  Plain(const Plain&s):values{s.values[0],s.values[1]},self(this),stats(s.stats){++stats->copied;}
  ~Plain(){++stats->destroyed;}
};
struct WithPlain { Plain grid[2][2];Leaf trigger;WithPlain&operator=(const WithPlain&)=default;~WithPlain()=default; };
struct Simple { int values[2];Simple*self;Simple&operator=(const Simple&)=default; };
Simple&simpleReceiver(Simple&target,Simple&source,int&trace){trace=trace*10+1;source.values[0]=77;return target;}
const Simple&simpleSource(const Simple&source,int&trace){trace=trace*10+2;return source;}
Simple&simpleReseat(Simple&target,Simple*&pointer,Simple&other){pointer=&other;return target;}
struct Outside { Leaf leaf;Outside&operator=(const Outside&);~Outside()=default; };
Outside&Outside::operator=(const Outside&)=default;
struct Qualified { Leaf leaf;Qualified&operator=(const Qualified&) & =default;~Qualified()=default; };
struct MutableLeaf {
  int value;Stats*stats;MutableLeaf*self;
  MutableLeaf(int n,Stats&s):value(n),stats(&s),self(this){}
  MutableLeaf&operator=(MutableLeaf&s){value=++s.value;self=this;++stats->assigned;return *this;}
  ~MutableLeaf(){++stats->destroyed;}
};
struct MutableBox { MutableLeaf items[2];~MutableBox()=default; };
struct Unused { int n;Unused&operator=(const Unused&)=default; };
struct Lazy { Leaf leaf;Lazy&operator=(const Lazy&)=default; };
int query(Lazy&target,const Lazy&source){return sizeof(target=source);}
int main(){
  Stats stats{0,0,0,0,{}};
  {
    Leaf unrelated(99,stats);
    Box source=make(stats,10),target=make(stats,30),third=make(stats,50);
    target.first.alias=&unrelated;
    if(&(target=source)!=&target || stats.assigned!=7 || stats.used!=7)return 1;
    if(!own(target) || target.first.value!=20 || target.grid[1][1].value!=26 ||
       target.before!=10 || target.numbers[0]!=11 || target.numbers[1]!=12 || target.after!=17)return 2;
    if(source.first.value!=10 || unrelated.value!=99 || target.first.alias!=&unrelated)return 3;
    for(int i=0;i<7;++i)if(stats.trace[i]!=10+i)return 4;
    stats.used=0;
    if(&(target=target)!=&target || target.first.value!=30 || target.grid[1][1].value!=36 || stats.assigned!=14)return 5;
    for(int i=0;i<7;++i)if(stats.trace[i]!=20+i)return 6;
    stats.used=0;
    if(&(third=target=source)!=&third || third.first.value!=30 || target.first.value!=20 || stats.assigned!=28)return 7;
    for(int i=0;i<7;++i)if(stats.trace[i]!=10+i || stats.trace[i+7]!=20+i)return 8;
    stats.used=0;
    if(&target.operator=(source)!=&target || stats.assigned!=35)return 9;
    Box*pointer=&target;
    if(&pointer->operator=(source)!=&target || stats.assigned!=42)return 10;
    int trace=0,left=0,right=0;stats.used=0;
    receiverEffect(target,source,trace,left)=sourceEffect(source,trace,right);
    if(trace!=21 || left!=1 || right!=1 || target.first.value!=87 || stats.assigned!=49)return 11;
    trace=left=right=0;source.first.value=10;stats.used=0;
    receiverEffect(target,source,trace,left).operator=(sourceEffect(source,trace,right));
    if(trace!=12 || left!=1 || right!=1 || target.first.value!=87 || stats.assigned!=56)return 12;
    pointer=&source;stats.used=0;
    reseatReceiver(target,pointer,third)=*pointer;
    if(pointer!=&third || target.first.value!=87 || stats.assigned!=63 || !own(target))return 13;
    if(stats.copied || stats.destroyed)return 14;
  }
  if(stats.destroyed!=22)return 15;
  {
    stats.used=0;
    WithPlain source{{{Plain(1,stats),Plain(3,stats)},{Plain(5,stats),Plain(7,stats)}},Leaf(9,stats)};
    WithPlain target{{{Plain(11,stats),Plain(13,stats)},{Plain(15,stats),Plain(17,stats)}},Leaf(19,stats)};
    if(&(target=source)!=&target || target.trigger.value!=19 || stats.assigned!=64)return 16;
    for(int i=0;i<2;++i)for(int j=0;j<2;++j)
      if(target.grid[i][j].self!=&source.grid[i][j] ||
         target.grid[i][j].values[0]!=source.grid[i][j].values[0] ||
         target.grid[i][j].values[1]!=source.grid[i][j].values[1])return 17;
    target=target;
    if(target.grid[1][1].self!=&source.grid[1][1] || target.trigger.value!=29 || stats.assigned!=65)return 18;
    if(stats.copied || stats.destroyed!=22)return 19;
    Simple a{{1,2},nullptr};a.self=&a;Simple b{{3,4},nullptr};
    if(&(b=a)!=&b || b.values[0]!=1 || b.values[1]!=2 || b.self!=&a)return 20;
    if(&b.operator=(a)!=&b || b.self!=&a)return 21;
    Simple*pointer=&b;
    if(&pointer->operator=(a)!=&b || b.self!=&a)return 22;
    int trace=0;
    simpleReceiver(b,a,trace)=simpleSource(a,trace);
    if(trace!=21 || b.values[0]!=77 || b.self!=&a)return 23;
    trace=0;a.values[0]=1;
    simpleReceiver(b,a,trace).operator=(simpleSource(a,trace));
    if(trace!=12 || b.values[0]!=77 || b.self!=&a)return 24;
    Simple other{{9,10},nullptr};pointer=&a;
    simpleReseat(b,pointer,other)=*pointer;
    if(pointer!=&other || b.values[0]!=77 || b.self!=&a)return 25;
  }
  if(stats.destroyed!=32)return 26;
  {
    stats.used=0;
    Outside source{Leaf(3,stats)},target{Leaf(5,stats)};
    if(&(target=source)!=&target || target.leaf.value!=13 || stats.assigned!=66)return 27;
    Qualified a{Leaf(7,stats)},b{Leaf(9,stats)};
    if(&(b=a)!=&b || b.leaf.value!=17 || stats.assigned!=67)return 28;
    MutableBox mutable_source{{MutableLeaf(1,stats),MutableLeaf(3,stats)}};
    MutableBox mutable_target{{MutableLeaf(5,stats),MutableLeaf(7,stats)}};
    if(&(mutable_target=mutable_source)!=&mutable_target || stats.assigned!=69)return 29;
    if(mutable_source.items[0].value!=2 || mutable_source.items[1].value!=4 ||
       mutable_target.items[0].value!=2 || mutable_target.items[1].value!=4 ||
       mutable_target.items[0].self!=&mutable_target.items[0] || stats.copied)return 30;
  }
  if(stats.destroyed!=40)return 31;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("generated-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2GeneratedAssignmentKeepsBuiltinAndReferenceBoundaries) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"deleted", "struct R{int n;R&operator=(const R&)=delete;};"},
      {"defaulted-deleted", "struct I{int n;I&operator=(const I&)=delete;};struct R{I i;R&operator=(const R&)=default;};"},
      {"rvalue-receiver", "struct R{int n;R&operator=(const R&)&&=default;};"},
      {"const-field", "struct R{const int n;R&operator=(const R&)=default;};"},
      {"reference-field", "struct R{int&n;R&operator=(const R&)=default;};"},
      {"private-field", "class R{int n;public:R&operator=(const R&)=default;};"},
      {"base-field", "struct B{int n;};struct R:B{int m;R&operator=(const R&)=default;};"},
      {"raw-builtin", "void f(int*a,int*b){__builtin_memcpy(a,b,4);}"},
      {"dead-builtin", "void f(int*a,int*b){if(false)__builtin_memcpy(a,b,4);}"},
      {"user-member-builtin", "struct R{int n[2];R&operator=(const R&s){__builtin_memcpy(n,s.n,sizeof(n));return *this;}};"},
      {"array-expansion", "struct I{int n;I&operator=(const I&s){n=s.n;return *this;}};struct R{int n[65536];I i;R&operator=(const R&)=default;};void f(R&a,const R&b){a=b;}"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("generated-assignment-reject-" + Name + ".cpp");
    const auto Output = tmpFile("generated-assignment-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("generated-assignment-boundary.cpp");
  const auto Output = tmpFile("generated-assignment-boundary.nc");
  const std::vector<std::pair<std::string, std::string>> InvalidCases = {
      {"volatile-source", "struct R{int n;R&operator=(const volatile R&)=default;};"},
      {"volatile-receiver", "struct R{int n;R&operator=(const R&)volatile=default;};"},
      {"const-receiver", "struct R{int n;R&operator=(const R&)const=default;};"},
      {"value-parameter", "struct R{int n;R&operator=(R)=default;};"},
      {"value-result", "struct R{int n;R operator=(const R&)=default;};"},
      {"const-result", "struct R{int n;const R&operator=(const R&)=default;};"},
  };
  for (const auto &[Name, Code] : InvalidCases) {
    SCOPED_TRACE(Name);
    writeFile(Source, Code);
    auto Invalid = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Invalid, "TR0202");
    expectNoArtifacts(Output);
  }
  writeFile(Source, "struct I{int n;I&operator=(const I&);};struct R{I i;R&operator=(const R&)=default;};void f(R&a,const R&b){a=b;}");
  auto Missing = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  expectCode(Missing, "TR0203");
  expectNoArtifacts(Output);
  writeFile(Source, "struct R{int n;R&operator=(const R&)=default;};");
  auto Old = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
  expectCode(Old, "TR0201");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2GeneratedCopyPreservesMembersAndObjectIdentity) {
  const auto Source = tmpFile("generated-copy.cpp");
  const auto Output = tmpFile("generated-copy.nc");
  writeFile(Source, R"cpp(
struct Stats { int copies,destroyed,trace[64],used; };
struct Leaf {
  int value;
  Stats *stats;
  Leaf *self;
  Leaf(int n,Stats &s):value(n),stats(&s),self(this){}
  Leaf(const Leaf &source):value(source.value+1),stats(source.stats),self(this){
    ++stats->copies;stats->trace[stats->used++]=source.value;
  }
  ~Leaf(){++stats->destroyed;}
};
struct Mixed {
  int before;
  Leaf first;
  int numbers[3];
  Leaf grid[2][2];
  int after;
  ~Mixed()=default;
};
struct Defaulted {
  int plain;
  Leaf first,items[2];
  Defaulted(const Defaulted&)=default;
  ~Defaulted()=default;
};
struct Outside {
  Leaf leaf;
  Outside(int n,Stats &s):leaf(n,s){}
  Outside(const Outside&);
  ~Outside()=default;
};
Outside::Outside(const Outside&)=default;
struct ExplicitCopy {
  Leaf leaf;
  ExplicitCopy(int n,Stats &s):leaf(n,s){}
  explicit ExplicitCopy(const ExplicitCopy&)=default;
  ~ExplicitCopy()=default;
};
struct Trivial {
  int values[2];Trivial *self;
  Trivial(const Trivial&)=default;
};
struct ArrayTrivial { Trivial items[2];Leaf leaf;~ArrayTrivial()=default; };
struct MutableLeaf {
  int value;Stats *stats;MutableLeaf *self;
  MutableLeaf(int n,Stats &s):value(n),stats(&s),self(this){}
  MutableLeaf(MutableLeaf &source):value(++source.value),stats(source.stats),self(this){++stats->copies;}
  ~MutableLeaf(){++stats->destroyed;}
};
struct MutableBox { MutableLeaf items[2];~MutableBox()=default; };
struct Unused { int n;Unused(const Unused&)=default; };
struct OnlySize { Leaf leaf;OnlySize(const OnlySize&)=default; };
int query(const OnlySize &source){return sizeof(OnlySize(source));}
bool own(const Mixed &value){
  if(value.first.self!=&value.first)return false;
  for(int i=0;i<2;++i)for(int j=0;j<2;++j)
    if(value.grid[i][j].self!=&value.grid[i][j])return false;
  return true;
}
Mixed make(Stats &stats){
  return {1,Leaf(2,stats),{3,4,5},{{Leaf(6,stats),Leaf(7,stats)},
         {Leaf(8,stats),Leaf(9,stats)}},10};
}
Mixed forward(Stats &stats){return make(stats);}
Mixed fromSource(const Mixed &source){return source;}
Mixed named(Stats &stats){Mixed value=make(stats);return value;}
Mixed identity(Mixed value){return value;}
int byValue(Mixed value,const Mixed &source){return own(value) && &value!=&source && value.first.value==source.first.value+1;}
int main(){
  Stats stats{0,0,{},0};
  {
    Mixed source=make(stats);
    if(!own(source) || stats.copies)return 1;
    Mixed copied=source;
    if(!own(copied) || copied.before!=1 || copied.after!=10 || copied.first.value!=3 ||
       copied.numbers[0]!=3 || copied.numbers[1]!=4 || copied.numbers[2]!=5)return 2;
    const int expected[5]={2,6,7,8,9};
    if(stats.copies!=5 || stats.used!=5)return 3;
    for(int i=0;i<5;++i)if(stats.trace[i]!=expected[i])return 4;
    if(copied.grid[0][0].value!=7 || copied.grid[0][1].value!=8 ||
       copied.grid[1][0].value!=9 || copied.grid[1][1].value!=10 ||
       source.first.value!=2 || source.grid[1][1].value!=9)return 5;
    stats.used=0;
    Mixed second=copied;
    if(!own(second) || second.first.value!=4 || second.grid[1][1].value!=11 || stats.copies!=10)return 6;
    for(int i=0;i<5;++i)if(stats.trace[i]!=expected[i]+1)return 7;
    stats.used=0;
    if(!byValue(source,source) || stats.copies!=15 || stats.destroyed!=5)return 8;
    Mixed result=forward(stats);
    if(!own(result) || result.first.value!=2 || stats.copies!=15)return 9;
    stats.used=0;
    Mixed returned=fromSource(source);
    if(!own(returned) || returned.first.value!=3 || stats.copies!=20)return 10;
    stats.used=0;
    Mixed named_result=named(stats);
    if(!own(named_result) || named_result.first.value!=3 || stats.copies!=25 || stats.destroyed!=10)return 11;
    stats.used=0;
    Mixed parameter_result=identity(source);
    if(!own(parameter_result) || parameter_result.first.value!=4 ||
       stats.copies!=35 || stats.destroyed!=15)return 12;
  }
  if(stats.destroyed!=50)return 13;
  {
    stats.used=0;
    Defaulted source{11,Leaf(12,stats),{Leaf(13,stats),Leaf(14,stats)}};
    Defaulted copied(source);
    if(copied.plain!=11 || copied.first.value!=13 || copied.items[0].value!=14 ||
       copied.items[1].value!=15 || copied.first.self!=&copied.first ||
       copied.items[1].self!=&copied.items[1] || stats.copies!=38)return 14;
    if(stats.used!=3 || stats.trace[0]!=12 || stats.trace[1]!=13 || stats.trace[2]!=14)return 15;
    Outside outside(3,stats);Outside outside_copy=outside;
    if(outside_copy.leaf.value!=4 || outside_copy.leaf.self!=&outside_copy.leaf || stats.copies!=39)return 16;
    ExplicitCopy explicit_source(5,stats);ExplicitCopy explicit_copy(explicit_source);
    if(explicit_copy.leaf.value!=6 || explicit_copy.leaf.self!=&explicit_copy.leaf || stats.copies!=40)return 17;
    Trivial trivial{{1,2},nullptr};trivial.self=&trivial;
    Trivial trivial_copy(trivial);
    if(trivial_copy.values[0]!=1 || trivial_copy.values[1]!=2 || trivial_copy.self!=&trivial)return 18;
    ArrayTrivial array{{{{1,2},nullptr},{{3,4},nullptr}},Leaf(9,stats)};
    for(int i=0;i<2;++i)array.items[i].self=&array.items[i];
    ArrayTrivial array_copy=array;
    if(array_copy.items[0].self!=&array.items[0] || array_copy.items[1].self!=&array.items[1] ||
       array_copy.items[1].values[1]!=4 || array_copy.leaf.value!=10 ||
       array_copy.leaf.self!=&array_copy.leaf || stats.copies!=41)return 19;
    MutableBox mutable_source{{MutableLeaf(1,stats),MutableLeaf(2,stats)}};
    MutableBox mutable_copy=mutable_source;
    if(mutable_source.items[0].value!=2 || mutable_source.items[1].value!=3 ||
       mutable_copy.items[0].value!=2 || mutable_copy.items[1].value!=3 ||
       mutable_copy.items[0].self!=&mutable_copy.items[0] || stats.copies!=43)return 20;
  }
  if(stats.destroyed!=66)return 21;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("generated-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2GeneratedCopyKeepsAssignmentAndLifetimeBoundaries) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"deleted-copy", "struct R{int n;R(const R&)=delete;};"},
      {"defaulted-deleted-copy", "struct I{int n;I(const I&)=delete;};struct R{I i;R(const R&)=default;};"},
      {"reference-field", "struct R{int &n;R(const R&)=default;};"},
      {"const-field", "struct R{const int n;R(const R&)=default;};"},
      {"nonpublic-field", "class R{int n;public:R(const R&)=default;};"},
      {"base-copy", "struct B{int n;};struct R:B{int m;R(const R&)=default;};"},
      {"lambda-array-copy", "int f(){int values[2]={1,2};auto capture=[values](){return values[0];};return capture();}"},
      {"decomposed-array-copy", "int f(){int values[2]={1,2};auto [a,b]=values;return a+b;}"},
      {"copy-expansion", "struct I{int n;I(const I&s):n(s.n){}};struct R{I items[65536];~R()=default;};R f(const R&s){return s;}"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("generated-copy-reject-" + Name + ".cpp");
    const auto Output = tmpFile("generated-copy-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("generated-copy-boundary.cpp");
  const auto Output = tmpFile("generated-copy-boundary.nc");
  writeFile(Source, "struct R{int n;R(const volatile R&)=default;};");
  auto Invalid = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  expectCode(Invalid, "TR0202");
  expectNoArtifacts(Output);
  writeFile(Source, "struct I{int n;I(const I&);};struct R{I i;R(const R&)=default;};R f(const R&s){return s;}");
  auto Missing = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  expectCode(Missing, "TR0203");
  expectNoArtifacts(Output);
  writeFile(Source, "struct R{int n;R(const R&)=default;};");
  auto Old = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
  expectCode(Old, "TR0201");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2DefaultedLifecyclePreservesInitializationAndCleanup) {
  const auto Source = tmpFile("defaulted-lifecycle.cpp");
  const auto Output = tmpFile("defaulted-lifecycle.nc");
  writeFile(Source, R"cpp(
struct Trace { int values[16]; int used; };
struct Leaf {
  int value;
  Leaf *self;
  Trace *trace;
  Leaf():value(7),self(this),trace(nullptr){}
  ~Leaf(){if(trace)trace->values[trace->used++]=value;}
};
struct Implicit { int plain; Leaf leaf; };
struct InClass { int plain; Leaf leaf; InClass()=default; ~InClass()=default; };
struct Explicit { int plain; Leaf leaf; explicit Explicit()=default; };
struct OutOfLine { int plain; Leaf leaf; OutOfLine(); ~OutOfLine(); };
OutOfLine::OutOfLine()=default;
OutOfLine::~OutOfLine()=default;
struct Nested { Implicit first; InClass second[2]; };
struct Trivial { int value; Trivial()=default; ~Trivial()=default; };
struct ExplicitTrivial { int value; explicit ExplicitTrivial()=default; };
struct Unused { int value; Unused()=default; ~Unused()=default; };
struct OnlySize { Leaf leaf; explicit OnlySize()=default; };
struct Later { Leaf leaf; Later(); ~Later(); };
Later makeLater(){return Later();}
Later::Later()=default;
Later::~Later()=default;
void returnCleanup(Trace &trace){InClass value;value.leaf.trace=&trace;value.leaf.value=9;return;}
int main(){
  Trace trace{{},0};
  {Implicit value;
   if(value.leaf.value!=7 || value.leaf.self!=&value.leaf || value.leaf.trace)return 1;
   value.plain=3;value.leaf.trace=&trace;value.leaf.value=1;}
  if(trace.used!=1 || trace.values[0]!=1)return 2;
  {Implicit zero=Implicit();
   if(zero.plain!=0 || zero.leaf.value!=7 || zero.leaf.self!=&zero.leaf)return 3;}
  {InClass value;value.plain=4;
   if(value.leaf.value!=7 || value.leaf.self!=&value.leaf)return 4;
   value.leaf.trace=&trace;value.leaf.value=2;}
  if(trace.used!=2 || trace.values[1]!=2)return 5;
  {InClass zero=InClass();
   if(zero.plain!=0 || zero.leaf.value!=7 || zero.leaf.self!=&zero.leaf)return 6;}
  {Explicit zero{};
   if(zero.plain!=0 || zero.leaf.value!=7 || zero.leaf.self!=&zero.leaf)return 7;}
  {OutOfLine value;value.plain=5;
   if(value.leaf.value!=7 || value.leaf.self!=&value.leaf)return 8;
   value.leaf.trace=&trace;value.leaf.value=3;}
  if(trace.used!=3 || trace.values[2]!=3)return 9;
  trace.used=0;
  {Implicit values[3];
   for(int i=0;i<3;++i){
     if(values[i].leaf.value!=7 || values[i].leaf.self!=&values[i].leaf)return 10;
     values[i].leaf.trace=&trace;values[i].leaf.value=i+1;
   }}
  if(trace.used!=3 || trace.values[0]!=3 || trace.values[1]!=2 || trace.values[2]!=1)return 11;
  trace.used=0;
  {Nested value;
   if(value.first.leaf.self!=&value.first.leaf ||
      value.second[0].leaf.self!=&value.second[0].leaf ||
      value.second[1].leaf.self!=&value.second[1].leaf)return 12;
   value.first.leaf.trace=&trace;value.first.leaf.value=1;
   for(int i=0;i<2;++i){value.second[i].leaf.trace=&trace;value.second[i].leaf.value=i+2;}}
  if(trace.used!=3 || trace.values[0]!=3 || trace.values[1]!=2 || trace.values[2]!=1)return 13;
  {Implicit matrix[2][2]{};
   for(int i=0;i<2;++i)for(int j=0;j<2;++j)
     if(matrix[i][j].plain!=0 || matrix[i][j].leaf.value!=7 ||
        matrix[i][j].leaf.self!=&matrix[i][j].leaf)return 14;}
  Trivial trivial=Trivial();ExplicitTrivial explicit_trivial{};
  if(trivial.value!=0 || explicit_trivial.value!=0)return 15;
  ExplicitTrivial uninitialized;uninitialized.value=11;
  if(uninitialized.value!=11)return 16;
  trace.used=0;returnCleanup(trace);
  if(trace.used!=1 || trace.values[0]!=9)return 17;
  trace.used=0;
  for(int i=0;i<3;++i){
    OutOfLine value;value.leaf.trace=&trace;value.leaf.value=i;
    if(i==1)break;
  }
  if(trace.used!=2 || trace.values[0]!=0 || trace.values[1]!=1)return 18;
  {Later value=makeLater();
   if(value.leaf.value!=7 || value.leaf.self!=&value.leaf)return 19;}
  if(sizeof(OnlySize{})!=sizeof(OnlySize))return 20;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("defaulted-lifecycle" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DefaultedLifecycleKeepsSourceAndCopyBoundaries) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"deleted-constructor", "struct R{int n;R()=delete;};"},
      {"deleted-destructor", "struct R{int n;~R()=delete;};"},
      {"defaulted-deleted-constructor", "struct I{int n;I()=delete;};struct R{I i;R()=default;};"},
      {"defaulted-deleted-destructor", "struct I{int n;~I()=delete;};struct R{I i;~R()=default;};"},
      {"nonpublic-field", "class R{int n;public:R()=default;};"},
      {"virtual-destructor", "struct R{int n;virtual ~R()=default;};"},
      {"explicit-destruction", "struct R{int n;~R()=default;};void f(){R r{1};r.~R();}"},
      {"throwing-member-constructor", "struct I{int n;I(){throw 1;}};struct R{I i;R()=default;};"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("defaulted-lifecycle-reject-" + Name + ".cpp");
    const auto Output = tmpFile("defaulted-lifecycle-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("defaulted-lifecycle-boundary.cpp");
  const auto Output = tmpFile("defaulted-lifecycle-boundary.nc");
  for (const std::string &Code : {
      "struct I{int n;I();};struct R{I i;R()=default;};void f(){R r;}",
      "struct I{int n;~I();};struct R{I i;~R()=default;};void f(){R r{{1}};}"}) {
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0203");
    expectNoArtifacts(Output);
  }
  for (const std::string &Code : {
      "struct R{int n;explicit R()=default;};",
      "struct R{int n;~R()=default;};"}) {
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2ConstructorConversionsKeepOneDestinationAndCleanup) {
  const auto Source = tmpFile("constructor-conversion-cleanup.cpp");
  const auto Output = tmpFile("constructor-conversion-cleanup.nc");
  writeFile(Source, R"cpp(
struct State { int made, destroyed, bad; };
struct R {
  State *state;
  R *self;
  int id;
  explicit R(State *s):state(s),self(this),id(++s->made){}
  ~R(){if(self!=this)++state->bad;++state->destroyed;}
};
struct Holder { R value; };
R make(State *s){return static_cast<R>(s);}
int take(R value){return value.self==&value?value.id:-1;}
int main(){
  State state{0,0,0};
  {R value=static_cast<R>(&state);
   if(value.self!=&value || state.made!=1 || state.destroyed!=0)return 1;}
  if(state.destroyed!=1 || state.bad)return 2;
  {R value=(R)&state;
   if(value.self!=&value || state.made!=2 || state.destroyed!=1)return 3;}
  if(state.destroyed!=2 || state.bad)return 4;
  static_cast<R>(&state);
  if(state.made!=3 || state.destroyed!=3 || state.bad)return 5;
  (R)&state;
  if(state.made!=4 || state.destroyed!=4 || state.bad)return 6;
  if(take(static_cast<R>(&state))!=5 || state.destroyed!=5 || state.bad)return 7;
  if(take((R)&state)!=6 || state.destroyed!=6 || state.bad)return 8;
  {R value=make(&state);
   if(value.self!=&value || state.made!=7 || state.destroyed!=6)return 9;}
  if(state.destroyed!=7 || state.bad)return 10;
  {R values[2]={static_cast<R>(&state),(R)&state};
   if(values[0].self!=&values[0] || values[1].self!=&values[1] ||
      state.made!=9 || state.destroyed!=7)return 11;}
  if(state.destroyed!=9 || state.bad)return 12;
  {Holder holder{static_cast<R>(&state)};
   if(holder.value.self!=&holder.value || state.made!=10 || state.destroyed!=9)return 13;}
  if(state.destroyed!=10 || state.bad)return 14;
  {R value=state.made==10?static_cast<R>(&state):(R)&state;
   if(value.self!=&value || state.made!=11 || state.destroyed!=10)return 15;}
  if(state.destroyed!=11 || state.bad)return 16;
  {R value=(static_cast<R>(&state),(R)&state);
   if(value.self!=&value || state.made!=13 || state.destroyed!=12)return 17;}
  if(state.destroyed!=13 || state.bad)return 18;
  int id=static_cast<R>(&state).id;
  if(id!=14 || state.made!=14 || state.destroyed!=14 || state.bad)return 19;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("constructor-conversion-cleanup" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2RecordDestructionPreservesLifetimeAndExitOrder) {
  const auto Source = tmpFile("record-destruction.cpp");
  const auto Output = tmpFile("record-destruction.nc");
  writeFile(Source, R"cpp(
// Execution fixture for the admitted C++17 destruction and lifetime rules.
struct Trace {
  int values[64];
  int used;
  void add(int value) { values[used++]=value; }
  void clear() { used=0; }
  bool matches(const int *expected,int count) const {
    if(used!=count)return false;
    for(int i=0;i<count;++i)if(values[i]!=expected[i])return false;
    return true;
  }
};
struct Guard {
  Trace *trace;
  int id;
  Guard *self;
  Guard(Trace &log,int value):trace(&log),id(value),self(this){trace->add(id);}
  ~Guard(){trace->add(-id);}
  static void mark(Trace &trace){trace.add(9);}
};
struct Outer {
  Guard first,second;
  Trace *trace;
  Outer(Trace &log):first(log,1),second(log,2),trace(&log){}
  ~Outer(){Guard local(*trace,3);trace->add(9);return;}
};
struct ImplicitOuter { Guard first,second; };
struct Pair { int first,second; };
struct Reset {
  int *value,*count;
  ~Reset(){*value=0;++*count;}
};
struct D { Trace *trace; int id; ~D(){trace->add(id);} };
struct NestedD { D value; };
D resultD(Trace &trace,int id){return {&trace,id};}
struct Increment {
  int *value;
  explicit Increment(int &n):value(&n){}
  ~Increment(){++*value;}
};
Guard make(Trace &trace,int id){return Guard(trace,id);}
Guard forward(Trace &trace,int id){return make(trace,id);}
Guard named(Trace &trace,int id){Guard local(trace,id);return local;}
int parameter(Guard guard){return guard.self==&guard?guard.id:-99;}
int parameters(Guard first,Guard second,Trace &trace){trace.add(9);return first.id+second.id;}
int scalarArguments(int a,int b,Trace &trace){trace.add(9);return a+b;}
int capture(int &value){Increment local(value);return value;}
int &captureReference(int &value){Increment local(value);return value;}
struct InitializerTemps {
  int first,second,array[2];
  InitializerTemps(Trace &trace):first(Guard(trace,1).id),
    second((trace.add(9),2)),array{Guard(trace,2).id,(trace.add(8),3)}{}
};
struct ParameterOwner {
  int value;
  ParameterOwner(Guard guard):value(guard.id){guard.trace->add(9);}
};
struct OutOfLine { Trace *trace; ~OutOfLine(); };
OutOfLine::~OutOfLine(){trace->add(7);}
Guard &alias(Guard &guard){return guard;}
Guard recursive(Trace &trace,int depth){
  if(!depth)return Guard(trace,8);
  Guard local(trace,depth);
  return recursive(trace,depth-1);
}
struct MethodParameter {
  Trace *trace;
  int consume(Guard guard){trace->add(9);return guard.id;}
};
struct ArrayGuard { Guard values[2]; };
int captureTemporary(int &value,int &cleanups){return (Reset{&value,&cleanups},value);}
int main(){
  Trace trace{{},0};
  {Guard first(trace,1);{Guard second(trace,2);}}
  const int block_expected[4]={1,2,-2,-1};
  if(!trace.matches(block_expected,4))return 1;
  trace.clear();
  {Guard array[2][2]={{Guard(trace,1),Guard(trace,2)},{Guard(trace,3),Guard(trace,4)}};}
  const int array_expected[8]={1,2,3,4,-4,-3,-2,-1};
  if(!trace.matches(array_expected,8))return 2;
  trace.clear();
  {Outer outer(trace);}
  const int outer_expected[7]={1,2,3,9,-3,-2,-1};
  if(!trace.matches(outer_expected,7))return 3;
  trace.clear();
  {ImplicitOuter outer{Guard(trace,1),Guard(trace,2)};}
  if(!trace.matches(block_expected,4))return 4;
  trace.clear();
  {Guard result=forward(trace,4);if(result.self!=&result)return 5;}
  const int result_expected[2]={4,-4};
  if(!trace.matches(result_expected,2))return 6;
  trace.clear();
  if(parameter(Guard(trace,5))!=5)return 7;
  const int parameter_expected[2]={5,-5};
  if(!trace.matches(parameter_expected,2))return 8;
  trace.clear();
  if(parameters(Guard(trace,1),Guard(trace,2),trace)!=3)return 9;
  const int parameters_expected[5]={1,2,9,-2,-1};
  const int parameters_reverse[5]={2,1,9,-1,-2};
  if(!trace.matches(parameters_expected,5) && !trace.matches(parameters_reverse,5))return 10;
  trace.clear();
  if(scalarArguments(Guard(trace,1).id,Guard(trace,2).id,trace)!=3)return 11;
  if(!trace.matches(parameters_expected,5) && !trace.matches(parameters_reverse,5))return 12;
  trace.clear();
  int choose=1;
  (choose?Guard(trace,1):Guard(trace,2)).id;
  false && Guard(trace,3).id;
  true || Guard(trace,4).id;
  const int conditional_expected[2]={1,-1};
  if(!trace.matches(conditional_expected,2))return 13;
  trace.clear();
  for(Guard outer(trace,1);choose<4;++choose){
    Guard inner(trace,choose+1);
    if(choose==1)continue;
    break;
  }
  const int for_expected[6]={1,2,-2,3,-3,-1};
  if(!trace.matches(for_expected,6))return 14;
  trace.clear();
  for(int i=0;i<2;++i){
    Guard loop(trace,1);
    switch(Guard init(trace,2);i){
      case 0:{Guard branch(trace,3);continue;}
      default:{Guard branch(trace,4);break;}
    }
  }
  const int switch_expected[12]={1,2,3,-3,-2,-1,1,2,4,-4,-2,-1};
  if(!trace.matches(switch_expected,12))return 15;
  trace.clear();
  if(Guard init(trace,1);choose==2)Guard body(trace,2);
  if(!trace.matches(block_expected,4))return 16;
  trace.clear();
  {Guard copy_source(trace,6);if(parameter(copy_source)!=-99)return 17;}
  const int copy_expected[3]={6,-6,-6};
  if(!trace.matches(copy_expected,3))return 18;
  trace.clear();
  {Guard result=named(trace,7);if(result.id!=7)return 19;}
  const int named_expected[3]={7,-7,-7};
  if(!trace.matches(named_expected,3))return 20;
  int value=3;
  if(capture(value)!=3 || value!=4)return 21;
  if(&captureReference(value)!=&value || value!=5)return 22;
  trace.clear();
  int comma=(Guard(trace,1).id,Guard(trace,2).id);
  if(comma!=2 || !trace.matches(block_expected,4))return 23;
  trace.clear();
  int loops=0;
  do{Guard body(trace,++loops);continue;}while(loops<2);
  const int repeat_expected[4]={1,-1,2,-2};
  if(!trace.matches(repeat_expected,4))return 24;
  trace.clear();
  Pair pair{Guard(trace,1).id,(trace.add(9),2)};
  const int aggregate_expected[3]={1,9,-1};
  if(pair.first!=1 || pair.second!=2 || !trace.matches(aggregate_expected,3))return 25;
  int condition=1,cleanups=0,hits=0;
  if((Reset{&condition,&cleanups},condition))++hits;
  if(hits!=1 || condition!=0 || cleanups!=1)return 26;
  condition=1;cleanups=0;hits=0;
  while((Reset{&condition,&cleanups},condition))++hits;
  if(hits!=1 || condition!=0 || cleanups!=2)return 27;
  condition=1;cleanups=0;hits=0;
  for(;(Reset{&condition,&cleanups},condition);)++hits;
  if(hits!=1 || condition!=0 || cleanups!=2)return 28;
  condition=1;cleanups=0;hits=0;
  do{++hits;}while((Reset{&condition,&cleanups},condition));
  if(hits!=2 || condition!=0 || cleanups!=2)return 29;
  condition=7;cleanups=0;hits=0;
  switch((Reset{&condition,&cleanups},condition)){case 7:hits=1;break;default:hits=2;}
  if(hits!=1 || condition!=0 || cleanups!=1)return 30;
  trace.clear();
  D{&trace,(D{&trace,1},2)};
  const int completion_expected[2]={2,1};
  if(!trace.matches(completion_expected,2))return 31;
  trace.clear();
  resultD(trace,(D{&trace,1},2));
  if(!trace.matches(completion_expected,2))return 32;
  trace.clear();
  NestedD{{&trace,(D{&trace,1},2)}};
  if(!trace.matches(completion_expected,2))return 33;
  trace.clear();
  InitializerTemps initializer(trace);
  const int initializer_expected[6]={1,-1,9,2,8,-2};
  if(initializer.first!=1 || initializer.second!=2 || initializer.array[1]!=3 ||
     !trace.matches(initializer_expected,6))return 34;
  trace.clear();
  {ParameterOwner owner(Guard(trace,4));if(owner.value!=4)return 35;}
  const int owner_expected[3]={4,9,-4};
  if(!trace.matches(owner_expected,3))return 36;
  trace.clear();
  Guard(trace,1).mark(trace);
  if(!trace.matches(aggregate_expected,3))return 37;
  trace.clear();
  {OutOfLine out{&trace};{Guard local(trace,1);alias(local).id;}}
  const int alias_expected[3]={1,-1,7};
  if(!trace.matches(alias_expected,3))return 38;
  value=7;cleanups=0;
  if(captureTemporary(value,cleanups)!=7 || value!=0 || cleanups!=1)return 39;
  trace.clear();
  {Guard result=recursive(trace,2);if(result.self!=&result)return 40;}
  const int recursive_expected[6]={2,1,8,-1,-2,-8};
  if(!trace.matches(recursive_expected,6))return 41;
  trace.clear();
  MethodParameter method{&trace};
  if(method.consume(Guard(trace,4))!=4 || !trace.matches(owner_expected,3))return 42;
  trace.clear();
  int element=ArrayGuard{{Guard(trace,1),Guard(trace,2)}}.values[1].id;
  if(element!=2 || !trace.matches(block_expected,4))return 43;
  trace.clear();
  {D elements[2];for(int i=0;i<2;++i){elements[i].trace=&trace;elements[i].id=i+1;}}
  if(!trace.matches(completion_expected,2))return 44;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("record-destruction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2RecordDestructionRetainsUnsupportedLifetimeDiagnostics) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"explicit-deleted", "struct R{int n;~R()=delete;};"},
      {"virtual", "struct R{int n;virtual ~R(){}};"},
      {"explicit-call", "struct R{int n;~R(){}};void f(R&r){r.~R();}"},
      {"explicit-dead-call", "struct R{int n;~R(){}};void f(R&r){if(false)r.~R();}"},
      {"explicit-alias-call", "struct R{int n;~R(){}};using T=R;void f(R&r){r.~T();}"},
      {"global", "struct R{int n;~R(){}};const R r{1};"},
      {"global-containing", "struct R{int n;~R(){}};struct Box{R r;};const Box box{{1}};"},
      {"static-local", "struct R{int n;~R(){}};int f(){static R r{1};return r.n;}"},
      {"thread-local", "struct R{int n;~R(){}};int f(){thread_local R r{1};return r.n;}"},
      {"allocation", "struct R{int n;~R(){}};R*f(){return new R{1};}"},
      {"delete", "struct R{int n;~R(){}};void f(R*p){delete p;}"},
      {"unwinding", "struct R{int n;~R(){}};void f(){R r{1};throw 7;}"},
      {"body-throw", "struct R{int n;~R(){throw 7;}};"},
      {"body-try", "struct R{int n;~R(){try{n=1;}catch(...){n=2;}}};"},
      {"cleanup-expansion", "struct R{int n;~R(){}};void f(){R r[65536];}"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("destruction-reject-" + Name + ".cpp");
    const auto Output = tmpFile("destruction-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("destruction-definition.cpp");
  const auto Output = tmpFile("destruction-definition.nc");
  writeFile(Source, "struct R{int n;~R();};int main(){R r{1};return r.n;}");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  expectCode(Result, "TR0203");
  expectNoArtifacts(Output);
  writeFile(Source, "struct R{int n;~R(){}};int main(){R r{1};return r.n;}");
  Result = translate(Source, {"--profile", "cpp-core-v1", "-o", Output.string()});
  expectCode(Result, "TR0201");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2RecordCallsPreserveObjectStorageAndSourceCopies) {
  const auto Source = tmpFile("record-call-storage.cpp");
  const auto Output = tmpFile("record-call-storage.nc");
  writeFile(Source, R"cpp(
// The self-address checks pin NeverC's selected storage convention for these
// trivial records; C++17 permits other implementations to introduce copies.
struct R {
  int value;
  R *self;
  explicit R(int n):value(n),self(this){}
  R plus(int n) const { return R(value+n); }
  R combine(R other) const { return R(value+other.value); }
  static R build(int n) { return R(n); }
};
struct Pair { R first; R second; };
struct Consumer {
  R member;
  bool distinct_parameter;
  explicit Consumer(R value):member(value.value+1),
    distinct_parameter(value.self==&value && &member!=&value){}
};
R make(int n) { return R(n); }
R forward(int n) { return make(n); }
R recursive(int n) { if(n==0)return R(1);return recursive(n-1); }
R named(int n) { R local(n); return local; }
R identity(R value) { return value; }
int observe(R value) { return value.self==&value ? value.value : -1; }
int copied(R value,const R &original) {
  return value.self==&original && &value!=&original && value.value==original.value;
}
int mutate(R value,R &original) { value.value+=5;return value.value+original.value; }
int twice(R first,R second) {
  first.value=7;
  return &first!=&second && second.value==3;
}
int constant(const R value) { return value.value; }
R &alias(R &value) { return value; }
R *receiver(R *pointer,int &trace) { trace=trace*10+1;return pointer; }
R argument(R *&pointer,R &other,int &trace) {
  trace=trace*10+2;pointer=&other;return R(5);
}
R effect(int &trace,int digit) { trace=trace*10+digit;return R(digit); }
int sum(R first,R second) { return first.value+second.value; }
struct ArrayResult { int values[2]; };
ArrayResult arrayResult() { return {{61,67}}; }
struct OrderedResult { int first,second; };
OrderedResult ordered(OrderedResult *result) { return {71,result->first+2}; }
const R qualifiedResult() { return R(79); }
struct Aggregate { int value; };
int aggregateArgument(Aggregate value) { return value.value; }
int main() {
  R direct=make(3);
  R forwarded=forward(5);
  if(direct.value!=3 || direct.self!=&direct ||
     forwarded.value!=5 || forwarded.self!=&forwarded) return 1;
  R nested=direct.plus(4);
  R static_result=R::build(11);
  if(nested.value!=7 || nested.self!=&nested ||
     static_result.value!=11 || static_result.self!=&static_result) return 2;
  Pair pair{make(13),forward(17)};
  if(pair.first.value!=13 || pair.first.self!=&pair.first ||
     pair.second.value!=17 || pair.second.self!=&pair.second) return 3;
  R array[2]={make(19),forward(23)};
  if(array[0].value!=19 || array[0].self!=&array[0] ||
     array[1].value!=23 || array[1].self!=&array[1]) return 4;
  if(observe(R(29))!=29 || observe(make(31))!=31) return 5;
  if(!copied(direct,direct)) return 6;
  if(mutate(direct,direct)!=11 || direct.value!=3) return 7;
  if(!twice(direct,direct) || direct.value!=3) return 8;
  if(constant(direct)!=3 || &alias(direct)!=&direct) return 9;
  Consumer consumer(R(37));
  if(!consumer.distinct_parameter || consumer.member.value!=38 ||
     consumer.member.self!=&consumer.member) return 10;
  R repeated=recursive(4);
  if(repeated.value!=1 || repeated.self!=&repeated) return 11;
  // Named returns may select a source copy/move. Do not inspect a self pointer
  // after the referenced local/parameter has ended its lifetime.
  if(named(41).value!=41 || identity(direct).value!=3) return 12;
  R *pointer=&direct;
  int trace=0;
  R combined=receiver(pointer,trace)->combine(argument(pointer,forwarded,trace));
  if(combined.value!=8 || combined.self!=&combined || pointer!=&forwarded ||
     trace!=12 || direct.value!=3 || forwarded.value!=5) return 13;
  R conditional=direct.value==3?make(43):make(47);
  R comma=(++trace,forward(53));
  if(conditional.value!=43 || conditional.self!=&conditional ||
     comma.value!=53 || comma.self!=&comma || trace!=13) return 14;
  trace=0;
  if(sum(effect(trace,1),effect(trace,2))!=3 || (trace!=12 && trace!=21)) return 15;
  if(make(59).value!=59) return 16;
  if(arrayResult().values[1]!=67) return 17;
  OrderedResult in_place=ordered(&in_place);
  if(in_place.first!=71 || in_place.second!=73) return 18;
  R qualified=qualifiedResult();
  if(qualified.value!=79 || qualified.self!=&qualified) return 19;
  if(aggregateArgument({83})!=83) return 20;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("record-call-storage" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2RecordCallsRetainLifetimeAndSourceTypeBoundaries) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"record-parameter-expansion", "struct R{int n[65536];};int ignore(R a,R b,R c,R d){return 0;}int f(){R r;for(int i=0;i<65536;++i)r.n[i]=0;return ignore(r,r,r,r);}"},
      {"record-result-fallthrough", "struct R{int n;};R f(bool b){if(b)return {1};}"},
      {"record-result-reference-binding", "struct R{int n;};R f(){return {1};}int main(){const R&r=f();return r.n;}"},
      {"record-result-method-receiver", "struct R{int n;int get(){return n;}};R f(){return {1};}int main(){return f().get();}"},
      {"record-result-c-export", "struct R{int n;};extern \"C\" R exported(){return {1};}"},
      {"record-parameter-c-export", "struct R{int n;};extern \"C\" int exported(R r){return r.n;}"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("call-storage-reject-" + Name + ".cpp");
    const auto Output = tmpFile("call-storage-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("call-storage-const-write.cpp");
  const auto Output = tmpFile("call-storage-const-write.nc");
  writeFile(Source, "struct R{int n;};int f(const R r){r.n=1;return r.n;}");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  expectCode(Result, "TR0202");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2RecordConstructorsPreserveDestinationsAndInitializationOrder) {
  const auto Source = tmpFile("record-constructors.cpp");
  const auto Output = tmpFile("record-constructors.nc");
  writeFile(Source, R"cpp(
struct Self {
  int value;
  Self *self;
  Self():value(7),self(this){}
  explicit Self(int n):value(n),self(this){}
  int get()const{return value;}
};
struct Inner {
  int value;
  explicit Inner(int n):value(n){}
};
int mark(int &trace,int digit,int value) {
  trace=trace*10+digit;
  return value;
}
struct Ordered {
  int first;
  int second;
  Inner member;
  int values[2];
  explicit Ordered(int &trace);
  int getFirst()const{return first;}
};
Ordered::Ordered(int &trace)
  :second(mark(trace,2,getFirst()+4)),first(mark(trace,1,3)),
   member(mark(trace,3,second+5)),values{member.value,member.value+1} {
  trace=trace*10+4;
}
struct Partial {
  int initialized;
  int untouched;
  explicit Partial(int n):initialized(n){}
  int get()const{return initialized;}
};
struct Counted {
  int value;
  Counted(int &count,int n):value(n){++count;}
};
struct Convertible {
  int value;
  Convertible(int n):value(n){}
};
struct Early {
  int value;
  explicit Early(bool stop):value(3){if(stop)return;value=5;}
};
struct Wrap { Self value; };
struct ImplicitMember { Self value; ImplicitMember() {} };
struct View {
  int values[2];
  explicit View(View *&out):values{3,5}{out=this;}
};
struct Pick {
  int value;
  explicit Pick(signed char):value(3){}
  explicit Pick(short):value(5){}
  explicit Pick(long):value(7){}
  explicit Pick(long long):value(11){}
};
struct ConstexprValue {
  int value;
  constexpr explicit ConstexprValue(int n):value(n){}
  constexpr int get()const{return value;}
};
constexpr ConstexprValue constant{31};
static_assert(constant.get()==31);
Self make(int &count) { ++count; return Self(17); }
int consume(Self value) { return value.get(); }
int main() {
  Self a;
  Self b(11);
  Self c{13};
  Self d=Self(17);
  if(a.get()!=7 || b.get()!=11 || c.get()!=13 || d.get()!=17) return 1;
  if(a.self!=&a || b.self!=&b || c.self!=&c || d.self!=&d) return 2;
  const Self readonly(19);
  if(readonly.get()!=19 || readonly.self!=&readonly) return 3;
  int trace=0;
  Ordered ordered(trace);
  if(trace!=1234 || ordered.first!=3 || ordered.second!=7 ||
     ordered.member.value!=12 || ordered.values[0]!=12 || ordered.values[1]!=13)
    return 4;
  Partial partial(23);
  if(partial.get()!=23) return 5;
  Self array[3]={Self(2)};
  if(array[0].value!=2 || array[1].value!=7 || array[2].value!=7) return 6;
  for(int i=0;i!=3;++i) if(array[i].self!=&array[i]) return 7;
  Self matrix[2][2]{};
  for(int i=0;i!=2;++i) for(int j=0;j!=2;++j)
    if(matrix[i][j].self!=&matrix[i][j] || matrix[i][j].value!=7) return 8;
  Wrap wrapped{Self(29)};
  if(wrapped.value.self!=&wrapped.value || wrapped.value.value!=29) return 9;
  int effects=0;
  Counted counted[3]={Counted(effects,2),Counted(effects,3),Counted(effects,5)};
  if(effects!=3 || counted[0].value+counted[1].value+counted[2].value!=10) return 10;
  Self conditional=(effects==3)?Self(37):Self(41);
  Self comma=(++effects,Self(43));
  if(conditional.self!=&conditional || conditional.value!=37 ||
     comma.self!=&comma || comma.value!=43 || effects!=4) return 11;
  Self copied=b;
  c=b;
  if(copied.self!=&b || c.self!=&b || copied.value!=11 || c.value!=11) return 12;
  Convertible implicit=47;
  Early early(true);
  Early late(false);
  if(implicit.value!=47 || early.value!=3 || late.value!=5) return 13;
  effects=0;
  if(make(effects).value!=17 || consume(make(effects))!=17 || effects!=2) return 14;
  if(constant.get()!=31) return 15;
  View *witness=nullptr;
  int *view=nullptr;
  bool same_view=(view=View(witness).values,view==witness->values);
  if(!same_view) return 16;
  Self default_array[2];
  if(default_array[0].value!=7 || default_array[1].value!=7 ||
     default_array[0].self!=&default_array[0] ||
     default_array[1].self!=&default_array[1]) return 17;
  ImplicitMember implicit_member;
  if(implicit_member.value.value!=7 ||
     implicit_member.value.self!=&implicit_member.value) return 18;
  View *other_witness=nullptr;
  bool another_view=(view=View(other_witness).values,view==other_witness->values);
  if(!another_view) return 19;
  Pick byte_pick(static_cast<signed char>(1));
  Pick short_pick(static_cast<short>(1));
  Pick long_pick(1L);
  Pick wide_pick(1LL);
  if(byte_pick.value!=3 || short_pick.value!=5 ||
     long_pick.value!=7 || wide_pick.value!=11) return 20;
  Self alternate=(effects==0)?Self(41):Self(43);
  if(alternate.self!=&alternate || alternate.value!=43) return 21;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("record-constructors" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2RecordConstructorsRetainLifetimeAndSourceBoundaries) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"delegating", "struct R{int n;R():R(1){} R(int v):n(v){}};"},
      {"base-initializer", "struct B{int n;B(int v):n(v){}};struct R:B{R():B(1){}};"},
      {"inherited-constructor", "struct B{int n;B(int v):n(v){}};struct R:B{using B::B;};"},
      {"virtual-method", "struct R{int n;R():n(1){} virtual int get(){return n;}};"},
      {"template-constructor", "struct R{int n;template<class T> R(T v):n(v){}};"},
      {"variadic-constructor", "struct R{int n;R(int v,...):n(v){}};"},
      {"deleted-constructor", "struct R{int n;R()=delete;};"},
      {"default-argument", "struct R{int n;R(int v=1):n(v){}};"},
      {"private-field", "class R{int n;public:R():n(1){}};"},
      {"protected-field", "struct R{protected:int n;public:R():n(1){}};"},
      {"const-field", "struct R{const int n;R():n(1){}};"},
      {"reference-field", "struct R{int &n;R(int &v):n(v){}};"},
      {"mutable-field", "struct R{mutable int n;R():n(1){}};"},
      {"nested-record", "struct R{struct I{int n;};I i;R():i{1}{}};"},
      {"union", "union R{int n;unsigned u;R():n(1){}};"},
      {"bitfield", "struct R{unsigned n:3;R():n(1){}};"},
      {"static-data", "struct R{int n;static int value;R():n(1){}};int R::value=1;"},
      {"folded-unsupported-initializer", "struct R{int n;constexpr R():n(sizeof(float)){}};constexpr R r;"},
      {"folded-throw-body", "struct R{int n;constexpr R(int v):n(v){if(v)throw 1;}};constexpr R r(0);"},
      {"dynamic-global", "struct R{int n;R():n(1){}};R global;"},
      {"global-array", "struct R{int n;constexpr R(int v):n(v){}};constexpr R global[1]={{1}};"},
      {"global-pointer", "struct R{int n;constexpr R(int v):n(v){}};constexpr R global(1);constexpr const R *pointer=&global;"},
  };
  for (const auto &[Name, Code] : Cases) {
    SCOPED_TRACE(Name);
    const auto Source = tmpFile("constructor-reject-" + Name + ".cpp");
    const auto Output = tmpFile("constructor-reject-" + Name + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2RecordConstructorsRequireDefinitionsAndDoNotBroadenV1) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"cpp-core-v1", "struct R{int n;R(int v):n(v){}};int main(){R r(1);return r.n-1;}"},
      {"cpp-core-v2", "struct R{int n;R(int);};int f(){R r(1);return r.n;}"},
  };
  for (const auto &[Profile, Code] : Cases) {
    SCOPED_TRACE(Profile);
    const auto Source = tmpFile("constructor-profile-" + Profile + ".cpp");
    const auto Output = tmpFile("constructor-profile-" + Profile + ".nc");
    writeFile(Source, Code);
    auto Result = translate(Source, {"--profile", Profile, "-o", Output.string()});
    expectCode(Result, Profile == "cpp-core-v1" ? "TR0201" : "TR0203");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2RecordMethodsPreserveReceiverIdentityAndSequencing) {
  const auto Source = tmpFile("record-methods.cpp");
  const auto Output = tmpFile("record-methods.nc");
  writeFile(Source, R"cpp(
struct Counter {
  int value;
  int spare;
  int *pointer;
  int values[2];
  int get() const;
  void set(int n) & { value = n; }
  Counter &add(int n) { this->value += n; return *this; }
  Counter *self() { return this; }
  int &front() { return values[0]; }
  const int &front() const { return values[0]; }
  int &pointed() const { return *pointer; }
  int kind() { return 1; }
  int kind() const { return 2; }
  int choose(signed char) const { return 3; }
  int choose(short) const { return 5; }
  int choose(long) const { return 7; }
  int choose(long long) const { return 11; }
  int recursive(int n) const { return n == 0 ? get() : recursive(n - 1) + 1; }
  static int plus(int a, int b) { return a + b; }
};
int Counter::get() const { return value; }
struct Access {
  int value;
private:
  int implementation() const { return value; }
public:
  int get() const { return implementation(); }
};
struct Handle { Counter *pointer; };
Counter *receiver(Counter *p, int &trace) { trace = trace * 10 + 1; return p; }
int argument(Counter *&p, Counter &other, int &trace) {
  trace = trace * 10 + 2;
  p = &other;
  return 5;
}
Counter static_receiver(int &trace) {
  trace = trace * 10 + 1;
  return Counter{1, 2, nullptr, {3, 4}};
}
int static_argument(int &trace) { trace = trace * 10 + 2; return 17; }
int main() {
  Counter partial;
  partial.value = 13;
  if (partial.get() != 13) return 1;
  partial.set(19);
  if (partial.value != 19) return 2;
  int pointed = 23;
  Counter first{2, 101, &pointed, {3, 5}};
  Counter second{7, 103, &pointed, {11, 13}};
  const Counter &constant = first;
  if (first.kind() != 1 || constant.kind() != 2) return 3;
  Counter &same = first.add(17);
  if (&same != &first || first.value != 19 || first.self() != &first) return 4;
  first.front() = 29;
  constant.pointed() = 31;
  if (constant.front() != 29 || pointed != 31) return 5;
  if (constant.choose(static_cast<signed char>(1)) != 3 ||
      constant.choose(static_cast<short>(1)) != 5 ||
      constant.choose(1L) != 7 || constant.choose(1LL) != 11) return 6;
  if (constant.recursive(3) != 22) return 7;
  Counter *pointer = &first;
  int trace = 0;
  receiver(pointer, trace)->add(argument(pointer, second, trace));
  if (trace != 12 || pointer != &second || first.value != 24 || second.value != 7)
    return 8;
  trace = 0;
  if (static_receiver(trace).plus(static_argument(trace), 2) != 19 || trace != 12)
    return 9;
  trace = 0;
  if ((receiver(&first, trace))->plus(1, 2) != 3 || trace != 1) return 10;
  if (Counter::plus(3, 5) != 8 || ((Counter::plus))(7, 11) != 18) return 11;
  Access access{37};
  if (access.get() != 37 || Handle{&first}.pointer->get() != 24) return 12;
  Counter copied = first;
  second = copied;
  if (copied.get() != 24 || second.get() != 24 || second.values[0] != 29 ||
      copied.self() == first.self() || second.pointer != &pointed) return 13;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("record-methods" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2RecordMethodsRetainLifetimeAndCalleeBoundaries) {
  const std::vector<std::pair<std::string, std::string>> Cases = {
      {"folded-static-function-value", "struct R{int n;static int get(){return 1;}};static_assert((R::get,true));"},
      {"folded-parenthesized-static-value", "struct R{int n;static int get(){return 1;}};static_assert(((R::get),true));"},
      {"method-pointer", "struct R{int n;int get(){return n;}};auto f(){return &R::get;}"},
      {"static-function-pointer", "struct R{int n;static int get(){return 1;}};int f(){auto p=&R::get;return p();}"},
      {"virtual-method", "struct R{int n;virtual int get(){return n;}};"},
      {"base-class", "struct B{int n;};struct R:B{int get(){return n;}};"},
      {"volatile-method", "struct R{int n;int get()volatile{return n;}};"},
      {"mutable-field", "struct R{mutable int n;int get()const{return n;}};"},
      {"reference-field", "struct R{int&n;int get()const{return n;}};"},
      {"member-template", "struct R{int n;template<class T>T get(T v){return v;}};"},
      {"static-data", "struct R{int n;static int value;int get(){return value;}};int R::value=1;"},
      {"default-argument", "struct R{int n;int get(int v=1){return n+v;}};int f(){R r{1};return r.get();}"},
      {"constant-static-data", "struct R{int n;static const int value=1;int get(){return value;}};"},
      {"method-comma-callee", "struct R{int n;static int get(){return 1;}};int f(){return (0,R::get)();}"},
  };
  for (const auto &Case : Cases) {
    SCOPED_TRACE(Case.first);
    const auto Source = tmpFile(Case.first + ".cpp");
    const auto Output = tmpFile(Case.first + ".nc");
    writeFile(Source, Case.second);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
  const auto Source = tmpFile("const-method-write.cpp");
  const auto Output = tmpFile("const-method-write.nc");
  writeFile(Source, "struct R{int n;void set()const{n=1;}};");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  expectCode(Result, "TR0202");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2RecordMethodsDoNotBroadenCoreV1) {
  const auto Source = tmpFile("v1-record-methods.cpp");
  const auto Output = tmpFile("v1-record-methods.nc");
  writeFile(Source, "struct R{int n;int get()const{return n;}};int main(){R r{1};return r.get()-1;}");
  auto Result = translate(Source, {"-o", Output.string()});
  expectCode(Result, "TR0201");
  expectNoArtifacts(Output);
}

TEST_F(TranslateTest, CoreV2IntegerWidthsCharactersAndSizeQueriesPreserveValues) {
  const auto Source = tmpFile("integer-widths.cpp");
  const auto Output = tmpFile("integer-widths.nc");
  writeFile(Source, R"cpp(
using Size = decltype(sizeof(0));
using Signed64 = long long;
using Unsigned64 = unsigned long long;
enum class Tiny : unsigned char { high = 255 };
enum class Wide : Unsigned64 { high = 0xffffffffffffffffull };
enum class Flag : bool { off = false, on = true };
struct Mixed { signed char byte; Signed64 wide; char16_t unit; };
int choose(signed char) { return 1; }
int choose(short) { return 2; }
int choose(long) { return 3; }
int choose(Signed64) { return 4; }
Signed64 &alias(Signed64 &value) { return value; }
int tiny_switch(Tiny value) {
  switch (value) {case Tiny::high: return 7; default: return 9;}
}
int wide_switch(Unsigned64 value) {
  switch (value) {
  case 0xffffffffffffffffull: return 11;
  case 0x8000000000000000ull: return 13;
  default: return 17;
  }
}
int main() {
  signed char small = 127;
  signed char previous = small++;
  if (previous != 127 || small != -128) return 1;
  --small;
  if (small != 127) return 2;
  unsigned char byte = 255;
  if (byte + 1 != 256 || byte * 2 != 510) return 3;
  ++byte;
  if (byte != 0) return 4;
  short narrow = -32767 - 1;
  narrow -= 1;
  if (narrow != 32767) return 5;
  unsigned short units = 65535;
  if (units + 1 != 65536) return 6;
  ++units;
  if (units != 0) return 7;
  Signed64 minimum = -9223372036854775807ll - 1ll;
  Signed64 maximum = 9223372036854775807ll;
  Unsigned64 all = 0xffffffffffffffffull;
  if (static_cast<Signed64>(all) != -1ll ||
      static_cast<Signed64>(0x8000000000000000ull) != minimum) return 8;
  if (static_cast<signed char>(all) != -1 ||
      static_cast<short>(all) != -1 || static_cast<int>(all) != -1) return 9;
  if (static_cast<Unsigned64>(-1) != all ||
      static_cast<unsigned char>(-1ll) != 255) return 10;
  if ((1ll << 63) != minimum || (minimum >> 63) != -1ll ||
      (minimum >> 1) != -4611686018427387904ll) return 11;
  if ((all >> 63) != 1ull || (1ull << 63) != 0x8000000000000000ull)
    return 12;
  if (maximum + -1ll != 9223372036854775806ll || all + 1ull != 0ull)
    return 13;
  if (!(-1ll < 1u) || (-1ll < 1ull)) return 14;
  if (choose(static_cast<signed char>(1)) != 1 ||
      choose(static_cast<short>(1)) != 2 || choose(1L) != 3 || choose(1LL) != 4)
    return 15;
  Signed64 values[3] = {minimum, maximum};
  Size index = 1;
  alias(values[index]) = 0x100000001ll;
  if (values[0] != minimum || values[1] != 4294967297ll || values[2] != 0)
    return 16;
  wchar_t wide = L'\u4e2d';
  char16_t utf16 = u'\u4e2d';
  char32_t utf32 = U'\U0001f600';
  char raw = '\xff';
  if (static_cast<unsigned int>(wide) != 0x4e2du || utf16 != 0x4e2d ||
      utf32 != 0x1f600u || static_cast<unsigned char>(raw) != 255) return 17;
  if (tiny_switch(Tiny::high) != 7 || wide_switch(all) != 11 ||
      wide_switch(0x8000000000000000ull) != 13 || wide_switch(0) != 17) return 18;
  if (!(Tiny::high == static_cast<Tiny>(255)) ||
      !(static_cast<Tiny>(1) < Tiny::high)) return 25;
  if (Flag::off == Flag::on || !(Flag::off < Flag::on)) return 26;
  Flag flag = Flag::on;
  switch (flag) {case Flag::on: break; default: return 19;}
  int effects = 0;
  Size size = sizeof(++effects);
  if (size != sizeof(int) || effects != 0) return 20;
  int &reference = effects;
  if (sizeof(reference) != sizeof(int) || sizeof(int&) != sizeof(int)) return 21;
  if (sizeof(values) != 3 * sizeof(Signed64) || sizeof(Signed64) != 8 ||
      sizeof(short) != 2 || sizeof(unsigned char) != 1) return 22;
  if (sizeof(long) != __SIZEOF_LONG__ || sizeof(wchar_t) != __SIZEOF_WCHAR_T__)
    return 23;
  if (alignof(Mixed) < alignof(Signed64) || alignof(short) > sizeof(short))
    return 24;
  Mixed record{-128, minimum, utf16};
  if (record.byte != -128 || record.wide != minimum || record.unit != utf16)
    return 27;
  if (static_cast<int>(0x80000001ll) != -2147483647 ||
      static_cast<short>(0x8001ll) != -32767 ||
      static_cast<signed char>(0x81ll) != -127 ||
      static_cast<signed char>(-129ll) != 127 ||
      static_cast<int>(minimum) != 0) return 28;
  if (0x100000001ll * 3 != 12884901891ll ||
      (-9223372036854775807ll / 3) != -3074457345618258602ll ||
      (-9223372036854775807ll % 3) != -1ll) return 29;
  if ((-1ll & 0xffffffffu) != 4294967295ll) return 30;
  static_assert(sizeof(Signed64) == 8);
  static_assert(sizeof(char16_t) == 2 && sizeof(char32_t) == 4);
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("integer-widths" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2IntegerExpansionRejectsUnsupportedTypesAndSizeQueries) {
  const std::vector<std::string> Sources = {
      "using Wide=__int128; int main(){}",
      "int main(){if(false){unsigned __int128 hidden=0;} return 0;}",
      "using Wide=_BitInt(65); int main(){}",
      "static_assert(sizeof(void)>0); int main(){}",
      "int f(){return 0;} int main(){return sizeof(f);}",
      "int main(){int n=0; return __alignof__(n);}",
      "int main(){int n=0; return alignof(n);}",
      "int main(){return sizeof(double);}",
      "int main(){return sizeof(1.0);}",
      "int main(){return sizeof((void(0),1));}"};
  for (size_t I = 0; I < Sources.size(); ++I) {
    SCOPED_TRACE(Sources[I]);
    const auto Source = tmpFile("integer-reject" + std::to_string(I) + ".cpp");
    const auto Output = tmpFile("integer-reject" + std::to_string(I) + ".nc");
    writeFile(Source, Sources[I]);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    // Some extended integer forms are rejected by Clang on individual targets;
    // on others the owned-source allowlist diagnoses them before lowering.
    EXPECT_NE(Result.exitCode, 0);
    EXPECT_TRUE(Result.stderrContains("TR0201") || Result.stderrContains("TR0202"))
        << Result.out << Result.err;
    expectNoArtifacts(Output);
  }
  for (const auto &SourceText : {
           "int main(){long long value=1; return value;}",
           "int main(){char value='a'; return value;}",
           "int main(){return sizeof(int);}"}) {
    const auto Source = tmpFile("v1-integer-reject.cpp");
    const auto Output = tmpFile("v1-integer-reject.nc");
    writeFile(Source, SourceText);
    auto Result = translate(Source, {"-o", Output.string()});
    expectCode(Result, "TR0201");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2LayoutEvidenceMatchesCompiledRecordStorage) {
  const auto Source = tmpFile("layout.cpp");
  const auto Output = tmpFile("layout.nc");
  writeFile(Source, R"cpp(
struct Inner { bool flag; int value; bool tail; };
struct Outer { bool flag; Inner items[2]; Inner *pointer; };
int main() {
  Outer value{true, {{true, 11, false}, {false, 17, true}}, nullptr};
  value.pointer = &value.items[1];
  value.pointer->value += value.items[0].value;
  if (!value.flag || !value.items[0].flag || value.items[0].tail) return 1;
  if (value.items[1].flag || !value.items[1].tail) return 2;
  if (value.items[1].value != 28) return 3;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  auto Manifest = llvm::json::parse(
      readFile(fs::path(Output.string() + ".manifest.json")));
  ASSERT_TRUE(static_cast<bool>(Manifest))
      << llvm::toString(Manifest.takeError()).str().str();
  const auto *Object = Manifest->getAsObject();
  ASSERT_NE(Object, nullptr);
  const auto *Target = Object->getObject("target");
  ASSERT_NE(Target, nullptr);
  const auto *Carriers = Target->getObject("carrier_layout");
  ASSERT_NE(Carriers, nullptr);
  EXPECT_EQ(Carriers->size(), 11u);
  const auto *Records = Object->getArray("record_layouts");
  ASSERT_NE(Records, nullptr);
  ASSERT_EQ(Records->size(), 2u);
  int64_t InnerSize = 0, InnerAlign = 0;
  const auto *Inner = (*Records)[0].getAsObject();
  ASSERT_NE(Inner, nullptr);
  ASSERT_TRUE(Inner->getInteger("size_bits", InnerSize));
  ASSERT_TRUE(Inner->getInteger("abi_align_bits", InnerAlign));
  EXPECT_EQ(InnerSize, 96);
  EXPECT_EQ(InnerAlign, 32);
  const auto *Offsets = Inner->getArray("field_offsets_bits");
  ASSERT_NE(Offsets, nullptr);
  EXPECT_EQ(Offsets->size(), 3u);
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
  // Prove the output compiler evaluates the recorded field-layout assertions.
  auto Text = readFile(Output);
  auto Guard = Text.find("static_assert(__builtin_offsetof(");
  ASSERT_NE(Guard, std::string::npos);
  auto Value = Text.find("== ", Guard);
  ASSERT_NE(Value, std::string::npos);
  auto End = Text.find(',', Value);
  ASSERT_NE(End, std::string::npos);
  Text.replace(Value, End - Value, "== 1");
  const auto Corrupt = tmpFile("layout-corrupt.nc");
  writeFile(Corrupt, Text);
  auto Compile = compileGenerated(Corrupt, tmpFile("layout-corrupt"), "-O0");
  EXPECT_NE(Compile.exitCode, 0);
  EXPECT_NE(Compile.err.find("translated field offset mismatch"), std::string::npos)
      << Compile.out << Compile.err;
}

TEST_F(TranslateTest, CoreV2DeclarationsPreserveValuesAndOverloads) {
  const auto Source = tmpFile("declarations.cpp");
  const auto Output = tmpFile("declarations.nc");
  writeFile(Source, R"cpp(
namespace model {
using Count = unsigned int;
typedef int Result;
using Copy = Count;
using Nothing = void;
enum class Mode : Copy { low = 1u, high = 0xffffffffu };
enum class Signed : Result { minimum = -2147483647 - 1, negative = -3 };
enum Legacy { zero, answer = 40 };
enum Large : Count { top = 0xffffffffu };
enum class Opaque : Count;
struct Pair { Mode mode; Signed sign; };
constexpr Pair original{Mode::high, Signed::minimum};
static_assert(static_cast<Count>(Mode::high) == 0xffffffffu, "enum width");
static_assert(static_cast<int>(Signed::minimum) == -2147483647 - 1);
int select(Mode value) { return value == Mode::high ? 7 : 8; }
int select(Count value) { return value == 0xffffffffu ? 9 : 10; }
}
int main() {
  using Local = model::Mode;
  typedef model::Result Result;
  enum class LocalEnum : int { value = 3 };
  static_assert(static_cast<int>(LocalEnum::value) == 3, "local assertion");
  Local mode = model::Mode::high;
  Local direct{1u};
  Local zero{};
  model::Opaque opaque = static_cast<model::Opaque>(23u);
  model::Pair copy = model::original;
  Result value = model::answer;
  if (static_cast<model::Count>(mode) != 0xffffffffu) return 1;
  if (static_cast<int>(copy.sign) != -2147483647 - 1) return 2;
  if (copy.mode != mode) return 3;
  if (model::select(mode) != 7) return 4;
  if (model::select(static_cast<model::Count>(mode)) != 9) return 5;
  if (static_cast<int>(model::Signed::negative) != -3) return 6;
  if (static_cast<Local>(1u) != model::Mode::low) return 7;
  if (value + static_cast<int>(LocalEnum::value) != 43) return 8;
  if (direct != model::Mode::low || static_cast<model::Count>(zero) != 0u)
    return 9;
  if (static_cast<model::Count>(opaque) != 23u) return 10;
  if (model::top + 1 != 0u || !(model::top == -1)) return 11;
  return 0;
}
)cpp");
  auto Result = translate(
      Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  auto Manifest = llvm::json::parse(
      readFile(fs::path(Output.string() + ".manifest.json")));
  ASSERT_TRUE(static_cast<bool>(Manifest))
      << llvm::toString(Manifest.takeError()).str().str();
  const auto *Object = Manifest->getAsObject();
  ASSERT_NE(Object, nullptr);
  EXPECT_TRUE(Object->getString("profile") == "cpp-core-v2");
  int64_t Version = 0;
  ASSERT_TRUE(Object->getInteger("profile_version", Version));
  EXPECT_EQ(Version, 2);
  const auto *Mappings = Object->getArray("mappings");
  ASSERT_NE(Mappings, nullptr);
  EXPECT_TRUE(Mappings->empty());
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("declarations" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2CheckAcceptsDeclarationsWithoutOutput) {
  const auto Source = tmpFile("assertions.cpp");
  const auto Report = tmpFile("assertions.json");
  writeFile(Source, "using Value = int; enum class E : Value { answer = 42 };"
                    "static_assert(static_cast<Value>(E::answer) == 42, "
                    "\"diagnostic text only\");");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check",
                                    "--report", Report.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  auto Parsed = llvm::json::parse(readFile(Report));
  ASSERT_TRUE(static_cast<bool>(Parsed))
      << llvm::toString(Parsed.takeError()).str().str();
  const auto *Object = Parsed->getAsObject();
  ASSERT_NE(Object, nullptr);
  EXPECT_TRUE(Object->getString("profile") == "cpp-core-v2");
  EXPECT_TRUE(Object->getString("status") == "success");
  EXPECT_FALSE(fs::exists(tmpFile("assertions.nc")));
}

TEST_F(TranslateTest, CoreV2RejectsUnsupportedErasedDeclarations) {
  struct Rejection {
    const char *Name;
    const char *Source;
    const char *Code;
  };
  const Rejection Cases[] = {
      {"pointer-alias", "using Hidden = float *;", "TR0201"},
      {"volatile-alias", "using Hidden = volatile int;", "TR0201"},
      {"function-alias", "using Hidden = void();", "TR0201"},
      {"alias-template", "template<class T> using Hidden = T;", "TR0201"},
      {"folded-enum",
       "enum E : int { value = (static_cast<void>(0), 1) };", "TR0201"},
      {"folded-assertion",
       "static_assert((static_cast<void>(0), true), \"checked condition\");",
       "TR0201"},
      {"floating-assertion", "static_assert(1.0 == 1.0, \"checked types\");",
       "TR0201"},
      {"runtime-string",
       "static_assert(true, \"message\"); const char *value = \"runtime\";",
       "TR0201"},
      {"failed-assertion", "static_assert(false, \"must fail\");", "TR0202"}};
  for (const auto &Case : Cases) {
    SCOPED_TRACE(Case.Name);
    const auto Source = tmpFile(std::string(Case.Name) + ".cpp");
    const auto Output = tmpFile(std::string(Case.Name) + ".nc");
    writeFile(Source, std::string(Case.Source) + "\nint main() { return 0; }\n");
    auto Result = translate(
        Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
    expectCode(Result, Case.Code);
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, CoreV2DeclarationsDoNotBroadenCoreV1) {
  unsigned Index = 0;
  for (const auto *Declaration : {"using Value = int;",
                                 "typedef int Value;",
                                 "enum class E : int { value = 1 };",
                                 "static_assert(true, \"message\");",
                                 "static_assert(true, \"joined \" \"message\");",
                                 "static_assert(true);",
                                 "namespace N { static_assert(true, \"message\"); }",
                                 "void f() { static_assert(true, \"message\"); }"}) {
    SCOPED_TRACE(Declaration);
    const auto Name = "old-contract-" + std::to_string(++Index);
    const auto Source = tmpFile(Name + ".cpp");
    const auto Output = tmpFile(Name + ".nc");
    writeFile(Source, std::string(Declaration) + "\nint main() { return 0; }\n");
    expectCode(translate(Source, {"-o", Output.string()}), "TR0201");
    expectNoArtifacts(Output);
  }
}

TEST_F(TranslateTest, UntypedAssemblyStringsAreDiagnosedWithoutFrontendCrashes) {
  const auto Source = tmpFile("untyped-assembly.cpp");
  writeFile(Source, "asm(\"\");\nint main() { return 0; }\n");
  for (const std::string &Profile : {"cpp-core-v1", "cpp-core-v2"}) {
    SCOPED_TRACE(Profile);
    const auto Output = tmpFile("untyped-assembly-" + Profile + ".nc");
    expectCode(translate(Source, {"--profile", Profile, "-o", Output.string()}),
               "TR0201");
    expectNoArtifacts(Output);
  }
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
