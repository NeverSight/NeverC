#include "NeverCTestFixture.h"
#include <cstdlib>

namespace {

class ScopedDiagnosticMode {
public:
  explicit ScopedDiagnosticMode(const char *Value) {
    if (const char *Current =
            std::getenv("NEVERC_TEST_DIAGNOSTIC_MODE")) {
      HadPrevious = true;
      Previous = Current;
    }
#if defined(_WIN32)
    _putenv_s("NEVERC_TEST_DIAGNOSTIC_MODE", Value);
#else
    setenv("NEVERC_TEST_DIAGNOSTIC_MODE", Value, 1);
#endif
  }

  ~ScopedDiagnosticMode() {
#if defined(_WIN32)
    _putenv_s("NEVERC_TEST_DIAGNOSTIC_MODE",
              HadPrevious ? Previous.c_str() : "");
#else
    if (HadPrevious)
      setenv("NEVERC_TEST_DIAGNOSTIC_MODE", Previous.c_str(), 1);
    else
      unsetenv("NEVERC_TEST_DIAGNOSTIC_MODE");
#endif
  }

private:
  bool HadPrevious = false;
  std::string Previous;
};

} // namespace

class PluginDiagnosticDriverTest : public NeverCTest {
protected:
  std::string pluginArgument() const {
    return std::string("-fplugin=") + NEVERC_TEST_DIAGNOSTIC_PLUGIN;
  }

  CmdResult compileWithMode(const char *Mode) {
    const fs::path Source = tmpFile("plugin_diagnostic.c");
    writeFile(Source, "int plugin_diagnostic;\n");
    ScopedDiagnosticMode Scope(Mode);
    return ncc({pluginArgument(), "-fsyntax-only", Source.string()});
  }
};

TEST_F(PluginDiagnosticDriverTest, RendersWarningCodeAndNotes) {
  CmdResult Result = compileWithMode("warning");
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  size_t Warning = Result.err.find(
      "plugin 'org.neverc.test.diagnostic' in phase "
      "'neverc.driver.raw_arguments' [plugin-7001]: diagnostic warning");
  size_t FirstNote = Result.err.find("diagnostic note one");
  size_t SecondNote = Result.err.find("diagnostic note two");
  ASSERT_NE(Warning, std::string::npos) << Result.err;
  ASSERT_NE(FirstNote, std::string::npos) << Result.err;
  ASSERT_NE(SecondNote, std::string::npos) << Result.err;
  EXPECT_LT(Warning, FirstNote);
  EXPECT_LT(FirstNote, SecondNote);
}

TEST_F(PluginDiagnosticDriverTest, ErrorDiagnosticTurnsSuccessIntoFailure) {
  CmdResult Result = compileWithMode("error");
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(
      Result.err.find(
          "plugin 'org.neverc.test.diagnostic' in phase "
          "'neverc.driver.parsed_arguments' [plugin-7002]: "
          "diagnostic error"),
      std::string::npos)
      << Result.err;
  EXPECT_NE(Result.err.find("diagnostic note one"), std::string::npos)
      << Result.err;
}

TEST_F(PluginDiagnosticDriverTest,
       SynthesizesDiagnosticForUnreportedCallbackFailure) {
  CmdResult Result = compileWithMode("implicit");
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("plugin callback 'SessionEnd' failed without "
                            "a structured diagnostic"),
            std::string::npos)
      << Result.err;
  EXPECT_NE(Result.err.find("plugin phase failed: plugin runtime cleanup "
                            "failed:"),
            std::string::npos)
      << Result.err;
}

TEST_F(PluginDiagnosticDriverTest, FatalDiagnosticCancelsCompilation) {
  CmdResult Result = compileWithMode("fatal");
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(
      Result.err.find(
          "plugin 'org.neverc.test.diagnostic' in phase "
          "'neverc.driver.execute_job' [plugin-7003]: diagnostic fatal"),
      std::string::npos)
      << Result.err;
}

TEST_F(PluginDiagnosticDriverTest, LoaderFailureUsesDedicatedDiagnostic) {
  CmdResult Result =
      ncc({"-fplugin=/definitely/missing/neverc-plugin.so",
           "-fsyntax-only"});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("plugin load failed:"), std::string::npos)
      << Result.err;
  EXPECT_EQ(Result.err.find("invalid value"), std::string::npos)
      << Result.err;
}

TEST_F(PluginDiagnosticDriverTest,
       ABIMismatchUsesNegotiationDiagnostic) {
  CmdResult Result =
      ncc({std::string("-fplugin=") +
               NEVERC_TEST_DRIVER_WRONG_ABI_PLUGIN,
           "-fsyntax-only"});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("plugin ABI negotiation failed:"),
            std::string::npos)
      << Result.err;
  EXPECT_NE(Result.err.find("ABI major does not match"),
            std::string::npos)
      << Result.err;
}
