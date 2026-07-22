//===- SDKABIConformanceTests.cpp - descriptor/ABI negotiation ----------===//
//
// Proves the host accepts a well-formed plugin (including a forward-compatible
// unknown tail) and rejects every malformed descriptor and failed ABI
// negotiation, all through the public CLI with fixtures built by the system C
// compiler against the staged SDK.
//
//===----------------------------------------------------------------------===//

#include "ConformanceTest.h"

namespace neverc::conformance {
namespace {

class SDKABIConformance : public ConformanceTest {
protected:
  std::string label() const override { return "abi"; }

  RunResult loadInvalid(const std::vector<std::string> &Defines) {
    const std::string Plugin = buildOrFail("InvalidDescriptorPlugins", Defines);
    if (Plugin.empty())
      return {};
    const std::string Input = trivialInput();
    return Env.runNeverc({"-fplugin=" + Plugin, "--no-default-config",
                          "-fsyntax-only", Input});
  }
};

TEST_F(SDKABIConformance, MinimalPluginLoadsAndRegisters) {
  const std::string Plugin = buildOrFail("MinimalPlugin");
  ASSERT_FALSE(Plugin.empty());
  const std::string Input = trivialInput();
  const RunResult R = Env.runNeverc(
      {"-fplugin=" + Plugin, "--no-default-config", "-fsyntax-only", Input},
      {{"NEVERC_CONFORMANCE_LOG", logPath()}});
  EXPECT_EQ(R.exitCode, 0) << R.err;
  EXPECT_NE(readLog().find("minimal:register"), std::string::npos)
      << "plugin register callback did not run";
}

TEST_F(SDKABIConformance, AcceptsForwardCompatibleUnknownTail) {
  const RunResult R = loadInvalid({"NCF_UNKNOWN_TAIL"});
  EXPECT_EQ(R.exitCode, 0) << R.err;
}

TEST_F(SDKABIConformance, RejectsWrongAbiMajor) {
  const RunResult R = loadInvalid({"NCF_WRONG_MAJOR"});
  EXPECT_NE(R.exitCode, 0) << R.err;
  EXPECT_TRUE(R.errContains("major")) << R.err;
}

TEST_F(SDKABIConformance, RejectsNewerAbiMinor) {
  const RunResult R = loadInvalid({"NCF_NEWER_MINOR"});
  EXPECT_NE(R.exitCode, 0) << R.err;
  EXPECT_TRUE(R.errContains("minor")) << R.err;
}

TEST_F(SDKABIConformance, RejectsUnsupportedFlags) {
  EXPECT_NE(loadInvalid({"NCF_BAD_FLAGS"}).exitCode, 0);
}

TEST_F(SDKABIConformance, RejectsShortDescriptor) {
  EXPECT_NE(loadInvalid({"NCF_SHORT_STRUCT"}).exitCode, 0);
}

TEST_F(SDKABIConformance, RejectsNullRegisterCallback) {
  EXPECT_NE(loadInvalid({"NCF_NULL_REGISTER"}).exitCode, 0);
}

TEST_F(SDKABIConformance, RejectsEmptyPluginId) {
  EXPECT_NE(loadInvalid({"NCF_EMPTY_ID"}).exitCode, 0);
}

TEST_F(SDKABIConformance, RejectsNonCanonicalPluginId) {
  EXPECT_NE(loadInvalid({"NCF_NONCANON_ID"}).exitCode, 0);
}

TEST_F(SDKABIConformance, RejectsEmptyDisplayName) {
  EXPECT_NE(loadInvalid({"NCF_EMPTY_NAME"}).exitCode, 0);
}

TEST_F(SDKABIConformance, RejectsInvalidConcurrencyModel) {
  EXPECT_NE(loadInvalid({"NCF_BAD_CONCURRENCY"}).exitCode, 0);
}

TEST_F(SDKABIConformance, RejectsInvalidReentrancyModel) {
  EXPECT_NE(loadInvalid({"NCF_BAD_REENTRANCY"}).exitCode, 0);
}

TEST_F(SDKABIConformance, RejectsFailedEntryStatus) {
  EXPECT_NE(loadInvalid({"NCF_ENTRY_ERROR"}).exitCode, 0);
}

TEST_F(SDKABIConformance, RejectsMissingRequiredCapability) {
  const RunResult R = loadInvalid({"NCF_MISSING_CAP"});
  EXPECT_NE(R.exitCode, 0) << R.err;
  EXPECT_TRUE(R.errContains("interface") || R.errContains("capability"))
      << R.err;
}

} // namespace
} // namespace neverc::conformance
