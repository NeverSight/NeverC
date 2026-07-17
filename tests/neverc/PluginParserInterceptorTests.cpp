#include "NeverCTestFixture.h"

class PluginParserInterceptorTest : public NeverCTest {};

TEST_F(PluginParserInterceptorTest,
       ExtendsDeclarationStatementExpressionTypeAndKeywordSyntax) {
  const fs::path Source = tmpFile("parser_interceptor.c");
  const fs::path IRPath = tmpFile("parser_interceptor.ll");
  writeFile(Source, R"(
__neverc_test_decl;

int plugin_type_result =
    __builtin_types_compatible_p(__neverc_test_type, int);

int plugin_parser_result(void) {
  __neverc_test_stmt;
  return __neverc_test_expr + __neverc_test_keyword;
}
)");

  CmdResult Baseline = ncc({"-std=c11", "-Werror", "-S", "-emit-llvm",
                            Source.string(), "-o", IRPath.string()});
  EXPECT_NE(Baseline.exitCode, 0)
      << "the control compile must reject plugin-only syntax";

  CmdResult Extended =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PARSER_INTERCEPTOR_PLUGIN,
           "-std=c11", "-Werror", "-O2", "-S", "-emit-llvm", Source.string(),
           "-o", IRPath.string()});
  ASSERT_EQ(Extended.exitCode, 0) << Extended.err;

  const std::string IR = readFile(IRPath);
  EXPECT_NE(IR.find("ret i32 42"), std::string::npos) << IR;
  EXPECT_NE(IR.find("@plugin_type_result"), std::string::npos) << IR;
}

TEST_F(PluginParserInterceptorTest,
       ConvertsNamespacedAttributeIntoHostParsedAttribute) {
  const fs::path Source = tmpFile("parser_attribute.c");
  const fs::path IRPath = tmpFile("parser_attribute.ll");
  writeFile(Source, R"(
[[neverc::plugin_unused]] static int plugin_unused;
int parser_attribute_result(void) { return 1; }
)");

  CmdResult Extended =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PARSER_INTERCEPTOR_PLUGIN,
           "-std=c11", "-Werror", "-S", "-emit-llvm", Source.string(), "-o",
           IRPath.string()});
  ASSERT_EQ(Extended.exitCode, 0) << Extended.err;
}

TEST_F(PluginParserInterceptorTest,
       RollsBackSpeculativeCursorWhenInterceptorDoesNotHandle) {
  const fs::path Source = tmpFile("parser_cursor_rollback.c");
  const fs::path IRPath = tmpFile("parser_cursor_rollback.ll");
  writeFile(Source, R"(
static int __neverc_test_probe = 40;
int parser_cursor_rollback(void) { return __neverc_test_probe + 2; }
)");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PARSER_INTERCEPTOR_PLUGIN,
           "-std=c11", "-Werror", "-O2", "-S", "-emit-llvm", Source.string(),
           "-o", IRPath.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(IRPath).find("ret i32 42"), std::string::npos);
}

TEST_F(PluginParserInterceptorTest,
       RestoresParserStateAfterHalfConsumedInterceptorFailure) {
  const fs::path Source = tmpFile("parser_cursor_failure.c");
  const fs::path IRPath = tmpFile("parser_cursor_failure.ll");
  writeFile(Source, R"(
int parser_cursor_failure(void) {
  return __neverc_test_error + 1;
}
)");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_PARSER_INTERCEPTOR_PLUGIN,
       "-std=c11", "-S", "-emit-llvm", Source.string(), "-o", IRPath.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("neverc.syntax.extension.expression"),
            std::string::npos)
      << Result.err;
}
