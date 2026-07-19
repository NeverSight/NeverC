#include "NeverCTestFixture.h"

class PluginIRPipelineTest : public NeverCTest {};

TEST_F(PluginIRPipelineTest,
       ReplacementBypassesBuiltinPipelineAndChangesProgramBehavior) {
  const fs::path Source = tmpFile("ir_optimization_provider.c");
  const fs::path IR = tmpFile("ir_optimization_provider.ll");
  const fs::path Executable = tmpFile("ir_optimization_provider");
  writeFile(Source,
            "int builtin_only(void) { return 7; }\n"
            "int main(void) { return builtin_only(); }\n");

  CmdResult EmitIR =
      ncc({std::string("-fplugin=") +
               NEVERC_TEST_IR_OPTIMIZATION_PROVIDER_PLUGIN,
           "-O2", "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
           IR.string()});
  ASSERT_EQ(EmitIR.exitCode, 0) << EmitIR.err;
  const std::string Module = readFile(IR);
  EXPECT_EQ(Module.find("builtin_only"), std::string::npos);
  EXPECT_EQ(Module.find("neverc.test.must-not-run"), std::string::npos);
  EXPECT_NE(Module.find("define i32 @main()"), std::string::npos);
  EXPECT_NE(Module.find("ret i32 42"), std::string::npos);

  std::vector<std::string> Arguments = {
      std::string("-fplugin=") +
          NEVERC_TEST_IR_OPTIMIZATION_PROVIDER_PLUGIN,
      "-O2", "-std=c11", Source.string(), "-o", Executable.string()};
  std::vector<std::string> LinkArguments = linkFlags();
  Arguments.insert(Arguments.end(), LinkArguments.begin(), LinkArguments.end());
  CmdResult Compile = ncc(Arguments);
  ASSERT_EQ(Compile.exitCode, 0) << Compile.err;
  CmdResult Run = exec(Executable.string(), {});
  EXPECT_EQ(Run.exitCode, 42) << Run.out << Run.err;
}

TEST_F(PluginIRPipelineTest, ReplacementRunsWhenLLVMPassesAreDisabled) {
  const fs::path Source = tmpFile("ir_optimization_disabled.c");
  const fs::path IR = tmpFile("ir_optimization_disabled.ll");
  writeFile(Source, "int main(void) { return 7; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") +
               NEVERC_TEST_IR_OPTIMIZATION_PROVIDER_PLUGIN,
           "-disable-llvm-passes", "-O0", "-std=c11", "-S", "-emit-llvm",
           Source.string(), "-o", IR.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(IR).find("ret i32 42"), std::string::npos);
}

TEST_F(PluginIRPipelineTest, SealedVerifierRejectsInvalidReplacement) {
  const fs::path Source = tmpFile("ir_optimization_invalid.c");
  writeFile(Source, "int main(void) { return 7; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") +
               NEVERC_TEST_IR_OPTIMIZATION_INVALID_PLUGIN,
           "-O2", "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
           tmpFile("ir_optimization_invalid.ll").string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("verification"), std::string::npos) << Result.err;
}
