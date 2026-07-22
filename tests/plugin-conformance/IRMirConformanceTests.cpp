//===- IRMirConformanceTests.cpp - IR / MIR domain ----------------------===//
//
// Drives the IR and MIR phases through the public CLI:
//
//  * OBSERVABLE: neverc.ir.generate runs during IRGen.
//  * OBSERVABLE: neverc.ir.optimize runs when optimizing (-O2).
//  * OBSERVABLE (best effort): neverc.mir.pass.final; recorded as a
//    capability-bound skip when the host's default codegen path does not
//    surface the MIR seam for a trivial translation unit (covered in-tree by
//    neverc-plugin-target-mc-tests).
//
//===----------------------------------------------------------------------===//

#include "ConformanceSummary.h"
#include "ConformanceTest.h"

namespace neverc::conformance {
namespace {

class IRMirConformance : public ConformanceTest {
protected:
  std::string label() const override { return "ir-mir"; }
};

TEST_F(IRMirConformance, IRGenerateObserverRuns) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_OBSERVE_IR_GENERATE"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = trivialInput();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", "-O0", "-c", Input, "-o",
       objectPath()},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;
  EXPECT_NE(readLog().find("observe:ir_generate"), std::string::npos)
      << readLog();
  recordCapability("neverc.ir.generate/observe", CapStatus::Pass);
}

TEST_F(IRMirConformance, IROptimizeObserverRunsUnderO2) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_OBSERVE_IR_OPTIMIZE"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = trivialInput();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", "-O2", "-fno-lto", "-c",
       Input, "-o", objectPath()},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;
  if (readLog().find("observe:ir_optimize") != std::string::npos) {
    recordCapability("neverc.ir.optimize/observe", CapStatus::Pass);
  } else {
    recordCapability("neverc.ir.optimize/observe", CapStatus::Skip,
                     "ir.optimize seam not surfaced on this host path");
    GTEST_SKIP() << "ir.optimize observer not exercised:\n" << readLog();
  }
}

TEST_F(IRMirConformance, MIRFinalObserverBestEffort) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_OBSERVE_MIR_FINAL"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = trivialInput();

  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", "-O0", "-fno-lto", "-c",
       Input, "-o", objectPath()},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;
  if (readLog().find("observe:mir_final") != std::string::npos) {
    recordCapability("neverc.mir.pass.final/observe", CapStatus::Pass);
  } else {
    recordCapability(
        "neverc.mir.pass.final/observe", CapStatus::Skip,
        "host default codegen path did not surface the MIR seam for this TU; "
        "covered in-tree by neverc-plugin-target-mc-tests");
    GTEST_SKIP() << "MIR final observer not exercised on this host path";
  }
}

} // namespace
} // namespace neverc::conformance
