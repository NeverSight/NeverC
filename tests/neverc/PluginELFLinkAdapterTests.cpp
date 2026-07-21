#include "NeverCTestFixture.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <string>

namespace {

class PluginELFLinkAdapterTest : public NeverCTest {};

bool hasELFMagic(const std::string &Bytes) {
  return Bytes.size() >= 4 && static_cast<uint8_t>(Bytes[0]) == UINT8_C(0x7f) &&
         Bytes[1] == 'E' && Bytes[2] == 'L' && Bytes[3] == 'F';
}

TEST_F(PluginELFLinkAdapterTest,
       ActivatedSessionPreservesBuiltinELFLinkOutput) {
  const fs::path Source = tmpFile("plugin-elf-adapter.c");
  writeFile(Source, "static volatile int plugin_elf_adapter_data = 42;\n"
                    "int plugin_elf_adapter_answer(void) {\n"
                    "  return plugin_elf_adapter_data;\n"
                    "}\n"
                    "void _start(void) {\n"
                    "  for (;;) plugin_elf_adapter_data = "
                    "plugin_elf_adapter_answer();\n"
                    "}\n");

  struct OutputCase {
    const char *Name;
    std::vector<std::string> Flags;
  };
  const std::vector<OutputCase> OutputCases = {
      {"shared", {"-fPIC", "-shared"}},
      {"pie", {"-fPIE", "-pie", "-Wl,-e,_start"}},
      {"static", {"-fno-pic", "-static", "-Wl,-e,_start"}},
      {"relocatable", {"-r"}},
  };
  const std::vector<std::string> Targets = {
      "x86_64-unknown-linux-gnu",
      "aarch64-unknown-linux-gnu",
  };

  for (const std::string &Target : Targets) {
    for (const OutputCase &Output : OutputCases) {
      SCOPED_TRACE(Target + "/" + Output.Name);
      const std::string Stem = "plugin-elf-" +
                               Target.substr(0, Target.find('-')) + "-" +
                               Output.Name;
      const fs::path Baseline = tmpFile(Stem + "-baseline");
      const fs::path WithPlugin = tmpFile(Stem + "-session");

      std::vector<std::string> Common = {"--no-default-config",
                                         "--target=" + Target, "-O0",
                                         "-fno-lto", "-nostdlib"};
      Common.insert(Common.end(), Output.Flags.begin(), Output.Flags.end());
      Common.push_back(Source.string());

      std::vector<std::string> BaselineArguments = Common;
      BaselineArguments.insert(BaselineArguments.end(),
                               {"-o", Baseline.string()});
      CmdResult BaselineResult = ncc(BaselineArguments);
      ASSERT_EQ(BaselineResult.exitCode, 0) << BaselineResult.err;

      std::vector<std::string> PluginArguments = Common;
      PluginArguments.insert(PluginArguments.begin() + 1,
                             std::string("-fplugin=") +
                                 NEVERC_TEST_TARGET_VALID_PLUGIN);
      PluginArguments.insert(PluginArguments.end(),
                             {"-o", WithPlugin.string()});
      CmdResult PluginResult = ncc(PluginArguments);
      ASSERT_EQ(PluginResult.exitCode, 0) << PluginResult.err;

      const std::string BaselineBytes = readFile(Baseline);
      const std::string PluginBytes = readFile(WithPlugin);
      ASSERT_TRUE(hasELFMagic(PluginBytes));
      EXPECT_EQ(PluginBytes, BaselineBytes);
    }
  }
}

} // namespace
