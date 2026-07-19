#include "NeverCTestFixture.h"

class PluginMIRPassTest : public NeverCTest {};

TEST_F(PluginMIRPassTest, RunsEveryStableMachinePipelineHook) {
  const fs::path Source = tmpFile("mir_pass.c");
  const fs::path Object = tmpFile("mir_pass.o");
  writeFile(Source,
            "volatile int sink;\n"
            "int add(int left, int right) { return left + right; }\n"
            "int main(void) { sink = add(1, 2); return sink; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_MIR_PASS_PLUGIN, "-O2",
           "-fno-lto", "-std=c11", "-c", Source.string(), "-o",
           Object.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Object));
}

TEST_F(PluginMIRPassTest, RejectsPreserveAllAfterMachineMutation) {
  const fs::path Source = tmpFile("mir_pass_invalid.c");
  const fs::path Object = tmpFile("mir_pass_invalid.o");
  writeFile(Source, "int main(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_MIR_PASS_INVALID_PLUGIN,
           "-O2", "-fno-lto", "-std=c11", "-c", Source.string(), "-o",
           Object.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("mutated MIR while preserving all analyses"),
            std::string::npos)
      << Result.err;
}

TEST_F(PluginMIRPassTest, RunsFunctionPassesInParallelCodegenPartitions) {
  const fs::path Source = tmpFile("mir_pass_parallel.c");
  const fs::path Object = tmpFile("mir_pass_parallel.o");
  std::string Program;
  for (unsigned Index = 0; Index != 10; ++Index)
    Program += "__attribute__((noinline,used)) int f" +
               std::to_string(Index) + "(int x) { return x + " +
               std::to_string(Index) + "; }\n";
  Program += "int main(void) { return f0(f1(f2(f3(f4(0))))); }\n";
  writeFile(Source, Program);

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_MIR_PASS_PLUGIN, "-O2",
       "-fno-lto", "-fparallel-codegen=2", "-mllvm",
       "-neverc-pcg-min-funcs=2", "-mllvm",
       "-neverc-pcg-weight-floor=0", "-std=c11", "-c", Source.string(), "-o",
       Object.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Object));
}

TEST_F(PluginMIRPassTest, SerializesModuleLevelPassesAtPipelineBarriers) {
  const fs::path Source = tmpFile("mir_pass_module.c");
  const fs::path Object = tmpFile("mir_pass_module.o");
  std::string Program;
  for (unsigned Index = 0; Index != 10; ++Index)
    Program += "__attribute__((noinline,used)) int m" +
               std::to_string(Index) + "(int x) { return x ^ " +
               std::to_string(Index + 1) + "; }\n";
  Program += "int main(void) { return m0(m1(m2(m3(m4(0))))); }\n";
  writeFile(Source, Program);

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_MIR_PASS_MODULE_PLUGIN, "-O2",
       "-fno-lto", "-fparallel-codegen=2", "-mllvm",
       "-neverc-pcg-min-funcs=2", "-mllvm",
       "-neverc-pcg-weight-floor=0", "-std=c11", "-c", Source.string(), "-o",
       Object.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Object));
}

TEST_F(PluginMIRPassTest, RunsBasicBlockPassForEveryLiveBlock) {
  const fs::path Source = tmpFile("mir_pass_basic_block.c");
  const fs::path Object = tmpFile("mir_pass_basic_block.o");
  writeFile(Source,
            "int choose(int x) { return x > 0 ? x + 1 : x - 1; }\n"
            "int main(void) { return choose(1); }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") +
               NEVERC_TEST_MIR_PASS_BASIC_BLOCK_PLUGIN,
           "-O2", "-fno-lto", "-std=c11", "-c", Source.string(), "-o",
           Object.string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Object));
}
