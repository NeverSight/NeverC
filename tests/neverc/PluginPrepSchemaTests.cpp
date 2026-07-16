#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Plugin/PluginPrep.h"
#include "gtest/gtest.h"
#include <array>
#include <set>
#include <string_view>

namespace {

struct TokenSchemaEntry {
  neverc::tok::TokenKind Internal;
  NevercTokenKind Stable;
  NevercTokenCategory Category;
  NevercBool Constructible;
  const char *InternalName;
  const char *Spelling;
  NevercBool HasSpelling;
  NevercBool PragmaAnnotation;
};

constexpr TokenSchemaEntry TokenSchema[] = {
#define NEVERC_PREP_SCHEMA_INTERNAL_TOKEN(                                  \
    Internal, Symbol, ID, Category, Constructible, InternalName, Spelling,  \
    HasSpelling, PragmaAnnotation)                                           \
  {neverc::tok::Internal, ID, Category, Constructible, InternalName,         \
   Spelling, HasSpelling, PragmaAnnotation},
#include "neverc/Plugin/Schema/PluginPrepSchema.inc"
#undef NEVERC_PREP_SCHEMA_INTERNAL_TOKEN
};

struct PPKeywordSchemaEntry {
  neverc::tok::PPKeywordKind Internal;
  NevercPPKeywordKind Stable;
  const char *Spelling;
};

constexpr PPKeywordSchemaEntry PPKeywordSchema[] = {
#define NEVERC_PREP_SCHEMA_INTERNAL_PP_KEYWORD(Internal, Symbol, ID,        \
                                               Spelling)                    \
  {neverc::tok::Internal, ID, Spelling},
#include "neverc/Plugin/Schema/PluginPrepSchema.inc"
#undef NEVERC_PREP_SCHEMA_INTERNAL_PP_KEYWORD
};

const TokenSchemaEntry *findToken(NevercTokenKind Kind) {
  for (const TokenSchemaEntry &Entry : TokenSchema)
    if (Entry.Stable == Kind)
      return &Entry;
  return nullptr;
}

TEST(PluginPrepSchemaTest, CoversEveryInternalKindWithIndependentStableIDs) {
  static_assert(std::size(TokenSchema) == neverc::tok::NUM_TOKENS);
  static_assert(std::size(PPKeywordSchema) ==
                neverc::tok::NUM_PP_KEYWORDS);
  static_assert(std::size(TokenSchema) == NEVERC_TOKEN_KIND_COUNT);
  static_assert(std::size(PPKeywordSchema) == NEVERC_PP_KEYWORD_COUNT);

  std::array<bool, neverc::tok::NUM_TOKENS> SeenTokens{};
  std::set<NevercTokenKind> StableTokens;
  for (const TokenSchemaEntry &Entry : TokenSchema) {
    const size_t Internal = static_cast<size_t>(Entry.Internal);
    ASSERT_LT(Internal, SeenTokens.size());
    EXPECT_FALSE(SeenTokens[Internal]) << Entry.InternalName;
    SeenTokens[Internal] = true;
    EXPECT_TRUE(StableTokens.insert(Entry.Stable).second)
        << Entry.InternalName;
    EXPECT_GE(Entry.Stable, UINT32_C(0x10000001));
    EXPECT_LT(Entry.Stable, UINT32_C(0x20000000));
  }
  for (bool Seen : SeenTokens)
    EXPECT_TRUE(Seen);

  std::array<bool, neverc::tok::NUM_PP_KEYWORDS> SeenKeywords{};
  std::set<NevercPPKeywordKind> StableKeywords;
  for (const PPKeywordSchemaEntry &Entry : PPKeywordSchema) {
    const size_t Internal = static_cast<size_t>(Entry.Internal);
    ASSERT_LT(Internal, SeenKeywords.size());
    EXPECT_FALSE(SeenKeywords[Internal]) << Entry.Spelling;
    SeenKeywords[Internal] = true;
    EXPECT_TRUE(StableKeywords.insert(Entry.Stable).second)
        << Entry.Spelling;
    EXPECT_GE(Entry.Stable, UINT32_C(0x20000001));
    EXPECT_LT(Entry.Stable, UINT32_C(0x30000000));
  }
  for (bool Seen : SeenKeywords)
    EXPECT_TRUE(Seen);

  EXPECT_NE(NEVERC_TOKEN_EOF,
            static_cast<NevercTokenKind>(neverc::tok::eof));
  EXPECT_NE(NEVERC_PP_KEYWORD_IF,
            static_cast<NevercPPKeywordKind>(neverc::tok::pp_if));
}

TEST(PluginPrepSchemaTest, PublishesCategoryAndConstructibilityMetadata) {
  const TokenSchemaEntry *Plus = findToken(NEVERC_TOKEN_PLUS);
  const TokenSchemaEntry *Keyword = findToken(NEVERC_TOKEN_KW_RETURN);
  const TokenSchemaEntry *Literal =
      findToken(NEVERC_TOKEN_NUMERIC_CONSTANT);
  const TokenSchemaEntry *Annotation =
      findToken(NEVERC_TOKEN_ANNOT_PRAGMA_PACK);
  const TokenSchemaEntry *Identifier =
      findToken(NEVERC_TOKEN_IDENTIFIER);
  const TokenSchemaEntry *RawIdentifier =
      findToken(NEVERC_TOKEN_RAW_IDENTIFIER);
  ASSERT_NE(Plus, nullptr);
  ASSERT_NE(Keyword, nullptr);
  ASSERT_NE(Literal, nullptr);
  ASSERT_NE(Annotation, nullptr);
  ASSERT_NE(Identifier, nullptr);
  ASSERT_NE(RawIdentifier, nullptr);

  EXPECT_EQ(Plus->Category, NEVERC_TOKEN_CATEGORY_PUNCTUATOR);
  EXPECT_EQ(std::string_view(Plus->Spelling), "+");
  EXPECT_EQ(Plus->Constructible, NEVERC_TRUE);
  EXPECT_EQ(Keyword->Category, NEVERC_TOKEN_CATEGORY_KEYWORD);
  EXPECT_EQ(std::string_view(Keyword->Spelling), "return");
  EXPECT_EQ(Keyword->Constructible, NEVERC_FALSE);
  EXPECT_EQ(Literal->Category, NEVERC_TOKEN_CATEGORY_LITERAL);
  EXPECT_EQ(Literal->Constructible, NEVERC_TRUE);
  EXPECT_EQ(Annotation->Category, NEVERC_TOKEN_CATEGORY_ANNOTATION);
  EXPECT_EQ(Annotation->PragmaAnnotation, NEVERC_TRUE);
  EXPECT_EQ(Annotation->Constructible, NEVERC_FALSE);
  EXPECT_EQ(Identifier->Constructible, NEVERC_TRUE);
  EXPECT_EQ(RawIdentifier->Constructible, NEVERC_FALSE);
  EXPECT_EQ(findToken(NEVERC_TOKEN_EOF)->Constructible, NEVERC_TRUE);
}

TEST(PluginPrepSchemaTest, KeepsFirstVersionGoldenDigest) {
  static_assert(NEVERC_PREP_SCHEMA_CAPABILITY_MAJOR ==
                NEVERC_PREP_API_MAJOR);
  EXPECT_STREQ(
      NEVERC_PREP_SCHEMA_DIGEST,
      "9e286b1b696ed55715a978ff5cfad5c0908995d5cc5de5025aadde0a739a3eeb");
}

} // namespace
