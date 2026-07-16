#include "NeverCTestFixture.h"
#include <cstdlib>

namespace {

class ScopedPluginTracePath {
public:
  explicit ScopedPluginTracePath(const fs::path &Path) {
    if (const char *Current = std::getenv("NEVERC_PLUGIN_TRACE_FILE")) {
      HadPrevious = true;
      Previous = Current;
    }
    set(Path.string());
  }

  ~ScopedPluginTracePath() {
    if (HadPrevious)
      set(Previous);
    else
      clear();
  }

private:
  static void set(const std::string &Value) {
#if defined(_WIN32)
    _putenv_s("NEVERC_PLUGIN_TRACE_FILE", Value.c_str());
#else
    setenv("NEVERC_PLUGIN_TRACE_FILE", Value.c_str(), 1);
#endif
  }

  static void clear() {
#if defined(_WIN32)
    _putenv_s("NEVERC_PLUGIN_TRACE_FILE", "");
#else
    unsetenv("NEVERC_PLUGIN_TRACE_FILE");
#endif
  }

  bool HadPrevious = false;
  std::string Previous;
};

} // namespace

class PluginJobExecutionTest : public NeverCTest {};

TEST_F(PluginJobExecutionTest,
       ReplacementBypassesBuiltinFrontendForNoOutputJob) {
  const fs::path Source = tmpFile("job_execution_replace.c");
  const fs::path Trace = tmpFile("job_execution_replace.trace");
  writeFile(Source, "int deliberately_invalid( { return 0; }\n");
  writeFile(Trace, "");

  CmdResult Result;
  {
    ScopedPluginTracePath TracePath(Trace);
    Result = ncc(
        {std::string("-fplugin=") +
             NEVERC_TEST_REPLACE_FRONTEND_JOB_PLUGIN,
         "-fsyntax-only", Source.string()});
  }

  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(Trace).find("execute-job:replacement"),
            std::string::npos);
}

TEST_F(PluginJobExecutionTest,
       ReplacementPublishesDeclaredOutputThroughHostSealedGate) {
  const fs::path Source = tmpFile("job_execution_output.c");
  const fs::path Output = tmpFile("job_execution_output.o");
  writeFile(Source, "int output_job(void) { return 0; }\n");
  std::error_code Error;
  fs::remove(Output, Error);

  CmdResult Result =
      ncc({std::string("-fplugin=") +
               NEVERC_TEST_OUTPUT_JOB_PLUGIN,
           "-c", Source.string(), "-o", Output.string()});

  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  ASSERT_TRUE(fs::exists(Output));
  EXPECT_EQ(readFile(Output), "plugin-object");
}

TEST_F(PluginJobExecutionTest,
       ReplacementCannotClaimAStalePreexistingOutputWithoutSeal) {
  const fs::path Source = tmpFile("job_execution_missing_output.c");
  const fs::path Output = tmpFile("job_execution_missing_output.o");
  writeFile(Source, "int missing_output_job(void) { return 0; }\n");
  writeFile(Output, "preexisting-output");

  CmdResult Result =
      ncc({std::string("-fplugin=") +
               NEVERC_TEST_OUTPUT_JOB_MISSING_PLUGIN,
           "-c", Source.string(), "-o", Output.string()});

  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("exactly the declared outputs"),
            std::string::npos)
      << Result.err;
  ASSERT_TRUE(fs::exists(Output));
  EXPECT_EQ(readFile(Output), "preexisting-output");
}

TEST_F(PluginJobExecutionTest,
       BuiltinFrontendFailurePreservesPreexistingOutput) {
  const fs::path Source = tmpFile("builtin_output_failure.c");
  const fs::path Output = tmpFile("builtin_output_failure.o");
  writeFile(Source, "int deliberately_invalid( { return 0; }\n");
  writeFile(Output, "preexisting-output");

  CmdResult Result =
      ncc({"-c", Source.string(), "-o", Output.string()});

  EXPECT_NE(Result.exitCode, 0);
  ASSERT_TRUE(fs::exists(Output));
  EXPECT_EQ(readFile(Output), "preexisting-output");
}

TEST_F(PluginJobExecutionTest,
       ReplacementRejectsAndAbortsUndeclaredExtraOutput) {
  const fs::path Source = tmpFile("job_execution_extra_output.c");
  const fs::path Output = tmpFile("job_execution_extra_output.o");
  const fs::path Extra = Output.string() + ".extra";
  writeFile(Source, "int extra_output_job(void) { return 0; }\n");
  std::error_code Error;
  fs::remove(Output, Error);
  fs::remove(Extra, Error);

  CmdResult Result =
      ncc({std::string("-fplugin=") +
               NEVERC_TEST_OUTPUT_JOB_EXTRA_PLUGIN,
           "-c", Source.string(), "-o", Output.string()});

  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("exactly the declared outputs"),
            std::string::npos)
      << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_FALSE(fs::exists(Extra));
}

TEST_F(PluginJobExecutionTest,
       ProviderFailurePropagatesThroughFailingCommand) {
  const fs::path Source = tmpFile("job_execution_failure.c");
  const fs::path Trace = tmpFile("job_execution_failure.trace");
  writeFile(Source, "int valid_input(void) { return 0; }\n");
  writeFile(Trace, "");

  CmdResult Result;
  {
    ScopedPluginTracePath TracePath(Trace);
    Result =
        ncc({std::string("-fplugin=") + NEVERC_TEST_FAIL_JOB_PLUGIN,
             "-fsyntax-only", Source.string()});
  }

  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("execute_job"), std::string::npos)
      << Result.err;
  EXPECT_NE(readFile(Trace).find("execute-job:failure"),
            std::string::npos);
}
