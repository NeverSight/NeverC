//===- SupportUnicodeTests.cpp - Unicode StringRef boundaries ------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Unicode.h"

#include <gtest/gtest.h>

#include <string>

using namespace llvm;

TEST(SupportUnicodeTest, EmptyTrieNodeHasAnEmptyFullName) {
  sys::unicode::Node Root = sys::unicode::createRoot();
  EXPECT_TRUE(Root.fullName().empty());
}

TEST(SupportUnicodeTest, GeneratedNameDoesNotReadPastStringRef) {
  std::string Storage = "CJK UNIFIED IDEOGRAPH-4E00F";
  StringRef Name(Storage.data(), Storage.size() - 1);
  sys::unicode::BufferType CanonicalName;

  EXPECT_EQ(sys::unicode::nameToCodepoint(Name, /*Strict=*/true, CanonicalName),
            U'\u4E00');
}
