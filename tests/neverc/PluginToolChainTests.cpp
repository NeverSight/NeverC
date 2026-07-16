#include "NeverCTestFixture.h"

class PluginToolChainTest : public NeverCTest {
protected:
  void expectAArch64Assembly(const char *PluginPath, const std::string &Stem) {
    const fs::path Source = tmpFile(Stem + ".c");
    const fs::path Assembly = tmpFile(Stem + ".s");
    writeFile(Source, R"(
int neverc_toolchain_plus_one(int value) {
  return value + 1;
}
)");

    CmdResult Result = ncc({std::string("-fplugin=") + PluginPath,
                            "--target=x86_64-unknown-linux-gnu", "-O2", "-S",
                            Source.string(), "-o", Assembly.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;

    const std::string Output = readFile(Assembly);
    EXPECT_NE(Output.find("add\tw0, w0, #1"), std::string::npos) << Output;
    EXPECT_EQ(Output.find("%eax"), std::string::npos) << Output;
  }
};

TEST_F(PluginToolChainTest, InterceptorRewritesToolChainRequest) {
  expectAArch64Assembly(NEVERC_TEST_TOOLCHAIN_REWRITE_PLUGIN,
                        "toolchain_interceptor");
}

TEST_F(PluginToolChainTest, ReplacementPublishesBuiltinSelection) {
  expectAArch64Assembly(NEVERC_TEST_TOOLCHAIN_REPLACE_PLUGIN,
                        "toolchain_replacement");
}

TEST_F(PluginToolChainTest, InterceptorForwardsCPUAndFeatures) {
  const fs::path Source = tmpFile("toolchain_features.c");
  writeFile(Source, "int toolchain_features(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_TOOLCHAIN_REWRITE_PLUGIN,
           "--target=x86_64-unknown-linux-gnu", "-###", "-c", Source.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  EXPECT_NE(Result.err.find("\"-target-cpu\" \"generic\""), std::string::npos)
      << Result.err;
  EXPECT_NE(Result.err.find("\"-target-feature\" \"+neon\""), std::string::npos)
      << Result.err;
}

TEST_F(PluginToolChainTest, UnknownTripleIsRejectedBySelectionVerifier) {
  const fs::path Source = tmpFile("toolchain_invalid.c");
  writeFile(Source, "int toolchain_invalid(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_TOOLCHAIN_INVALID_PLUGIN,
           "--target=x86_64-unknown-linux-gnu", "-S", Source.string(), "-o",
           tmpFile("toolchain_invalid.s").string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("unknown or unsupported toolchain target triple"),
            std::string::npos)
      << Result.err;
}

TEST_F(PluginToolChainTest, CustomProviderNeedsTargetCapability) {
  const fs::path Source = tmpFile("toolchain_custom.c");
  writeFile(Source, "int toolchain_custom(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_TOOLCHAIN_CUSTOM_PLUGIN,
           "--target=x86_64-unknown-linux-gnu", "-S", Source.string(), "-o",
           tmpFile("toolchain_custom.s").string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("provider exists but target capability is missing"),
            std::string::npos)
      << Result.err;
}
