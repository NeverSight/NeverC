#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "gtest/gtest.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>

using namespace llvm;
using namespace neverc::plugin;

namespace {

constexpr NevercInterfaceID StableID{0x1111111111111111ULL,
                                     0x2222222222222222ULL};
constexpr NevercInterfaceID LockstepID{0x3333333333333333ULL,
                                       0x4444444444444444ULL};

struct TestInterfaceTable {
  NevercABITableHeader Header;
  uint64_t Value;
};

const TestInterfaceTable StableTable{{sizeof(TestInterfaceTable), 1, 2, 0},
                                     42};
const TestInterfaceTable LockstepTable{{sizeof(TestInterfaceTable), 3, 1, 0},
                                       84};

OwnedCompatibilityKey lockstepKey() {
  return {"neverc-test-build", "arm64-test-abi", 22};
}

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

TEST(PluginInterfaceRegistryTest, NegotiatesMajorMinorAndTableExtent) {
  PluginInterfaceRegistry Registry;
  EXPECT_FALSE(Registry.registerInterface(StableID, NEVERC_INTERFACE_STABLE,
                                          &StableTable, {}));
  EXPECT_FALSE(Registry.freeze());

  auto Match = Registry.query(StableID, 1, 1);
  ASSERT_TRUE(static_cast<bool>(Match));
  EXPECT_EQ(Match->Table, &StableTable);
  EXPECT_EQ(Match->Minor, 2u);
  EXPECT_EQ(Match->StructSize, sizeof(StableTable));

  auto WrongMajor = Registry.query(StableID, 2, 0);
  ASSERT_FALSE(static_cast<bool>(WrongMajor));
  EXPECT_NE(takeErrorMessage(WrongMajor.takeError()).find("major"),
            std::string::npos);

  auto MinorTooNew = Registry.query(StableID, 1, 3);
  ASSERT_FALSE(static_cast<bool>(MinorTooNew));
  EXPECT_NE(takeErrorMessage(MinorTooNew.takeError()).find("minor"),
            std::string::npos);
}

TEST(PluginInterfaceRegistryTest, IsImmutableAfterFreeze) {
  PluginInterfaceRegistry Registry;
  EXPECT_FALSE(Registry.registerInterface(StableID, NEVERC_INTERFACE_STABLE,
                                          &StableTable, {}));
  EXPECT_FALSE(Registry.freeze());

  Error Mutation = Registry.registerInterface(
      LockstepID, NEVERC_INTERFACE_LOCKSTEP, &LockstepTable, lockstepKey());
  ASSERT_TRUE(static_cast<bool>(Mutation));
  EXPECT_NE(takeErrorMessage(std::move(Mutation)).find("frozen"),
            std::string::npos);
}

TEST(PluginInterfaceRegistryTest, RejectsDuplicateInterfaceMajor) {
  PluginInterfaceRegistry Registry;
  EXPECT_FALSE(Registry.registerInterface(StableID, NEVERC_INTERFACE_STABLE,
                                          &StableTable, {}));
  Error Duplicate = Registry.registerInterface(
      StableID, NEVERC_INTERFACE_STABLE, &StableTable, {});
  ASSERT_TRUE(static_cast<bool>(Duplicate));
  EXPECT_NE(takeErrorMessage(std::move(Duplicate)).find("duplicate"),
            std::string::npos);
}

TEST(PluginInterfaceRegistryTest, EnforcesLockstepCompatibility) {
  PluginInterfaceRegistry Registry;
  EXPECT_FALSE(Registry.registerInterface(
      LockstepID, NEVERC_INTERFACE_LOCKSTEP, &LockstepTable, lockstepKey()));
  EXPECT_FALSE(Registry.freeze());

  OwnedInterfaceRequirement Requirement;
  Requirement.Interface = LockstepID;
  Requirement.Major = 3;
  Requirement.MinimumMinor = 1;
  Requirement.Required = true;
  Requirement.Stability = NEVERC_INTERFACE_LOCKSTEP;
  Requirement.Compatibility = lockstepKey();
  EXPECT_FALSE(Registry.validateRequirement(Requirement));

  Requirement.Compatibility.TargetABIKey = "x86_64-test-abi";
  Error Mismatch = Registry.validateRequirement(Requirement);
  ASSERT_TRUE(static_cast<bool>(Mismatch));
  EXPECT_NE(takeErrorMessage(std::move(Mismatch)).find("compatibility"),
            std::string::npos);
}

TEST(PluginInterfaceRegistryTest, CBridgeClearsOutputsOnFailure) {
  PluginInterfaceRegistry Registry;
  EXPECT_FALSE(Registry.registerInterface(StableID, NEVERC_INTERFACE_STABLE,
                                          &StableTable, {}));
  EXPECT_FALSE(Registry.freeze());

  const void *Table = reinterpret_cast<const void *>(uintptr_t{1});
  uint16_t Minor = 99;
  uint64_t StructSize = 99;
  NevercInterfaceID Missing{0xAAAAAAAAAAAAAAAAULL, 0xBBBBBBBBBBBBBBBBULL};
  NevercStatus Status =
      queryPluginInterface(&Registry, Missing, 1, 0, &Table, &Minor,
                           &StructSize);

  EXPECT_EQ(Status.Code, NEVERC_STATUS_MISSING_INTERFACE);
  EXPECT_EQ(Table, nullptr);
  EXPECT_EQ(Minor, 0u);
  EXPECT_EQ(StructSize, 0u);
}

} // namespace
