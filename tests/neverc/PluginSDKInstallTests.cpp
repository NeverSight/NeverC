//===- PluginSDKInstallTests.cpp - installed SDK consumption tests -------===//
//
// Installs the neverc-pluginsdk component into a throwaway prefix and drives the
// out-of-tree consumer check (utils/plugin-api/test-installed-sdk.py) against
// it. This proves the packaged SDK - single header, modular headers, schemas,
// manifest, CMake config, pkg-config and templates - is self-sufficient and
// buildable by an independent compiler, catching packaging gaps that in-tree
// builds hide.
//
//===----------------------------------------------------------------------===//

#include "NeverCTestFixture.h"

namespace {

class PluginSDKInstallTest : public NeverCTest {};

TEST_F(PluginSDKInstallTest, InstallsAndConsumesSDKFromCleanPrefix) {
  const fs::path Prefix = tmp() / "sdk-install";
  fs::create_directories(Prefix);

  CmdResult Install =
      exec(NEVERC_CMAKE_COMMAND, {"--install", NEVERC_BUILD_DIR, "--prefix",
                                  Prefix.string(), "--component",
                                  "neverc-pluginsdk"});
  ASSERT_EQ(Install.exitCode, 0)
      << "cmake --install failed:\n"
      << Install.out << "\n"
      << Install.err;

  CmdResult Consume =
      exec(NEVERC_PYTHON, {NEVERC_TEST_INSTALLED_SDK_SCRIPT, "--prefix",
                           Prefix.string()});
  EXPECT_EQ(Consume.exitCode, 0)
      << "installed SDK consumer failed:\n"
      << Consume.out << "\n"
      << Consume.err;
  EXPECT_TRUE(Consume.contains("consumed successfully"))
      << Consume.out << "\n"
      << Consume.err;
}

} // namespace
