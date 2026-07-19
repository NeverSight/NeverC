#include "NeverCTestFixture.h"

class PluginLTOIRPassTest : public NeverCTest {};

namespace {

std::string ltoProgram() {
  std::string Program;
  for (unsigned Index = 0; Index != 12; ++Index)
    Program += "int lto_f" +
               std::to_string(Index) + "(unsigned x) { return x * 33u + " +
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

TEST_F(PluginLTOIRPassTest, RunsInLTOChildAndParallelPartitions) {
  const fs::path Source = tmpFile("lto_plugin.c");
  const fs::path Output = tmpFile("lto_plugin");
  writeFile(Source, ltoProgram());

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_LTO_IR_PASS_PLUGIN, "-O2",
       "-fparallel-codegen=2", "-mllvm", "-neverc-pcg-min-funcs=2", "-mllvm",
       "-neverc-pcg-weight-floor=0", Source.string(), "-o", Output.string()});
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
