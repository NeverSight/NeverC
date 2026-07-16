#include "PluginFrontendTestSupport.h"
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "neverc/Plugin/PluginPrep.h"
#include "neverc/Plugin/PluginSema.h"
#include "neverc/Plugin/PluginSource.h"
#include "gtest/gtest.h"
#include <cstddef>
#include <type_traits>

namespace {

TEST(PluginFrontendABITest, PublishesIndependentStableInterfacePrefixes) {
  static_assert(std::is_standard_layout_v<NevercIOAPI>);
  static_assert(std::is_standard_layout_v<NevercSourceLocationAPI>);
  static_assert(std::is_standard_layout_v<NevercPrepAPI>);
  static_assert(std::is_standard_layout_v<NevercASTAPI>);
  static_assert(std::is_standard_layout_v<NevercSemaAPI>);
  static_assert(offsetof(NevercIOAPI, Header) == 0);
  static_assert(offsetof(NevercSourceLocationAPI, Header) == 0);
  static_assert(offsetof(NevercPrepAPI, Header) == 0);
  static_assert(offsetof(NevercASTAPI, Header) == 0);
  static_assert(offsetof(NevercSemaAPI, Header) == 0);

  const auto IDs = neverc::test::frontendInterfaceIDs();
  for (size_t I = 0; I != IDs.size(); ++I) {
    EXPECT_NE(IDs[I].High | IDs[I].Low, 0U);
    for (size_t J = I + 1; J != IDs.size(); ++J)
      EXPECT_TRUE(IDs[I].High != IDs[J].High || IDs[I].Low != IDs[J].Low);
  }
}

TEST(PluginFrontendABITest, PublishesFixedFrontendPhasePolicies) {
  static_assert(NEVERC_BUILTIN_PHASE_COUNT == 17);
  static_assert(NEVERC_EXTENSION_FAMILY_COUNT == 4);

  EXPECT_EQ(NEVERC_PHASE_SOURCE_RESOLVE_INPUT_POLICY,
            NEVERC_PHASE_OBSERVABLE | NEVERC_PHASE_INTERCEPTABLE);
  EXPECT_EQ(NEVERC_PHASE_SOURCE_OPEN_POLICY, NEVERC_PHASE_OBSERVABLE |
                                                 NEVERC_PHASE_INTERCEPTABLE |
                                                 NEVERC_PHASE_REPLACEABLE);
  EXPECT_EQ(NEVERC_PHASE_SOURCE_AFTER_OPEN_POLICY, NEVERC_PHASE_OBSERVABLE);
  EXPECT_EQ(NEVERC_PHASE_PREP_TOKEN_POLICY, NEVERC_PHASE_OBSERVABLE |
                                                NEVERC_PHASE_INTERCEPTABLE |
                                                NEVERC_PHASE_REPLACEABLE);
  EXPECT_EQ(NEVERC_PHASE_PREP_BUILD_TOKEN_STREAM_POLICY,
            NEVERC_PHASE_PREP_TOKEN_POLICY);
  EXPECT_EQ(NEVERC_PHASE_PREP_INCLUDE_POLICY, NEVERC_PHASE_PREP_TOKEN_POLICY);
  EXPECT_EQ(NEVERC_PHASE_PREP_MACRO_POLICY, NEVERC_PHASE_PREP_TOKEN_POLICY);
  EXPECT_EQ(NEVERC_PHASE_PREP_PRAGMA_POLICY, NEVERC_PHASE_PREP_TOKEN_POLICY);
  EXPECT_EQ(NEVERC_PHASE_PREP_FEATURE_QUERY_POLICY,
            NEVERC_PHASE_PREP_TOKEN_POLICY);
  EXPECT_EQ(NEVERC_PHASE_SYNTAX_PARSE_POLICY, NEVERC_PHASE_PREP_TOKEN_POLICY);
  EXPECT_EQ(NEVERC_PHASE_SEMA_ANALYZE_POLICY, NEVERC_PHASE_PREP_TOKEN_POLICY);

  EXPECT_EQ(NEVERC_PHASE_SOURCE_AFTER_OPEN_KIND, NEVERC_PHASE_KIND_EVENT);
  EXPECT_EQ(NEVERC_PHASE_SOURCE_OPEN_KIND, NEVERC_PHASE_KIND_TRANSITION);
  EXPECT_EQ(NEVERC_PHASE_SOURCE_AFTER_OPEN_BUILTIN_FALLBACK, NEVERC_FALSE);
  EXPECT_EQ(NEVERC_PHASE_SOURCE_OPEN_BUILTIN_FALLBACK, NEVERC_TRUE);
}

} // namespace
