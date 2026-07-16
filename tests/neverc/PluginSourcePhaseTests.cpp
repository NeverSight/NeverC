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
#if defined(_WIN32)
    _putenv_s("NEVERC_PLUGIN_TRACE_FILE", Path.string().c_str());
#else
    setenv("NEVERC_PLUGIN_TRACE_FILE", Path.string().c_str(), 1);
#endif
  }

  ~ScopedPluginTracePath() {
#if defined(_WIN32)
    _putenv_s("NEVERC_PLUGIN_TRACE_FILE",
              HadPrevious ? Previous.c_str() : "");
#else
    if (HadPrevious)
      setenv("NEVERC_PLUGIN_TRACE_FILE", Previous.c_str(), 1);
    else
      unsetenv("NEVERC_PLUGIN_TRACE_FILE");
#endif
  }

private:
  bool HadPrevious = false;
  std::string Previous;
};

} // namespace

class PluginSourcePhaseTest : public NeverCTest {};

TEST_F(PluginSourcePhaseTest,
       ReplacementPublishesVerifiedMemorySourceAndAfterOpenEvent) {
  const fs::path Source = tmpFile("source_phase_replacement.c");
  const fs::path Trace = tmpFile("source_phase_replacement.trace");
  writeFile(Source, "int invalid_disk_source( { return 0; }\n");
  writeFile(Trace, "");

  CmdResult Result;
  {
    ScopedPluginTracePath TracePath(Trace);
    Result = ncc({std::string("-fplugin=") +
                      NEVERC_TEST_SOURCE_PHASE_PLUGIN,
                  "-fsyntax-only", Source.string()});
  }

  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  const std::string Events = readFile(Trace);
  EXPECT_NE(Events.find("source-open-provider"), std::string::npos);
  EXPECT_NE(Events.find("source-after-open:memory"), std::string::npos);
}
