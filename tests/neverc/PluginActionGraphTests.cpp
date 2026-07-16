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

class PluginActionGraphTest : public NeverCTest {
protected:
  void expectObjectRoot(const char *PluginPath, const std::string &Stem) {
    const fs::path Source = tmpFile(Stem + ".c");
    const fs::path Output = tmpFile(Stem + ".o");
    writeFile(Source, R"(
extern int neverc_action_graph_unresolved(void);
int main(void) {
  return neverc_action_graph_unresolved();
}
)");

    CmdResult Baseline =
        ncc({"-fno-lto", Source.string(), "-o",
             tmpFile(Stem + "_baseline").string()});
    EXPECT_NE(Baseline.exitCode, 0)
        << "control link unexpectedly resolved the missing symbol";

    CmdResult Result =
        ncc({std::string("-fplugin=") + PluginPath, "-fno-lto",
             Source.string(), "-o", Output.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    EXPECT_GT(fileSize(Output), 0u);
  }
};

TEST_F(PluginActionGraphTest, InterceptorReplacesLinkRootWithObjectRoot) {
  expectObjectRoot(NEVERC_TEST_ACTION_GRAPH_INTERCEPT_PLUGIN,
                   "action_graph_interceptor");
}

TEST_F(PluginActionGraphTest, ObserverSeesRealCompileLinkGraph) {
  const fs::path Source = tmpFile("action_graph_observer.c");
  const fs::path Trace = tmpFile("action_graph_observer.trace");
  writeFile(Source, "int main(void) { return 0; }\n");
  writeFile(Trace, "");
  ScopedPluginTracePath TracePath(Trace);

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_ACTION_GRAPH_OBSERVE_PLUGIN,
           "-fno-lto", "-ccc-print-phases", Source.string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(Trace).find("observer:compile-link"),
            std::string::npos);
}

TEST_F(PluginActionGraphTest, ProviderBuildsObjectGraphWithoutBuiltin) {
  const fs::path Trace = tmpFile("action_graph_replacement.trace");
  writeFile(Trace, "");
  {
    ScopedPluginTracePath TracePath(Trace);
    expectObjectRoot(NEVERC_TEST_ACTION_GRAPH_REPLACE_PLUGIN,
                     "action_graph_replacement");
  }
  EXPECT_NE(readFile(Trace).find("provider:replacement"),
            std::string::npos);
}

TEST_F(PluginActionGraphTest, RejectsCycleAtomically) {
  const fs::path Source = tmpFile("action_graph_cycle.c");
  const fs::path Output = tmpFile("action_graph_cycle.o");
  writeFile(Source, "int action_graph_cycle(void) { return 0; }\n");
  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_ACTION_GRAPH_CYCLE_PLUGIN,
           "-fno-lto", "-c", Source.string(), "-o", Output.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_GT(fileSize(Output), 0u);
}

TEST_F(PluginActionGraphTest, RejectsIncompatibleAdjacentTypes) {
  const fs::path Source = tmpFile("action_graph_type.c");
  const fs::path Output = tmpFile("action_graph_type.o");
  writeFile(Source, "int action_graph_type(void) { return 0; }\n");
  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_ACTION_GRAPH_TYPE_PLUGIN,
           "-fno-lto", "-c", Source.string(), "-o", Output.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_GT(fileSize(Output), 0u);
}
