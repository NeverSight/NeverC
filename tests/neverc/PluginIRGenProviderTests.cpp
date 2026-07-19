#include "NeverCTestFixture.h"

class PluginIRGenProviderTest : public NeverCTest {};

TEST_F(PluginIRGenProviderTest,
       ProviderBypassesBuiltinIRGenAndPublishesExecutableModule) {
  const fs::path Source = tmpFile("irgen_provider.c");
  const fs::path IR = tmpFile("irgen_provider.ll");
  const fs::path Executable = tmpFile("irgen_provider");
  writeFile(Source,
            "int builtin_only(void) { return 7; }\n"
            "int main(void) { return builtin_only(); }\n");

  CmdResult EmitIR =
      ncc({std::string("-fplugin=") + NEVERC_TEST_IRGEN_PROVIDER_PLUGIN,
           "-std=c11", "-S", "-emit-llvm", Source.string(), "-o",
           IR.string()});
  ASSERT_EQ(EmitIR.exitCode, 0) << EmitIR.err;
  const std::string Module = readFile(IR);
  EXPECT_EQ(Module.find("builtin_only"), std::string::npos);
  EXPECT_NE(Module.find("define i32 @main()"), std::string::npos);
  EXPECT_NE(Module.find("ret i32 42"), std::string::npos);

  std::vector<std::string> Arguments = {
      std::string("-fplugin=") + NEVERC_TEST_IRGEN_PROVIDER_PLUGIN,
      "-std=c11", Source.string(), "-o", Executable.string()};
  std::vector<std::string> LinkArguments = linkFlags();
  Arguments.insert(Arguments.end(), LinkArguments.begin(), LinkArguments.end());
  CmdResult Compile = ncc(Arguments);
  ASSERT_EQ(Compile.exitCode, 0) << Compile.err;

  CmdResult Run = exec(Executable.string(), {});
  EXPECT_EQ(Run.exitCode, 42) << Run.out << Run.err;
}

TEST_F(PluginIRGenProviderTest,
       ProviderAcceptsTheCustomSemanticProductItImplements) {
  const fs::path Source = tmpFile("irgen_custom_semantic_product.c");
  const fs::path IR = tmpFile("irgen_custom_semantic_product.ll");
  writeFile(Source, "this source is replaced by plugin providers\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_PARSER_PROVIDER_BINARY_PLUGIN,
       std::string("-fplugin=") + NEVERC_TEST_SEMA_PROVIDER_CUSTOM_PLUGIN,
       std::string("-fplugin=") + NEVERC_TEST_IRGEN_PROVIDER_PLUGIN,
       "-std=c11", "-S", "-emit-llvm", Source.string(), "-o", IR.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(IR).find("ret i32 42"), std::string::npos);
}
