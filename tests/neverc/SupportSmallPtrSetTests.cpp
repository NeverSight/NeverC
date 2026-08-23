#include "csupport/lsmall_lptr_lset.h"
#include "llvm/ADT/SmallPtrSet.h"

#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <limits>

TEST(SupportSmallPtrSetTest, SharedHashRetainsHighPointerBits) {
  if (std::numeric_limits<uintptr_t>::digits <= 32)
    GTEST_SKIP() << "the regression requires a 64-bit pointer";

  constexpr uintptr_t Value =
      (uintptr_t{0x1234} << 32) | uintptr_t{0x56789ab0};
  const auto *Pointer = reinterpret_cast<const void *>(Value);
  const unsigned Expected =
      static_cast<unsigned>(Value >> 4) ^ static_cast<unsigned>(Value >> 9);

  EXPECT_EQ(csupport_sps_hash_pointer(Pointer), Expected);
}

TEST(SupportSmallPtrSetTest, GrowthKeepsEveryPointerDiscoverable) {
  std::array<int, 128> Values{};
  llvm::SmallPtrSet<const void *, 4> Set;

  for (int &Value : Values)
    ASSERT_TRUE(Set.insert(&Value).second);
  for (int &Value : Values)
    EXPECT_TRUE(Set.contains(&Value));

  for (size_t I = 0; I < Values.size(); I += 2)
    ASSERT_TRUE(Set.erase(&Values[I]));
  for (size_t I = 0; I < Values.size(); ++I)
    EXPECT_EQ(Set.contains(&Values[I]), I % 2 != 0);
}
