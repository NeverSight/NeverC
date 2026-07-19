#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "neverc/Plugin/PluginTarget.h"
#include "gtest/gtest.h"
#include <array>
#include <cstddef>
#include <type_traits>

namespace {

TEST(PluginTargetMCABITest, PublishesIndependentFirstVersionPrefixes) {
  static_assert(std::is_standard_layout_v<NevercTargetAPI>);
  static_assert(std::is_standard_layout_v<NevercTargetABIAPI>);
  static_assert(std::is_standard_layout_v<NevercCallingConventionAPI>);
  static_assert(std::is_standard_layout_v<NevercMCAPI>);
  static_assert(std::is_standard_layout_v<NevercObjectAPI>);
  static_assert(std::is_standard_layout_v<NevercObjectFormatAPI>);
  static_assert(offsetof(NevercTargetAPI, Header) == 0);
  static_assert(offsetof(NevercTargetABIAPI, Header) == 0);
  static_assert(offsetof(NevercCallingConventionAPI, Header) == 0);
  static_assert(offsetof(NevercMCAPI, Header) == 0);
  static_assert(offsetof(NevercObjectAPI, Header) == 0);
  static_assert(offsetof(NevercObjectFormatAPI, Header) == 0);

  static_assert(NEVERC_TARGET_API_MAJOR == 1);
  static_assert(NEVERC_TARGET_ABI_API_MAJOR == 1);
  static_assert(NEVERC_CALLING_CONVENTION_API_MAJOR == 1);
  static_assert(NEVERC_MC_API_MAJOR == 1);
  static_assert(NEVERC_OBJECT_API_MAJOR == 1);
  static_assert(NEVERC_OBJECT_FORMAT_API_MAJOR == 1);
  EXPECT_EQ(NEVERC_TARGET_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_TARGET_ABI_INTERFACE_STABILITY,
            NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_CALLING_CONVENTION_INTERFACE_STABILITY,
            NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_MC_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_OBJECT_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_OBJECT_FORMAT_INTERFACE_STABILITY,
            NEVERC_INTERFACE_STABLE);
}

TEST(PluginTargetMCABITest, AssignsUniqueNonzeroDomainInterfaceIDs) {
  constexpr std::array<NevercInterfaceID, 6> IDs = {{
      {NEVERC_INTERFACE_TARGET_HIGH, NEVERC_INTERFACE_TARGET_LOW},
      {NEVERC_INTERFACE_TARGET_ABI_HIGH, NEVERC_INTERFACE_TARGET_ABI_LOW},
      {NEVERC_INTERFACE_CALLING_CONVENTION_HIGH,
       NEVERC_INTERFACE_CALLING_CONVENTION_LOW},
      {NEVERC_INTERFACE_MC_HIGH, NEVERC_INTERFACE_MC_LOW},
      {NEVERC_INTERFACE_OBJECT_HIGH, NEVERC_INTERFACE_OBJECT_LOW},
      {NEVERC_INTERFACE_OBJECT_FORMAT_HIGH,
       NEVERC_INTERFACE_OBJECT_FORMAT_LOW},
  }};

  for (size_t I = 0; I != IDs.size(); ++I) {
    EXPECT_NE(IDs[I].High | IDs[I].Low, 0U);
    for (size_t J = I + 1; J != IDs.size(); ++J)
      EXPECT_TRUE(IDs[I].High != IDs[J].High ||
                  IDs[I].Low != IDs[J].Low);
  }
}

TEST(PluginTargetMCABITest, PublishesInitialTargetMCObjectPhasePolicies) {
  static_assert(NEVERC_BUILTIN_CODEGEN_PHASE_COUNT == 4);
  static_assert(NEVERC_BUILTIN_MC_PHASE_COUNT == 13);
  static_assert(NEVERC_BUILTIN_ASSEMBLY_PHASE_COUNT == 4);
  static_assert(NEVERC_BUILTIN_OBJECT_PHASE_COUNT == 8);

  constexpr NevercPhasePolicy Replacement =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE |
      NEVERC_PHASE_REPLACEABLE;
  constexpr NevercPhasePolicy Mutation =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE;
  constexpr NevercPhasePolicy Sealed =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_SEALED_HOST_GATE;

  EXPECT_EQ(NEVERC_PHASE_CODEGEN_IR_TO_MIR_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_CODEGEN_MIR_TO_MC_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_CODEGEN_COARSE_LOWER_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_MC_ENCODE_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_MC_DECODE_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_MC_LAYOUT_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_ASSEMBLY_PARSE_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_ASSEMBLY_PRINT_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_OBJECT_PROBE_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_OBJECT_READ_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_OBJECT_WRITE_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_MC_EMISSION_PRE_INSTRUCTION_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_OBJECT_PRE_WRITE_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_OBJECT_POST_LAYOUT_POLICY, Replacement);
  EXPECT_EQ(NEVERC_PHASE_OBJECT_POST_WRITE_POLICY, Mutation);
  EXPECT_EQ(NEVERC_PHASE_MC_EMISSION_UNIT_BEGIN_POLICY,
            NEVERC_PHASE_OBSERVABLE);
  EXPECT_EQ(NEVERC_PHASE_MC_EMISSION_POST_LAYOUT_POLICY,
            NEVERC_PHASE_OBSERVABLE);
  EXPECT_EQ(NEVERC_PHASE_CODEGEN_PRODUCT_VERIFY_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_ASSEMBLY_FINAL_VERIFY_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_ASSEMBLY_COMMIT_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_OBJECT_FINAL_VERIFY_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_OBJECT_COMMIT_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_ASSEMBLY_COMMIT_GATE,
            NEVERC_PHASE_GATE_SEALED_COMMIT);
  EXPECT_EQ(NEVERC_PHASE_OBJECT_COMMIT_GATE,
            NEVERC_PHASE_GATE_SEALED_COMMIT);
}

} // namespace
