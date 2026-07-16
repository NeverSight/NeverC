#include "NeverCTestFixture.h"

class PluginVFSIntegrationTest : public NeverCTest {};

TEST_F(PluginVFSIntegrationTest,
       CompilesIncludeProvidedOnlyByPluginVFS) {
  const fs::path Source = tmpFile("plugin_vfs_input.c");
  const fs::path Dependencies = tmpFile("plugin_vfs_input.d");
  writeFile(Source,
            "#include \"plugin/virtual.h\"\n"
            "int use_plugin_header(void) { return one; }\n");

  CmdResult Result = ncc(
      {std::string("-fplugin=") + NEVERC_TEST_VFS_PLUGIN,
       "-fsyntax-only", "-MMD", "-MF", Dependencies.string(),
       Source.string()});

  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  ASSERT_TRUE(fs::exists(Dependencies));
  EXPECT_NE(readFile(Dependencies).find("plugin/virtual.h"),
            std::string::npos);
}
