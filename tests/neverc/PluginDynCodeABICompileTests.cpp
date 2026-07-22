extern "C" {
#include "neverc/Plugin/PluginDynCode.h"
}

#include "neverc/Plugin/NevercPluginAPI.h" // IWYU pragma: keep
#include "gtest/gtest.h"
#include <array>
#include <cstddef>
#include <type_traits>

namespace {

TEST(PluginDynCodeABICompileTest, PublishesFirstVersionPureCTables) {
  static_assert(std::is_same_v<NevercDynCodeExecutionLevel, uint32_t>);
  static_assert(std::is_same_v<NevercDynCodeImageState, uint32_t>);
  static_assert(std::is_same_v<NevercDynCodeImportKind, uint32_t>);
  static_assert(std::is_same_v<NevercDynCodeEntryPolicyKind, uint32_t>);
  static_assert(std::is_same_v<NevercDynCodeRelocationDisposition, uint32_t>);
  static_assert(
      std::is_same_v<NevercDynCodeExternalRefDisposition, uint32_t>);
  static_assert(std::is_standard_layout_v<NevercDynCodeAPI>);
  static_assert(std::is_standard_layout_v<NevercDynCodeRegistrarAPI>);
  static_assert(std::is_standard_layout_v<NevercDynCodePhaseAPI>);
  static_assert(std::is_standard_layout_v<NevercDynCodeRequestInfo>);
  static_assert(std::is_standard_layout_v<NevercDynCodeImageInfo>);
  static_assert(std::is_standard_layout_v<NevercDynCodeReportInfo>);
  static_assert(std::is_standard_layout_v<NevercDynCodeTargetDescriptor>);
  static_assert(offsetof(NevercDynCodeAPI, Header) == 0);
  static_assert(offsetof(NevercDynCodeRegistrarAPI, Header) == 0);
  static_assert(offsetof(NevercDynCodePhaseAPI, Header) == 0);
  static_assert(offsetof(NevercDynCodeRequestInfo, Header) == 0);
  static_assert(NEVERC_DYNCODE_API_MAJOR == 1);
  static_assert(NEVERC_DYNCODE_REGISTRAR_API_MAJOR == 1);
  static_assert(NEVERC_DYNCODE_PHASE_API_MAJOR == 1);
  EXPECT_EQ(NEVERC_DYNCODE_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_DYNCODE_REGISTRAR_INTERFACE_STABILITY,
            NEVERC_INTERFACE_STABLE);
  EXPECT_EQ(NEVERC_DYNCODE_PHASE_INTERFACE_STABILITY, NEVERC_INTERFACE_STABLE);
}

TEST(PluginDynCodeABICompileTest, UsesOpaqueDomainSpecificHandles) {
  static_assert(sizeof(NevercDynCodeRequestHandle) == sizeof(NevercHandle));
  static_assert(sizeof(NevercDynCodeTargetHandle) == sizeof(NevercHandle));
  static_assert(sizeof(NevercDynCodePlanHandle) == sizeof(NevercHandle));
  static_assert(sizeof(NevercDynCodeImageHandle) == sizeof(NevercHandle));
  static_assert(
      sizeof(NevercDynCodeImageBuilderHandle) == sizeof(NevercHandle));
  static_assert(sizeof(NevercDynCodeReportHandle) == sizeof(NevercHandle));

  constexpr std::array<NevercInterfaceID, 3> IDs = {{
      {NEVERC_INTERFACE_DYNCODE_HIGH, NEVERC_INTERFACE_DYNCODE_LOW},
      {NEVERC_INTERFACE_DYNCODE_REGISTRAR_HIGH,
       NEVERC_INTERFACE_DYNCODE_REGISTRAR_LOW},
      {NEVERC_INTERFACE_DYNCODE_PHASE_HIGH, NEVERC_INTERFACE_DYNCODE_PHASE_LOW},
  }};
  for (size_t I = 0; I != IDs.size(); ++I) {
    EXPECT_NE(IDs[I].High | IDs[I].Low, 0U);
    for (size_t J = I + 1; J != IDs.size(); ++J)
      EXPECT_TRUE(IDs[I].High != IDs[J].High || IDs[I].Low != IDs[J].Low);
  }
}

TEST(PluginDynCodeABICompileTest, PublishesFixedDynCodePhasePolicies) {
  static_assert(NEVERC_BUILTIN_DYNCODE_PHASE_COUNT == 34);
  constexpr NevercPhasePolicy Transition = NEVERC_PHASE_OBSERVABLE |
                                           NEVERC_PHASE_INTERCEPTABLE |
                                           NEVERC_PHASE_REPLACEABLE;
  constexpr NevercPhasePolicy Sealed =
      NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_SEALED_HOST_GATE;

  // 30 typed transitions.
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_REQUEST_FREEZE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_IR_PREPARE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_IR_INDIRECT_BR_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_IR_MEM_INTRIN_PRE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_IR_COMPILER_RT_FINAL_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_MIR_PREPARE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_OBJECT_IMPORT_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_EXTRACT_PLAN_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_EXTRACT_IMAGE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_BINARY_POST_EXTRACT_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_BINARY_CHARSET_ENCODE_POLICY, Transition);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_BINARY_PRE_VERIFY_POLICY, Transition);

  // 4 sealed host gates.
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_IR_FINAL_VERIFY_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_MIR_FINAL_VERIFY_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_VERIFY_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_COMMIT_POLICY, Sealed);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_IR_FINAL_VERIFY_GATE,
            NEVERC_PHASE_GATE_SEALED_VERIFIER);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_MIR_FINAL_VERIFY_GATE,
            NEVERC_PHASE_GATE_SEALED_VERIFIER);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_VERIFY_GATE,
            NEVERC_PHASE_GATE_SEALED_VERIFIER);
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_COMMIT_GATE,
            NEVERC_PHASE_GATE_SEALED_COMMIT);

  // Dyncode phases are staged as experimental until their host executors and
  // coverage entries land in the follow-up tasks.
  EXPECT_EQ(NEVERC_PHASE_DYNCODE_REQUEST_FREEZE_STABILITY,
            NEVERC_PHASE_STABILITY_EXPERIMENTAL);
  EXPECT_STREQ(NEVERC_PHASE_DYNCODE_REQUEST_FREEZE_DOMAIN, "dyncode");
  EXPECT_STREQ(NEVERC_EXTENSION_FAMILY_7_NAMESPACE,
               "neverc.dyncode.extension");
}

} // namespace
