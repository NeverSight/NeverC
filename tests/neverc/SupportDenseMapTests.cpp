#include "llvm/ADT/DenseMap.h"
#include "gtest/gtest.h"

TEST(SupportDenseMapTest, EraseClosesProbeHolesWithoutLosingSurvivors) {
  llvm::DenseMap<unsigned, unsigned> Map;
  for (unsigned I = 1; I <= 48; ++I)
    Map[I] = I * 3;

  for (unsigned I = 1; I <= 48; I += 2)
    ASSERT_TRUE(Map.erase(I));

  EXPECT_EQ(Map.size(), 24u);
  for (unsigned I = 2; I <= 48; I += 2) {
    auto It = Map.find(I);
    ASSERT_NE(It, Map.end());
    EXPECT_EQ(It->second, I * 3);
  }
  for (unsigned I = 1; I <= 48; I += 2)
    EXPECT_EQ(Map.find(I), Map.end());
}

TEST(SupportDenseMapTest, RemoveIfKeepsSurvivorsDiscoverable) {
  llvm::DenseMap<unsigned, unsigned> Map;
  for (unsigned I = 1; I <= 48; ++I)
    Map[I] = I;

  EXPECT_TRUE(Map.remove_if([](const auto &Entry) { return Entry.first % 3 == 0; }));
  EXPECT_EQ(Map.size(), 32u);
  for (unsigned I = 1; I <= 48; ++I) {
    if (I % 3 == 0)
      EXPECT_EQ(Map.find(I), Map.end());
    else {
      auto It = Map.find(I);
      ASSERT_NE(It, Map.end());
      EXPECT_EQ(It->second, I);
    }
  }
  EXPECT_FALSE(Map.remove_if([](const auto &) { return false; }));
}
