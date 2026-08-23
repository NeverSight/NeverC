#include "llvm/ADT/StringMap.h"
#include "llvm/Support/xxhash.h"
#include "gtest/gtest.h"
#include <array>
#include <string>

namespace {

std::array<std::string, 14> makeKeysForInitialBuckets() {
  std::array<std::string, 14> Keys;
  std::array<bool, 14> Found{};
  size_t Remaining = Keys.size();
  for (unsigned Candidate = 0; Remaining != 0; ++Candidate) {
    std::string Key = "string-map-bucket-" + std::to_string(Candidate);
    const size_t Bucket = llvm::xxh3_64bits(Key) & 15;
    if (Bucket >= Keys.size() || Found[Bucket])
      continue;
    Keys[Bucket] = std::move(Key);
    Found[Bucket] = true;
    --Remaining;
  }
  return Keys;
}

} // namespace

TEST(SupportStringMapTest, EraseClosesProbeHolesWithoutLosingSurvivors) {
  const auto Keys = makeKeysForInitialBuckets();
  llvm::StringMap<unsigned> Map;

  for (unsigned Bucket = 0; Bucket != 12; ++Bucket)
    ASSERT_TRUE(Map.try_emplace(Keys[Bucket], Bucket).second);
  ASSERT_EQ(Map.getNumBuckets(), 16u);

  for (unsigned Bucket = 0; Bucket != 10; ++Bucket)
    ASSERT_TRUE(Map.erase(Keys[Bucket]));

  ASSERT_TRUE(Map.try_emplace(Keys[12], 12).second);
  ASSERT_TRUE(Map.try_emplace(Keys[13], 13).second);
  EXPECT_EQ(Map.getNumBuckets(), 16u);

  for (unsigned Bucket = 10; Bucket != Keys.size(); ++Bucket) {
    auto It = Map.find(Keys[Bucket]);
    ASSERT_NE(It, Map.end());
    EXPECT_EQ(It->second, Bucket);
  }
  for (unsigned Bucket = 0; Bucket != 10; ++Bucket)
    EXPECT_EQ(Map.find(Keys[Bucket]), Map.end());

  for (unsigned Bucket = 0; Bucket != 10; ++Bucket)
    ASSERT_TRUE(Map.try_emplace(Keys[Bucket], Bucket).second);

  for (unsigned Bucket = 0; Bucket != Keys.size(); ++Bucket) {
    auto It = Map.find(Keys[Bucket]);
    ASSERT_NE(It, Map.end());
    EXPECT_EQ(It->second, Bucket);
  }
}
