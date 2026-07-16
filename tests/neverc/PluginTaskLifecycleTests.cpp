#include "NeverCTestFixture.h"
#include "neverc/Plugin/PluginCore.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>

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

class ScopedOutputLifecycle {
public:
  ScopedOutputLifecycle() {
    if (const char *Current = std::getenv("NEVERC_TEST_OUTPUT_LIFECYCLE")) {
      HadPrevious = true;
      Previous = Current;
    }
#if defined(_WIN32)
    _putenv_s("NEVERC_TEST_OUTPUT_LIFECYCLE", "1");
#else
    setenv("NEVERC_TEST_OUTPUT_LIFECYCLE", "1", 1);
#endif
  }

  ~ScopedOutputLifecycle() {
#if defined(_WIN32)
    _putenv_s("NEVERC_TEST_OUTPUT_LIFECYCLE",
              HadPrevious ? Previous.c_str() : "");
#else
    if (HadPrevious)
      setenv("NEVERC_TEST_OUTPUT_LIFECYCLE", Previous.c_str(), 1);
    else
      unsetenv("NEVERC_TEST_OUTPUT_LIFECYCLE");
#endif
  }

private:
  bool HadPrevious = false;
  std::string Previous;
};

std::vector<std::string> lines(const std::string &Text) {
  std::vector<std::string> Result;
  std::istringstream Input(Text);
  for (std::string Line; std::getline(Input, Line);)
    if (!Line.empty())
      Result.push_back(std::move(Line));
  return Result;
}

std::string sessionKey(const std::vector<std::string> &Trace) {
  const std::string Prefix = "session_begin:0:";
  auto It = std::find_if(Trace.begin(), Trace.end(),
                         [&](const std::string &Line) {
                           return Line.rfind(Prefix, 0) == 0;
                         });
  return It == Trace.end() ? std::string() : It->substr(Prefix.size());
}

size_t countEvent(const std::vector<std::string> &Trace,
                  const std::string &Event, uint32_t Kind,
                  const std::string &Session) {
  const std::string Prefix =
      Event + ":" + std::to_string(Kind) + ":" + Session + ":";
  return static_cast<size_t>(std::count_if(
      Trace.begin(), Trace.end(), [&](const std::string &Line) {
        return Line.rfind(Prefix, 0) == 0;
      }));
}

} // namespace

class PluginTaskLifecycleTest : public NeverCTest {};

TEST_F(PluginTaskLifecycleTest, FrontendAndLinkerShareDriverSession) {
  const fs::path Source = tmpFile("task_lifecycle_pipeline.c");
  const fs::path Output = tmpFile("task_lifecycle_pipeline");
  const fs::path Trace = tmpFile("task_lifecycle_pipeline.trace");
  writeFile(Source, "int main(void) { return 0; }\n");
  writeFile(Trace, "");

  std::vector<std::string> Arguments = {
      std::string("-fplugin=") + NEVERC_TEST_TASK_LIFECYCLE_PLUGIN,
      Source.string(), "-o", Output.string()};
  std::vector<std::string> LinkArguments = linkFlags();
  Arguments.insert(Arguments.end(), LinkArguments.begin(),
                   LinkArguments.end());

  CmdResult Result;
  {
    ScopedPluginTracePath TracePath(Trace);
    Result = ncc(Arguments);
  }

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  const std::vector<std::string> Events = lines(readFile(Trace));
  const std::string Session = sessionKey(Events);
  ASSERT_FALSE(Session.empty());
  EXPECT_GE(countEvent(Events, "task_begin", NEVERC_TASK_INVOCATION,
                       Session),
            2u);
  EXPECT_EQ(countEvent(Events, "task_begin",
                       NEVERC_TASK_TRANSLATION_UNIT, Session),
            1u);
  EXPECT_EQ(countEvent(Events, "task_end",
                       NEVERC_TASK_TRANSLATION_UNIT, Session),
            1u);
  EXPECT_EQ(countEvent(Events, "task_begin", NEVERC_TASK_LINK, Session),
            1u);
  EXPECT_EQ(countEvent(Events, "task_end", NEVERC_TASK_LINK, Session), 1u);
}

TEST_F(PluginTaskLifecycleTest,
       MultiTUPluginCompilationStaysInOneInProcessSession) {
  const fs::path First = tmpFile("task_lifecycle_first.c");
  const fs::path Second = tmpFile("task_lifecycle_second.c");
  const fs::path Trace = tmpFile("task_lifecycle_multi_tu.trace");
  writeFile(First, "int first_translation_unit;\n");
  writeFile(Second, "int second_translation_unit;\n");
  writeFile(Trace, "");

  CmdResult Result;
  {
    ScopedPluginTracePath TracePath(Trace);
    Result = ncc(
        {std::string("-fplugin=") +
             NEVERC_TEST_TASK_LIFECYCLE_PLUGIN,
         "-fsyntax-only", First.string(), Second.string()});
  }

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  const std::vector<std::string> Events = lines(readFile(Trace));
  const std::string Session = sessionKey(Events);
  ASSERT_FALSE(Session.empty());
  EXPECT_EQ(static_cast<size_t>(std::count_if(
                Events.begin(), Events.end(), [](const std::string &Line) {
                  return Line.rfind("session_begin:0:", 0) == 0;
                })),
            1u);
  EXPECT_EQ(countEvent(Events, "task_begin", NEVERC_TASK_INVOCATION,
                       Session),
            3u);
  EXPECT_EQ(countEvent(Events, "task_begin",
                       NEVERC_TASK_TRANSLATION_UNIT, Session),
            2u);
  EXPECT_EQ(countEvent(Events, "task_end",
                       NEVERC_TASK_TRANSLATION_UNIT, Session),
            2u);
}

TEST_F(PluginTaskLifecycleTest,
       DirectCC1ExpandsResponseAndRunsSessionTasks) {
  const fs::path Source = tmpFile("task_lifecycle_direct.c");
  const fs::path Response = tmpFile("task_lifecycle_direct.rsp");
  const fs::path Trace = tmpFile("task_lifecycle_direct.trace");
  writeFile(Source, "int direct_cc1_input;\n");
  writeFile(
      Response,
      std::string("-fplugin=\"") + NEVERC_TEST_TASK_LIFECYCLE_PLUGIN +
          "\" -triple \"" + hostTriple() +
          "\" -fsyntax-only -x c \"" + Source.string() + "\"\n");
  writeFile(Trace, "");

  CmdResult Result;
  {
    ScopedPluginTracePath TracePath(Trace);
    Result = exec(neverc().string(),
                  {"-cc1", std::string("@") + Response.string()});
  }

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  const std::vector<std::string> Events = lines(readFile(Trace));
  const std::string Session = sessionKey(Events);
  ASSERT_FALSE(Session.empty());
  EXPECT_EQ(countEvent(Events, "task_begin", NEVERC_TASK_INVOCATION,
                       Session),
            1u);
  EXPECT_EQ(countEvent(Events, "task_end", NEVERC_TASK_INVOCATION,
                       Session),
            1u);
  EXPECT_EQ(countEvent(Events, "task_begin",
                       NEVERC_TASK_TRANSLATION_UNIT, Session),
            1u);
  EXPECT_EQ(countEvent(Events, "task_end",
                       NEVERC_TASK_TRANSLATION_UNIT, Session),
            1u);
  EXPECT_NE(std::find(Events.begin(), Events.end(),
                      std::string("session_end:0:") + Session),
            Events.end());
  EXPECT_NE(std::find(Events.begin(), Events.end(), "destroy"),
            Events.end());
}

TEST_F(PluginTaskLifecycleTest,
       TaskEndCanInspectAutoAbortedOutputButCannotWriteIt) {
  const fs::path Source = tmpFile("task_lifecycle_output.c");
  const fs::path Trace = tmpFile("task_lifecycle_output.trace");
  writeFile(Source, "int task_lifecycle_output;\n");
  writeFile(Trace, "");

  CmdResult Result;
  {
    ScopedPluginTracePath TracePath(Trace);
    ScopedOutputLifecycle EnableOutputLifecycle;
    Result =
        ncc({std::string("-fplugin=") +
                 NEVERC_TEST_TASK_LIFECYCLE_PLUGIN,
             "-fsyntax-only", Source.string()});
  }

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  const std::vector<std::string> Events = lines(readFile(Trace));
  const size_t SummaryCount = static_cast<size_t>(std::count(
      Events.begin(), Events.end(), "output_summary:0:4"));
  const size_t WriteCount = static_cast<size_t>(std::count(
      Events.begin(), Events.end(), "output_write:16:0"));
  EXPECT_GT(SummaryCount, 0U);
  EXPECT_EQ(WriteCount, SummaryCount);
}
