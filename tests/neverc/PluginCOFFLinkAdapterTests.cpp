#include "NeverCTestFixture.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <string>

namespace {

class PluginCOFFLinkAdapterTest : public NeverCTest {};

bool hasPEMagic(const std::string &Bytes) {
  if (Bytes.size() < 64 || Bytes[0] != 'M' || Bytes[1] != 'Z')
    return false;
  const uint32_t PEOffset =
      static_cast<uint8_t>(Bytes[0x3c]) |
      (static_cast<uint32_t>(static_cast<uint8_t>(Bytes[0x3d])) << 8) |
      (static_cast<uint32_t>(static_cast<uint8_t>(Bytes[0x3e])) << 16) |
      (static_cast<uint32_t>(static_cast<uint8_t>(Bytes[0x3f])) << 24);
  return PEOffset <= Bytes.size() && Bytes.size() - PEOffset >= 4 &&
         Bytes[PEOffset] == 'P' && Bytes[PEOffset + 1] == 'E' &&
         Bytes[PEOffset + 2] == '\0' && Bytes[PEOffset + 3] == '\0';
}

TEST_F(PluginCOFFLinkAdapterTest,
       ActivatedSessionPreservesBuiltinPELinkOutput) {
  const fs::path Source = tmpFile("plugin-coff-adapter.c");
  writeFile(Source,
            "__declspec(dllexport) volatile int "
            "plugin_coff_adapter_data = 42;\n"
            "__declspec(dllexport) int plugin_coff_adapter_answer(void) {\n"
            "  return plugin_coff_adapter_data;\n"
            "}\n"
            "__declspec(dllexport) void _start(void) {\n"
            "  for (;;) plugin_coff_adapter_data = "
            "plugin_coff_adapter_answer();\n"
            "}\n");

  struct OutputCase {
    const char *Name;
    std::vector<std::string> Flags;
  };
  const std::vector<OutputCase> OutputCases = {
      {"exe", {"-Wl,--entry=_start", "-Wl,--subsystem=console"}},
      {"dll", {"-shared", "-Wl,--noentry"}},
  };
  const std::vector<std::string> Targets = {
      "x86_64-pc-windows-msvc",
      "aarch64-pc-windows-msvc",
  };

  for (const std::string &Target : Targets) {
    for (const OutputCase &Output : OutputCases) {
      SCOPED_TRACE(Target + "/" + Output.Name);
      const std::string Stem = "plugin-coff-" +
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
      ASSERT_TRUE(hasPEMagic(PluginBytes));
      EXPECT_EQ(PluginBytes, BaselineBytes);
    }
  }
}

} // namespace
