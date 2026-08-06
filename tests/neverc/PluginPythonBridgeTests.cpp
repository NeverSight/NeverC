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
