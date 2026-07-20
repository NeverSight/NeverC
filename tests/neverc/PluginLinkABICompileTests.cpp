extern "C" {
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"
}

#include "neverc/Plugin/Host/LinkPluginInterfaces.h"
#include "neverc/Plugin/NevercPluginAPI.h" // IWYU pragma: keep
#include "gtest/gtest.h"
#include <array>
#include <cstddef>
#include <type_traits>

namespace {

TEST(PluginLinkABICompileTest, PublishesFirstVersionPureCTables) {
  static_assert(std::is_same_v<NevercLinkInputKind, uint32_t>);
  static_assert(std::is_same_v<NevercLinkOutputKind, uint32_t>);
  static_assert(std::is_same_v<NevercLinkState, uint32_t>);
  static_assert(std::is_same_v<NevercLTOType, uint32_t>);
  static_assert(std::is_same_v<NevercLTOCacheScope, uint32_t>);
  static_assert(std::is_standard_layout_v<NevercLinkAPI>);
  static_assert(std::is_standard_layout_v<NevercLinkRegistrarAPI>);
  static_assert(std::is_standard_layout_v<NevercLTOAPI>);
  static_assert(std::is_standard_layout_v<NevercLTORegistrarAPI>);
  static_assert(offsetof(NevercLinkAPI, Header) == 0);
  static_assert(offsetof(NevercLinkRegistrarAPI, Header) == 0);
  static_assert(offsetof(NevercLTOAPI, Header) == 0);
  static_assert(NEVERC_LINK_API_MAJOR == 1);
  static_assert(NEVERC_LINK_REGISTRAR_API_MAJOR == 1);
  static_assert(NEVERC_LTO_API_MAJOR == 1);
  EXPECT_EQ(NEVERC_LINK_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_LINK_REGISTRAR_INTERFACE_STABILITY,
            NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_LTO_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
}

TEST(PluginLinkABICompileTest, UsesOpaqueDomainSpecificHandles) {
  static_assert(sizeof(NevercLinkRequestHandle) == sizeof(NevercHandle));
  static_assert(sizeof(NevercLinkInputHandle) == sizeof(NevercHandle));
  static_assert(sizeof(NevercLinkGraphHandle) == sizeof(NevercHandle));
  static_assert(sizeof(NevercLinkSymbolHandle) == sizeof(NevercHandle));
  static_assert(sizeof(NevercLinkEdgeHandle) == sizeof(NevercHandle));
  static_assert(sizeof(NevercBinaryImageHandle) == sizeof(NevercHandle));
  static_assert(sizeof(NevercLTOModuleHandle) == sizeof(NevercHandle));
  static_assert(sizeof(NevercLTOSummaryHandle) == sizeof(NevercHandle));

  constexpr std::array<NevercInterfaceID, 4> IDs = {{
      {NEVERC_INTERFACE_LINK_HIGH, NEVERC_INTERFACE_LINK_LOW},
      {NEVERC_INTERFACE_LINK_REGISTRAR_HIGH,
       NEVERC_INTERFACE_LINK_REGISTRAR_LOW},
      {NEVERC_INTERFACE_LTO_HIGH, NEVERC_INTERFACE_LTO_LOW},
      {NEVERC_INTERFACE_LTO_REGISTRAR_HIGH,
       NEVERC_INTERFACE_LTO_REGISTRAR_LOW},
  }};
  for (size_t I = 0; I != IDs.size(); ++I) {
    EXPECT_NE(IDs[I].High | IDs[I].Low, 0U);
    for (size_t J = I + 1; J != IDs.size(); ++J)
      EXPECT_TRUE(IDs[I].High != IDs[J].High ||
                  IDs[I].Low != IDs[J].Low);
  }
}

TEST(PluginLinkABICompileTest, PublishesFixedLinkPhasePolicies) {
  static_assert(NEVERC_BUILTIN_LINK_PHASE_COUNT == 20);
  constexpr NevercPhasePolicy Transition =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
      NEVERC_PHASE_REPLACEABLE | NEVERC_PHASE_SKIPPABLE_WITH_PROOF;
  constexpr NevercPhasePolicy Replaceable =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
      NEVERC_PHASE_REPLACEABLE;
  constexpr NevercPhasePolicy Interceptable =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE;
  constexpr NevercPhasePolicy Sealed =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_SEALED_HOST_GATE;

  EXPECT_EQ(NEVERC_PHASE_LINK_INPUT_PROBE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_READ_INPUTS_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_LTO_RESOLVE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_LTO_GENERATE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_RESOLVE_SYMBOLS_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_SELECT_COMDAT_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_GC_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_ICF_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_SYNTHESIZE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_RELAX_THUNKS_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_LAYOUT_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_RELOCATE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_EMIT_IMAGE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_LINK_FULL_POLICY, Replaceable);
  EXPECT_EQ(NEVERC_PHASE_LINK_OBJECT_MERGE_POLICY, Replaceable);
  EXPECT_EQ(NEVERC_PHASE_LINK_POST_EMIT_POLICY, Interceptable);
  EXPECT_EQ(NEVERC_PHASE_LINK_IMAGE_VERIFY_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_LINK_SIDE_OUTPUTS_VERIFY_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_LINK_COMMIT_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_LINK_AFTER_COMMIT_POLICY,
            NEVERC_PHASE_OBSERVABLE);
  EXPECT_EQ(NEVERC_PHASE_LINK_COMMIT_GATE,
            NEVERC_PHASE_GATE_SEALED_COMMIT);

  const auto PhaseIDs = neverc::plugin::builtInLinkPhaseIDs();
  ASSERT_EQ(PhaseIDs.size(), NEVERC_BUILTIN_LINK_PHASE_COUNT);
  EXPECT_EQ(PhaseIDs.front().High, NEVERC_PHASE_LINK_INPUT_PROBE_HIGH);
  EXPECT_EQ(PhaseIDs.front().Low, NEVERC_PHASE_LINK_INPUT_PROBE_LOW);
  EXPECT_EQ(PhaseIDs.back().High, NEVERC_PHASE_LINK_AFTER_COMMIT_HIGH);
  EXPECT_EQ(PhaseIDs.back().Low, NEVERC_PHASE_LINK_AFTER_COMMIT_LOW);
}

} // namespace
