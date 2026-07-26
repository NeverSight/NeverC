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

class PluginJobGraphTest : public NeverCTest {
protected:
  CmdResult runTwoSyntaxJobs(const char *PluginPath,
                             const std::string &Stem) {
    const fs::path First = tmpFile(Stem + "_first.c");
    const fs::path Second = tmpFile(Stem + "_second.c");
    writeFile(First, "int job_graph_first(void) { return 1; }\n");
    writeFile(Second, "int job_graph_second(void) { return 2; }\n");
    return ncc({std::string("-fplugin=") + PluginPath, "-fsyntax-only",
                First.string(), Second.string()});
  }
};

TEST_F(PluginJobGraphTest, InterceptorRemovesSelectedFrontendJob) {
  const fs::path Good = tmpFile("job_graph_good.c");
  const fs::path Bad = tmpFile("job_graph_bad.c");
  writeFile(Good, "int job_graph_good(void) { return 0; }\n");
  writeFile(Bad, "int job_graph_bad( { return 0; }\n");

  CmdResult Baseline = ncc({"-fsyntax-only", Good.string(), Bad.string()});
  EXPECT_NE(Baseline.exitCode, 0) << Baseline.err;

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_JOB_GRAPH_REMOVE_PLUGIN,
           "-fsyntax-only", Good.string(), Bad.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}

TEST_F(PluginJobGraphTest, FrontendArgumentRewriteRefreshesDirectOptions) {
  const fs::path Source = tmpFile("job_graph_rewrite.c");
  writeFile(Source, R"(
#ifndef NEVERC_JOB_GRAPH_REWRITTEN
#error "job graph argv rewrite did not reach direct frontend options"
#endif
int job_graph_rewrite(void) { return 0; }
)");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_JOB_GRAPH_REWRITE_PLUGIN,
           "-DNEVERC_JOB_GRAPH_ORIGINAL=1", "-fsyntax-only", Source.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}

TEST_F(PluginJobGraphTest, InterceptorInsertsPluginJob) {
  const fs::path Source = tmpFile("job_graph_plugin_job.c");
  const fs::path Trace = tmpFile("job_graph_plugin_job.trace");
  writeFile(Source, "int job_graph_plugin_job(void) { return 0; }\n");
  writeFile(Trace, "");
  {
    ScopedPluginTracePath TracePath(Trace);
    CmdResult Result =
        ncc({std::string("-fplugin=") + NEVERC_TEST_JOB_GRAPH_INSERT_PLUGIN,
             "-fsyntax-only", Source.string()});
    EXPECT_EQ(Result.exitCode, 0) << Result.err;
  }
  EXPECT_NE(readFile(Trace).find("job:inserted"), std::string::npos);
}

TEST_F(PluginJobGraphTest, ReplacementBuildsPlanWithoutBuiltinJobGeneration) {
  const fs::path Source = tmpFile("job_graph_replacement.c");
  const fs::path Trace = tmpFile("job_graph_replacement.trace");
  writeFile(Source, "int job_graph_replacement( { return 0; }\n");
  writeFile(Trace, "");
  {
    ScopedPluginTracePath TracePath(Trace);
    CmdResult Result =
        ncc({std::string("-fplugin=") + NEVERC_TEST_JOB_GRAPH_REPLACE_PLUGIN,
             "-fsyntax-only", Source.string()});
    EXPECT_EQ(Result.exitCode, 0) << Result.err;
  }
  const std::string Events = readFile(Trace);
  EXPECT_NE(Events.find("provider:replacement"), std::string::npos);
  EXPECT_NE(Events.find("job:replacement"), std::string::npos);
}

TEST_F(PluginJobGraphTest, RejectsDependencyCycleAtomically) {
  CmdResult Result =
      runTwoSyntaxJobs(NEVERC_TEST_JOB_GRAPH_CYCLE_PLUGIN, "job_graph_cycle");
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}

TEST_F(PluginJobGraphTest, RejectsDuplicateOutputsAtomically) {
  CmdResult Result = runTwoSyntaxJobs(
      NEVERC_TEST_JOB_GRAPH_DUPLICATE_OUTPUT_PLUGIN,
      "job_graph_duplicate_output");
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}

TEST_F(PluginJobGraphTest, RejectsDanglingInputAtomically) {
  CmdResult Result = runTwoSyntaxJobs(
      NEVERC_TEST_JOB_GRAPH_DANGLING_INPUT_PLUGIN,
      "job_graph_dangling_input");
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}
