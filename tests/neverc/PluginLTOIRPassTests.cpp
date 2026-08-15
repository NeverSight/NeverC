#include "NeverCTestFixture.h"

#include <cstdlib>
#include <optional>

class PluginLTOIRPassTest : public NeverCTest {};

namespace {

class ScopedPluginTestEnv {
public:
  ScopedPluginTestEnv(const char *NameValue, const char *Value)
      : Name(NameValue) {
    if (const char *Existing = std::getenv(NameValue))
      Previous = Existing;
#ifdef _WIN32
    _putenv_s(NameValue, Value);
#else
    setenv(NameValue, Value, 1);
#endif
  }

  ~ScopedPluginTestEnv() {
#ifdef _WIN32
    _putenv_s(Name.c_str(), Previous ? Previous->c_str() : "");
#else
    if (Previous)
      setenv(Name.c_str(), Previous->c_str(), 1);
    else
      unsetenv(Name.c_str());
#endif
  }

  ScopedPluginTestEnv(const ScopedPluginTestEnv &) = delete;
  ScopedPluginTestEnv &operator=(const ScopedPluginTestEnv &) = delete;

private:
  std::string Name;
  std::optional<std::string> Previous;
};

std::string ltoProgram() {
  std::string Program = "volatile unsigned lto_sink;\n";
  for (unsigned Index = 0; Index != 12; ++Index)
    Program += "int lto_f" + std::to_string(Index) +
               "(unsigned x) { for (unsigned i = 0; i < x; ++i) "
               "lto_sink += i + " +
               std::to_string(Index) + "u; return x * 33u + " +
               std::to_string(Index) + "u; }\n";
  Program += "typedef int (*lto_fn)(unsigned);\n"
             "volatile lto_fn lto_functions[12] = {";
  for (unsigned Index = 0; Index != 12; ++Index) {
    if (Index != 0)
      Program += ", ";
    Program += "lto_f" + std::to_string(Index);
  }
  Program += "};\nint main(void) { return ";
  for (unsigned Index = 0; Index != 12; ++Index) {
    if (Index != 0)
      Program += " + ";
    Program += "lto_functions[" + std::to_string(Index) + "](" +
               std::to_string(Index + 1) + "u)";
  }
  Program += "; }\n";
  return Program;
}

} // namespace

TEST_F(PluginLTOIRPassTest, RunsOnceAroundParallelPartitions) {
  const fs::path Source = tmpFile("lto_plugin.c");
  const fs::path Output = tmpFile("lto_plugin");
  writeFile(Source, ltoProgram());
  ScopedPluginTestEnv DebugPCG("NEVERC_PCG_DEBUG", "1");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_LTO_IR_PASS_PLUGIN, "-O2",
       // The 12 functions above are sufficient to force parallel partitions;
       // the allocator is unrelated input that makes Windows LTO dominate the
       // test and can push an LTO-built compiler past CTest's 900 s timeout.
       "-fno-builtin-mimalloc", "-fparallel-codegen=2", "-mllvm",
       "-neverc-pcg-min-funcs=2", "-mllvm", "-neverc-pcg-weight-floor=0",
       Source.string(), "-o", Output.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(Result.err.find("WholeModuleBarrier=yes"), std::string::npos)
      << Result.err;
  EXPECT_TRUE(fs::exists(Output));
}

TEST_F(PluginLTOIRPassTest, OrdinaryLTOHasNoWholeModuleBarrier) {
  const fs::path Source = tmpFile("lto_without_plugin.c");
  const fs::path Output = tmpFile("lto_without_plugin");
  writeFile(Source, ltoProgram());
  ScopedPluginTestEnv DebugPCG("NEVERC_PCG_DEBUG", "1");

  CmdResult Result =
      ncc({"-O2", "-fno-builtin-mimalloc", "-fparallel-codegen=2", "-mllvm",
           "-neverc-pcg-min-funcs=2", "-mllvm", "-neverc-pcg-weight-floor=0",
           Source.string(), "-o", Output.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(Result.err.find("WholeModuleBarrier=no"), std::string::npos)
      << Result.err;
  EXPECT_EQ(Result.err.find("WholeModuleBarrier=yes"), std::string::npos)
      << Result.err;
  EXPECT_TRUE(fs::exists(Output));
}

TEST_F(PluginLTOIRPassTest,
       WholeModulePassesAreNotReplayedWhenFinalMergeFallsBack) {
  const fs::path Source = tmpFile("lto_plugin_final_fallback.c");
  const fs::path Output = tmpFile("lto_plugin_final_fallback");
  writeFile(Source, ltoProgram());
  ScopedPluginTestEnv ForceMergeFailure("NEVERC_PCG_FORCE_MERGE_FAIL", "1");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_LTO_IR_PASS_PLUGIN, "-O2",
           "-fno-builtin-mimalloc", "-fparallel-codegen=2", "-mllvm",
           "-neverc-pcg-min-funcs=2", "-mllvm", "-neverc-pcg-weight-floor=0",
           Source.string(), "-o", Output.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Output));
}

TEST_F(PluginLTOIRPassTest, PropagatesLTOPassFailure) {
  const fs::path Source = tmpFile("lto_plugin_error.c");
  const fs::path Output = tmpFile("lto_plugin_error");
  writeFile(Source, "int main(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_LTO_IR_PASS_ERROR_PLUGIN,
           "-O2", Source.string(), "-o", Output.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("neverc.test.lto.post_opt"), std::string::npos)
      << Result.err;
}

TEST_F(PluginLTOIRPassTest, PropagatesParallelLTOPassFailure) {
  const fs::path Source = tmpFile("lto_plugin_parallel_error.c");
  const fs::path Output = tmpFile("lto_plugin_parallel_error");
  writeFile(Source, ltoProgram());

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_LTO_IR_PASS_ERROR_PLUGIN,
           "-O2", "-fno-builtin-mimalloc", "-fparallel-codegen=2", "-mllvm",
           "-neverc-pcg-min-funcs=2", "-mllvm", "-neverc-pcg-weight-floor=0",
           Source.string(), "-o", Output.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("neverc.test.lto.post_opt"), std::string::npos)
      << Result.err;
  EXPECT_FALSE(fs::exists(Output));
}
