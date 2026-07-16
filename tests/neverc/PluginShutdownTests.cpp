#include "NeverCTestFixture.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace {

class ScopedShutdownTrace {
public:
  explicit ScopedShutdownTrace(const fs::path &Path) {
    if (const char *Current = std::getenv("NEVERC_PLUGIN_TRACE_FILE")) {
      HadPrevious = true;
      Previous = Current;
    }
    set(Path.string());
  }

  ~ScopedShutdownTrace() {
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

class ScopedShutdownFailure {
public:
  explicit ScopedShutdownFailure(const std::string &Value) {
    if (const char *Current =
            std::getenv("NEVERC_PLUGIN_SHUTDOWN_FAIL")) {
      HadPrevious = true;
      Previous = Current;
    }
#if defined(_WIN32)
    _putenv_s("NEVERC_PLUGIN_SHUTDOWN_FAIL", Value.c_str());
#else
    setenv("NEVERC_PLUGIN_SHUTDOWN_FAIL", Value.c_str(), 1);
#endif
  }

  ~ScopedShutdownFailure() {
#if defined(_WIN32)
    _putenv_s("NEVERC_PLUGIN_SHUTDOWN_FAIL",
              HadPrevious ? Previous.c_str() : "");
#else
    if (HadPrevious)
      setenv("NEVERC_PLUGIN_SHUTDOWN_FAIL", Previous.c_str(), 1);
    else
      unsetenv("NEVERC_PLUGIN_SHUTDOWN_FAIL");
#endif
  }

private:
  bool HadPrevious = false;
  std::string Previous;
};

std::vector<std::string> traceLines(const std::string &Text) {
  std::vector<std::string> Result;
  std::istringstream Input(Text);
  for (std::string Line; std::getline(Input, Line);)
    if (!Line.empty())
      Result.push_back(std::move(Line));
  return Result;
}

void expectCompleteShutdown(const std::string &Text) {
  const std::vector<std::string> Events = traceLines(Text);
  ASSERT_FALSE(Events.empty());
  const auto SessionEnd =
      std::find(Events.begin(), Events.end(), "session_end");
  const auto Destroy = std::find(Events.begin(), Events.end(), "destroy");
  ASSERT_NE(SessionEnd, Events.end());
  ASSERT_NE(Destroy, Events.end());
  EXPECT_LT(SessionEnd, Destroy);
  EXPECT_EQ(Events.back(), "destroy");

  size_t TaskBegins = 0;
  size_t TaskEnds = 0;
  for (auto It = Events.begin(); It != Events.end(); ++It) {
    if (It->rfind("task_begin:", 0) == 0)
      ++TaskBegins;
    if (It->rfind("task_end:", 0) == 0) {
      ++TaskEnds;
      EXPECT_LT(It, SessionEnd);
    }
  }
  EXPECT_GT(TaskBegins, 0u);
  EXPECT_EQ(TaskBegins, TaskEnds);
}

} // namespace

class PluginShutdownTest : public NeverCTest {
protected:
  std::string pluginArgument() const {
    return std::string("-fplugin=") + NEVERC_TEST_SHUTDOWN_PLUGIN;
  }
};

TEST_F(PluginShutdownTest, CleansUpSuccessfulCompilationBeforeExit) {
  const fs::path Source = tmpFile("shutdown_success.c");
  const fs::path Trace = tmpFile("shutdown_success.trace");
  writeFile(Source, "int shutdown_success;\n");
  writeFile(Trace, "");
  CmdResult Result;
  {
    ScopedShutdownTrace Scope(Trace);
    Result = ncc({pluginArgument(), "-fsyntax-only", Source.string()});
  }
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  expectCompleteShutdown(readFile(Trace));
}

TEST_F(PluginShutdownTest, CleansUpAfterDriverArgumentError) {
  const fs::path Trace = tmpFile("shutdown_driver_error.trace");
  writeFile(Trace, "");
  CmdResult Result;
  {
    ScopedShutdownTrace Scope(Trace);
    Result = ncc(
        {pluginArgument(), "--neverc-invalid-shutdown-option"});
  }
  ASSERT_NE(Result.exitCode, 0);
  expectCompleteShutdown(readFile(Trace));
}

TEST_F(PluginShutdownTest, CleansUpAfterFrontendError) {
  const fs::path Source = tmpFile("shutdown_frontend_error.c");
  const fs::path Trace = tmpFile("shutdown_frontend_error.trace");
  writeFile(Source, "int broken_shutdown( { return 0; }\n");
  writeFile(Trace, "");
  CmdResult Result;
  {
    ScopedShutdownTrace Scope(Trace);
    Result = ncc({pluginArgument(), "-fsyntax-only", Source.string()});
  }
  ASSERT_NE(Result.exitCode, 0);
  expectCompleteShutdown(readFile(Trace));
}

TEST_F(PluginShutdownTest, CleansUpAfterLinkerError) {
  const fs::path Source = tmpFile("shutdown_linker_error.c");
  const fs::path Output = tmpFile("shutdown_linker_error");
  const fs::path Trace = tmpFile("shutdown_linker_error.trace");
  writeFile(Source,
            "extern int missing_shutdown_symbol(void);"
            "int main(void) { return missing_shutdown_symbol(); }\n");
  writeFile(Trace, "");
  std::vector<std::string> Arguments = {
      pluginArgument(), Source.string(), "-o", Output.string()};
  std::vector<std::string> LinkArguments = linkFlags();
  Arguments.insert(Arguments.end(), LinkArguments.begin(),
                   LinkArguments.end());
  CmdResult Result;
  {
    ScopedShutdownTrace Scope(Trace);
    Result = ncc(Arguments);
  }
  ASSERT_NE(Result.exitCode, 0);
  expectCompleteShutdown(readFile(Trace));
}

TEST_F(PluginShutdownTest, CleansUpImmediateHelp) {
  const fs::path Trace = tmpFile("shutdown_help.trace");
  writeFile(Trace, "");
  CmdResult Result;
  {
    ScopedShutdownTrace Scope(Trace);
    Result = ncc({pluginArgument(), "--help"});
  }
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  expectCompleteShutdown(readFile(Trace));
}

TEST_F(PluginShutdownTest, CleansUpDirectCC1) {
  const fs::path Source = tmpFile("shutdown_direct.c");
  const fs::path Trace = tmpFile("shutdown_direct.trace");
  writeFile(Source, "int shutdown_direct;\n");
  writeFile(Trace, "");
  CmdResult Result;
  {
    ScopedShutdownTrace Scope(Trace);
    Result = exec(neverc().string(),
                  {"-cc1", pluginArgument(), "-triple", hostTriple(),
                   "-fsyntax-only", "-x", "c", Source.string()});
  }
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  expectCompleteShutdown(readFile(Trace));
}

TEST_F(PluginShutdownTest, CleanupFailureTurnsSuccessIntoFailure) {
  const fs::path Source = tmpFile("shutdown_cleanup_failure.c");
  const fs::path Trace = tmpFile("shutdown_cleanup_failure.trace");
  writeFile(Source, "int shutdown_cleanup_failure;\n");
  writeFile(Trace, "");
  CmdResult Result;
  {
    ScopedShutdownTrace TraceScope(Trace);
    ScopedShutdownFailure FailureScope("session_end");
    Result = ncc({pluginArgument(), "-fsyntax-only", Source.string()});
  }
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("plugin runtime cleanup failed"),
            std::string::npos)
      << Result.err;
  expectCompleteShutdown(readFile(Trace));
}

TEST_F(PluginShutdownTest, CleanupFailurePreservesCompilationFailure) {
  const fs::path Source = tmpFile("shutdown_double_failure.c");
  const fs::path Trace = tmpFile("shutdown_double_failure.trace");
  writeFile(Source, "int broken_shutdown_cleanup( { return 0; }\n");
  writeFile(Trace, "");
  CmdResult Result;
  {
    ScopedShutdownTrace TraceScope(Trace);
    ScopedShutdownFailure FailureScope("destroy");
    Result = ncc({pluginArgument(), "-fsyntax-only", Source.string()});
  }
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("plugin runtime cleanup failed"),
            std::string::npos)
      << Result.err;
  EXPECT_NE(Result.err.find("expected"), std::string::npos)
      << Result.err;
  expectCompleteShutdown(readFile(Trace));
}
