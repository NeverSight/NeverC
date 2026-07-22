//===- DriverFrontendConformanceTests.cpp - driver/frontend domain ------===//
//
// Drives the Driver and front-end phases through the public CLI:
//
//  * OBSERVABLE: neverc.driver.raw_arguments observer runs and sees the phase.
//  * INTERCEPTABLE: neverc.driver.execute_job interceptor runs, calls
//    InvokeNext and returns CONTINUE, leaving a correct compile.
//  * OBSERVABLE: neverc.ir.generate observer runs during front-end codegen.
//
// Firings are observed through a deterministic on-disk log the fixture writes,
// independent of diagnostic formatting.
//
//===----------------------------------------------------------------------===//

#include "ConformanceSummary.h"
#include "ConformanceTest.h"

namespace neverc::conformance {
namespace {

class DriverFrontendConformance : public ConformanceTest {
protected:
  std::string label() const override { return "driver-frontend"; }
};

TEST_F(DriverFrontendConformance, RawArgumentsObserverRuns) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_OBSERVE_DRIVER_ARGS"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = trivialInput();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", "-c", Input, "-o",
       objectPath()},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;
  EXPECT_NE(readLog().find("observe:driver_args"), std::string::npos)
      << "driver raw_arguments observer did not run:\n"
      << readLog();
  recordCapability("neverc.driver.raw_arguments/observe", CapStatus::Pass);
}

TEST_F(DriverFrontendConformance, ExecuteJobInterceptorRunsAndContinues) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_INTERCEPT_DRIVER_JOB"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = trivialInput();
  const std::string Object = objectPath();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", "-c", Input, "-o", Object},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;
  EXPECT_NE(readLog().find("intercept:driver_job"), std::string::npos)
      << "driver execute_job interceptor did not run:\n"
      << readLog();
  EXPECT_FALSE(readBytes(Object).empty())
      << "interceptor that continued should still produce the object";
  recordCapability("neverc.driver.execute_job/intercept", CapStatus::Pass);
}

TEST_F(DriverFrontendConformance, IRGenerateObserverRuns) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_OBSERVE_IR_GENERATE"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = trivialInput();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", "-c", Input, "-o",
       objectPath()},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;
  EXPECT_NE(readLog().find("observe:ir_generate"), std::string::npos)
      << "ir.generate observer did not run:\n"
      << readLog();
  recordCapability("neverc.ir.generate/observe", CapStatus::Pass);
}

} // namespace
} // namespace neverc::conformance
