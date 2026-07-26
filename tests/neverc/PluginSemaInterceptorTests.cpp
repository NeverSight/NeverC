#include "NeverCTestFixture.h"

class PluginSemaInterceptorTest : public NeverCTest {};

TEST_F(PluginSemaInterceptorTest,
       ExpressionReplacementBypassesBuiltinBinaryAnalysis) {
  const fs::path Source = tmpFile("sema_expression_interceptor.c");
  const fs::path BaselineIR = tmpFile("sema_expression_baseline.ll");
  const fs::path ReplacedIR = tmpFile("sema_expression_replaced.ll");
  writeFile(Source, R"(
int sema_expression_result(void) {
  return 40 + 2;
}
)");

  CmdResult Baseline =
      ncc({"-std=c11", "-Werror", "-O2", "-S", "-emit-llvm",
           Source.string(), "-o", BaselineIR.string()});
  ASSERT_EQ(Baseline.exitCode, 0) << Baseline.err;
  EXPECT_NE(readFile(BaselineIR).find("ret i32 42"), std::string::npos);

  CmdResult Replaced =
      ncc({std::string("-fplugin=") + NEVERC_TEST_SEMA_INTERCEPTOR_PLUGIN,
           "-std=c11", "-Werror", "-O2", "-S", "-emit-llvm", Source.string(),
           "-o", ReplacedIR.string()});
  ASSERT_EQ(Replaced.exitCode, 0) << Replaced.err;
  const std::string IR = readFile(ReplacedIR);
  EXPECT_NE(IR.find("ret i32 40"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("ret i32 42"), std::string::npos) << IR;
}

TEST_F(PluginSemaInterceptorTest,
       StatementReplacementBypassesBuiltinCompoundAnalysis) {
  const fs::path Source = tmpFile("sema_statement_interceptor.c");
  const fs::path BaselineIR = tmpFile("sema_statement_baseline.ll");
  const fs::path ReplacedIR = tmpFile("sema_statement_replaced.ll");
  writeFile(Source, R"(
int sema_statement_result(void) {
  return 1;
  return 2;
}
)");

  CmdResult Baseline =
      ncc({"-std=c11", "-Werror", "-O2", "-S", "-emit-llvm",
           Source.string(), "-o", BaselineIR.string()});
  ASSERT_EQ(Baseline.exitCode, 0) << Baseline.err;
  EXPECT_NE(readFile(BaselineIR).find("ret i32 1"), std::string::npos);

  CmdResult Replaced =
      ncc({std::string("-fplugin=") + NEVERC_TEST_SEMA_INTERCEPTOR_PLUGIN,
           "-std=c11", "-Werror", "-O2", "-S", "-emit-llvm", Source.string(),
           "-o", ReplacedIR.string()});
  ASSERT_EQ(Replaced.exitCode, 0) << Replaced.err;
  const std::string IR = readFile(ReplacedIR);
  EXPECT_NE(IR.find("ret i32 2"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("ret i32 1"), std::string::npos) << IR;
}

TEST_F(PluginSemaInterceptorTest,
       DeclarationReplacementRedirectsReferenceBinding) {
  const fs::path Source = tmpFile("sema_declaration_interceptor.c");
  const fs::path BaselineIR = tmpFile("sema_declaration_baseline.ll");
  const fs::path ReplacedIR = tmpFile("sema_declaration_replaced.ll");
  writeFile(Source, R"(
int sema_decl_original = 1;
int sema_decl_replacement = 2;
int sema_declaration_result(void) {
  return sema_decl_original;
}
)");

  CmdResult Baseline =
      ncc({"-std=c11", "-O0", "-S", "-emit-llvm", Source.string(), "-o",
           BaselineIR.string()});
  ASSERT_EQ(Baseline.exitCode, 0) << Baseline.err;
  EXPECT_NE(readFile(BaselineIR).find("ptr @sema_decl_original"),
            std::string::npos);

  CmdResult Replaced =
      ncc({std::string("-fplugin=") + NEVERC_TEST_SEMA_INTERCEPTOR_PLUGIN,
           "-std=c11", "-O0", "-S", "-emit-llvm", Source.string(), "-o",
           ReplacedIR.string()});
  ASSERT_EQ(Replaced.exitCode, 0) << Replaced.err;
  const std::string IR = readFile(ReplacedIR);
  EXPECT_NE(IR.find("ptr @sema_decl_replacement"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("ptr @sema_decl_original"), std::string::npos) << IR;
}

TEST_F(PluginSemaInterceptorTest, TypeReplacementResolvesPluginTypeName) {
  const fs::path Source = tmpFile("sema_type_interceptor.c");
  const fs::path Output = tmpFile("sema_type_replaced.ll");
  writeFile(Source, R"(
plugin_int sema_type_result(void) {
  return 42;
}
)");

  CmdResult Baseline = ncc({"-std=c11", "-fsyntax-only", Source.string()});
  EXPECT_NE(Baseline.exitCode, 0) << Baseline.err;

  CmdResult Replaced =
      ncc({std::string("-fplugin=") + NEVERC_TEST_SEMA_INTERCEPTOR_PLUGIN,
           "-std=c11", "-O2", "-S", "-emit-llvm", Source.string(), "-o",
           Output.string()});
  ASSERT_EQ(Replaced.exitCode, 0) << Replaced.err;
  EXPECT_NE(readFile(Output).find("ret i32 42"), std::string::npos);
}

TEST_F(PluginSemaInterceptorTest, LookupReplacementResolvesPluginAlias) {
  const fs::path Source = tmpFile("sema_lookup_interceptor.c");
  const fs::path Output = tmpFile("sema_lookup_replaced.ll");
  writeFile(Source, R"(
int sema_lookup_target = 7;
int sema_decl_replacement = 9;
int sema_lookup_result(void) {
  return sema_lookup_alias;
}
)");

  CmdResult Baseline = ncc({"-std=c11", "-fsyntax-only", Source.string()});
  EXPECT_NE(Baseline.exitCode, 0) << Baseline.err;

  CmdResult Replaced =
      ncc({std::string("-fplugin=") + NEVERC_TEST_SEMA_INTERCEPTOR_PLUGIN,
           "-std=c11", "-O0", "-S", "-emit-llvm", Source.string(), "-o",
           Output.string()});
  ASSERT_EQ(Replaced.exitCode, 0) << Replaced.err;
  EXPECT_NE(readFile(Output).find("ptr @sema_decl_replacement"),
            std::string::npos);
}

TEST_F(PluginSemaInterceptorTest,
       ConversionReplacementMakesReturnConversionExplicit) {
  const fs::path Source = tmpFile("sema_conversion_interceptor.c");
  const fs::path Output = tmpFile("sema_conversion_replaced.ll");
  writeFile(Source, R"(
int *sema_conversion_result(void) {
  return 1;
}
)");

  CmdResult Baseline =
      ncc({"-std=c11", "-Werror", "-fsyntax-only", Source.string()});
  EXPECT_NE(Baseline.exitCode, 0) << Baseline.err;

  CmdResult Replaced =
      ncc({std::string("-fplugin=") + NEVERC_TEST_SEMA_INTERCEPTOR_PLUGIN,
           "-std=c11", "-Werror", "-O0", "-S", "-emit-llvm",
           Source.string(), "-o", Output.string()});
  ASSERT_EQ(Replaced.exitCode, 0) << Replaced.err;
  EXPECT_NE(readFile(Output).find("inttoptr"), std::string::npos);
}
