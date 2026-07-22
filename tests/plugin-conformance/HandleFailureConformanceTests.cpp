//===- HandleFailureConformanceTests.cpp - error/handle safety ----------===//
//
// Proves, through the public CLI, that plugin failures and malformed inputs are
// propagated and rejected rather than ignored or crashing the host:
//
//  * A failing Register callback aborts the compilation.
//  * A malformed registration descriptor (Header.StructSize below the required
//    prefix) is rejected instead of being read out of bounds.
//  * An interceptor that returns an error propagates the failure and fails the
//    phase (no silent fallback to the built-in).
//
// The stale-handle, wrong-session and wrong-scope handle checks are covered
// exhaustively in-tree by the handle-arena tests; here we prove the CLI surface
// fails closed on the failure paths a downstream can trigger.
//
//===----------------------------------------------------------------------===//

#include "ConformanceSummary.h"
#include "ConformanceTest.h"

namespace neverc::conformance {
namespace {

class HandleFailureConformance : public ConformanceTest {
protected:
  std::string label() const override { return "handle-failure"; }

  RunResult compileWith(const std::vector<std::string> &Defines) {
    const std::string Plugin = buildOrFail("DomainConformancePlugin", Defines);
    if (Plugin.empty())
      return {};
    const std::string Input = trivialInput();
    return Env.runNeverc({"-fplugin=" + Plugin, "--no-default-config", "-c",
                          Input, "-o", objectPath()});
  }
};

TEST_F(HandleFailureConformance, RegisterFailureAbortsCompilation) {
  const RunResult R = compileWith({"NCF_REGISTER_FAILURE"});
  EXPECT_NE(R.exitCode, 0) << "a failing Register callback must abort";
  EXPECT_TRUE(R.errContains("plugin")) << R.err;
  recordCapability("core.register_failure_propagates",
                   R.exitCode != 0 ? CapStatus::Pass : CapStatus::Fail);
}

TEST_F(HandleFailureConformance, MalformedDescriptorIsRejected) {
  const RunResult R = compileWith({"NCF_BAD_OBSERVER_SIZE"});
  EXPECT_NE(R.exitCode, 0)
      << "a malformed observer descriptor must be rejected";
  EXPECT_TRUE(R.errContains("plugin")) << R.err;
  recordCapability("core.malformed_descriptor_rejected",
                   R.exitCode != 0 ? CapStatus::Pass : CapStatus::Fail);
}

TEST_F(HandleFailureConformance, InterceptorErrorPropagates) {
  const RunResult R =
      compileWith({"NCF_INTERCEPT_DRIVER_JOB", "NCF_FAIL_IN_INTERCEPT"});
  EXPECT_NE(R.exitCode, 0)
      << "an interceptor returning an error must fail the phase";
  EXPECT_TRUE(R.errContains("phase") || R.errContains("plugin")) << R.err;
  recordCapability("neverc.driver.execute_job/error_propagates",
                   R.exitCode != 0 ? CapStatus::Pass : CapStatus::Fail);
}

TEST_F(HandleFailureConformance, UnknownPluginPathIsRejected) {
  const std::string Input = trivialInput();
  const RunResult R = Env.runNeverc(
      {"-fplugin=" + (std::filesystem::path(Dir) / "does-not-exist.so").string(),
       "--no-default-config", "-fsyntax-only", Input});
  EXPECT_NE(R.exitCode, 0) << "loading a missing plugin must fail";
  recordCapability("core.missing_plugin_rejected",
                   R.exitCode != 0 ? CapStatus::Pass : CapStatus::Fail);
}

} // namespace
} // namespace neverc::conformance
