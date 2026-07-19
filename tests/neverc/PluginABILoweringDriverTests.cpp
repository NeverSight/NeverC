#include "NeverCTestFixture.h"

class PluginABILoweringDriverTest : public NeverCTest {};

TEST_F(PluginABILoweringDriverTest,
       SameSignatureLowersDifferentlyForTwoPluginTargets) {
  const fs::path Source = tmpFile("plugin_abi_signature.c");
  const fs::path DirectIR = tmpFile("plugin_abi_direct.ll");
  const fs::path IndirectIR = tmpFile("plugin_abi_indirect.ll");
  writeFile(Source,
            "int plugin_abi_plus_one(int value) { return value + 1; }\n");

  CmdResult Direct = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_TARGET_VALID_PLUGIN,
       "-target", "test.fixture-target", "-S", "-emit-llvm",
       Source.string(), "-o", DirectIR.string()});
  ASSERT_EQ(Direct.exitCode, 0) << Direct.err;
  CmdResult Indirect = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_TARGET_INDIRECT_PLUGIN,
       "-target", "test.fixture-target-indirect", "-S", "-emit-llvm",
       Source.string(), "-o", IndirectIR.string()});
  ASSERT_EQ(Indirect.exitCode, 0) << Indirect.err;

  const std::string DirectModule = readFile(DirectIR);
  const std::string IndirectModule = readFile(IndirectIR);
  EXPECT_NE(DirectModule.find("@plugin_abi_plus_one(i32"),
            std::string::npos);
  EXPECT_NE(IndirectModule.find("@plugin_abi_plus_one(ptr"),
            std::string::npos);
  EXPECT_NE(IndirectModule.find("byval(i32)"), std::string::npos);
}

TEST_F(PluginABILoweringDriverTest,
       PluginABIEmitsVariadicAccess) {
  const fs::path Source = tmpFile("plugin_abi_variadic.c");
  const fs::path IR = tmpFile("plugin_abi_variadic.ll");
  writeFile(
      Source,
      "#include <stdarg.h>\n"
      "int plugin_abi_first(int count, ...) {\n"
      "  va_list arguments;\n"
      "  va_start(arguments, count);\n"
      "  int value = va_arg(arguments, int);\n"
      "  va_end(arguments);\n"
      "  return value;\n"
      "}\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_TARGET_VALID_PLUGIN,
       "-target", "test.fixture-target", "-S", "-emit-llvm",
       Source.string(), "-o", IR.string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(IR).find("va_arg"), std::string::npos);
}
