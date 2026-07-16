#include "NeverCTestFixture.h"

class PluginPreprocessorProviderTest : public NeverCTest {};

TEST_F(PluginPreprocessorProviderTest,
       ProviderReplacesInvalidSourceWithVerifiedTokenStream) {
  const fs::path Source = tmpFile("preprocessor_provider.c");
  writeFile(Source, "int broken_source( { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PREP_PROVIDER_PLUGIN,
           "-fsyntax-only", Source.string()});

  EXPECT_EQ(Result.exitCode, 0) << Result.err;
}

TEST_F(PluginPreprocessorProviderTest, ProviderOutputWithoutEOFIsRejected) {
  const fs::path Source = tmpFile("preprocessor_provider_missing_eof.c");
  writeFile(Source, "int disk_source(void) { return 0; }\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_PREP_PROVIDER_MISSING_EOF_PLUGIN,
       "-fsyntax-only", Source.string()});

  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("missing EOF"), std::string::npos) << Result.err;
}

TEST_F(PluginPreprocessorProviderTest,
       ProviderCannotPublishTokenWithoutSourceMapping) {
  const fs::path Source = tmpFile("preprocessor_provider_unmapped.c");
  writeFile(Source, "int disk_source(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PREP_PROVIDER_UNMAPPED_PLUGIN,
           "-fsyntax-only", Source.string()});

  EXPECT_NE(Result.exitCode, 0) << Result.err;
}

TEST_F(PluginPreprocessorProviderTest, ProviderCannotExceedTokenStreamLimit) {
  const fs::path Source = tmpFile("preprocessor_provider_over_limit.c");
  writeFile(Source, "int disk_source(void) { return 0; }\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_PREP_PROVIDER_OVER_LIMIT_PLUGIN,
       "-fsyntax-only", Source.string()});

  EXPECT_NE(Result.exitCode, 0) << Result.err;
}

TEST_F(PluginPreprocessorProviderTest, ProviderErrorAbortsPreprocessing) {
  const fs::path Source = tmpFile("preprocessor_provider_error.c");
  writeFile(Source, "int disk_source(void) { return 0; }\n");

  CmdResult Result =
      ncc({std::string("-fplugin=") + NEVERC_TEST_PREP_PROVIDER_ERROR_PLUGIN,
           "-fsyntax-only", Source.string()});

  EXPECT_NE(Result.exitCode, 0) << Result.err;
}
