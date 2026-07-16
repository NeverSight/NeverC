#include "NeverCTestFixture.h"

class PluginArgumentPhaseTest : public NeverCTest {};

TEST_F(PluginArgumentPhaseTest, RawInterceptorRewritesEffectiveArguments) {
  const fs::path Source = tmpFile("argument_phase.c");
  const fs::path BaselineIR = tmpFile("baseline.ll");
  const fs::path RewrittenIR = tmpFile("rewritten.ll");
  writeFile(Source, R"(
#ifndef NEVERC_ARGUMENT_REWRITTEN
#error "argument rewrite plugin did not add its definition"
#endif

int argument_phase_result(void) {
  int unused;
  return 40 + 2;
}
)");

  CmdResult Baseline =
      ncc({"-std=c11", "-O0", "-Wall", "-Werror", "-S", "-emit-llvm",
           Source.string(), "-o", BaselineIR.string()});
  EXPECT_NE(Baseline.exitCode, 0)
      << "the control compile must fail before the plugin rewrites arguments";

  CmdResult Rewritten =
      ncc({std::string("-fplugin=") + NEVERC_TEST_ARGUMENT_REWRITE_PLUGIN,
           "-std=c11", "-O0", "-Wall", "-Werror", "-S", "-emit-llvm",
           Source.string(), "-o", RewrittenIR.string()});
  ASSERT_EQ(Rewritten.exitCode, 0) << Rewritten.err;

  const std::string IR = readFile(RewrittenIR);
  EXPECT_NE(IR.find("ret i32 42"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("alloca"), std::string::npos) << IR;
}

TEST_F(PluginArgumentPhaseTest, InvalidRawMutationPreservesOriginalArguments) {
  const fs::path Source = tmpFile("invalid_argument_mutation.c");
  const fs::path IRPath = tmpFile("invalid_argument_mutation.ll");
  writeFile(Source, R"(
#ifndef NEVERC_TEST_INVALID_ARGUMENT_MUTATION
#error "control definition disappeared"
#endif

int invalid_argument_mutation(int value) {
  int local = value + 1;
  return local;
}
)");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_ARGUMENT_REWRITE_PLUGIN,
           "-std=c11", "-O0", "-DNEVERC_TEST_INVALID_ARGUMENT_MUTATION=1", "-S",
           "-emit-llvm", Source.string(), "-o", IRPath.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string IR = readFile(IRPath);
  EXPECT_NE(IR.find("alloca"), std::string::npos) << IR;
}

TEST_F(PluginArgumentPhaseTest, RawArgumentsPreserveConfigAndCommandOrigins) {
  const fs::path Source = tmpFile("argument_origins.c");
  const fs::path Config = tmpFile("argument_origins.cfg");
  const fs::path IRPath = tmpFile("argument_origins.ll");
  writeFile(Config, "-O0\n-DNEVERC_TEST_CONFIG_ORIGIN=1\n");
  writeFile(Source, R"(
#ifndef NEVERC_TEST_CONFIG_ORIGIN
#error "configuration argument origin was lost"
#endif
#ifndef NEVERC_TEST_COMMAND_ORIGIN
#error "command-line argument origin was lost"
#endif

int argument_origin_result(int value) {
  int incremented = value + 1;
  return incremented * 2;
}
)");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_ARGUMENT_REWRITE_PLUGIN,
           "--no-default-config", "--config=" + Config.string(),
           "-DNEVERC_TEST_COMMAND_ORIGIN=1", "-O2", "-S", "-emit-llvm",
           Source.string(), "-o", IRPath.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string IR = readFile(IRPath);
  EXPECT_EQ(IR.find("alloca"), std::string::npos) << IR;
}

TEST_F(PluginArgumentPhaseTest,
       ParsedInterceptorRewritesStructuredOccurrences) {
  const fs::path Source = tmpFile("parsed_argument_phase.c");
  const fs::path IRPath = tmpFile("parsed_argument_phase.ll");
  writeFile(Source, R"(
#ifndef NEVERC_PARSED_ARGUMENT_REWRITTEN
#error "parsed argument plugin did not add its definition"
#endif

int parsed_argument_phase_result(void) {
  int unused;
  return 21 * 2;
}
)");

  CmdResult Rewritten =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PARSED_ARGUMENT_PLUGIN,
           "-std=c11", "-O0", "-Wall", "-Werror", "-S", "-emit-llvm",
           Source.string(), "-o", IRPath.string()});
  ASSERT_EQ(Rewritten.exitCode, 0) << Rewritten.err;

  const std::string IR = readFile(IRPath);
  EXPECT_NE(IR.find("ret i32 42"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("alloca"), std::string::npos) << IR;
}
