#include "NeverCTestFixture.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

enum class ObjectFormat {
  ELF,
  COFFX86_64,
  COFFAArch64,
  MachO,
};

struct RouteCase {
  const char *Triple;
  ObjectFormat Format;
  bool UsesWin64X86CallingConvention;
};

constexpr std::array<RouteCase, 9> Routes = {{
    {"x86_64-apple-macosx", ObjectFormat::MachO, false},
    {"aarch64-apple-macosx", ObjectFormat::MachO, false},
    {"x86_64-unknown-linux-gnu", ObjectFormat::ELF, false},
    {"aarch64-unknown-linux-gnu", ObjectFormat::ELF, false},
    {"x86_64-unknown-linux-android29", ObjectFormat::ELF, false},
    {"aarch64-unknown-linux-android29", ObjectFormat::ELF, false},
    {"x86_64-pc-windows-msvc", ObjectFormat::COFFX86_64, true},
    {"aarch64-pc-windows-msvc", ObjectFormat::COFFAArch64, false},
    {"aarch64-apple-ios", ObjectFormat::MachO, false},
}};

bool hasObjectHeader(const std::string &Bytes, ObjectFormat Format) {
  if (Bytes.size() < 4)
    return false;
  const auto Byte = [&](size_t Index) {
    return static_cast<uint8_t>(Bytes[Index]);
  };
  switch (Format) {
  case ObjectFormat::ELF:
    return Byte(0) == 0x7f && Byte(1) == 'E' && Byte(2) == 'L' &&
           Byte(3) == 'F';
  case ObjectFormat::COFFX86_64:
    return Byte(0) == 0x64 && Byte(1) == 0x86;
  case ObjectFormat::COFFAArch64:
    return Byte(0) == 0x64 && Byte(1) == 0xaa;
  case ObjectFormat::MachO:
    return Byte(0) == 0xcf && Byte(1) == 0xfa &&
           Byte(2) == 0xed && Byte(3) == 0xfe;
  }
  return false;
}

class PluginBuiltinTargetProviderDriverTest : public NeverCTest {};

TEST_F(PluginBuiltinTargetProviderDriverTest,
       FullInventoryPreservesDriverIRAssemblyAndObjectRoutes) {
  const fs::path Source = tmpFile("builtin_target_routes.c");
  writeFile(Source,
            "int builtin_route_probe(int first, int second) {\n"
            "  return first + second;\n"
            "}\n");

  for (const RouteCase &Route : Routes) {
    SCOPED_TRACE(Route.Triple);
    const std::string Prefix =
        std::string("builtin-route-") + Route.Triple;
    const std::string Target = std::string("--target=") + Route.Triple;
    const std::string Plugin =
        std::string("-fplugin=") + NEVERC_TEST_TARGET_VALID_PLUGIN;

    CmdResult Jobs =
        ncc({"--no-default-config", Plugin, Target, "-###", "-c",
             Source.string()});
    ASSERT_EQ(Jobs.exitCode, 0) << Jobs.err;
    EXPECT_TRUE(Jobs.contains(Route.Triple) ||
                Jobs.stderrContains(Route.Triple));

    const fs::path IR = tmpFile(Prefix + ".ll");
    CmdResult EmitIR =
        ncc({"--no-default-config", Plugin, Target, "-O0", "-S",
             "-emit-llvm", Source.string(), "-o", IR.string()});
    ASSERT_EQ(EmitIR.exitCode, 0) << EmitIR.err;
    const std::string IRText = readFile(IR);
    EXPECT_NE(IRText.find("target datalayout = "), std::string::npos);
    EXPECT_NE(IRText.find("target triple = \""), std::string::npos);
    EXPECT_NE(IRText.find("builtin_route_probe"), std::string::npos);

    const fs::path Assembly = tmpFile(Prefix + ".s");
    CmdResult EmitAssembly =
        ncc({"--no-default-config", Plugin, Target, "-O0", "-S",
             Source.string(), "-o", Assembly.string()});
    ASSERT_EQ(EmitAssembly.exitCode, 0) << EmitAssembly.err;
    const std::string AssemblyText = readFile(Assembly);
    EXPECT_NE(AssemblyText.find("builtin_route_probe"), std::string::npos);
    if (Route.UsesWin64X86CallingConvention) {
      EXPECT_NE(AssemblyText.find("ecx"), std::string::npos);
      EXPECT_NE(AssemblyText.find("edx"), std::string::npos);
    } else if (std::string(Route.Triple).find("x86_64") !=
               std::string::npos) {
      EXPECT_NE(AssemblyText.find("%edi"), std::string::npos);
      EXPECT_NE(AssemblyText.find("%esi"), std::string::npos);
    }

    const fs::path Object = tmpFile(Prefix + ".o");
    CmdResult EmitObject =
        ncc({"--no-default-config", Plugin, Target, "-O0", "-fno-lto",
             "-c", Source.string(), "-o", Object.string()});
    ASSERT_EQ(EmitObject.exitCode, 0) << EmitObject.err;
    EXPECT_TRUE(hasObjectHeader(readFile(Object), Route.Format));
  }
}

TEST_F(PluginBuiltinTargetProviderDriverTest,
       ObjectEmissionRunsTransactionalPostWriteInterceptor) {
  const fs::path Source = tmpFile("object_post_write.c");
  const fs::path Object = tmpFile("object_post_write.o");
  writeFile(Source, "int object_post_write(void) { return 42; }\n");

  const std::string TargetPlugin =
      std::string("-fplugin=") + NEVERC_TEST_TARGET_VALID_PLUGIN;
  const std::string ObjectPlugin =
      std::string("-fplugin=") + NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN;
  CmdResult Result =
      ncc({"--no-default-config", TargetPlugin, ObjectPlugin,
           "--target=x86_64-unknown-linux-gnu", "-O0", "-fno-lto",
           "-c", Source.string(), "-o", Object.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;

  const std::string Bytes = readFile(Object);
  ASSERT_GE(Bytes.size(), 16U);
  EXPECT_TRUE(hasObjectHeader(Bytes, ObjectFormat::ELF));
  EXPECT_EQ(static_cast<uint8_t>(Bytes[9]), UINT8_C(0x42));
}

TEST_F(PluginBuiltinTargetProviderDriverTest,
       InactivePluginSeamPreservesNativeObjectBytes) {
  const fs::path Source = tmpFile("native_object_golden.c");
  writeFile(Source, "int native_object_golden(void) { return 42; }\n");

  for (const RouteCase &Route : Routes) {
    SCOPED_TRACE(Route.Triple);
    const std::string Target = std::string("--target=") + Route.Triple;
    const fs::path Baseline =
        tmpFile(std::string("native-baseline-") + Route.Triple + ".o");
    const fs::path WithPlugin =
        tmpFile(std::string("native-plugin-") + Route.Triple + ".o");
    CmdResult BaselineResult =
        ncc({"--no-default-config", Target, "-O0", "-fno-lto", "-c",
             Source.string(), "-o", Baseline.string()});
    ASSERT_EQ(BaselineResult.exitCode, 0) << BaselineResult.err;
    CmdResult PluginResult = ncc(
        {"--no-default-config",
         std::string("-fplugin=") + NEVERC_TEST_TARGET_VALID_PLUGIN,
         Target, "-O0", "-fno-lto", "-c", Source.string(), "-o",
         WithPlugin.string()});
    ASSERT_EQ(PluginResult.exitCode, 0) << PluginResult.err;
    EXPECT_EQ(readFile(WithPlugin), readFile(Baseline));
  }
}

} // namespace
