#include "llvm/ADT/FoldingSet.h"
#include "llvm/Config/abi-breaking.h"
#include "gtest/gtest.h"

#include <climits>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <vector>

namespace neverc_folding_set_test {

struct PairNode : llvm::FoldingSetNode {
  unsigned Key;
  unsigned Value;

  PairNode(unsigned Key, unsigned Value) : Key(Key), Value(Value) {}

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(Key);
    ID.AddInteger(Value);
  }
};

struct CountingNode : llvm::FoldingSetNode {
  unsigned Key;
  unsigned Hash;
  unsigned RehashProfileCalls = 0;

  explicit CountingNode(unsigned Key) : Key(Key) {
    llvm::FoldingSetNodeID ID;
    Profile(ID);
    Hash = ID.ComputeHash();
  }

  void Profile(llvm::FoldingSetNodeID &ID) const { ID.AddInteger(Key); }
};

} // namespace neverc_folding_set_test

namespace llvm {

template <>
struct FoldingSetTrait<neverc_folding_set_test::CountingNode>
    : DefaultFoldingSetTrait<neverc_folding_set_test::CountingNode> {
  using Node = neverc_folding_set_test::CountingNode;

  static void Profile(Node &N, FoldingSetNodeID &ID) { N.Profile(ID); }

  static bool Equals(Node &N, const FoldingSetNodeID &, unsigned IDHash,
                     FoldingSetNodeID &) {
    return N.Hash == IDHash;
  }

  static unsigned ComputeHash(Node &N, FoldingSetNodeID &) {
    ++N.RehashProfileCalls;
    return N.Hash;
  }
};

} // namespace llvm

namespace {

using neverc_folding_set_test::CountingNode;
using neverc_folding_set_test::PairNode;

llvm::FoldingSetNodeID makeID(unsigned Key, unsigned Value) {
  llvm::FoldingSetNodeID ID;
  ID.AddInteger(Key);
  ID.AddInteger(Value);
  return ID;
}

TEST(SupportFoldingSetTest, NodeStoresOnlyCachedHashWord) {
  EXPECT_EQ(sizeof(llvm::FoldingSetNode), sizeof(uint32_t));
}

TEST(SupportFoldingSetTest, OversizedReserveFailsBeforeBucketCountOverflows) {
  EXPECT_DEATH_IF_SUPPORTED(
      {
        llvm::FoldingSet<PairNode> Set;
        Set.reserve(UINT_MAX);
      },
      "FoldingSet capacity exceeds maximum");
}

TEST(SupportFoldingSetTest, GrowthDoesNotReprofileExistingNodes) {
  llvm::FoldingSet<CountingNode> Set;
  std::vector<std::unique_ptr<CountingNode>> Nodes;

  for (unsigned I = 0; I != 200; ++I) {
    Nodes.push_back(std::make_unique<CountingNode>(I));
    EXPECT_EQ(Set.GetOrInsertNode(Nodes.back().get()), Nodes.back().get());
  }

  for (const auto &N : Nodes)
    EXPECT_EQ(N->RehashProfileCalls, 0u);
}

TEST(SupportFoldingSetTest, InsertPosSurvivesGrowth) {
  llvm::FoldingSet<PairNode> Set;
  PairNode Late(9999, 9999);

  llvm::FoldingSetNodeID ID = makeID(Late.Key, Late.Value);
  void *InsertPos = nullptr;
  ASSERT_EQ(Set.FindNodeOrInsertPos(ID, InsertPos), nullptr);
  ASSERT_NE(InsertPos, nullptr);

  std::vector<std::unique_ptr<PairNode>> Nodes;
  for (unsigned I = 0; I != 200; ++I) {
    Nodes.push_back(std::make_unique<PairNode>(I, I));
    Set.InsertNode(Nodes.back().get());
  }

  Set.InsertNode(&Late, InsertPos);
  void *Unused = nullptr;
  EXPECT_EQ(Set.FindNodeOrInsertPos(ID, Unused), &Late);
  EXPECT_EQ(Set.size(), 201u);
}

TEST(SupportFoldingSetTest, InsertEraseStressMatchesReferenceModel) {
  llvm::FoldingSet<PairNode> Set;
  std::map<unsigned, std::unique_ptr<PairNode>> Model;
  std::mt19937 Rng(42);

  for (unsigned Op = 0; Op != 2000; ++Op) {
    unsigned Key = Rng() % 4096;
    llvm::FoldingSetNodeID ID = makeID(Key, Key);
    auto It = Model.find(Key);

    if (Rng() & 1) {
      void *InsertPos = nullptr;
      PairNode *Found = Set.FindNodeOrInsertPos(ID, InsertPos);
      if (It != Model.end()) {
        ASSERT_EQ(Found, It->second.get());
        continue;
      }

      ASSERT_EQ(Found, nullptr);
      auto N = std::make_unique<PairNode>(Key, Key);
      Set.InsertNode(N.get(), InsertPos);
      Model.emplace(Key, std::move(N));
    } else if (It != Model.end()) {
      ASSERT_TRUE(Set.RemoveNode(It->second.get()));
      ASSERT_FALSE(Set.RemoveNode(It->second.get()));
      Model.erase(It);
    }

    ASSERT_EQ(Set.size(), Model.size());
  }

  for (const auto &[Key, Node] : Model) {
    llvm::FoldingSetNodeID ID = makeID(Key, Key);
    void *InsertPos = nullptr;
    EXPECT_EQ(Set.FindNodeOrInsertPos(ID, InsertPos), Node.get());
  }

  std::set<PairNode *> Visited;
  for (PairNode &N : Set)
    EXPECT_TRUE(Visited.insert(&N).second);
  EXPECT_EQ(Visited.size(), Model.size());
}

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
TEST(SupportFoldingSetTest, InsertInvalidatesIterator) {
  llvm::FoldingSet<PairNode> Set;
  PairNode First(1, 1);
  PairNode Second(2, 2);
  Set.InsertNode(&First);
  auto It = Set.begin();
  Set.InsertNode(&Second);
  EXPECT_DEATH((void)It->Value, "invalid iterator access");
}

TEST(SupportFoldingSetTest, AbsentRemovalKeepsIteratorValid) {
  llvm::FoldingSet<PairNode> Set;
  PairNode Present(1, 1);
  PairNode Absent(2, 2);
  Set.InsertNode(&Present);
  auto It = Set.begin();
  EXPECT_FALSE(Set.RemoveNode(&Absent));
  EXPECT_EQ(&*It, &Present);
}
#endif

} // namespace
