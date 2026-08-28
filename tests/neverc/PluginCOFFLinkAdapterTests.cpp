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

TEST_F(PluginCOFFLinkAdapterTest, StdoutReproHashPreservesInMemoryPE) {
  const fs::path Source = tmpFile("coff-stdout-repro.c");
  writeFile(Source,
            "volatile unsigned char payload[2 * 1024 * 1024 + 257] = {1};\n"
            "void _start(void) {\n"
            "  for (;;) payload[sizeof(payload) - 1]++;\n"
            "}\n");

  CmdResult Result =
      ncc({"--no-default-config", "--target=x86_64-pc-windows-msvc", "-O0",
           "-fno-lto", "-nostdlib", "-mno-incremental-linker-compatible",
           "-Wl,-nodefaultlib", "-Wl,--entry=_start", "-Wl,--subsystem=console",
           Source.string(), "-o", "-"});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  // The Unix fixture captures command output as text, so the first NUL in the
  // binary ends the retained string.  The prefix is still sufficient:
  // discarding anonymous output pages zeroes the MZ header before commit.
  ASSERT_GE(Result.out.size(), 2U);
  EXPECT_EQ(Result.out.substr(0, 2), "MZ")
      << "reproducible PE written through an in-memory output lost its header";
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
      // PE embeds the output module name in its export directory, so the two
      // links must share one output path; otherwise the baseline and
      // plugin images differ only by their file name, not the adapter.
      const fs::path Output_ = tmpFile(Stem);

      // -nodefaultlib keeps the link fully self-contained: ARM64 otherwise
      // pulls an implicit runtimeobject.lib that is unavailable when
      // cross-linking off a Windows host, and it guarantees the image depends
      // only on the input object so the comparison stays host-independent.
      //
      // -mno-incremental-linker-compatible forces a reproducible PE image: the
      // COFF/debug/export TimeDateStamps become a content hash instead of the
      // wall-clock default, so the baseline and plugin links are byte-identical
      // even when they straddle a one-second boundary.  Without it this exact
      // comparison is flaky (the two links otherwise embed different clock
      // timestamps); it does not weaken the check -- a plugin that perturbs the
      // image changes the content, hence the hash, hence the bytes.
      std::vector<std::string> Common = {
          "--no-default-config",
          "--target=" + Target,
          "-O0",
          "-fno-lto",
          "-nostdlib",
          "-mno-incremental-linker-compatible",
          "-Wl,-nodefaultlib"};
      Common.insert(Common.end(), Output.Flags.begin(), Output.Flags.end());
      Common.push_back(Source.string());

      std::vector<std::string> BaselineArguments = Common;
      BaselineArguments.insert(BaselineArguments.end(),
                               {"-o", Output_.string()});
      CmdResult BaselineResult = ncc(BaselineArguments);
      ASSERT_EQ(BaselineResult.exitCode, 0) << BaselineResult.err;
      const std::string BaselineBytes = readFile(Output_);

      std::vector<std::string> PluginArguments = Common;
      PluginArguments.insert(PluginArguments.begin() + 1,
                             std::string("-fplugin=") +
                                 NEVERC_TEST_TARGET_VALID_PLUGIN);
      PluginArguments.insert(PluginArguments.end(),
                             {"-o", Output_.string()});
      CmdResult PluginResult = ncc(PluginArguments);
      ASSERT_EQ(PluginResult.exitCode, 0) << PluginResult.err;
      const std::string PluginBytes = readFile(Output_);

      ASSERT_TRUE(hasPEMagic(PluginBytes));
      EXPECT_EQ(PluginBytes, BaselineBytes);
    }
  }
}

} // namespace
