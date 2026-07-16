#include "PluginFrontendTestSupport.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/StringRef.h"
#include <cstddef>

using namespace llvm;
using namespace neverc;
using namespace neverc::plugin;
using namespace neverc::test;

namespace {

StringRef view(NevercStringView Value) {
  return StringRef(Value.Data ? Value.Data : "",
                   static_cast<size_t>(Value.Length));
}

class PluginMacroTest : public PluginPrepTest {};

TEST_F(PluginMacroTest, ReadsIdentifierAndVariadicMacroDefinition) {
  (void)lexAll();
  const NevercPrepAPI &API = prepBridge().prepAPI();

  NevercIdentifierHandle Name{};
  ASSERT_EQ(API.GetOrCreateIdentifier(
                    API.Context, task().handle(), {"SUM", 3}, &Name)
                .Code,
            NEVERC_STATUS_OK);
  NevercIdentifierInfo NameInfo{};
  NameInfo.Header.StructSize = sizeof(NameInfo);
  ASSERT_EQ(API.GetIdentifierInfo(API.Context, task().handle(), Name,
                                  &NameInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(view(NameInfo.Name), "SUM");
  EXPECT_NE(NameInfo.Flags & NEVERC_IDENTIFIER_HAS_MACRO, 0U);

  NevercMacroDefinitionHandle Definition{};
  ASSERT_EQ(API.GetMacroDefinitionForIdentifier(
                    API.Context, task().handle(), Name, &Definition)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_FALSE(neverc_handle_is_null(Definition));

  NevercMacroDefinitionInfo DefinitionInfo{};
  DefinitionInfo.Header.StructSize = sizeof(DefinitionInfo);
  ASSERT_EQ(API.GetMacroDefinitionInfo(
                    API.Context, task().handle(), Definition,
                    &DefinitionInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_NE(DefinitionInfo.Flags & NEVERC_MACRO_FUNCTION_LIKE, 0U);
  EXPECT_NE(DefinitionInfo.Flags & NEVERC_MACRO_VARIADIC, 0U);
  EXPECT_NE(DefinitionInfo.Flags & NEVERC_MACRO_C99_VARIADIC, 0U);
  EXPECT_NE(DefinitionInfo.Flags & NEVERC_MACRO_HAS_VA_OPT, 0U);
  EXPECT_GE(DefinitionInfo.ParameterCount, 1U);
  EXPECT_GT(DefinitionInfo.ReplacementTokenCount, 0U);
  EXPECT_FALSE(neverc_handle_is_null(
      DefinitionInfo.DefinitionLocation));
  EXPECT_FALSE(neverc_handle_is_null(
      DefinitionInfo.DefinitionEndLocation));
  EXPECT_TRUE(neverc_handle_is_null(
      DefinitionInfo.UndefinitionLocation));

  NevercIdentifierHandle FirstParameter{};
  ASSERT_EQ(API.GetMacroParameter(
                    API.Context, task().handle(), Definition, 0,
                    &FirstParameter)
                .Code,
            NEVERC_STATUS_OK);
  NevercIdentifierInfo ParameterInfo{};
  ParameterInfo.Header.StructSize = sizeof(ParameterInfo);
  ASSERT_EQ(API.GetIdentifierInfo(
                    API.Context, task().handle(), FirstParameter,
                    &ParameterInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(view(ParameterInfo.Name), "x");
  NevercIdentifierHandle OutOfRangeParameter{};
  EXPECT_EQ(API.GetMacroParameter(
                    API.Context, task().handle(), Definition,
                    DefinitionInfo.ParameterCount, &OutOfRangeParameter)
                .Code,
            NEVERC_STATUS_INVALID_ARGUMENT);

  bool SawPlus = false;
  bool SawVAOpt = false;
  for (uint32_t Index = 0;
       Index != DefinitionInfo.ReplacementTokenCount; ++Index) {
    NevercTokenHandle Replacement{};
    ASSERT_EQ(API.GetMacroReplacementToken(
                      API.Context, task().handle(), Definition, Index,
                      &Replacement)
                  .Code,
              NEVERC_STATUS_OK);
    NevercTokenInfo TokenInfo{};
    TokenInfo.Header.StructSize = sizeof(TokenInfo);
    ASSERT_EQ(API.GetTokenInfo(API.Context, task().handle(), Replacement,
                               &TokenInfo)
                  .Code,
              NEVERC_STATUS_OK);
    SawPlus |= TokenInfo.Kind == NEVERC_TOKEN_PLUS;
    SawVAOpt |= view(TokenInfo.Spelling) == "__VA_OPT__";
  }
  EXPECT_TRUE(SawPlus);
  EXPECT_TRUE(SawVAOpt);

  NevercTokenHandle OutOfRangeToken{};
  EXPECT_EQ(API.GetMacroReplacementToken(
                    API.Context, task().handle(), Definition,
                    DefinitionInfo.ReplacementTokenCount, &OutOfRangeToken)
                .Code,
            NEVERC_STATUS_INVALID_ARGUMENT);
}

TEST_F(PluginMacroTest, ExposesMacroDirectiveChainAndValidatesIdentifiers) {
  (void)lexAll();
  const NevercPrepAPI &API = prepBridge().prepAPI();

  NevercIdentifierHandle InvalidIdentifier{};
  EXPECT_EQ(API.GetOrCreateIdentifier(
                    API.Context, task().handle(), {"bad-name", 8},
                    &InvalidIdentifier)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);

  NevercIdentifierHandle Name{};
  ASSERT_EQ(API.GetOrCreateIdentifier(
                    API.Context, task().handle(), {"SUM", 3}, &Name)
                .Code,
            NEVERC_STATUS_OK);
  NevercMacroDefinitionHandle Definition{};
  ASSERT_EQ(API.GetMacroDefinitionForIdentifier(
                    API.Context, task().handle(), Name, &Definition)
                .Code,
            NEVERC_STATUS_OK);
  NevercMacroDefinitionInfo DefinitionInfo{};
  DefinitionInfo.Header.StructSize = sizeof(DefinitionInfo);
  ASSERT_EQ(API.GetMacroDefinitionInfo(
                    API.Context, task().handle(), Definition,
                    &DefinitionInfo)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_FALSE(neverc_handle_is_null(DefinitionInfo.Directive));

  NevercMacroDirectiveInfo DirectiveInfo{};
  DirectiveInfo.Header.StructSize = sizeof(DirectiveInfo);
  ASSERT_EQ(API.GetMacroDirectiveInfo(
                    API.Context, task().handle(), DefinitionInfo.Directive,
                    &DirectiveInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(DirectiveInfo.Kind, NEVERC_MACRO_DIRECTIVE_DEFINE);
  EXPECT_FALSE(neverc_handle_is_null(DirectiveInfo.Location));
  EXPECT_TRUE(neverc_handle_is_null(DirectiveInfo.Previous));
  EXPECT_FALSE(neverc_handle_is_null(DirectiveInfo.Definition));
}

} // namespace
