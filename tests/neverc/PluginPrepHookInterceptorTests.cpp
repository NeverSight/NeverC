#include "NeverCTestFixture.h"

class PluginPrepHookInterceptorTest : public NeverCTest {};

TEST_F(PluginPrepHookInterceptorTest,
       InterceptsIncludeMacroPragmaAndFeatureQueries) {
  const fs::path Source = tmpFile("prep_hook_interceptor.c");
  const fs::path Header = tmpFile("plugin_target.h");
  const fs::path IRPath = tmpFile("prep_hook_interceptor.ll");
  writeFile(Header, "#define TARGET_VALUE 10\n");
  writeFile(Source, R"(
#include "plugin_redirect.h"
#include "plugin_skip.h"

#define PLUGIN_SUPPRESS_DEFINE 100
#ifdef PLUGIN_SUPPRESS_DEFINE
#define SUPPRESSED_VALUE 1000
#else
#define SUPPRESSED_VALUE 0
#endif

#define PLUGIN_KEEP 3
#undef PLUGIN_KEEP
#define PLUGIN_VALUE 1

#pragma plugin handled
#pragma plugin_replace
#pragma mark plugin_passthrough
_Pragma("mark plugin_operator_passthrough")

#if __has_feature(plugin_feature)
#define FEATURE_VALUE 1
#else
#define FEATURE_VALUE 0
#endif
#if __has_extension(plugin_extension)
#define EXTENSION_VALUE 1
#else
#define EXTENSION_VALUE 0
#endif
#if __has_builtin(plugin_builtin)
#define BUILTIN_VALUE 1
#else
#define BUILTIN_VALUE 0
#endif
#if __has_include("plugin_header.h")
#define INCLUDE_VALUE 1
#else
#define INCLUDE_VALUE 0
#endif

int prep_hook_result(void) {
  return TARGET_VALUE + SUPPRESSED_VALUE + PLUGIN_KEEP + PLUGIN_VALUE +
         __LINE__ + FEATURE_VALUE + EXTENSION_VALUE + BUILTIN_VALUE +
         INCLUDE_VALUE;
}
)");

  CmdResult Baseline =
      ncc({"-std=c11", "-Werror", "-I", tmp().string(), "-S", "-emit-llvm",
           Source.string(), "-o", IRPath.string()});
  EXPECT_NE(Baseline.exitCode, 0)
      << "the control compile must reject plugin-only prep behavior";

  CmdResult Rewritten =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PREP_HOOK_PLUGIN, "-std=c11",
           "-Werror", "-O2", "-I", tmp().string(), "-S", "-emit-llvm",
           Source.string(), "-o", IRPath.string()});
  ASSERT_EQ(Rewritten.exitCode, 0) << Rewritten.err;

  const std::string IR = readFile(IRPath);
  EXPECT_NE(IR.find("ret i32 59"), std::string::npos) << IR;
  EXPECT_EQ(IR.find("PLUGIN_"), std::string::npos) << IR;
}

TEST_F(PluginPrepHookInterceptorTest, RejectsMalformedIncludeReplacement) {
  const fs::path Source = tmpFile("prep_hook_invalid.c");
  const fs::path IRPath = tmpFile("prep_hook_invalid.ll");
  writeFile(Source, "#include \"plugin_invalid.h\"\nint value;\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PREP_HOOK_PLUGIN, "-std=c11",
           "-S", "-emit-llvm", Source.string(), "-o", IRPath.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("neverc.prep.include.intercept"), std::string::npos)
      << Result.err;
}

TEST_F(PluginPrepHookInterceptorTest, PropagatesCancellation) {
  const fs::path Source = tmpFile("prep_hook_cancel.c");
  const fs::path IRPath = tmpFile("prep_hook_cancel.ll");
  writeFile(Source, "#include \"plugin_cancel.h\"\nint value;\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PREP_HOOK_PLUGIN, "-std=c11",
           "-S", "-emit-llvm", Source.string(), "-o", IRPath.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("cancel"), std::string::npos) << Result.err;
}
