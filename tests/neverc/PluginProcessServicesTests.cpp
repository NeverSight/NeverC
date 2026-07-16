#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "gtest/gtest.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <cstring>
#include <limits>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

TEST(PluginProcessServicesTest, AllocatesProcessUniqueNonzeroOwnerTokens) {
  PluginProcessServices Services("neverc-plugin-process-tests", 22);

  auto First = Services.allocateOwnerToken();
  ASSERT_TRUE(static_cast<bool>(First));
  auto Second = Services.allocateOwnerToken();
  ASSERT_TRUE(static_cast<bool>(Second));

  EXPECT_EQ(*First, 1u);
  EXPECT_EQ(*Second, 2u);
  EXPECT_NE(*First, *Second);
  EXPECT_EQ(Services.registry().generation(), 0u);
  EXPECT_EQ(Services.interfaces().size(), 1u);
}

TEST(PluginProcessServicesTest, OwnerTokenAllocatorNeverWrapsOrReusesZero) {
  OwnerTokenAllocator Allocator(std::numeric_limits<uint64_t>::max());

  auto Last = Allocator.allocate();
  ASSERT_TRUE(static_cast<bool>(Last));
  EXPECT_EQ(*Last, std::numeric_limits<uint64_t>::max());

  auto Exhausted = Allocator.allocate();
  ASSERT_FALSE(static_cast<bool>(Exhausted));
  EXPECT_NE(takeErrorMessage(Exhausted.takeError()).find("exhausted"),
            std::string::npos);
}

TEST(PluginProcessServicesTest, ShutdownIsExplicitAndIdempotent) {
  PluginProcessServices Services("neverc-plugin-process-tests", 22);
  EXPECT_FALSE(Services.shutdown());
  EXPECT_FALSE(Services.shutdown());
  EXPECT_FALSE(static_cast<bool>(Services.registry().acquireSnapshot()));
}

TEST(PluginProcessServicesTest, PublishesAUsableCoreInterface) {
  PluginProcessServices Services("neverc-plugin-process-tests", 22);
  EXPECT_FALSE(Services.interfaces().freeze());

  NevercInterfaceID CoreID{NEVERC_INTERFACE_CORE_HIGH,
                            NEVERC_INTERFACE_CORE_LOW};
  auto Query =
      Services.interfaces().query(CoreID, NEVERC_CORE_API_MAJOR,
                                  NEVERC_CORE_API_MINOR);
  ASSERT_TRUE(static_cast<bool>(Query));
  const auto *Core = static_cast<const NevercCoreAPI *>(Query->Table);
  ASSERT_NE(Core, nullptr);
  ASSERT_NE(Core->Allocate, nullptr);
  ASSERT_NE(Core->Reallocate, nullptr);
  ASSERT_NE(Core->Deallocate, nullptr);

  void *Memory = nullptr;
  NevercStatus Status = Core->Allocate(Core->Context, 8, 64, &Memory);
  ASSERT_EQ(Status.Code, NEVERC_STATUS_OK);
  ASSERT_NE(Memory, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(Memory) % 64, 0u);
  std::memcpy(Memory, "neverc", 7);

  void *Larger = nullptr;
  Status = Core->Reallocate(Core->Context, Memory, 8, 32, 64, &Larger);
  ASSERT_EQ(Status.Code, NEVERC_STATUS_OK);
  ASSERT_NE(Larger, nullptr);
  EXPECT_EQ(std::memcmp(Larger, "neverc", 7), 0);

  Status = Core->Deallocate(Core->Context, Larger, 32, 64);
  EXPECT_EQ(Status.Code, NEVERC_STATUS_OK);

  void *Invalid = reinterpret_cast<void *>(uintptr_t{1});
  Status = Core->Allocate(Core->Context, 8, 3, &Invalid);
  EXPECT_EQ(Status.Code, NEVERC_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ(Invalid, nullptr);
}

} // namespace
