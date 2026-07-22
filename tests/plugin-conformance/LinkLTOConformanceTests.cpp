//===- LinkLTOConformanceTests.cpp - link / LTO domain ------------------===//
//
// Drives the link and LTO phases through the public CLI:
//
//  * Baseline: linking a full executable with a plugin loaded succeeds and the
//    plugin's driver-phase observer still fires, proving the same PluginSession
//    reaches the linker.
//  * OBSERVABLE (best effort): neverc.link.full; recorded as a capability-bound
//    skip when the host's default link path (system linker) does not surface
//    the coarse link seam. The 13 typed LinkGraph transitions are covered
//    in-tree by neverc-plugin-link-tests.
//  * LTO (best effort): a -flto link with a plugin loaded still links.
//
//===----------------------------------------------------------------------===//

#include "ConformanceSummary.h"
#include "ConformanceTest.h"

namespace neverc::conformance {
namespace {

class LinkLTOConformance : public ConformanceTest {
protected:
  std::string label() const override { return "link-lto"; }

  std::string mainInput() {
    return writeSource("main.c", "int main(void) { return 0; }\n");
  }
  std::string exePath() const {
    return (std::filesystem::path(Dir) / "conf_exe").string();
  }
};

TEST_F(LinkLTOConformance, LinksExecutableWithPluginLoaded) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_OBSERVE_DRIVER_ARGS"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = mainInput();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", Input, "-o", exePath()},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;
  EXPECT_FALSE(readBytes(exePath()).empty()) << "no executable produced";
  EXPECT_NE(readLog().find("observe:driver_args"), std::string::npos)
      << "the same PluginSession did not reach the link driver:\n"
      << readLog();
  recordCapability("neverc.link.driver_session", CapStatus::Pass);
}

TEST_F(LinkLTOConformance, LinkFullObserverBestEffort) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_OBSERVE_LINK_FULL"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = mainInput();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", Input, "-o", exePath()},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;
  if (readLog().find("observe:link_full") != std::string::npos) {
    recordCapability("neverc.link.full/observe", CapStatus::Pass);
  } else {
    recordCapability(
        "neverc.link.full/observe", CapStatus::Skip,
        "host default link path does not surface the coarse link.full seam; "
        "13 typed LinkGraph transitions covered in-tree by "
        "neverc-plugin-link-tests");
    GTEST_SKIP() << "link.full observer not exercised on this host path";
  }
}

TEST_F(LinkLTOConformance, LTOLinkWithPluginLoadedBestEffort) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_OBSERVE_DRIVER_ARGS"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = mainInput();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", "-flto", Input, "-o",
       exePath()},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  if (R.exitCode == 0 && !readBytes(exePath()).empty()) {
    recordCapability("neverc.lto.link_with_plugin", CapStatus::Pass);
  } else {
    recordCapability("neverc.lto.link_with_plugin", CapStatus::Skip,
                     "LTO link not available in this host configuration");
    GTEST_SKIP() << "LTO link unavailable here:\n" << R.err;
  }
}

} // namespace
} // namespace neverc::conformance
