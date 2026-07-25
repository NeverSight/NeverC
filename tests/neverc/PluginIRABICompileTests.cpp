#include "neverc/Plugin/PluginIR.h"
#include "neverc/Plugin/PluginMIR.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "gtest/gtest.h"
#include <array>
#include <cstddef>
#include <type_traits>

namespace {

TEST(PluginIRMIRABITest, PublishesIndependentFirstVersionTablePrefixes) {
  static_assert(std::is_same_v<NevercIRPassLevel, uint32_t>);
  static_assert(std::is_same_v<NevercIROptimizationLevel, uint32_t>);
  static_assert(std::is_standard_layout_v<NevercIRCoreAPI>);
  static_assert(std::is_standard_layout_v<NevercIRGenAPI>);
  static_assert(std::is_standard_layout_v<NevercIROptimizationAPI>);
  static_assert(std::is_standard_layout_v<NevercIRBuilderAPI>);
  static_assert(std::is_standard_layout_v<NevercIRAnalysisAPI>);
  static_assert(std::is_standard_layout_v<NevercIRPassAPI>);
  static_assert(std::is_standard_layout_v<NevercMIRAPI>);
  static_assert(offsetof(NevercIRCoreAPI, Header) == 0);
  static_assert(offsetof(NevercIRGenAPI, Header) == 0);
  static_assert(offsetof(NevercIROptimizationAPI, Header) == 0);
  static_assert(offsetof(NevercIRBuilderAPI, Header) == 0);
  static_assert(offsetof(NevercIRAnalysisAPI, Header) == 0);
  static_assert(offsetof(NevercIRPassAPI, Header) == 0);
  static_assert(offsetof(NevercMIRAPI, Header) == 0);

  static_assert(NEVERC_IR_CORE_API_MAJOR == 1);
  static_assert(NEVERC_IR_GEN_API_MAJOR == 1);
  static_assert(NEVERC_IR_OPTIMIZATION_API_MAJOR == 1);
  static_assert(NEVERC_IR_BUILDER_API_MAJOR == 1);
  static_assert(NEVERC_IR_ANALYSIS_API_MAJOR == 1);
  static_assert(NEVERC_IR_PASS_API_MAJOR == 1);
  static_assert(NEVERC_MIR_API_MAJOR == 1);
  EXPECT_EQ(NEVERC_IR_CORE_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_IR_GEN_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_IR_OPTIMIZATION_INTERFACE_STABILITY,
            NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_IR_BUILDER_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_IR_ANALYSIS_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_IR_PASS_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_MIR_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
}

TEST(PluginIRMIRABITest, AssignsUniqueNonzeroDomainInterfaceIDs) {
  constexpr std::array<NevercInterfaceID, 7> IDs = {{
      {NEVERC_INTERFACE_IR_CORE_HIGH, NEVERC_INTERFACE_IR_CORE_LOW},
      {NEVERC_INTERFACE_IR_GEN_HIGH, NEVERC_INTERFACE_IR_GEN_LOW},
      {NEVERC_INTERFACE_IR_OPTIMIZATION_HIGH,
       NEVERC_INTERFACE_IR_OPTIMIZATION_LOW},
      {NEVERC_INTERFACE_IR_BUILDER_HIGH, NEVERC_INTERFACE_IR_BUILDER_LOW},
      {NEVERC_INTERFACE_IR_ANALYSIS_HIGH,
       NEVERC_INTERFACE_IR_ANALYSIS_LOW},
      {NEVERC_INTERFACE_IR_PASS_HIGH, NEVERC_INTERFACE_IR_PASS_LOW},
      {NEVERC_INTERFACE_MIR_HIGH, NEVERC_INTERFACE_MIR_LOW},
  }};

  for (size_t I = 0; I != IDs.size(); ++I) {
    EXPECT_NE(IDs[I].High | IDs[I].Low, 0U);
    for (size_t J = I + 1; J != IDs.size(); ++J)
      EXPECT_TRUE(IDs[I].High != IDs[J].High ||
                  IDs[I].Low != IDs[J].Low);
  }
}

TEST(PluginIRMIRABITest, PublishesFirstVersionIRAndMIRPhasePolicies) {
  static_assert(NEVERC_BUILTIN_IR_PHASE_COUNT == 8);
  static_assert(NEVERC_BUILTIN_MIR_PHASE_COUNT == 10);
  static_assert(NEVERC_BUILTIN_PHASE_COUNT == 130);
  static_assert(NEVERC_EXTENSION_FAMILY_COUNT == 8);

  constexpr NevercPhasePolicy Replacement =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
      NEVERC_PHASE_REPLACEABLE;
  constexpr NevercPhasePolicy PassSlot =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE;
  constexpr NevercPhasePolicy Sealed =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_SEALED_HOST_GATE;

  EXPECT_EQ(NEVERC_PHASE_IR_GENERATE_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_IR_OPTIMIZE_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_IR_PASS_PRE_OPT_POLICY, PassSlot);
  EXPECT_EQ(NEVERC_PHASE_IR_PASS_PRE_CODEGEN_POLICY, PassSlot);
  EXPECT_EQ(NEVERC_PHASE_MIR_PASS_POST_ISEL_POLICY, PassSlot);
  EXPECT_EQ(NEVERC_PHASE_MIR_PASS_FINAL_POLICY, PassSlot);
  EXPECT_EQ(NEVERC_PHASE_IR_FINAL_VERIFY_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_MIR_FINAL_VERIFY_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_IR_FINAL_VERIFY_GATE,
            NEVERC_PHASE_GATE_SEALED_VERIFIER);
  EXPECT_EQ(NEVERC_PHASE_MIR_FINAL_VERIFY_GATE,
            NEVERC_PHASE_GATE_SEALED_VERIFIER);

  // The real executors and sealed verifier barriers are published, so these
  // phases report as stable rather than provisional.
  EXPECT_EQ(NEVERC_PHASE_IR_GENERATE_STABILITY,
            NEVERC_PHASE_STABILITY_STABLE);
  EXPECT_EQ(NEVERC_PHASE_MIR_PASS_FINAL_STABILITY,
            NEVERC_PHASE_STABILITY_STABLE);
}

} // namespace
