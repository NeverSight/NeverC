#include "neverc/Foundation/Core/ThreadLocalStorage.h"
#include "gtest/gtest.h"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using namespace neverc;

namespace {

struct LifetimeProbe {
  int Value;
  int *DestructionCount;

  LifetimeProbe(int Value, int &DestructionCount)
      : Value(Value), DestructionCount(&DestructionCount) {}

  ~LifetimeProbe() { ++*DestructionCount; }
};

static_assert(
    std::is_trivially_destructible_v<ScopedThreadLocalValue<LifetimeProbe>>);

struct StackProbe {
  int Value;
  std::vector<int> *DestructionOrder;

  StackProbe(int Value, std::vector<int> &DestructionOrder)
      : Value(Value), DestructionOrder(&DestructionOrder) {}
  StackProbe(const StackProbe &) = delete;
  StackProbe &operator=(const StackProbe &) = delete;
  StackProbe(StackProbe &&Other) noexcept
      : Value(Other.Value),
        DestructionOrder(std::exchange(Other.DestructionOrder, nullptr)) {}
  StackProbe &operator=(StackProbe &&) = delete;

  ~StackProbe() {
    if (DestructionOrder)
      DestructionOrder->push_back(Value);
  }
};

struct ThrowingProbe {
  int Value;
  int *DestructionCount;

  ThrowingProbe(int Value, int &DestructionCount, bool ShouldThrow)
      : Value(Value), DestructionCount(&DestructionCount) {
    if (ShouldThrow)
      throw std::runtime_error("requested construction failure");
  }
  ThrowingProbe(const ThrowingProbe &) = delete;
  ThrowingProbe &operator=(const ThrowingProbe &) = delete;
  ThrowingProbe(ThrowingProbe &&Other) noexcept
      : Value(Other.Value),
        DestructionCount(std::exchange(Other.DestructionCount, nullptr)) {}
  ThrowingProbe &operator=(ThrowingProbe &&) = delete;

  ~ThrowingProbe() {
    if (DestructionCount)
      ++*DestructionCount;
  }
};

static_assert(
    std::is_trivially_destructible_v<ScopedThreadLocalStack<StackProbe, 2>>);

} // namespace

TEST(ThreadLocalStorageTest, ValueReleasesContainedStateWhenReset) {
  int DestructionCount = 0;
  ScopedThreadLocalValue<LifetimeProbe> Value;

  EXPECT_FALSE(Value.hasValue());
  LifetimeProbe &Stored = Value.emplace(7, DestructionCount);
  EXPECT_TRUE(Value.hasValue());
  EXPECT_EQ(Value.get().Value, 7);
  EXPECT_EQ(&Value.get(), &Stored);

  Value.reset();
  EXPECT_FALSE(Value.hasValue());
  EXPECT_EQ(DestructionCount, 1);

  Value.reset();
  EXPECT_EQ(DestructionCount, 1);
}

TEST(ThreadLocalStorageTest, StackReleasesContainedStateInLifoOrder) {
  std::vector<int> DestructionOrder;
  ScopedThreadLocalStack<StackProbe, 2> Stack;

  EXPECT_TRUE(Stack.empty());
  Stack.emplace_back(1, DestructionOrder);
  Stack.emplace_back(2, DestructionOrder);
  Stack.emplace_back(3, DestructionOrder);

  EXPECT_FALSE(Stack.empty());
  EXPECT_EQ(Stack.size(), 3U);
  EXPECT_EQ(Stack.back().Value, 3);
  EXPECT_TRUE(
      Stack.any_of([](const StackProbe &Probe) { return Probe.Value == 2; }));
  EXPECT_FALSE(
      Stack.any_of([](const StackProbe &Probe) { return Probe.Value == 4; }));

  Stack.pop_back();
  EXPECT_EQ(Stack.back().Value, 2);
  Stack.pop_back();
  Stack.pop_back();

  EXPECT_TRUE(Stack.empty());
  EXPECT_EQ(DestructionOrder, (std::vector<int>{3, 2, 1}));
}

TEST(ThreadLocalStorageTest, FailedStackInsertionPreservesState) {
  int DestructionCount = 0;
  ScopedThreadLocalStack<ThrowingProbe, 1> Stack;

  EXPECT_THROW(Stack.emplace_back(1, DestructionCount, true),
               std::runtime_error);
  EXPECT_TRUE(Stack.empty());
  EXPECT_EQ(DestructionCount, 0);

  Stack.emplace_back(10, DestructionCount, false);
  EXPECT_THROW(Stack.emplace_back(20, DestructionCount, true),
               std::runtime_error);
  ASSERT_EQ(Stack.size(), 1U);
  EXPECT_EQ(Stack.back().Value, 10);
  EXPECT_EQ(DestructionCount, 0);

  Stack.emplace_back(20, DestructionCount, false);
  ASSERT_EQ(Stack.size(), 2U);
  EXPECT_EQ(Stack.back().Value, 20);
  Stack.pop_back();
  EXPECT_EQ(DestructionCount, 1);
  EXPECT_EQ(Stack.back().Value, 10);
  Stack.pop_back();
  EXPECT_EQ(DestructionCount, 2);
  EXPECT_TRUE(Stack.empty());
}
