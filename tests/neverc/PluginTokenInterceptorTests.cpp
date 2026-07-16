#include "NeverCTestFixture.h"

class PluginTokenInterceptorTest : public NeverCTest {};

TEST_F(PluginTokenInterceptorTest,
       ReplacesDeletesAndExpandsFinalPreprocessorTokens) {
  const fs::path Source = tmpFile("token_interceptor.c");
  const fs::path IRPath = tmpFile("token_interceptor.ll");
  writeFile(Source, R"(
static const int PLUGIN_REINJECT = 7;

int token_plugin_result(void) {
  return PLUGIN_CONST + PLUGIN_DROP 0 + PLUGIN_PAIR + PLUGIN_REINJECT;
}
)");

  CmdResult Baseline = ncc({"-std=c11", "-O2", "-S", "-emit-llvm",
                            Source.string(), "-o", IRPath.string()});
  EXPECT_NE(Baseline.exitCode, 0)
      << "the control compile must reject the plugin-only identifiers";

  CmdResult Rewritten = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_TOKEN_REWRITE_PLUGIN, "-std=c11",
       "-O2", "-S", "-emit-llvm", Source.string(), "-o", IRPath.string()});
  ASSERT_EQ(Rewritten.exitCode, 0) << Rewritten.err;

  const std::string IR = readFile(IRPath);
  EXPECT_NE(IR.find("ret i32 91"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("PLUGIN_"), std::string::npos) << IR;
}

TEST_F(PluginTokenInterceptorTest, EnforcesExpansionBudget) {
  const fs::path Source = tmpFile("token_interceptor_budget.c");
  const fs::path IRPath = tmpFile("token_interceptor_budget.ll");
  writeFile(Source, R"(
int token_plugin_budget(void) {
  return PLUGIN_OVERFLOW;
}
)");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_TOKEN_REWRITE_PLUGIN, "-std=c11",
       "-S", "-emit-llvm", Source.string(), "-o", IRPath.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("neverc.prep.token"), std::string::npos)
      << Result.err;
}

TEST_F(PluginTokenInterceptorTest, PropagatesCancellation) {
  const fs::path Source = tmpFile("token_interceptor_cancel.c");
  const fs::path IRPath = tmpFile("token_interceptor_cancel.ll");
  writeFile(Source, R"(
int token_plugin_cancel(void) {
  return PLUGIN_CANCEL;
}
)");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_TOKEN_REWRITE_PLUGIN, "-std=c11",
       "-S", "-emit-llvm", Source.string(), "-o", IRPath.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("cancel"), std::string::npos) << Result.err;
}
