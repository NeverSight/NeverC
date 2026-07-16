#include "neverc/Plugin/Host/PluginLLVMOptionSnapshot.h"

#include "llvm/Support/CommandLine.h"
#include "gtest/gtest.h"
#include <chrono>
#include <future>
#include <thread>

using namespace llvm;
using namespace neverc::plugin;

namespace {

cl::opt<int> SnapshotScalar("neverc-plugin-test-llvm-snapshot-scalar",
                            cl::init(7));
cl::list<std::string> SnapshotList("neverc-plugin-test-llvm-snapshot-list",
                                   cl::ZeroOrMore);

TEST(PluginLLVMOptionSnapshotTest, RestoresValuesAndOccurrenceMetadata) {
  SnapshotScalar.reset();
  SnapshotList.reset();
  ASSERT_FALSE(SnapshotScalar.addOccurrence(3, SnapshotScalar.ArgStr, "41"));
  ASSERT_FALSE(SnapshotList.addOccurrence(5, SnapshotList.ArgStr, "before"));
  ASSERT_EQ(SnapshotScalar.getNumOccurrences(), 1);
  ASSERT_EQ(SnapshotList.getNumOccurrences(), 1);

  {
    PluginLLVMOptionSnapshot Snapshot(pluginLLVMOptionGate());
    SnapshotScalar = 99;
    SnapshotScalar.reset();
    SnapshotList.reset();
    ASSERT_FALSE(SnapshotList.addOccurrence(9, SnapshotList.ArgStr, "after"));
  }

  EXPECT_EQ(static_cast<int>(SnapshotScalar), 41);
  EXPECT_EQ(SnapshotScalar.getNumOccurrences(), 1);
  EXPECT_EQ(SnapshotScalar.getPosition(), 3u);
  ASSERT_EQ(SnapshotList.size(), 1u);
  EXPECT_EQ(SnapshotList[0], "before");
  EXPECT_EQ(SnapshotList.getNumOccurrences(), 1);
  EXPECT_EQ(SnapshotList.getPosition(0), 5u);
}

TEST(PluginLLVMOptionSnapshotTest, SerializesConcurrentMutationScopes) {
  auto Outer =
      std::make_unique<PluginLLVMOptionSnapshot>(pluginLLVMOptionGate());
  std::promise<void> StartedPromise;
  std::future<void> Started = StartedPromise.get_future();
  std::promise<void> AcquiredPromise;
  std::future<void> Acquired = AcquiredPromise.get_future();

  std::thread Worker([&] {
    StartedPromise.set_value();
    PluginLLVMOptionSnapshot Inner(pluginLLVMOptionGate());
    AcquiredPromise.set_value();
  });
  Started.wait();
  EXPECT_EQ(Acquired.wait_for(std::chrono::milliseconds(50)),
            std::future_status::timeout);

  Outer.reset();
  EXPECT_EQ(Acquired.wait_for(std::chrono::seconds(1)),
            std::future_status::ready);
  Worker.join();
}

TEST(PluginLLVMOptionSnapshotTest, AllowsSameThreadNestedExclusiveLease) {
  EXPECT_FALSE(pluginLLVMOptionGateHeldExclusivelyByCurrentThread());
  {
    PluginLLVMOptionExclusiveLease Outer(pluginLLVMOptionGate());
    EXPECT_TRUE(pluginLLVMOptionGateHeldExclusivelyByCurrentThread());
    {
      PluginLLVMOptionExclusiveLease Inner(pluginLLVMOptionGate());
      PluginLLVMOptionSharedLease Read(pluginLLVMOptionGate());
      EXPECT_TRUE(pluginLLVMOptionGateHeldExclusivelyByCurrentThread());
    }
    EXPECT_TRUE(pluginLLVMOptionGateHeldExclusivelyByCurrentThread());
  }
  EXPECT_FALSE(pluginLLVMOptionGateHeldExclusivelyByCurrentThread());
}

} // namespace
