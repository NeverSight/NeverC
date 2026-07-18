#include "NeverCTestFixture.h"

class PluginParserProviderTest : public NeverCTest {};

TEST_F(PluginParserProviderTest, ReplacesBuiltinParserAndPublishesASTUnit) {
  const fs::path Source = tmpFile("parser_provider.c");
  writeFile(Source, "this is deliberately not valid C\n");

  CmdResult Baseline =
      ncc({"-std=c11", "-Werror", "-fsyntax-only", Source.string()});
  EXPECT_NE(Baseline.exitCode, 0)
      << "the control compile must reach and fail in the builtin parser";

  CmdResult Replaced =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PARSER_PROVIDER_PLUGIN,
           "-std=c11", "-Werror", "-fsyntax-only", Source.string()});
  EXPECT_EQ(Replaced.exitCode, 0) << Replaced.err;
}

TEST_F(PluginParserProviderTest,
       ReplaysPluginASTAndPublishesItToDownstreamIRGeneration) {
  const fs::path Source = tmpFile("parser_provider_ir.c");
  const fs::path IRPath = tmpFile("parser_provider_ir.ll");
  writeFile(Source, "this input must never reach the builtin parser\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PARSER_PROVIDER_PLUGIN,
           "-std=c11", "-Werror", "-O2", "-S", "-emit-llvm", Source.string(),
           "-o", IRPath.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(IRPath).find("ret i32 42"), std::string::npos);
}

TEST_F(PluginParserProviderTest, RejectsASTUnitWithoutTranslationUnitRoot) {
  const fs::path Source = tmpFile("parser_provider_missing_root.c");
  writeFile(Source, "int ignored_by_provider;\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_PARSER_PROVIDER_MISSING_ROOT_PLUGIN,
       "-std=c11", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("neverc.syntax.parse"), std::string::npos)
      << Result.err;
}

TEST_F(PluginParserProviderTest,
       InterceptorInspectsTokensAndForwardsToBuiltinParser) {
  const fs::path Source = tmpFile("parser_phase_interceptor.c");
  const fs::path IRPath = tmpFile("parser_phase_interceptor.ll");
  writeFile(Source, "int parser_phase_result(void) { return 42; }\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_PARSER_PHASE_INTERCEPTOR_PLUGIN,
       "-std=c11", "-Werror", "-O2", "-S", "-emit-llvm", Source.string(),
       "-o", IRPath.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(IRPath).find("ret i32 42"), std::string::npos);
}
