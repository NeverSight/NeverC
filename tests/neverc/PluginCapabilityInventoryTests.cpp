//===- PluginCapabilityInventoryTests.cpp - compiled-in inventory --------===//
//
// Validates the capability inventory that is compiled into the host from the
// generated phase schema: the enumerated builtin phase count matches the
// published constant, every phase declares a non-empty policy, and sealed host
// gates never expose interceptable/replaceable/skippable policy bits. These are
// static (compile-time) guarantees over the same macros a plugin sees, plus a
// runtime assertion that pins the first-version ABI.
//
//===----------------------------------------------------------------------===//

#include "neverc/Plugin/NevercPluginAPI.h"

#include "gtest/gtest.h"

namespace {

// Count the enumerated builtin phases and compare against the published count.
#define NEVERC_INVENTORY_COUNT_PHASE(sym) +1
constexpr int kEnumeratedPhaseCount =
    0 NEVERC_FOR_EACH_BUILTIN_PHASE(NEVERC_INVENTORY_COUNT_PHASE);
#undef NEVERC_INVENTORY_COUNT_PHASE

static_assert(kEnumeratedPhaseCount == (int)NEVERC_BUILTIN_PHASE_COUNT,
              "enumerated builtin phases must match NEVERC_BUILTIN_PHASE_COUNT");

// Every builtin phase must declare a non-empty policy set.
#define NEVERC_INVENTORY_CHECK_POLICY(sym)                                     \
  static_assert(NEVERC_PHASE_##sym##_POLICY != UINT64_C(0),                    \
                #sym " must declare a policy");
NEVERC_FOR_EACH_BUILTIN_PHASE(NEVERC_INVENTORY_CHECK_POLICY)
#undef NEVERC_INVENTORY_CHECK_POLICY

// Sealed host gates must never be interceptable, replaceable or skippable.
#define NEVERC_INVENTORY_CHECK_SEALED(sym)                                     \
  static_assert(                                                               \
      !((NEVERC_PHASE_##sym##_POLICY & NEVERC_PHASE_SEALED_HOST_GATE) &&       \
        (NEVERC_PHASE_##sym##_POLICY &                                         \
         (NEVERC_PHASE_INTERCEPTABLE | NEVERC_PHASE_REPLACEABLE |              \
          NEVERC_PHASE_SKIPPABLE_WITH_PROOF))),                               \
      #sym " sealed gate must not be interceptable/replaceable/skippable");
NEVERC_FOR_EACH_BUILTIN_PHASE(NEVERC_INVENTORY_CHECK_SEALED)
#undef NEVERC_INVENTORY_CHECK_SEALED

TEST(PluginCapabilityInventoryTest, EnumeratesAllCompiledInPhases) {
  EXPECT_GT(NEVERC_BUILTIN_PHASE_COUNT, UINT32_C(0));
  EXPECT_EQ(kEnumeratedPhaseCount, (int)NEVERC_BUILTIN_PHASE_COUNT);
}

TEST(PluginCapabilityInventoryTest, LocksFirstVersionABI) {
  EXPECT_EQ(NEVERC_PLUGIN_ABI_MAJOR, UINT16_C(1));
  EXPECT_EQ(NEVERC_PLUGIN_ABI_MINOR, UINT16_C(0));
}

} // namespace
