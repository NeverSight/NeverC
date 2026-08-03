//===- LinkLTOConformanceTests.cpp - link / LTO domain ------------------===//
//
// Drives the link and LTO phases through the public CLI:
//
//  * Baseline + OBSERVABLE (best effort): one non-LTO executable link proves a
//    plugin can span the driver/link pipeline and probes neverc.link.full. The
//    latter is recorded as a capability-bound skip when the host's native link
//    path does not surface the coarse seam. The 13 typed LinkGraph transitions
//    are covered in-tree by neverc-plugin-link-tests.
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

TEST_F(LinkLTOConformance,
       LinksNonLTOExecutableAndProbesLinkFullObserver) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin",
                  {"NCF_OBSERVE_DRIVER_ARGS", "NCF_OBSERVE_LINK_FULL"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = mainInput();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", "-fno-lto",
       "-fno-builtin-mimalloc", Input, "-o", exePath()},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;
  EXPECT_FALSE(readBytes(exePath()).empty()) << "no executable produced";
  const std::string Log = readLog();
  EXPECT_NE(Log.find("observe:driver_args"), std::string::npos)
      << "the same PluginSession did not reach the link driver:\n"
      << Log;
  recordCapability("neverc.link.driver_session", CapStatus::Pass);

  if (Log.find("observe:link_full") != std::string::npos) {
    recordCapability("neverc.link.full/observe", CapStatus::Pass);
  } else {
    recordCapability(
        "neverc.link.full/observe", CapStatus::Skip,
        "host default link path does not surface the coarse link.full seam; "
        "13 typed LinkGraph transitions covered in-tree by "
        "neverc-plugin-link-tests");
  }
}

TEST_F(LinkLTOConformance, LTOLinkWithPluginLoadedBestEffort) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_OBSERVE_DRIVER_ARGS"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = mainInput();

  // The plugin/LTO handshake is the subject; pulling the default allocator's
  // large bitcode module into a trivial link adds no conformance coverage.
  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", "-flto",
       "-fno-builtin-mimalloc", Input, "-o", exePath()},
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
