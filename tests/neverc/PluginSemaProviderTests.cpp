#include "NeverCTestFixture.h"

class PluginSemaProviderTest : public NeverCTest {};

TEST_F(PluginSemaProviderTest,
       BuiltinReplayRejectsAnExplicitlyUnsupportedASTKind) {
  const fs::path Source = tmpFile("sema_replay_unsupported.c");
  writeFile(Source, "this source is replaced by a plugin AST\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_PARSER_PROVIDER_BINARY_PLUGIN,
       "-std=c11", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("BinaryOperator"), std::string::npos) << Result.err;
}

TEST_F(PluginSemaProviderTest,
       PluginProviderPublishesSemanticUnitWithoutBuiltinReplay) {
  const fs::path Source = tmpFile("sema_provider.c");
  const fs::path IRPath = tmpFile("sema_provider.ll");
  writeFile(Source, "this source is replaced by a plugin AST\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_PARSER_PROVIDER_BINARY_PLUGIN,
       std::string("-fplugin=") + NEVERC_TEST_SEMA_PROVIDER_PLUGIN,
       "-std=c11", "-Werror", "-O2", "-S", "-emit-llvm", Source.string(),
       "-o", IRPath.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(IRPath).find("ret i32 42"), std::string::npos);
}

TEST_F(PluginSemaProviderTest,
       RejectsCustomSemanticProductWithoutMatchingIRProvider) {
  const fs::path Source = tmpFile("sema_provider_custom_product.c");
  writeFile(Source, "this source is replaced by a plugin AST\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_PARSER_PROVIDER_BINARY_PLUGIN,
       std::string("-fplugin=") + NEVERC_TEST_SEMA_PROVIDER_CUSTOM_PLUGIN,
       "-std=c11", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("no matching downstream IR provider"),
            std::string::npos)
      << Result.err;
}
