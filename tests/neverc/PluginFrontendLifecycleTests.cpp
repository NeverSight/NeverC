#include "NeverCTestFixture.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

class ScopedEnvironment {
public:
  ScopedEnvironment(const char *NameValue, const std::string &Value)
      : Name(NameValue) {
    if (const char *Current = std::getenv(Name.c_str())) {
      HadPrevious = true;
      Previous = Current;
    }
#if defined(_WIN32)
    _putenv_s(Name.c_str(), Value.c_str());
#else
    setenv(Name.c_str(), Value.c_str(), 1);
#endif
  }

  ~ScopedEnvironment() {
#if defined(_WIN32)
    _putenv_s(Name.c_str(), HadPrevious ? Previous.c_str() : "");
#else
    if (HadPrevious)
      setenv(Name.c_str(), Previous.c_str(), 1);
    else
      unsetenv(Name.c_str());
#endif
  }

private:
  std::string Name;
  std::string Previous;
  bool HadPrevious = false;
};

std::vector<std::string> traceLines(const fs::path &Path) {
  std::vector<std::string> Lines;
  std::ifstream Input(Path);
  for (std::string Line; std::getline(Input, Line);)
    if (!Line.empty())
      Lines.push_back(std::move(Line));
  return Lines;
}

bool contains(const std::vector<std::string> &Lines,
              const std::string &Expected) {
  return std::find(Lines.begin(), Lines.end(), Expected) != Lines.end();
}

} // namespace

class PluginFrontendLifecycleTest : public NeverCTest {};

TEST_F(PluginFrontendLifecycleTest,
       EmitsStableTreeAndSemaLifecycleForAValidTranslationUnit) {
  const fs::path Source = tmpFile("frontend_lifecycle.c");
  const fs::path Trace = tmpFile("frontend_lifecycle.trace");
  writeFile(Source,
            "struct Box { int value; };\n"
            "int tentative;\n"
            "int main(void) { return 0; }\n");
  ScopedEnvironment TracePath("NEVERC_PLUGIN_TRACE_FILE", Trace.string());

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_FRONTEND_LIFECYCLE_PLUGIN,
       "-std=c11", "-fsyntax-only", Source.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  std::vector<std::string> Lines = traceLines(Trace);
  ASSERT_GE(Lines.size(), 5u);
  EXPECT_EQ(Lines.front(), "tree_initialize");
  EXPECT_EQ(Lines[1], "sema_begin");
  EXPECT_EQ(Lines.back(), "sema_end");
  EXPECT_TRUE(contains(Lines, "tag_definition"));
  EXPECT_TRUE(contains(Lines, "top_level_decl"));
  EXPECT_TRUE(contains(Lines, "tentative_definition"));
  EXPECT_TRUE(contains(Lines, "translation_unit"));
}

TEST_F(PluginFrontendLifecycleTest,
       BalancesSemaLifecycleAfterSyntaxErrors) {
  const fs::path Source = tmpFile("frontend_lifecycle_syntax_error.c");
  const fs::path Trace = tmpFile("frontend_lifecycle_syntax_error.trace");
  writeFile(Source,
            "int broken\n"
            "int main(void) { return 0; }\n");
  ScopedEnvironment TracePath("NEVERC_PLUGIN_TRACE_FILE", Trace.string());

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_FRONTEND_LIFECYCLE_PLUGIN,
       "-std=c11", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);

  std::vector<std::string> Lines = traceLines(Trace);
  ASSERT_GE(Lines.size(), 3u);
  EXPECT_EQ(Lines.front(), "tree_initialize");
  EXPECT_EQ(Lines[1], "sema_begin");
  EXPECT_EQ(Lines.back(), "sema_end");
}

TEST_F(PluginFrontendLifecycleTest,
       ReportsObserverFailureAndStillEmitsSemaEnd) {
  const fs::path Source = tmpFile("frontend_lifecycle_failure.c");
  const fs::path Trace = tmpFile("frontend_lifecycle_failure.trace");
  writeFile(Source, "int main(void) { return 0; }\n");
  ScopedEnvironment TracePath("NEVERC_PLUGIN_TRACE_FILE", Trace.string());
  ScopedEnvironment Mode("NEVERC_TEST_FRONTEND_LIFECYCLE_MODE",
                         "fail-top-level");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_FRONTEND_LIFECYCLE_PLUGIN,
       "-std=c11", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("AST lifecycle observer failed"),
            std::string::npos)
      << Result.err;

  std::vector<std::string> Lines = traceLines(Trace);
  ASSERT_FALSE(Lines.empty());
  EXPECT_EQ(Lines.back(), "sema_end");
}

TEST_F(PluginFrontendLifecycleTest,
       CancellationAtTopLevelBoundaryAbortsOutputAndCleansUp) {
  const fs::path Source = tmpFile("frontend_lifecycle_cancel.c");
  const fs::path Output = tmpFile("frontend_lifecycle_cancel.ll");
  const fs::path Trace = tmpFile("frontend_lifecycle_cancel.trace");
  writeFile(Source,
            "int first(void) { return 1; }\n"
            "int second(void) { return 2; }\n");
  ScopedEnvironment TracePath("NEVERC_PLUGIN_TRACE_FILE", Trace.string());
  ScopedEnvironment Mode("NEVERC_TEST_FRONTEND_LIFECYCLE_MODE",
                         "cancel-top-level");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_FRONTEND_LIFECYCLE_PLUGIN,
       "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
       Output.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_FALSE(fs::exists(Output));

  std::vector<std::string> Lines = traceLines(Trace);
  ASSERT_FALSE(Lines.empty());
  EXPECT_EQ(Lines.back(), "sema_end");
  EXPECT_EQ(std::count(Lines.begin(), Lines.end(), "top_level_decl"), 1);
}

TEST_F(PluginFrontendLifecycleTest,
       DisableFreeStillEndsPluginCallbacksBeforeBuryingTheTree) {
  const fs::path Source = tmpFile("frontend_lifecycle_disable_free.c");
  const fs::path Trace = tmpFile("frontend_lifecycle_disable_free.trace");
  writeFile(Source, "int main(void) { return 0; }\n");
  ScopedEnvironment TracePath("NEVERC_PLUGIN_TRACE_FILE", Trace.string());

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_FRONTEND_LIFECYCLE_PLUGIN,
       "-std=c11", "-disable-free", "-fsyntax-only", Source.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  std::vector<std::string> Lines = traceLines(Trace);
  ASSERT_FALSE(Lines.empty());
  EXPECT_EQ(Lines.back(), "sema_end");
}
