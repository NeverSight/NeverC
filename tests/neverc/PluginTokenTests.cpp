#include "PluginFrontendTestSupport.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;
using namespace neverc;
using namespace neverc::plugin;
using namespace neverc::test;

namespace {

StringRef view(NevercStringView Value) {
  return StringRef(Value.Data ? Value.Data : "",
                   static_cast<size_t>(Value.Length));
}

std::string takeErrorMessage(Error Value) {
  return toString(std::move(Value)).str().str();
}

class PluginTokenTest : public PluginPrepTest {};

TEST_F(PluginTokenTest, PublishesTokenInfoAndContiguousStreamViews) {
  std::vector<Token> NativeTokens = lexAll();
  ASSERT_GE(NativeTokens.size(), 4U);
  auto Stream = prepBridge().createTokenStream(NativeTokens);
  ASSERT_TRUE(static_cast<bool>(Stream))
      << takeErrorMessage(Stream.takeError());

  const NevercPrepAPI &API = prepBridge().prepAPI();
  const size_t LiveBeforeView = task().handles().liveCount();
  NevercTokenViewList StreamView{};
  StreamView.Header.StructSize = sizeof(StreamView);
  ASSERT_EQ(API.GetTokenStreamView(API.Context, task().handle(), *Stream,
                                   &StreamView)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(task().handles().liveCount(), LiveBeforeView);
  ASSERT_EQ(StreamView.Count, NativeTokens.size());
  ASSERT_EQ(StreamView.Stride, sizeof(NevercTokenView));
  ASSERT_NE(StreamView.Data, nullptr);
  if (StreamView.Count > 1) {
    EXPECT_EQ(reinterpret_cast<const char *>(&StreamView.Data[1]) -
                  reinterpret_cast<const char *>(&StreamView.Data[0]),
              static_cast<ptrdiff_t>(StreamView.Stride));
  }

  size_t ValueIndex = NativeTokens.size();
  for (size_t Index = 0; Index != NativeTokens.size(); ++Index) {
    if (view(StreamView.Data[Index].Spelling) == "value") {
      ValueIndex = Index;
      break;
    }
  }
  ASSERT_LT(ValueIndex, NativeTokens.size());
  EXPECT_EQ(StreamView.Data[ValueIndex].Kind,
            NEVERC_TOKEN_IDENTIFIER);
  EXPECT_EQ(StreamView.Data[ValueIndex].Origin,
            NEVERC_TOKEN_ORIGIN_MACRO_ARGUMENT);

  NevercTokenHandle ValueToken{};
  ASSERT_EQ(API.GetTokenStreamToken(
                    API.Context, task().handle(), *Stream, ValueIndex,
                    &ValueToken)
                .Code,
            NEVERC_STATUS_OK);
  NevercTokenInfo ValueInfo{};
  ValueInfo.Header.StructSize = sizeof(ValueInfo);
  ASSERT_EQ(API.GetTokenInfo(API.Context, task().handle(), ValueToken,
                             &ValueInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(ValueInfo.Kind, NEVERC_TOKEN_IDENTIFIER);
  EXPECT_EQ(view(ValueInfo.Spelling), "value");
  EXPECT_FALSE(neverc_handle_is_null(ValueInfo.Location));
  EXPECT_FALSE(neverc_handle_is_null(ValueInfo.Range));
  EXPECT_FALSE(neverc_handle_is_null(ValueInfo.Identifier));

  NevercIdentifierInfo IdentifierInfo{};
  IdentifierInfo.Header.StructSize = sizeof(IdentifierInfo);
  ASSERT_EQ(API.GetIdentifierInfo(
                    API.Context, task().handle(), ValueInfo.Identifier,
                    &IdentifierInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(view(IdentifierInfo.Name), "value");
  EXPECT_EQ(IdentifierInfo.TokenKind, NEVERC_TOKEN_IDENTIFIER);

  NevercTokenHandle NextToken{};
  ASSERT_EQ(API.GetTokenStreamToken(
                    API.Context, task().handle(), *Stream, ValueIndex + 1,
                    &NextToken)
                .Code,
            NEVERC_STATUS_OK);
  std::array<NevercTokenHandle, 2> Handles = {ValueToken, NextToken};
  std::array<NevercTokenInfo, 2> Infos{};
  for (NevercTokenInfo &Info : Infos)
    Info.Header.StructSize = sizeof(Info);
  ASSERT_EQ(API.GetTokenInfoBatch(
                    API.Context, task().handle(), Handles.data(),
                    Handles.size(), Infos.data(), Infos.size())
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(view(Infos[0].Spelling), "value");
  EXPECT_FALSE(view(Infos[1].Spelling).empty());
}

TEST_F(PluginTokenTest, BuilderCreatesVerifiedIdentifierLiteralPunctuationAndEOF) {
  const NevercPrepAPI &API = prepBridge().prepAPI();
  SourceLocation Anchor =
      sourceManager().getLocForStartOfFile(sourceManager().getMainFileID());
  auto PublicAnchor = locations().createLocation(Anchor);
  ASSERT_TRUE(static_cast<bool>(PublicAnchor))
      << takeErrorMessage(PublicAnchor.takeError());

  NevercIdentifierHandle Identifier{};
  NevercStringView IdentifierName{"plugin_name", 11};
  ASSERT_EQ(API.GetOrCreateIdentifier(
                    API.Context, task().handle(), IdentifierName, &Identifier)
                .Code,
            NEVERC_STATUS_OK);

  NevercTokenBuilderHandle IdentifierBuilder{};
  ASSERT_EQ(API.CreateTokenBuilder(API.Context, task().handle(),
                                   &IdentifierBuilder)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetIdentifier(
                    API.Context, task().handle(), IdentifierBuilder,
                    Identifier)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetLocation(
                    API.Context, task().handle(), IdentifierBuilder,
                    *PublicAnchor)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetFlags(
                    API.Context, task().handle(), IdentifierBuilder,
                    NEVERC_TOKEN_FLAG_START_OF_LINE |
                        NEVERC_TOKEN_FLAG_LEADING_SPACE)
                .Code,
            NEVERC_STATUS_OK);
  NevercTokenHandle IdentifierToken{};
  ASSERT_EQ(API.TokenBuilderCommit(
                    API.Context, task().handle(), IdentifierBuilder,
                    &IdentifierToken)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.TokenBuilderSetKind(
                    API.Context, task().handle(), IdentifierBuilder,
                    NEVERC_TOKEN_PLUS)
                .Code,
            NEVERC_STATUS_INVALID_STATE);
  EXPECT_EQ(API.DestroyTokenBuilder(
                    API.Context, task().handle(), IdentifierBuilder)
                .Code,
            NEVERC_STATUS_OK);

  NevercTokenInfo IdentifierTokenInfo{};
  IdentifierTokenInfo.Header.StructSize = sizeof(IdentifierTokenInfo);
  ASSERT_EQ(API.GetTokenInfo(API.Context, task().handle(), IdentifierToken,
                             &IdentifierTokenInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(IdentifierTokenInfo.Kind, NEVERC_TOKEN_IDENTIFIER);
  EXPECT_EQ(view(IdentifierTokenInfo.Spelling), "plugin_name");
  EXPECT_EQ(IdentifierTokenInfo.Origin,
            NEVERC_TOKEN_ORIGIN_SYNTHESIZED);
  EXPECT_EQ(IdentifierTokenInfo.Flags &
                (NEVERC_TOKEN_FLAG_START_OF_LINE |
                 NEVERC_TOKEN_FLAG_LEADING_SPACE),
            NEVERC_TOKEN_FLAG_START_OF_LINE |
                NEVERC_TOKEN_FLAG_LEADING_SPACE);

  NevercTokenBuilderHandle LiteralBuilder{};
  ASSERT_EQ(API.CreateTokenBuilder(API.Context, task().handle(),
                                   &LiteralBuilder)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetLiteral(
                    API.Context, task().handle(), LiteralBuilder,
                    NEVERC_TOKEN_NUMERIC_CONSTANT, {"123", 3})
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetLocation(
                    API.Context, task().handle(), LiteralBuilder,
                    *PublicAnchor)
                .Code,
            NEVERC_STATUS_OK);
  NevercTokenHandle LiteralToken{};
  ASSERT_EQ(API.TokenBuilderCommit(
                    API.Context, task().handle(), LiteralBuilder,
                    &LiteralToken)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.DestroyTokenBuilder(
                    API.Context, task().handle(), LiteralBuilder)
                .Code,
            NEVERC_STATUS_OK);
  NevercTokenInfo LiteralInfo{};
  LiteralInfo.Header.StructSize = sizeof(LiteralInfo);
  ASSERT_EQ(API.GetTokenInfo(API.Context, task().handle(), LiteralToken,
                             &LiteralInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(LiteralInfo.Kind, NEVERC_TOKEN_NUMERIC_CONSTANT);
  EXPECT_EQ(view(LiteralInfo.Spelling), "123");

  NevercTokenBuilderHandle PunctuationBuilder{};
  ASSERT_EQ(API.CreateTokenBuilder(API.Context, task().handle(),
                                   &PunctuationBuilder)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetKind(
                    API.Context, task().handle(), PunctuationBuilder,
                    NEVERC_TOKEN_PLUS)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetLocation(
                    API.Context, task().handle(), PunctuationBuilder,
                    *PublicAnchor)
                .Code,
            NEVERC_STATUS_OK);
  NevercTokenHandle PunctuationToken{};
  ASSERT_EQ(API.TokenBuilderCommit(
                    API.Context, task().handle(), PunctuationBuilder,
                    &PunctuationToken)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.DestroyTokenBuilder(
                    API.Context, task().handle(), PunctuationBuilder)
                .Code,
            NEVERC_STATUS_OK);
  NevercTokenInfo PunctuationInfo{};
  PunctuationInfo.Header.StructSize = sizeof(PunctuationInfo);
  ASSERT_EQ(API.GetTokenInfo(API.Context, task().handle(), PunctuationToken,
                             &PunctuationInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(PunctuationInfo.Kind, NEVERC_TOKEN_PLUS);
  EXPECT_EQ(view(PunctuationInfo.Spelling), "+");

  NevercTokenBuilderHandle EOFBuilder{};
  ASSERT_EQ(API.CreateTokenBuilder(API.Context, task().handle(), &EOFBuilder)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetKind(API.Context, task().handle(), EOFBuilder,
                                    NEVERC_TOKEN_EOF)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetLocation(
                    API.Context, task().handle(), EOFBuilder, *PublicAnchor)
                .Code,
            NEVERC_STATUS_OK);
  NevercTokenHandle EOFToken{};
  ASSERT_EQ(API.TokenBuilderCommit(API.Context, task().handle(), EOFBuilder,
                                   &EOFToken)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.DestroyTokenBuilder(API.Context, task().handle(), EOFBuilder)
                .Code,
            NEVERC_STATUS_OK);
  NevercTokenInfo EOFInfo{};
  EOFInfo.Header.StructSize = sizeof(EOFInfo);
  ASSERT_EQ(API.GetTokenInfo(API.Context, task().handle(), EOFToken, &EOFInfo)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(EOFInfo.Kind, NEVERC_TOKEN_EOF);
  EXPECT_TRUE(view(EOFInfo.Spelling).empty());
}

TEST_F(PluginTokenTest, BuilderRejectsInvalidKindsSpellingsFlagsAndLocations) {
  const NevercPrepAPI &API = prepBridge().prepAPI();
  SourceLocation Anchor =
      sourceManager().getLocForStartOfFile(sourceManager().getMainFileID());
  auto PublicAnchor = locations().createLocation(Anchor);
  ASSERT_TRUE(static_cast<bool>(PublicAnchor))
      << takeErrorMessage(PublicAnchor.takeError());

  NevercTokenBuilderHandle Builder{};
  ASSERT_EQ(API.CreateTokenBuilder(API.Context, task().handle(), &Builder)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.TokenBuilderSetKind(
                    API.Context, task().handle(), Builder,
                    NEVERC_TOKEN_ANNOT_TYPENAME)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_EQ(API.TokenBuilderSetFlags(
                    API.Context, task().handle(), Builder,
                    NEVERC_TOKEN_FLAG_NEEDS_CLEANING)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
  ASSERT_EQ(API.TokenBuilderSetLiteral(
                    API.Context, task().handle(), Builder,
                    NEVERC_TOKEN_NUMERIC_CONSTANT, {"not_a_number", 12})
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetLocation(
                    API.Context, task().handle(), Builder, *PublicAnchor)
                .Code,
            NEVERC_STATUS_OK);
  NevercTokenHandle Token{};
  EXPECT_EQ(API.TokenBuilderCommit(API.Context, task().handle(), Builder,
                                   &Token)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);

  SourceManagerForFile OtherSource("other.c", "x\n");
  FrontendPluginBridge OtherLocations(task(), OtherSource.get());
  SourceLocation OtherAnchor = OtherSource.get().getLocForStartOfFile(
      OtherSource.get().getMainFileID());
  auto OtherPublicAnchor = OtherLocations.createLocation(OtherAnchor);
  ASSERT_TRUE(static_cast<bool>(OtherPublicAnchor))
      << takeErrorMessage(OtherPublicAnchor.takeError());
  EXPECT_EQ(API.TokenBuilderSetLocation(
                    API.Context, task().handle(), Builder,
                    *OtherPublicAnchor)
                .Code,
            NEVERC_STATUS_WRONG_SCOPE);
  EXPECT_EQ(API.DestroyTokenBuilder(API.Context, task().handle(), Builder)
                .Code,
            NEVERC_STATUS_OK);

  NevercTokenBuilderHandle FlagBuilder{};
  ASSERT_EQ(API.CreateTokenBuilder(API.Context, task().handle(),
                                   &FlagBuilder)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetKind(
                    API.Context, task().handle(), FlagBuilder,
                    NEVERC_TOKEN_PLUS)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetLocation(
                    API.Context, task().handle(), FlagBuilder,
                    *PublicAnchor)
                .Code,
            NEVERC_STATUS_OK);
  ASSERT_EQ(API.TokenBuilderSetFlags(
                    API.Context, task().handle(), FlagBuilder,
                    NEVERC_TOKEN_FLAG_DISABLE_EXPANSION)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(API.TokenBuilderCommit(
                    API.Context, task().handle(), FlagBuilder, &Token)
                .Code,
            NEVERC_STATUS_VERIFICATION_FAILED);
  EXPECT_EQ(API.DestroyTokenBuilder(
                    API.Context, task().handle(), FlagBuilder)
                .Code,
            NEVERC_STATUS_OK);
}

} // namespace
