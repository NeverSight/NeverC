#include "NeverCTestFixture.h"

class PluginPythonBridgeTest : public NeverCTest {
protected:
  fs::path source(const std::string &Name) {
    fs::path Source = tmpFile(Name);
    writeFile(Source, "int python_plugin_test(void) { return 42; }\n");
    return Source;
  }
};

TEST_F(PluginPythonBridgeTest, LoadsPythonPluginAndObservesRawArguments) {
  fs::path Source = source("python_driver_trace.c");
  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PYTHON_DRIVER_TRACE_PLUGIN,
           "--python-driver-trace", "--python-driver-mode", "compact",
           "-fsyntax-only", Source.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(
      Result.err.find("plugin 'org.neverc.test.python-driver-trace' in phase "
                      "'neverc.driver.raw_arguments' [plugin-8101]: python raw "
                      "arguments (before): mode=compact count="),
      std::string::npos)
      << Result.err;
  EXPECT_NE(Result.err.find("Python retained frame was rejected as stale"),
            std::string::npos)
      << Result.err;
}

TEST_F(PluginPythonBridgeTest, PythonOptionIsRejectedWithoutItsPlugin) {
  fs::path Source = source("python_option_unregistered.c");
  CmdResult Result =
      ncc({"--python-driver-trace", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_EQ(Result.err.find("python raw arguments"), std::string::npos)
      << Result.err;
}

TEST_F(PluginPythonBridgeTest, ObserverExceptionProducesTracebackDiagnostic) {
  fs::path Source = source("python_observer_exception.c");
  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PYTHON_EXCEPTION_PLUGIN,
           "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("intentional Python observer explosion"),
            std::string::npos)
      << Result.err;
  EXPECT_NE(Result.err.find("Traceback"), std::string::npos) << Result.err;
  EXPECT_NE(Result.err.find("ExceptionPlugin.py"), std::string::npos)
      << Result.err;
}

TEST_F(PluginPythonBridgeTest, LoadsTwoPythonPluginsTogether) {
  fs::path Source = source("two_python_plugins.c");
  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PYTHON_MINIMAL_PLUGIN,
           std::string("-fplugin=") + NEVERC_TEST_PYTHON_DRIVER_TRACE_PLUGIN,
           "--python-driver-trace", "-fsyntax-only", Source.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(Result.err.find("python raw arguments"), std::string::npos)
      << Result.err;
}

TEST_F(PluginPythonBridgeTest, LoadsNativeAndPythonPluginsTogether) {
  fs::path Source = source("mixed_native_python_plugins.c");
  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_EMPTY_PLUGIN,
           std::string("-fplugin=") + NEVERC_TEST_PYTHON_DRIVER_TRACE_PLUGIN,
           "--python-driver-trace", "-fsyntax-only", Source.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(Result.err.find("python raw arguments"), std::string::npos)
      << Result.err;
}

TEST_F(PluginPythonBridgeTest, ExposesCapabilitiesAndRejectsStaleViews) {
  fs::path Source = source("python_ffi_capabilities.c");
  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PYTHON_FFI_CAPABILITY_PLUGIN,
           "-fsyntax-only", Source.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(Result.err.find("Python FFI capabilities and stale guards passed"),
            std::string::npos)
      << Result.err;
}

TEST_F(PluginPythonBridgeTest, RunsGeneratedStatusAndVoidCallbackTrampolines) {
  fs::path Source = source("python_raw_ffi_callback.c");
  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_PYTHON_RAW_FFI_CALLBACK_PLUGIN,
       "-fsyntax-only", Source.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(Result.err.find("Python generated callback trampoline passed"),
            std::string::npos)
      << Result.err;
}

TEST_F(PluginPythonBridgeTest,
       PythonOLLVMTransformsRunsAndIsCrossTargetDeterministic) {
  const fs::path Source = tmpFile("python_ollvm.c");
  writeFile(
      Source,
      "int ollvm_transform(int x, int y) {\n"
      "  int result = x + y;\n"
      "  if ((x & 1) != 0) result ^= y; else result |= y;\n"
      "  return result;\n"
      "}\n"
      "int main(void) { return ollvm_transform(7, 11) == 25 ? 0 : 1; }\n");
  const std::string Plugin =
      std::string("-fplugin=") + NEVERC_TEST_PYTHON_OLLVM_PLUGIN;
  const std::vector<std::string> OLLVM = {
      Plugin,         "--ollvm-sub", "--ollvm-bcf",     "--ollvm-fla",
      "--ollvm-seed", "42",          "--ollvm-include", "ollvm_transform"};

  for (const std::string &Optimization : {"-O0", "-O2"}) {
    fs::path Executable = tmpFile("python_ollvm" + Optimization.substr(1));
    std::vector<std::string> Arguments = OLLVM;
    Arguments.push_back(Optimization);
    Arguments.push_back(Source.string());
    Arguments.push_back("-o");
    Arguments.push_back(Executable.string());
    CmdResult Compile = ncc(Arguments);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.err;
    CmdResult Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }

  std::vector<fs::path> Outputs;
  for (const std::string &Target :
       {"x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu",
        "x86_64-unknown-linux-gnu"}) {
    fs::path IR =
        tmpFile("python_ollvm_" + std::to_string(Outputs.size()) + ".ll");
    std::vector<std::string> Arguments = OLLVM;
    // Assertions-off builds default to -fdiscard-value-names, while these
    // checks intentionally prove that each named Python transformation ran.
    Arguments.insert(Arguments.end(),
                     {"--target=" + Target, "-O0",
                      "-fno-discard-value-names", "-S", "-emit-llvm",
                      Source.string(), "-o", IR.string()});
    CmdResult Emit = ncc(Arguments);
    ASSERT_EQ(Emit.exitCode, 0) << Emit.err;
    const std::string Module = readFile(IR);
    EXPECT_NE(Module.find("ollvm.sub"), std::string::npos);
    EXPECT_NE(Module.find("ollvm.bcf.gate"), std::string::npos);
    EXPECT_NE(Module.find("ollvm.fla.dispatch"), std::string::npos);
    Outputs.push_back(IR);
  }
  EXPECT_EQ(readFile(Outputs[0]), readFile(Outputs[2]));
}
