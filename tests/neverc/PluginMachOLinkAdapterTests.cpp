#include "NeverCTestFixture.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <string>
#include <vector>

namespace {

class PluginMachOLinkAdapterTest : public NeverCTest {};

bool hasMachOMagic(const std::string &Bytes) {
  if (Bytes.size() < sizeof(uint32_t))
    return false;
  const auto B0 = static_cast<uint8_t>(Bytes[0]);
  const auto B1 = static_cast<uint8_t>(Bytes[1]);
  const auto B2 = static_cast<uint8_t>(Bytes[2]);
  const auto B3 = static_cast<uint8_t>(Bytes[3]);
  // MH_MAGIC_64 (0xfeedfacf) little-endian, and MH_MAGIC (0xfeedface) for the
  // 32-bit form; also accept the relocatable object magic emitted by -r.
  const bool Magic64 = B0 == 0xcf && B1 == 0xfa && B2 == 0xed && B3 == 0xfe;
  const bool Magic32 = B0 == 0xce && B1 == 0xfa && B2 == 0xed && B3 == 0xfe;
  return Magic64 || Magic32;
}

// Activating a plugin session routes the built-in Mach-O linker through the
// LinkGraph adapter. With no user providers registered the projection must be
// a faithful no-op, so the emitted image stays byte-identical to the baseline.
TEST_F(PluginMachOLinkAdapterTest,
       ActivatedSessionPreservesBuiltinMachOLinkOutput) {
  const fs::path Source = tmpFile("plugin-macho-adapter.c");
  writeFile(Source, "__attribute__((visibility(\"default\"))) volatile int "
                    "plugin_macho_adapter_data = 42;\n"
                    "__attribute__((visibility(\"default\"))) int "
                    "plugin_macho_adapter_answer(void) {\n"
                    "  return plugin_macho_adapter_data;\n"
                    "}\n"
                    "void _start(void) {\n"
                    "  for (;;) plugin_macho_adapter_data = "
                    "plugin_macho_adapter_answer();\n"
                    "}\n");

  struct OutputCase {
    const char *Name;
    std::vector<std::string> Flags;
  };
  const std::vector<OutputCase> OutputCases = {
      {"executable", {"-Wl,-e,_start", "-Wl,-undefined,dynamic_lookup"}},
      {"dylib",
       {"-shared", "-Wl,-install-name,@rpath/libplugin-macho-adapter.dylib",
        "-Wl,-undefined,dynamic_lookup"}},
      {"bundle", {"-bundle", "-Wl,-undefined,dynamic_lookup"}},
      {"relocatable", {"-r"}},
  };
  const std::vector<std::string> Targets = {
      "x86_64-apple-macosx13.0",
      "aarch64-apple-macosx13.0",
  };

  for (const std::string &Target : Targets) {
    for (const OutputCase &Output : OutputCases) {
      SCOPED_TRACE(Target + "/" + Output.Name);
      const std::string Stem = "plugin-macho-" +
                               Target.substr(0, Target.find('-')) + "-" +
                               Output.Name;
      const fs::path Baseline = tmpFile(Stem + "-baseline");
      const fs::path WithPlugin = tmpFile(Stem + "-session");

      std::vector<std::string> Common = {
          "--no-default-config", "--target=" + Target,
          "-O0",                 "-fno-lto",
          "-nostdlib",           "-Wl,--no-uuid",
          "-Wl,--no-adhoc-codesign"};
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
      ASSERT_TRUE(hasMachOMagic(PluginBytes));
      EXPECT_EQ(PluginBytes, BaselineBytes);
    }
  }
}

} // namespace
