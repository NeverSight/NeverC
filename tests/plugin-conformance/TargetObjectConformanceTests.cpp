//===- TargetObjectConformanceTests.cpp - target/MC/object domain -------===//
//
// Drives the Target, MC and Object domains through the public CLI using
// fixtures built by the system C compiler against the staged SDK:
//
//  * REPLACEABLE / end-to-end: a plugin registers a whole custom target
//    ("neverc-test-none") and a ".nobj" object format, then compiles C and
//    assembly all the way to a deterministic custom object image the built-in
//    backend would never emit -- proving C -> custom Object end to end and
//    that the sealed commit gate still publishes a plugin-produced artifact.
//  * INTERCEPTABLE: a plugin intercepts neverc.object.pre_write and adds a
//    deterministic ".nvc_conf" section, verified by inspecting the emitted
//    native object's bytes (a black-box artifact proof, not just a callback
//    trace).
//  * OBSERVABLE (best effort): neverc.mc.emission.pre_instruction, recorded as
//    a capability-bound skip when the host's default object path does not
//    surface the MC seam for a trivial translation unit.
//
//===----------------------------------------------------------------------===//

#include "ConformanceSummary.h"
#include "ConformanceTest.h"

namespace neverc::conformance {
namespace {

class TargetObjectConformance : public ConformanceTest {
protected:
  std::string label() const override { return "target-object"; }
};

// C -> custom .nobj through the coarse-grained codegen provider.
TEST_F(TargetObjectConformance, CompilesCThroughCustomBackendToCustomObject) {
  const std::string Plugin = buildOrFail("ReplacementConformancePlugin");
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = trivialInput();
  const std::string Object = objectPath("answer.nobj");

  const RunResult R = Env.runNeverc({"-fplugin=" + Plugin, "--no-default-config",
                                     "--target=neverc-test-none", "-O0",
                                     "-fno-lto", "-c", Input, "-o", Object});
  ASSERT_EQ(R.exitCode, 0) << R.err;

  const std::string Bytes = readBytes(Object);
  ASSERT_EQ(Bytes.size(), 21u) << "custom backend produced unexpected size";
  EXPECT_EQ(Bytes.substr(0, 4), "NOBJ")
      << "built-in backend was not replaced by the plugin";
  recordCapability("neverc.codegen.coarse_lower/replace", CapStatus::Pass);
  recordCapability("neverc.object.write/replace", CapStatus::Pass);
}

// Assembly -> custom .nobj through the replacement assembly parser provider.
TEST_F(TargetObjectConformance,
       AssemblesSourceThroughReplacementParserToCustomObject) {
  const std::string Plugin = buildOrFail("ReplacementConformancePlugin");
  ASSERT_FALSE(Plugin.empty());
  const std::string Source = writeSource("answer.s", ".nobj_answer\n");
  const std::string Object = objectPath("answer-from-asm.nobj");

  const RunResult R = Env.runNeverc({"-fplugin=" + Plugin, "--no-default-config",
                                     "--target=neverc-test-none", "-c", Source,
                                     "-o", Object});
  ASSERT_EQ(R.exitCode, 0) << R.err;

  const std::string Bytes = readBytes(Object);
  ASSERT_EQ(Bytes.size(), 21u);
  EXPECT_EQ(Bytes.substr(0, 4), "NOBJ");
  recordCapability("neverc.assembly.parse/replace", CapStatus::Pass);
}

// object.pre_write interceptor adds a section that lands in the final object.
TEST_F(TargetObjectConformance, ObjectPreWriteInterceptorAddsSection) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_INTERCEPT_OBJECT"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = trivialInput();
  const std::string Object = objectPath("intercepted.o");

  // -fno-lto forces native object emission so the ObjectGraph pre_write seam
  // actually runs (an LTO/bitcode -c would bypass it).
  const RunResult R = Env.runNeverc({"-fplugin=" + Plugin, "--no-default-config",
                                     "-fno-lto", "-c", Input, "-o", Object},
                                    {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;

  const std::string Bytes = readBytes(Object);
  EXPECT_NE(Bytes.find("neverc conformance section"), std::string::npos)
      << "pre_write interceptor did not add its section to the object";
  recordCapability("neverc.object.pre_write/intercept", CapStatus::Pass);
}

// MC pre-instruction observer: best effort on the host's default object path.
TEST_F(TargetObjectConformance, MCPreInstructionObserverBestEffort) {
  const std::string Plugin =
      buildOrFail("DomainConformancePlugin", {"NCF_OBSERVE_MC_PRE_INSN"});
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = trivialInput();
  const std::string Object = objectPath("mc.o");

  const RunResult R = Env.runNeverc({"-fplugin=" + Plugin, "--no-default-config",
                                     "-fno-lto", "-c", Input, "-o", Object},
                                    {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  ASSERT_EQ(R.exitCode, 0) << R.err;
  if (readLog().find("observe:mc_pre_instruction") != std::string::npos) {
    recordCapability("neverc.mc.emission.pre_instruction/observe",
                     CapStatus::Pass);
  } else {
    recordCapability(
        "neverc.mc.emission.pre_instruction/observe", CapStatus::Skip,
        "host default object path did not surface the MC seam for this TU; "
        "covered in-tree by neverc-plugin-target-mc-tests");
    GTEST_SKIP() << "MC emission seam not exercised on this host path";
  }
}

} // namespace
} // namespace neverc::conformance
