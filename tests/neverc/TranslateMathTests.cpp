#include "../../neverc/lib/Translate/Cpp/CppSdk.h"
#include "../../neverc/lib/Translate/JSON.h"
#include "NeverCTestFixture.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>
#include <optional>

namespace {
using namespace neverc::translate;
class ScopedMathEnvironment {
  std::string Name;
  std::optional<std::string> Previous;
  static void set(const std::string &Name, const char *Value) {
#ifdef _WIN32
    _putenv_s(Name.c_str(), Value ? Value : "");
#else
    if (Value)
      setenv(Name.c_str(), Value, 1);
    else
      unsetenv(Name.c_str());
#endif
  }

public:
  ScopedMathEnvironment(const char *Name, const std::string &Value)
      : Name(Name) {
    if (const char *Old = std::getenv(Name))
      Previous = Old;
    set(this->Name, Value.c_str());
  }
  ~ScopedMathEnvironment() {
    set(Name, Previous ? Previous->c_str() : nullptr);
  }
};

class TranslateMathTest : public NeverCTest {
protected:
  std::string environment(const char *Name) {
    const char *Value = std::getenv(Name);
    return Value ? Value : "";
  }
  std::string target() {
    auto Result = ncc({"-dumpmachine"});
    EXPECT_EQ(Result.exitCode, 0) << Result.err;
    if (Result.out.find("apple") == std::string::npos)
      return "";
    return Result.out.find("arm64") != std::string::npos ||
                   Result.out.find("aarch64") != std::string::npos
               ? "arm64-apple-macosx15.0.0"
               : "x86_64-apple-macosx15.0.0";
  }
  bool configured() {
    return !environment("NEVERC_CPP_FRONTEND").empty() &&
           !environment("NEVERC_CPP_SDK").empty() && !target().empty();
  }
  fs::path fixture(const std::string &Name) {
    return testDir() / "Inputs" / "translate" / "cpp" / "stdlib" / Name;
  }
  fs::path project(const std::string &Name) {
    const auto Root = tmpFile(Name);
    fs::create_directory(Root);
    writeFile(Root / "math.cpp", readFile(fixture("math.cpp")));
    llvm::json::Array Entries{llvm::json::Object{
        {"directory", jsonString(Root.string())},
        {"file", "math.cpp"},
        {"arguments", llvm::json::Array{"clang++", "-std=c++17", "-c",
                                        "math.cpp", "-o", "math.o"}}}};
    std::string Text;
    llvm::raw_string_ostream OS(Text);
    OS << llvm::formatv("{0:2}", llvm::json::Value(std::move(Entries)));
    writeFile(Root / "compile_commands.json", Text);
    return Root;
  }
  std::vector<std::string> args(const fs::path &Root) {
    return {"translate",
            "--from",
            "cpp",
            "--profile",
            "cpp-math-v1",
            "--target",
            target(),
            "--project-root",
            Root.string(),
            "--compdb",
            (Root / "compile_commands.json").string(),
            "--frontend",
            environment("NEVERC_CPP_FRONTEND"),
            "--cpp-sdk",
            environment("NEVERC_CPP_SDK"),
            (Root / "math.cpp").string()};
  }
  void reject(const CmdResult &Result, const fs::path &Output,
              const char *Code) {
    EXPECT_NE(Result.exitCode, 0) << Result.out;
    EXPECT_TRUE(Result.stderrContains(Code)) << Result.err;
    EXPECT_FALSE(fs::exists(Output));
  }
};

TEST_F(TranslateMathTest,
       SourceToStandaloneModuleMatchesPinnedCppAndFloatingEnvironment) {
  if (!configured() || environment("NEVERC_CPP_REFERENCE_COMPILER").empty())
    GTEST_SKIP();
  const auto Root = project("math-contract"), Output = Root / "generated";
  auto Args = args(Root);
  Args.insert(Args.end(), {"--out-dir", Output.string()});
  auto Translation = ncc(Args);
  ASSERT_EQ(Translation.exitCode, 0) << Translation.err;
  auto Manifest =
      llvm::json::parse(readFile(Output / "translate-manifest.json"));
  ASSERT_TRUE(bool(Manifest));
  const auto *Object = Manifest->getAsObject();
  ASSERT_NE(Object, nullptr);
  EXPECT_EQ(Object->getString("fp_contract"), CppMathFPContractID);
  ASSERT_NE(Object->getObject("sdk"), nullptr);
  EXPECT_EQ(Object->getObject("sdk")->getString("catalog_sha256"),
            approvedCppSdkCatalogSHA256());
  ASSERT_NE(Object->getArray("mappings"), nullptr);
  EXPECT_EQ(Object->getArray("mappings")->size(), 2u);
  const auto *Runtime = Object->getObject("runtime_capabilities");
  ASSERT_NE(Runtime, nullptr);
  ASSERT_NE(Runtime->getArray("modules"), nullptr);
  EXPECT_EQ(Runtime->getArray("modules")->size(), 2u);
  EXPECT_EQ(Object->getArray("required_headers")->size(), 2u);
  CppSdkContext SDK;
  Diagnostics D;
  ASSERT_TRUE(loadCppSdk(environment("NEVERC_CPP_SDK"), target(), SDK, D));
  for (const auto &SDKRoot : SDK.Roots)
    EXPECT_EQ(readFile(Output / "translated.nc").find(SDKRoot.AbsolutePath),
              std::string::npos);
  for (const std::string Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto ReferenceObject = Root / ("reference" + Optimization + ".o");
    std::vector<std::string> ReferenceArgs{
        "--target=" + target(),
        "-std=c++17",
        Optimization,
        "-nostdinc++",
        "-Dtranslate_math_abs=reference_abs",
        "-Dtranslate_math_floor=reference_floor"};
    for (const auto &SDKRoot : SDK.Roots) {
      if (SDKRoot.Name == "libcxx")
        ReferenceArgs.insert(ReferenceArgs.end(),
                             {"-isystem", SDKRoot.AbsolutePath});
      if (SDKRoot.Name == "resource")
        ReferenceArgs.insert(ReferenceArgs.end(),
                             {"-resource-dir", SDKRoot.AbsolutePath});
      if (SDKRoot.Name == "platform")
        ReferenceArgs.insert(ReferenceArgs.end(),
                             {"-isysroot", SDKRoot.AbsolutePath});
    }
    ReferenceArgs.insert(
        ReferenceArgs.end(),
        {"-c", (Root / "math.cpp").string(), "-o", ReferenceObject.string()});
    auto Reference =
        exec(environment("NEVERC_CPP_REFERENCE_COMPILER"), ReferenceArgs);
    ASSERT_EQ(Reference.exitCode, 0) << Reference.err;
    const auto GeneratedObject = Root / ("generated" + Optimization + ".o");
    auto Generated = ncc({"--target=" + target(), "-std=c23", Optimization,
                          "-fno-lto", "-c", (Output / "translated.nc").string(),
                          "-o", GeneratedObject.string()});
    ASSERT_EQ(Generated.exitCode, 0) << Generated.err;
    const auto Executable = Root / ("contract" + Optimization);
    auto Link = exec(environment("NEVERC_CPP_REFERENCE_COMPILER"),
                     {"--target=" + target(), "-x", "c", "-std=c17",
                      Optimization, fixture("math-contract-harness.c").string(),
                      "-x", "none", ReferenceObject.string(),
                      GeneratedObject.string(), "-o", Executable.string()});
    ASSERT_EQ(Link.exitCode, 0) << Link.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
    EXPECT_TRUE(Run.contains("math-contract cases=32944 failures=0"))
        << Run.out;
  }
}

TEST_F(TranslateMathTest,
       CheckUsesCapabilityAndLinkGatesAndPublishesOnlyReport) {
  if (!configured())
    GTEST_SKIP();
  const auto Root = project("math-check"), Report = Root / "report.json";
  auto Args = args(Root);
  Args.insert(Args.end(), {"--check", "--report", Report.string()});
  auto Result = ncc(Args);
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  const auto Text = readFile(Report);
  EXPECT_NE(Text.find("cpp.math.fabs.f64.v1"), std::string::npos);
  EXPECT_NE(Text.find("cpp.math.floor.f64.v1"), std::string::npos);
  EXPECT_FALSE(fs::exists(Root / "translated.nc"));
  EXPECT_FALSE(fs::exists(Root / "translate-manifest.json"));
}

TEST_F(TranslateMathTest, DisabledRuntimeRejectsBeforePublication) {
  if (!configured())
    GTEST_SKIP();
  const auto Root = project("math-disabled"), Output = Root / "generated";
  auto Args = args(Root);
  Args.insert(Args.end(), {"--out-dir", Output.string(), "-fno-builtin-std"});
  reject(ncc(Args), Output, "TR0403");
}

TEST_F(TranslateMathTest, MissingSdkAndUnapprovedTargetNeverPublish) {
  if (!configured())
    GTEST_SKIP();
  const auto Root = project("math-missing"), Output = Root / "generated";
  auto Args = args(Root);
  for (size_t I = 0; I + 1 < Args.size(); ++I)
    if (Args[I] == "--cpp-sdk")
      Args[I + 1] = (Root / "missing.json").string();
  Args.insert(Args.end(), {"--out-dir", Output.string()});
  reject(ncc(Args), Output, "TR0101");
  Args = args(Root);
  for (size_t I = 0; I + 1 < Args.size(); ++I)
    if (Args[I] == "--target")
      Args[I + 1] = "x86_64-apple-macosx14.0.0";
  Args.insert(Args.end(), {"--out-dir", Output.string()});
  reject(ncc(Args), Output, "TR0204");
}

TEST_F(TranslateMathTest,
       AmbientIncludePathsCannotReplaceApprovedRuntimeHeader) {
  if (!configured())
    GTEST_SKIP();
  const auto Root = project("math-environment"),
             Shadow = tmpFile("shadow-includes");
  fs::create_directories(Shadow / "neverc" / "std");
  writeFile(Shadow / "neverc" / "std" / "math.h",
            "#error NEVERC_TRANSLATE_UNAPPROVED_RUNTIME_HEADER\n");
  for (const char *Name : {"CPATH", "C_INCLUDE_PATH"}) {
    SCOPED_TRACE(Name);
    ScopedMathEnvironment Environment(Name, Shadow.string());
    const auto Output = Root / Name;
    auto Args = args(Root);
    Args.insert(Args.end(), {"--out-dir", Output.string()});
    auto Result = ncc(Args);
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    const auto Text = readFile(Output / "translate-manifest.json");
    EXPECT_EQ(Text.find(Shadow.string()), std::string::npos);
    EXPECT_NE(Text.find("--no-default-config"), std::string::npos);
    EXPECT_NE(
        Text.find(
            "2124e02ddab13569fb310376913cda90c3c9effd11daf381b9dcf50c4a2d5093"),
        std::string::npos);
  }
}

TEST_F(TranslateMathTest, AmbientIncludePathsCannotAdmitUnownedSourceHeaders) {
  if (!configured())
    GTEST_SKIP();
  const auto Root = project("math-unowned-environment"),
             External = tmpFile("unowned-includes");
  fs::create_directory(External);
  writeFile(External / "environment-only.h",
            "inline int hidden_input() { return 42; }\n");
  writeFile(
      Root / "math.cpp",
      "#include <environment-only.h>\nint main() { return hidden_input(); }\n");
  ScopedMathEnvironment Environment("CPATH", External.string());
  const auto Output = Root / "generated";
  auto Args = args(Root);
  Args.insert(Args.end(), {"--out-dir", Output.string()});
  reject(ncc(Args), Output, "TR0203");
}

TEST_F(TranslateMathTest,
       AdjacentDefaultCompilerConfigurationCannotAffectValidation) {
#ifdef _WIN32
  GTEST_SKIP() << "Initial pinned math SDK executes on macOS.";
#else
  if (!configured())
    GTEST_SKIP();
  const auto Root = project("math-default-config"),
             Toolchain = tmpFile("isolated-toolchain");
  fs::create_directories(Toolchain / "bin");
  const auto Compiler = Toolchain / "bin" / "neverc";
  std::error_code Error;
  fs::create_hard_link(neverc(), Compiler, Error);
  if (Error) {
    Error.clear();
    fs::copy_file(neverc(), Compiler, fs::copy_options::none, Error);
  }
  ASSERT_FALSE(Error) << Error.message();
  auto InstalledResources = ncc({"--no-default-config", "-print-resource-dir"});
  auto IsolatedResources =
      exec(Compiler.string(), {"--no-default-config", "-print-resource-dir"});
  ASSERT_EQ(InstalledResources.exitCode, 0) << InstalledResources.err;
  ASSERT_EQ(IsolatedResources.exitCode, 0) << IsolatedResources.err;
  const fs::path Installed =
      llvm::StringRef(InstalledResources.out).trim().str();
  const fs::path Isolated = llvm::StringRef(IsolatedResources.out).trim().str();
  ASSERT_NE(Installed, Isolated);
  fs::create_directories(Isolated.parent_path());
  fs::create_directory_symlink(Installed, Isolated);
  const auto Poison = Toolchain / "configuration-poison.h";
  writeFile(Poison, "#error NEVERC_TRANSLATE_DEFAULT_CONFIG_LOADED\n");
  writeFile(Toolchain / "bin" / "neverc.cfg",
            "-include \"" + Poison.string() + "\"\n");
  writeFile(Toolchain / "probe.nc", "int probe(void) { return 0; }\n");
  ScopedMathEnvironment DefaultConfig("NEVERC_NO_DEFAULT_CONFIG", "");
  auto Baseline = exec(Compiler.string(),
                       {"-fsyntax-only", (Toolchain / "probe.nc").string()});
  ASSERT_NE(Baseline.exitCode, 0);
  ASSERT_TRUE(Baseline.stderrContains("NEVERC_TRANSLATE_DEFAULT_CONFIG_LOADED"))
      << Baseline.err;
  auto Args = args(Root);
  Args.push_back("--check");
  auto Result = exec(Compiler.string(), Args);
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
#endif
}
} // namespace
