#include "PrepBridgeInternal.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/SmallString.h"
#include <cstring>
#include <limits>
#include <new>

using namespace llvm;

namespace neverc::plugin {
namespace prep_bridge_detail {

static_assert(static_cast<uint32_t>(tok::NUM_TOKENS) ==
              NEVERC_TOKEN_KIND_COUNT);
static_assert(static_cast<uint32_t>(tok::NUM_PP_KEYWORDS) ==
              NEVERC_PP_KEYWORD_COUNT);

NevercStatus status(NevercStatusCode Code) {
  NevercStatus Result = neverc_status_ok();
  Result.Code = Code;
  return Result;
}

bool sameHandle(NevercHandle Left, NevercHandle Right) {
  return Left.Owner == Right.Owner && Left.Value == Right.Value;
}

NevercStringView stringView(StringRef Value) {
  return {Value.data(), static_cast<uint64_t>(Value.size())};
}

NevercTokenKind stableTokenKind(tok::TokenKind Kind) {
  switch (Kind) {
#define NEVERC_PREP_SCHEMA_INTERNAL_TOKEN(                                    \
    Internal, Public, Stable, Category, Constructible, Name, Spelling,        \
    Keyword, PragmaAnnotation)                                                \
  case tok::Internal:                                                         \
    return Stable;
#include "neverc/Plugin/Schema/PluginPrepSchema.inc"
#undef NEVERC_PREP_SCHEMA_INTERNAL_TOKEN
  case tok::NUM_TOKENS:
    break;
  }
  return NEVERC_TOKEN_UNKNOWN;
}

bool nativeTokenKind(NevercTokenKind Stable, tok::TokenKind *OutKind) {
  if (!OutKind)
    return false;
  switch (Stable) {
#define NEVERC_PREP_SCHEMA_INTERNAL_TOKEN(                                    \
    Internal, Public, StableID, Category, Constructible, Name, Spelling,      \
    Keyword, PragmaAnnotation)                                                \
  case StableID:                                                              \
    *OutKind = tok::Internal;                                                  \
    return true;
#include "neverc/Plugin/Schema/PluginPrepSchema.inc"
#undef NEVERC_PREP_SCHEMA_INTERNAL_TOKEN
  default:
    return false;
  }
}

NevercPPKeywordKind stablePPKeywordKind(tok::PPKeywordKind Kind) {
  switch (Kind) {
#define NEVERC_PREP_SCHEMA_INTERNAL_PP_KEYWORD(Internal, Public, Stable, Name) \
  case tok::Internal:                                                         \
    return Stable;
#include "neverc/Plugin/Schema/PluginPrepSchema.inc"
#undef NEVERC_PREP_SCHEMA_INTERNAL_PP_KEYWORD
  case tok::NUM_PP_KEYWORDS:
    break;
  }
  return NEVERC_PP_KEYWORD_NOT_KEYWORD;
}

NevercTokenCategory tokenCategory(NevercTokenKind Kind) {
  switch (Kind) {
#define NEVERC_TOKEN_CATEGORY_CASE(Name, Stable, Category, Constructible)       \
  case Stable:                                                                 \
    return Category;
    NEVERC_FOR_EACH_TOKEN_KIND(NEVERC_TOKEN_CATEGORY_CASE)
#undef NEVERC_TOKEN_CATEGORY_CASE
  default:
    return NEVERC_TOKEN_CATEGORY_SPECIAL;
  }
}

bool tokenKindConstructible(NevercTokenKind Kind) {
  switch (Kind) {
#define NEVERC_TOKEN_CONSTRUCTIBLE_CASE(Name, Stable, Category, Constructible)  \
  case Stable:                                                                 \
    return Constructible == NEVERC_TRUE;
    NEVERC_FOR_EACH_TOKEN_KIND(NEVERC_TOKEN_CONSTRUCTIBLE_CASE)
#undef NEVERC_TOKEN_CONSTRUCTIBLE_CASE
  default:
    return false;
  }
}

NevercTokenFlags tokenFlags(const Token &Value) {
  NevercTokenFlags Flags = 0;
  if (Value.getFlag(Token::StartOfLine))
    Flags |= NEVERC_TOKEN_FLAG_START_OF_LINE;
  if (Value.getFlag(Token::LeadingSpace))
    Flags |= NEVERC_TOKEN_FLAG_LEADING_SPACE;
  if (Value.getFlag(Token::DisableExpand))
    Flags |= NEVERC_TOKEN_FLAG_DISABLE_EXPANSION;
  if (Value.getFlag(Token::NeedsCleaning))
    Flags |= NEVERC_TOKEN_FLAG_NEEDS_CLEANING;
  if (Value.getFlag(Token::LeadingEmptyMacro))
    Flags |= NEVERC_TOKEN_FLAG_LEADING_EMPTY_MACRO;
  if (Value.getFlag(Token::HasUCN))
    Flags |= NEVERC_TOKEN_FLAG_HAS_UCN;
  if (Value.getFlag(Token::IgnoredComma))
    Flags |= NEVERC_TOKEN_FLAG_IGNORED_COMMA;
  if (Value.getFlag(Token::StringifiedInMacro))
    Flags |= NEVERC_TOKEN_FLAG_STRINGIFIED_IN_MACRO;
  if (Value.getFlag(Token::CommaAfterElided))
    Flags |= NEVERC_TOKEN_FLAG_COMMA_AFTER_ELIDED;
  if (Value.getFlag(Token::IsReinjected))
    Flags |= NEVERC_TOKEN_FLAG_REINJECTED;
  return Flags;
}

NevercTokenOriginKind tokenOrigin(PrepEngine &Prep, const Token &Value) {
  if (Value.getFlag(Token::IsReinjected))
    return NEVERC_TOKEN_ORIGIN_SYNTHESIZED;
  SourceLocation Location = Value.getLocation();
  SourceManager &SourceMgr = Prep.getSourceManager();
  if (Location.isMacroID()) {
    if (SourceMgr.isMacroArgExpansion(Location))
      return NEVERC_TOKEN_ORIGIN_MACRO_ARGUMENT;
    return NEVERC_TOKEN_ORIGIN_MACRO_REPLACEMENT;
  }
  if (Location.isValid() &&
      SourceMgr.isWrittenInScratchSpace(SourceMgr.getSpellingLoc(Location)))
    return NEVERC_TOKEN_ORIGIN_SYNTHESIZED;
  return NEVERC_TOKEN_ORIGIN_FILE;
}

std::string tokenSpelling(PrepEngine &Prep, const Token &Value) {
  if (Value.is(tok::eof))
    return {};
  if (Value.isAnnotation())
    return tok::getTokenName(Value.getKind());
  bool Invalid = false;
  std::string Result = Prep.getSpelling(Value, &Invalid);
  if (Invalid)
    return tok::getTokenName(Value.getKind());
  return Result;
}

} // namespace prep_bridge_detail

using namespace prep_bridge_detail;

Expected<NevercTokenHandle>
PluginPrepBridge::createTokenWithOrigin(const Token &Value,
                                        NevercTokenOriginKind Origin) {
  if (Task.isEnded())
    return createStringError(inconvertibleErrorCode(),
                             "cannot publish a token after task end");

  auto *Payload = new (std::nothrow) TokenPayload();
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "unable to allocate plugin token payload");
  Payload->Engine = &Prep;
  Payload->Value = Value;
  Payload->Spelling = tokenSpelling(Prep, Value);
  Payload->Origin = Origin;

  SourceLocation NativeLocation = Value.getLocation();
  if (NativeLocation.isValid()) {
    auto PublicLocation = Locations.createLocation(NativeLocation);
    if (!PublicLocation) {
      delete Payload;
      return PublicLocation.takeError();
    }
    Payload->Location = *PublicLocation;

    CharSourceRange NativeRange;
    if (Value.isAnnotation()) {
      NativeRange = CharSourceRange::getTokenRange(Value.getAnnotationRange());
    } else {
      NativeRange = CharSourceRange::getCharRange(NativeLocation,
                                                   Value.getEndLoc());
    }
    auto PublicRange = Locations.createRange(NativeRange);
    if (!PublicRange) {
      delete Payload;
      return PublicRange.takeError();
    }
    Payload->Range = *PublicRange;
  }

  if (!Value.is(tok::raw_identifier) && !Value.isAnnotation() &&
      !Value.isLiteral() && !Value.is(tok::eof)) {
    if (IdentifierInfo *Identifier = Value.getIdentifierInfo()) {
      auto PublicIdentifier = createIdentifier(Identifier);
      if (!PublicIdentifier) {
        delete Payload;
        return PublicIdentifier.takeError();
      }
      Payload->Identifier = *PublicIdentifier;
    }
  }

  if (NativeLocation.isMacroID()) {
    StringRef MacroName = Prep.getImmediateMacroName(NativeLocation);
    if (!MacroName.empty()) {
      IdentifierInfo *Identifier = Prep.getIdentifierInfo(MacroName);
      if (MacroDirective *Directive =
              Prep.getLocalMacroDirectiveHistory(Identifier)) {
        if (isa<DefMacroDirective>(Directive)) {
          auto Definition = createMacroDefinition(Identifier, Directive);
          if (!Definition) {
            delete Payload;
            return Definition.takeError();
          }
          Payload->MacroDefinition = *Definition;
        }
      }
    }
  }

  auto Handle = Task.handles().create(
      PluginTokenHandleKind, Payload,
      [](void *Value) { delete static_cast<TokenPayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

Expected<NevercTokenHandle>
PluginPrepBridge::createToken(const Token &Value) {
  return createTokenWithOrigin(Value, tokenOrigin(Prep, Value));
}

Expected<NevercTokenStreamHandle>
PluginPrepBridge::createTokenStream(ArrayRef<Token> Tokens) {
  if (Task.isEnded())
    return createStringError(inconvertibleErrorCode(),
                             "cannot publish a token stream after task end");

  auto *Payload = new (std::nothrow) TokenStreamPayload();
  if (!Payload)
    return createStringError(inconvertibleErrorCode(),
                             "unable to allocate plugin token stream payload");
  Payload->Engine = &Prep;
  Payload->Tokens.assign(Tokens.begin(), Tokens.end());
  Payload->Views.resize(Tokens.size());

  struct SpellingRange {
    size_t Offset;
    size_t Length;
  };
  std::vector<SpellingRange> Ranges;
  Ranges.reserve(Tokens.size());
  SmallString<128> Scratch;
  for (const Token &Value : Tokens) {
    StringRef Spelling;
    if (Value.is(tok::eof)) {
      Spelling = {};
    } else if (Value.isAnnotation()) {
      Spelling = tok::getTokenName(Value.getKind());
    } else {
      Scratch.clear();
      bool Invalid = false;
      Spelling = Prep.getSpelling(Value, Scratch, &Invalid);
      if (Invalid)
        Spelling = tok::getTokenName(Value.getKind());
    }
    if (Spelling.size() >
        std::numeric_limits<size_t>::max() - Payload->SpellingStorage.size()) {
      delete Payload;
      return createStringError(inconvertibleErrorCode(),
                               "plugin token stream spelling overflow");
    }
    Ranges.push_back(
        {Payload->SpellingStorage.size(), static_cast<size_t>(Spelling.size())});
    Payload->SpellingStorage.insert(Payload->SpellingStorage.end(),
                                    Spelling.begin(), Spelling.end());
  }

  for (size_t Index = 0; Index != Tokens.size(); ++Index) {
    const Token &Value = Tokens[Index];
    NevercTokenView &View = Payload->Views[Index];
    View.Kind = stableTokenKind(Value.getKind());
    View.Flags = tokenFlags(Value);
    View.Origin = tokenOrigin(Prep, Value);
    const SpellingRange Range = Ranges[Index];
    const char *Data =
        Range.Length == 0
            ? nullptr
            : Payload->SpellingStorage.data() + Range.Offset;
    View.Spelling = {Data, static_cast<uint64_t>(Range.Length)};
  }

  auto Handle = Task.handles().create(
      PluginTokenStreamHandleKind, Payload,
      [](void *Value) { delete static_cast<TokenStreamPayload *>(Value); });
  if (!Handle) {
    delete Payload;
    return Handle.takeError();
  }
  return *Handle;
}

NevercStatus PluginPrepBridge::resolveToken(NevercTaskHandle TaskHandle,
                                             NevercTokenHandle TokenHandle,
                                             const Token **OutToken) {
  if (!OutToken)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutToken = nullptr;
  if (!sameHandle(TaskHandle, Task.handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Task.isEnded())
    return status(NEVERC_STATUS_INVALID_STATE);
  void *RawPayload = nullptr;
  NevercStatus Status =
      Task.handles().resolve(TokenHandle, PluginTokenHandleKind, &RawPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Payload = *static_cast<TokenPayload *>(RawPayload);
  if (Payload.Engine != &Prep)
    return status(NEVERC_STATUS_WRONG_SCOPE);
  *OutToken = &Payload.Value;
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::getTokenInfo(
    void *Context, NevercTaskHandle TaskHandle, NevercTokenHandle TokenHandle,
    NevercTokenInfo *OutInfo) {
  if (!Context || !OutInfo)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  const Token *NativeToken = nullptr;
  NevercStatus Status =
      Bridge.resolveToken(TaskHandle, TokenHandle, &NativeToken);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  void *RawPayload = nullptr;
  Status = Bridge.Task.handles().resolve(
      TokenHandle, PluginTokenHandleKind, &RawPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Payload = *static_cast<TokenPayload *>(RawPayload);

  NevercTokenInfo Info{};
  Info.Header = {sizeof(Info), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  Info.Kind = stableTokenKind(NativeToken->getKind());
  Info.Flags = tokenFlags(*NativeToken);
  Info.Origin = Payload.Origin;
  Info.Spelling = stringView(Payload.Spelling);
  Info.Location = Payload.Location;
  Info.Range = Payload.Range;
  Info.Identifier = Payload.Identifier;
  Info.MacroDefinition = Payload.MacroDefinition;
  return writeCallerBuffer(OutInfo, Info);
}

NevercStatus NEVERC_CALL PluginPrepBridge::getTokenInfoBatch(
    void *Context, NevercTaskHandle TaskHandle,
    const NevercTokenHandle *Tokens, uint64_t TokenCount,
    NevercTokenInfo *OutInfos, uint64_t OutInfoCapacity) {
  if (!Context || TokenCount > OutInfoCapacity ||
      TokenCount > std::numeric_limits<size_t>::max())
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  if (TokenCount == 0)
    return neverc_status_ok();
  if (!Tokens || !OutInfos)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  const size_t Count = static_cast<size_t>(TokenCount);
  for (size_t Index = 0; Index != Count; ++Index) {
    if (OutInfos[Index].Header.StructSize < sizeof(NevercABITableHeader))
      return status(NEVERC_STATUS_ABI_MISMATCH);
  }
  for (size_t Index = 0; Index != Count; ++Index) {
    NevercStatus Status = getTokenInfo(Context, TaskHandle, Tokens[Index],
                                       &OutInfos[Index]);
    if (Status.Code != NEVERC_STATUS_OK)
      return Status;
  }
  return neverc_status_ok();
}

NevercStatus NEVERC_CALL PluginPrepBridge::getTokenStreamView(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenStreamHandle Stream, NevercTokenViewList *OutView) {
  if (!Context || !OutView)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!sameHandle(TaskHandle, Bridge.Task.handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Bridge.Task.isEnded())
    return status(NEVERC_STATUS_INVALID_STATE);
  void *RawPayload = nullptr;
  NevercStatus Status = Bridge.Task.handles().resolve(
      Stream, PluginTokenStreamHandleKind, &RawPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Payload = *static_cast<TokenStreamPayload *>(RawPayload);
  if (Payload.Engine != &Bridge.Prep)
    return status(NEVERC_STATUS_WRONG_SCOPE);

  NevercTokenViewList View{};
  View.Header = {sizeof(View), NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, 0};
  View.Data = Payload.Views.data();
  View.Count = Payload.Views.size();
  View.Stride = sizeof(NevercTokenView);
  return writeCallerBuffer(OutView, View);
}

NevercStatus NEVERC_CALL PluginPrepBridge::getTokenStreamToken(
    void *Context, NevercTaskHandle TaskHandle,
    NevercTokenStreamHandle Stream, uint64_t Index,
    NevercTokenHandle *OutToken) {
  if (!Context || !OutToken)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutToken = {};
  auto &Bridge = *static_cast<PluginPrepBridge *>(Context);
  if (!sameHandle(TaskHandle, Bridge.Task.handle()))
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Bridge.Task.isEnded())
    return status(NEVERC_STATUS_INVALID_STATE);
  void *RawPayload = nullptr;
  NevercStatus Status = Bridge.Task.handles().resolve(
      Stream, PluginTokenStreamHandleKind, &RawPayload);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  auto &Payload = *static_cast<TokenStreamPayload *>(RawPayload);
  if (Payload.Engine != &Bridge.Prep)
    return status(NEVERC_STATUS_WRONG_SCOPE);
  if (Index >= Payload.Tokens.size())
    return status(NEVERC_STATUS_INVALID_ARGUMENT);

  auto TokenHandle =
      Bridge.createTokenWithOrigin(Payload.Tokens[static_cast<size_t>(Index)],
                                   Payload.Views[static_cast<size_t>(Index)]
                                       .Origin);
  if (!TokenHandle) {
    consumeError(TokenHandle.takeError());
    return status(NEVERC_STATUS_RESOURCE_EXHAUSTED);
  }
  *OutToken = *TokenHandle;
  return neverc_status_ok();
}

} // namespace neverc::plugin
