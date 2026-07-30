//===- SupportAtomicTests.cpp - Legacy atomic bridge regressions ----------===//
//
// llvm/Support/Atomic.h is still part of the public Support surface even
// though new code uses std::atomic.  The CSupport migration left its C entry
// points declared but removed their implementation, so any direct use compiled
// successfully and then failed at link time.  Exercising both operations here
// keeps that bridge complete on every host.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Atomic.h"

#include <gtest/gtest.h>

using namespace llvm;

TEST(SupportAtomicTest, CompareAndSwapReturnsThePreviousValue) {
  volatile sys::cas_flag Value = 7;

  EXPECT_EQ(sys::CompareAndSwap(&Value, 11, 7), 7);
  EXPECT_EQ(Value, 11);

  EXPECT_EQ(sys::CompareAndSwap(&Value, 13, 7), 11);
  EXPECT_EQ(Value, 11);
}

TEST(SupportAtomicTest, MemoryFenceIsAvailable) {
  // The regression was a missing symbol, so reaching this call is the
  // assertion.  Its ordering semantics require multiple threads to observe,
  // but those are supplied by the platform primitive rather than this wrapper.
  sys::MemoryFence();
}
