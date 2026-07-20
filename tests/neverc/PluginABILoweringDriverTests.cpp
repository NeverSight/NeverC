#include "NeverCTestFixture.h"

class PluginABILoweringDriverTest : public NeverCTest {};
class PluginTargetLanguageDriverTest : public NeverCTest {};

TEST_F(PluginABILoweringDriverTest,
       PluginCallingConventionPlanReachesX86Backend) {
  const fs::path Source = tmpFile("plugin_cc_plan.c");
  const fs::path IR = tmpFile("plugin_cc_plan.ll");
  const fs::path Assembly = tmpFile("plugin_cc_plan.s");
  writeFile(
      Source,
      "__attribute__((noinline)) int plugin_cc_add1(int value) {\n"
      "  return value + 1;\n"
      "}\n"
      "int plugin_cc_call(void) { return plugin_cc_add1(41); }\n");

  const std::vector<std::string> Common = {
      std::string("-fplugin=") + NEVERC_TEST_TARGET_CC_PLAN_PLUGIN,
      "-target", "test.fixture-target-cc-plan", "-O2"};
  std::vector<std::string> IRArguments = Common;
  IRArguments.insert(
      IRArguments.end(),
      {"-S", "-emit-llvm", Source.string(), "-o", IR.string()});
  CmdResult IRResult = ncc(IRArguments);
  ASSERT_EQ(IRResult.exitCode, 0) << IRResult.err;
  const std::string Module = readFile(IR);
  EXPECT_NE(Module.find("\"neverc-cc-plan-v1\"="),
            std::string::npos);
  EXPECT_NE(Module.find(
                "schema=0123456789abcdef0123456789abcdef"),
            std::string::npos);
  EXPECT_EQ(Module.find("\"neverc-callconv\"="),
            std::string::npos);

  std::vector<std::string> AssemblyArguments = Common;
  AssemblyArguments.insert(
      AssemblyArguments.end(),
      {"-S", Source.string(), "-o", Assembly.string()});
  CmdResult AssemblyResult = ncc(AssemblyArguments);
  ASSERT_EQ(AssemblyResult.exitCode, 0) << AssemblyResult.err;
  EXPECT_NE(readFile(Assembly).find("$41, %ecx"),
            std::string::npos);
}

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
       IndirectAliasedAggregateLowersWithByRefAttribute) {
  const fs::path Source = tmpFile("plugin_abi_indirect_aliased.c");
  const fs::path IR = tmpFile("plugin_abi_indirect_aliased.ll");
  writeFile(
      Source,
      "typedef struct { long first; long second; } PluginPair;\n"
      "long plugin_abi_pair_first(PluginPair value) {\n"
      "  return value.first;\n"
      "}\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") +
           NEVERC_TEST_TARGET_INDIRECT_ALIASED_PLUGIN,
       "-target", "test.fixture-target-indirect-aliased", "-S",
       "-emit-llvm", Source.string(), "-o", IR.string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  const std::string Module = readFile(IR);
  EXPECT_NE(Module.find("byref("), std::string::npos);
  EXPECT_EQ(Module.find("byval("), std::string::npos);
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

TEST_F(PluginABILoweringDriverTest,
       PluginABIEmitsMicrosoftVariadicAccess) {
  const fs::path Source = tmpFile("plugin_abi_ms_variadic.c");
  const fs::path IR = tmpFile("plugin_abi_ms_variadic.ll");
  writeFile(
      Source,
      "int plugin_abi_ms_first(__builtin_ms_va_list arguments) {\n"
      "  return __builtin_va_arg(arguments, int);\n"
      "}\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_TARGET_MS_VA_PLUGIN,
       "-target", "test.fixture-target-ms-va", "-S", "-emit-llvm",
       Source.string(), "-o", IR.string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(readFile(IR).find("va_arg"), std::string::npos);
}

TEST_F(PluginABILoweringDriverTest,
       CoerceAndExpandLowersPaddedAggregateToScalarPieces) {
  const fs::path Source = tmpFile("plugin_abi_coerce_expand.c");
  const fs::path IR = tmpFile("plugin_abi_coerce_expand.ll");
  writeFile(
      Source,
      "typedef struct { int first; double second; } PluginPair;\n"
      "double plugin_abi_pair_sum(PluginPair value) {\n"
      "  return value.first + value.second;\n"
      "}\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") +
           NEVERC_TEST_TARGET_COERCE_EXPAND_PLUGIN,
       "-target", "test.fixture-target-coerce-expand", "-S",
       "-emit-llvm", Source.string(), "-o", IR.string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  const std::string Module = readFile(IR);
  EXPECT_NE(Module.find(
                "define double @plugin_abi_pair_sum(i32"),
            std::string::npos);
  EXPECT_NE(Module.find(", double"), std::string::npos);
  EXPECT_EQ(Module.find("byval"), std::string::npos);
}

TEST_F(PluginTargetLanguageDriverTest,
       MacroBuiltinCPUAndConstraintsReachFrontendIR) {
  const fs::path Source = tmpFile("plugin_target_language.c");
  const fs::path IR = tmpFile("plugin_target_language.ll");
  writeFile(
      Source,
      "#ifndef __LANG_TARGET__\n"
      "#error target macro was not defined\n"
      "#endif\n"
      "int plugin_macro_value = __LANG_TARGET__;\n"
      "int plugin_add(int left, int right) {\n"
      "  return __builtin_lang_add(left, right);\n"
      "}\n"
      "int plugin_constraint(int input) {\n"
      "  int output;\n"
      "  __asm__(\"\" : \"=r\"(output) : \"I\"(3), \"r\"(input));\n"
      "  return output;\n"
      "}\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_TARGET_LANGUAGE_PLUGIN,
       "-target", "test.language-target", "-mcpu=turbo", "-S",
       "-emit-llvm", Source.string(), "-o", IR.string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_EQ(Result.err.find("unsupported option '-mcpu='"),
            std::string::npos)
      << Result.err;
  const std::string Module = readFile(IR);
  EXPECT_NE(Module.find("@plugin_macro_value"), std::string::npos);
  EXPECT_NE(Module.find("i32 42"), std::string::npos);
  EXPECT_NE(Module.find("plugin.add = add i32"), std::string::npos);
  EXPECT_NE(Module.find("asm"), std::string::npos);
  EXPECT_NE(Module.find("\"=r,I,r"), std::string::npos);
  EXPECT_NE(Module.find("\"target-cpu\"=\"fast\""),
            std::string::npos);
}
