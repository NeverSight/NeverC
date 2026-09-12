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
      {"array-temporary-subobject",
       "struct R{int a[2];}; int f(){const int &r=R{{1,2}}.a[0]; return r;}", "TR0201"},
      {"array-temporary-comma",
       "struct R{int a[2];}; int f(){int n=0; const int &r=(++n,R{{1,2}}.a)[0]; return r;}", "TR0201"},
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
      {"rvalue-reference", "int f(int &&value) {return value;}", "TR0201"},
      {"temporary-reference",
       "int f(){const int &value=42; return value;}", "TR0201"},
      {"dead-temporary-reference",
       "int f(){if(false){const int &value=42;} return 0;}", "TR0201"},
      {"temporary-reference-argument",
       "int f(const int &x){return x;} int main(){return f(42);}", "TR0201"},
      {"conversion-temporary",
       "int f(){int x=1; const unsigned int &r=x; return r;}", "TR0201"},
      {"temporary-subobject",
       "struct R{int x;}; int f(){const int &r=R{1}.x; return r;}", "TR0201"},
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
      {"temporary-dot", "struct R{int n;int get()const{return n;}};int f(){return R{1}.get();}"},
      {"temporary-arrow", "struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return H{{{1}}}.a->get();}"},
      {"temporary-arrow-offset", "struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return (H{{{1}}}.a+0)->get();}"},
      {"dead-temporary-receiver", "struct R{int n;int get()const{return n;}};int f(){if(false)return R{1}.get();return 0;}"},
      {"folded-temporary-receiver", "struct R{int n;constexpr int get()const{return n;}};static_assert(R{1}.get()==1);"},
      {"folded-static-function-value", "struct R{int n;static int get(){return 1;}};static_assert((R::get,true));"},
      {"folded-parenthesized-static-value", "struct R{int n;static int get(){return 1;}};static_assert(((R::get),true));"},
      {"method-pointer", "struct R{int n;int get(){return n;}};auto f(){return &R::get;}"},
      {"static-function-pointer", "struct R{int n;static int get(){return 1;}};int f(){auto p=&R::get;return p();}"},
      {"virtual-method", "struct R{int n;virtual int get(){return n;}};"},
      {"base-class", "struct B{int n;};struct R:B{int get(){return n;}};"},
      {"constructor", "struct R{int n;R(int v):n(v){}};"},
      {"destructor", "struct R{int n;~R(){}};"},
      {"conversion", "struct R{int n;operator int()const{return n;}};"},
      {"operator", "struct R{int n;int operator()()const{return n;}};"},
      {"volatile-method", "struct R{int n;int get()volatile{return n;}};"},
      {"rvalue-method", "struct R{int n;int get()&&{return n;}};"},
      {"noexcept-method", "struct R{int n;int get()const noexcept{return n;}};"},
      {"mutable-field", "struct R{mutable int n;int get()const{return n;}};"},
      {"reference-field", "struct R{int&n;int get()const{return n;}};"},
      {"member-template", "struct R{int n;template<class T>T get(T v){return v;}};"},
      {"static-data", "struct R{int n;static int value;int get(){return value;}};int R::value=1;"},
      {"default-argument", "struct R{int n;int get(int v=1){return n+v;}};int f(){R r{1};return r.get();}"},
      {"constant-static-data", "struct R{int n;static const int value=1;int get(){return value;}};"},
      {"method-temporary-reference-argument", "struct R{int n;int get(const int&v){return n+v;}};int f(){R r{1};return r.get(2);}"},
      {"static-temporary-reference-argument", "struct R{int n;static int get(const int&v){return v;}};int f(){return R::get(2);}"},
      {"method-comma-callee", "struct R{int n;static int get(){return 1;}};int f(){return (0,R::get)();}"},
      {"temporary-reverse-arrow-offset", "struct E{int n;int get()const{return n;}};struct H{E a[1];};int f(){return (0+H{{{1}}}.a)->get();}"}};
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
