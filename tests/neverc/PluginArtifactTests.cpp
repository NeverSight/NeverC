#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "gtest/gtest.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

PluginArtifactTypeDescriptor
ownedType(NevercInterfaceID ID, const char *Name, int &Destroyed) {
  PluginArtifactTypeDescriptor Descriptor;
  Descriptor.ID = ID;
  Descriptor.Name = Name;
  Descriptor.Ownership = PluginArtifactOwnership::Owned;
  Descriptor.Clone = [](const void *Value) -> Expected<void *> {
    return const_cast<void *>(Value);
  };
  Descriptor.Destroy = [&](void *) { ++Destroyed; };
  Descriptor.Verify = [](const void *Value) -> Error {
    if (!Value || *static_cast<const int *>(Value) < 0)
      return createStringError(inconvertibleErrorCode(),
                               "artifact verification failed");
    return Error::success();
  };
  return Descriptor;
}

TEST(PluginArtifactRegistryTest, ValidatesAndFreezesTypeRegistry) {
  PluginArtifactRegistry Registry;
  int Destroyed = 0;
  const NevercInterfaceID TypeID{0x1111, 0x2222};
  auto Type = Registry.registerType(
      ownedType(TypeID, "neverc.test.artifact", Destroyed));
  ASSERT_TRUE(static_cast<bool>(Type))
      << takeErrorMessage(Type.takeError());
  EXPECT_EQ((*Type)->name(), "neverc.test.artifact");
  EXPECT_EQ(Registry.size(), 1U);

  auto Duplicate = Registry.registerType(
      ownedType(TypeID, "neverc.test.duplicate", Destroyed));
  ASSERT_FALSE(static_cast<bool>(Duplicate));
  EXPECT_NE(takeErrorMessage(Duplicate.takeError()).find("duplicate"),
            std::string::npos);

  PluginArtifactTypeDescriptor NullID =
      ownedType({}, "neverc.test.null", Destroyed);
  auto Invalid = Registry.registerType(std::move(NullID));
  ASSERT_FALSE(static_cast<bool>(Invalid));

  EXPECT_FALSE(Registry.freeze());
  EXPECT_TRUE(Registry.isFrozen());
  auto Late = Registry.registerType(
      ownedType({0x3333, 0x4444}, "neverc.test.late", Destroyed));
  ASSERT_FALSE(static_cast<bool>(Late));
  EXPECT_NE(takeErrorMessage(Late.takeError()).find("frozen"),
            std::string::npos);
}

TEST(PluginArtifactTransactionTest, PublishesAtomicallyAndOnlyOnce) {
  PluginArtifactRegistry Registry;
  int Destroyed = 0;
  const NevercInterfaceID TypeID{0x1111, 0x2222};
  auto Type = Registry.registerType(
      ownedType(TypeID, "neverc.test.artifact", Destroyed));
  ASSERT_TRUE(static_cast<bool>(Type));
  ASSERT_FALSE(Registry.freeze());
  PluginArtifactSlot Slot(*Type);

  int First = 7;
  auto FirstTransaction =
      PluginArtifactTransaction::create(Registry, TypeID, &First);
  ASSERT_TRUE(static_cast<bool>(FirstTransaction));
  EXPECT_FALSE((*FirstTransaction)->commit(Slot));
  EXPECT_TRUE((*FirstTransaction)->isCommitted());
  EXPECT_EQ(Slot.payload(), &First);
  EXPECT_EQ(Slot.generation(), 1U);
  EXPECT_EQ(Destroyed, 0);

  Error DuplicateCommit = (*FirstTransaction)->commit(Slot);
  ASSERT_TRUE(static_cast<bool>(DuplicateCommit));
  EXPECT_NE(takeErrorMessage(std::move(DuplicateCommit)).find("once"),
            std::string::npos);

  int Second = 8;
  auto SecondTransaction =
      PluginArtifactTransaction::create(Registry, TypeID, &Second);
  ASSERT_TRUE(static_cast<bool>(SecondTransaction));
  EXPECT_FALSE((*SecondTransaction)->commit(Slot));
  EXPECT_EQ(Slot.payload(), &Second);
  EXPECT_EQ(Slot.generation(), 2U);
  EXPECT_EQ(Destroyed, 1);
}

TEST(PluginArtifactTransactionTest,
     FailedVerificationAndWrongTypePreservePublishedOutput) {
  PluginArtifactRegistry Registry;
  int DestroyedA = 0;
  int DestroyedB = 0;
  const NevercInterfaceID TypeA{0x1111, 0x2222};
  const NevercInterfaceID TypeB{0x3333, 0x4444};
  auto RegisteredA = Registry.registerType(
      ownedType(TypeA, "neverc.test.a", DestroyedA));
  auto RegisteredB = Registry.registerType(
      ownedType(TypeB, "neverc.test.b", DestroyedB));
  ASSERT_TRUE(static_cast<bool>(RegisteredA));
  ASSERT_TRUE(static_cast<bool>(RegisteredB));
  ASSERT_FALSE(Registry.freeze());
  PluginArtifactSlot Slot(*RegisteredA);

  int Published = 5;
  auto Initial =
      PluginArtifactTransaction::create(Registry, TypeA, &Published);
  ASSERT_TRUE(static_cast<bool>(Initial));
  ASSERT_FALSE((*Initial)->commit(Slot));

  {
    int Invalid = -1;
    auto Failed =
        PluginArtifactTransaction::create(Registry, TypeA, &Invalid);
    ASSERT_TRUE(static_cast<bool>(Failed));
    Error Verification = (*Failed)->commit(Slot);
    ASSERT_TRUE(static_cast<bool>(Verification));
    EXPECT_NE(takeErrorMessage(std::move(Verification)).find("verification"),
              std::string::npos);
    EXPECT_EQ(Slot.payload(), &Published);
    EXPECT_EQ(Slot.generation(), 1U);
  }
  EXPECT_EQ(DestroyedA, 1);

  {
    int Wrong = 9;
    auto WrongType =
        PluginArtifactTransaction::create(Registry, TypeB, &Wrong);
    ASSERT_TRUE(static_cast<bool>(WrongType));
    Error Mismatch = (*WrongType)->commit(Slot);
    ASSERT_TRUE(static_cast<bool>(Mismatch));
    EXPECT_NE(takeErrorMessage(std::move(Mismatch)).find("type"),
              std::string::npos);
    EXPECT_EQ(Slot.payload(), &Published);
  }
  EXPECT_EQ(DestroyedB, 1);

  {
    int Aborted = 10;
    auto Transaction =
        PluginArtifactTransaction::create(Registry, TypeA, &Aborted);
    ASSERT_TRUE(static_cast<bool>(Transaction));
    (*Transaction)->abort();
    EXPECT_EQ(Slot.payload(), &Published);
  }
  EXPECT_EQ(DestroyedA, 2);
}

} // namespace
