// Volume 6 task 24: per-plugin/phase callback statistics feeding the time-trace
// side summary and the no-plugin overhead gate.  These tests pin the pure
// aggregation logic of PluginCallbackStats and prove the single PluginSession
// callback gate records calls/time when a plugin callback runs.  The <=1%
// no-plugin wall-time / peak-RSS regression budget itself is enforced out of
// process by utils/plugin-api/measure-plugin-overhead.py, which needs a
// fully-disabled baseline binary that a unit test cannot build.

#include "neverc/Plugin/Host/PluginCallbackStats.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"

#include "gtest/gtest.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"

#include <array>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  return toString(std::move(ErrorValue)).str().str();
}

TEST(PluginCallbackStatsTest, AggregatesPerPluginAndCallback) {
  PluginCallbackStats Stats;
  EXPECT_TRUE(Stats.empty());
  EXPECT_EQ(Stats.totalCalls(), 0U);

  Stats.record("com.example.a", "phase.x", 100, /*Error=*/false);
  Stats.record("com.example.a", "phase.x", 200, /*Error=*/true);
  Stats.record("com.example.a", "phase.y", 50, /*Error=*/false);
  Stats.record("com.example.b", "phase.x", 10, /*Error=*/false);
  Stats.recordCacheHit("com.example.a", "phase.x");

  EXPECT_FALSE(Stats.empty());
  EXPECT_EQ(Stats.totalCalls(), 4U);
  EXPECT_EQ(Stats.totalNanos(), 360U);

  auto Snap = Stats.snapshot();
  ASSERT_EQ(Snap.size(), 3U);
  // Stable order: (a, phase.x), (a, phase.y), (b, phase.x).
  EXPECT_EQ(Snap[0].PluginID, "com.example.a");
  EXPECT_EQ(Snap[0].Callback, "phase.x");
  EXPECT_EQ(Snap[0].Calls, 2U);
  EXPECT_EQ(Snap[0].TotalNanos, 300U);
  EXPECT_EQ(Snap[0].Errors, 1U);
  EXPECT_EQ(Snap[0].CacheHits, 1U);
  EXPECT_EQ(Snap[1].PluginID, "com.example.a");
  EXPECT_EQ(Snap[1].Callback, "phase.y");
  EXPECT_EQ(Snap[1].Calls, 1U);
  EXPECT_EQ(Snap[2].PluginID, "com.example.b");
  EXPECT_EQ(Snap[2].Callback, "phase.x");
}

TEST(PluginCallbackStatsTest, IgnoresEmptyPluginID) {
  PluginCallbackStats Stats;
  Stats.record("", "phase.x", 100, /*Error=*/false);
  Stats.recordCacheHit("", "phase.x");
  EXPECT_TRUE(Stats.empty());
  EXPECT_EQ(Stats.totalCalls(), 0U);
}

TEST(PluginCallbackStatsTest, JSONIsDeterministicAndEscaped) {
  PluginCallbackStats Stats;
  EXPECT_EQ(Stats.toJSON(),
            "{\"schema\":\"neverc.plugin.callback-stats.v1\",\"entries\":[]}");

  Stats.record("com.example.b", "b/second", 2, /*Error=*/false);
  Stats.record("com.example.a", "a\"first", 1, /*Error=*/true);

  const std::string Expected =
      "{\"schema\":\"neverc.plugin.callback-stats.v1\",\"entries\":["
      "{\"plugin\":\"com.example.a\",\"callback\":\"a\\\"first\","
      "\"calls\":1,\"total_nanos\":1,\"errors\":1,\"cache_hits\":0},"
      "{\"plugin\":\"com.example.b\",\"callback\":\"b/second\","
      "\"calls\":1,\"total_nanos\":2,\"errors\":0,\"cache_hits\":0}]}";
  EXPECT_EQ(Stats.toJSON(), Expected);
}

TEST(PluginPerformanceTest, CallbackGateRecordsStats) {
  PluginProcessServices Services("neverc-plugin-performance-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded = Services.registry().load(NEVERC_TEST_SCOPE_SESSION_PLUGIN);
  ASSERT_TRUE(static_cast<bool>(Loaded))
      << takeErrorMessage(Loaded.takeError());

  const std::array<StringRef, 1> Selected = {"org.neverc.test.scope.session"};
  auto Plan = makePluginActivationPlan(Services.registry(), Selected);
  ASSERT_TRUE(static_cast<bool>(Plan)) << takeErrorMessage(Plan.takeError());
  auto Session = PluginSession::create(Services, *Plan);
  ASSERT_TRUE(static_cast<bool>(Session))
      << takeErrorMessage(Session.takeError());

  // Session setup (ProcessBegin/Register/SessionBegin) already runs plugin
  // callbacks through the same gate, so measure the delta rather than assuming
  // the stats start empty.
  const uint64_t Baseline = (*Session)->callbackStats().totalCalls();

  for (int I = 0; I < 3; ++I) {
    auto Result = (*Session)->invokeCallback(
        "org.neverc.test.scope.session", "perf.callback",
        []() { return NevercStatus{}; });
    ASSERT_TRUE(static_cast<bool>(Result))
        << takeErrorMessage(Result.takeError());
    EXPECT_EQ(Result->Code, NEVERC_STATUS_OK);
  }

  const PluginCallbackStats &Stats = (*Session)->callbackStats();
  EXPECT_FALSE(Stats.empty());
  EXPECT_EQ(Stats.totalCalls(), Baseline + 3);

  const PluginCallbackStats::Entry *PerfEntry = nullptr;
  auto Snap = Stats.snapshot();
  for (const auto &E : Snap)
    if (E.PluginID == "org.neverc.test.scope.session" &&
        E.Callback == "perf.callback")
      PerfEntry = &E;
  ASSERT_NE(PerfEntry, nullptr);
  EXPECT_EQ(PerfEntry->Calls, 3U);
  EXPECT_EQ(PerfEntry->Errors, 0U);
}

} // namespace
