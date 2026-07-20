#include "NeverCTestFixture.h"

class PluginAssemblerTest : public NeverCTest {};

TEST_F(PluginAssemblerTest, BuiltinAssemblerCompilesRawSourceWithoutCAST) {
  const fs::path Input =
      testDir() / "Inputs" / "Plugin" / "assembler-test.s";
  const fs::path Output = tmpFile("assembler-test.o");

  CmdResult Result =
      ncc({"-c", Input.string(), "-o", Output.string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Output));
  EXPECT_GT(fileSize(Output), 0U);
}

TEST_F(PluginAssemblerTest,
       BuiltinAssemblerCompilesPreprocessedSourceWithoutCAST) {
  const fs::path Input = testDir() / "Inputs" / "Plugin" /
                         "Preprocessed" / "assembler-test.S";
  const fs::path Output = tmpFile("assembler-test-preprocessed.o");

  CmdResult Result =
      ncc({"-c", Input.string(), "-o", Output.string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Output));
  EXPECT_GT(fileSize(Output), 0U);
}

TEST_F(PluginAssemblerTest,
       PluginParserAndPrinterReplaceAssemblySyntax) {
  const fs::path Input = testDir() / "Inputs" / "Plugin" /
                         "assembly-provider-test.s";
  const fs::path Output = tmpFile("assembly-provider-test.o");

  CmdResult Result = ncc(
      {std::string("-fplugin=") +
           NEVERC_TEST_ASSEMBLY_PROVIDER_PLUGIN,
       "--target=test.assembly", "-c", Input.string(), "-o",
       Output.string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_TRUE(fs::exists(Output));
  EXPECT_GT(fileSize(Output), 0U);
}
