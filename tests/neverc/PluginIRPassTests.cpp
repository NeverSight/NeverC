#include "NeverCTestFixture.h"

class PluginIRPassTest : public NeverCTest {};
class PluginCustomAnalysisTest : public NeverCTest {};

TEST_F(PluginIRPassTest, RunsModuleCGSCCFunctionAndLoopAdaptors) {
  const fs::path Source = tmpFile("ir_pass.c");
  const fs::path IR = tmpFile("ir_pass.ll");
  writeFile(Source,
            "volatile int sink;\n"
            "int accumulate(int n) {\n"
            "  int i = 0;\n"
            "  while (i < n) { sink += i; ++i; }\n"
            "  return sink;\n"
            "}\n"
            "int main(void) { return accumulate(3); }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_IR_PASS_PLUGIN, "-O0",
           "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
           IR.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Module = readFile(IR);
  EXPECT_NE(Module.find("!neverc.test.module ="), std::string::npos);
  EXPECT_NE(Module.find("!neverc.test.cgscc ="), std::string::npos);
  EXPECT_NE(Module.find("!neverc.test.function ="), std::string::npos);
  EXPECT_NE(Module.find("!neverc.test.loop ="), std::string::npos);
  EXPECT_NE(Module.find("!neverc.test.pipeline_start ="), std::string::npos);
  EXPECT_NE(Module.find("!neverc.test.optimizer_last ="), std::string::npos);
  EXPECT_NE(Module.find("!neverc.test.post_opt ="), std::string::npos);
  EXPECT_NE(Module.find("!neverc.test.pre_codegen ="), std::string::npos);
}

TEST_F(PluginIRPassTest, RejectsPreserveAllAfterMutation) {
  const fs::path Source = tmpFile("ir_pass_invalid_preserve.c");
  const fs::path IR = tmpFile("ir_pass_invalid_preserve.ll");
  writeFile(Source, "int main(void) { return 0; }\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_IR_PASS_INVALID_PLUGIN, "-O0",
       "-std=c11", "-S", "-emit-llvm", Source.string(), "-o", IR.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("mutated IR while preserving all analyses"),
            std::string::npos)
      << Result.err;
}

TEST_F(PluginCustomAnalysisTest, ComputesRequiredResultOncePerInvocation) {
  const fs::path Source = tmpFile("ir_custom_analysis.c");
  const fs::path IR = tmpFile("ir_custom_analysis.ll");
  writeFile(Source, "int main(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_IR_PASS_PLUGIN, "-O0",
           "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
           IR.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(IR).find("!neverc.test.module ="), std::string::npos);
}

TEST_F(PluginCustomAnalysisTest, RejectsDependencyCycleWhenPlanFreezes) {
  const fs::path Source = tmpFile("ir_custom_analysis_cycle.c");
  writeFile(Source, "int main(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_IR_ANALYSIS_CYCLE_PLUGIN,
           "-O0", "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
           tmpFile("ir_custom_analysis_cycle.ll").string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("custom IR analysis dependency cycle"),
            std::string::npos)
      << Result.err;
}

TEST_F(PluginCustomAnalysisTest, RejectsRecursiveSelfQuery) {
  const fs::path Source = tmpFile("ir_custom_analysis_recursive.c");
  writeFile(Source, "int main(void) { return 0; }\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_IR_ANALYSIS_RECURSIVE_PLUGIN,
       "-O0", "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
       tmpFile("ir_custom_analysis_recursive.ll").string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("required analysis (status 8)"), std::string::npos)
      << Result.err;
}

TEST_F(PluginCustomAnalysisTest, PropagatesComputeError) {
  const fs::path Source = tmpFile("ir_custom_analysis_error.c");
  writeFile(Source, "int main(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_IR_ANALYSIS_ERROR_PLUGIN,
           "-O0", "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
           tmpFile("ir_custom_analysis_error.ll").string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("required analysis (status 20)"),
            std::string::npos)
      << Result.err;
}

TEST_F(PluginCustomAnalysisTest, ConcurrentQueriesComputeOnce) {
  const fs::path Source = tmpFile("ir_custom_analysis_concurrent.c");
  writeFile(Source, "int main(void) { return 0; }\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_IR_ANALYSIS_CONCURRENT_PLUGIN,
       "-O0", "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
       tmpFile("ir_custom_analysis_concurrent.ll").string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}

TEST_F(PluginCustomAnalysisTest, DestroysDependentsBeforeDependencies) {
  const fs::path Source = tmpFile("ir_custom_analysis_destroy_order.c");
  writeFile(Source, "int main(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") +
               NEVERC_TEST_IR_ANALYSIS_DESTROY_ORDER_PLUGIN,
           "-O0", "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
           tmpFile("ir_custom_analysis_destroy_order.ll").string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}

TEST_F(PluginCustomAnalysisTest, RejectsIRMutationDuringComputation) {
  const fs::path Source = tmpFile("ir_custom_analysis_mutation.c");
  writeFile(Source, "int main(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") +
               NEVERC_TEST_IR_ANALYSIS_MUTATION_PLUGIN,
           "-O0", "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
           tmpFile("ir_custom_analysis_mutation.ll").string()});
  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}
