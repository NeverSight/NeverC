#ifndef NEVERC_LIB_PLUGIN_FRONTEND_PREPBRIDGEINTERNAL_H
#define NEVERC_LIB_PLUGIN_FRONTEND_PREPBRIDGEINTERNAL_H

#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Plugin/Host/FrontendPluginBridge.h"
#include "neverc/Scan/MacroRecord.h"
#include "neverc/Scan/Token.h"
#include "llvm/ADT/StringRef.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace neverc::plugin::prep_bridge_detail {

struct TokenPayload {
  PrepEngine *Engine = nullptr;
  Token Value;
  std::string Spelling;
  NevercTokenOriginKind Origin = NEVERC_TOKEN_ORIGIN_FILE;
  NevercSourceLocation Location{};
  NevercSourceRange Range{};
  NevercIdentifierHandle Identifier{};
  NevercMacroDefinitionHandle MacroDefinition{};
};

struct TokenStreamPayload {
  PrepEngine *Engine = nullptr;
  std::vector<Token> Tokens;
  std::vector<char> SpellingStorage;
  std::vector<NevercTokenView> Views;
};

struct TokenPhaseArtifact {
  std::vector<Token> Tokens;
  bool Replacement = false;
};

struct IncludePhaseArtifact {
  SourceLocation Location;
  Token IncludeToken;
  std::string Filename;
  std::string SearchPath;
  std::string RelativePath;
  NevercPrepIncludeAction Action = NEVERC_PREP_INCLUDE_CONTINUE;
  bool IsAngled = false;
  bool IsImport = false;
  bool IsIncludeNext = false;
  bool ReplacementIsAngled = false;
  std::string ReplacementFilename;
};

struct MacroPhaseArtifact {
  NevercPrepMacroOperation Operation = NEVERC_PREP_MACRO_EXPAND;
  NevercPrepMacroAction Action = NEVERC_PREP_MACRO_CONTINUE;
  Token NameToken;
  IdentifierInfo *Name = nullptr;
  MacroRecord *Definition = nullptr;
  MacroArgStorage *Arguments = nullptr;
  std::vector<Token> ReplacementTokens;
};

struct PragmaPhaseArtifact {
  SourceLocation Location;
  NevercPrepPragmaIntroducer Introducer = NEVERC_PREP_PRAGMA_HASH;
  NevercPrepPragmaAction Action = NEVERC_PREP_PRAGMA_CONTINUE;
  std::string Namespace;
  std::string Name;
  std::vector<Token> Tokens;
  std::vector<Token> ReplacementTokens;
};

struct FeatureQueryPhaseArtifact {
  SourceLocation Location;
  NevercPrepFeatureQueryKind Kind = NEVERC_PREP_QUERY_HAS_FEATURE;
  NevercPrepFeatureQueryAction Action = NEVERC_PREP_QUERY_CONTINUE;
  std::string Name;
  bool BuiltinValue = false;
  bool Value = false;
};

struct IdentifierPayload {
  PrepEngine *Engine = nullptr;
  IdentifierInfo *Identifier = nullptr;
};

struct MacroDefinitionPayload {
  PrepEngine *Engine = nullptr;
  IdentifierInfo *Name = nullptr;
  MacroRecord *Record = nullptr;
  DefMacroDirective *Directive = nullptr;
  SourceLocation UndefinitionLocation;
};

struct MacroDirectivePayload {
  PrepEngine *Engine = nullptr;
  IdentifierInfo *Name = nullptr;
  MacroDirective *Directive = nullptr;
};

struct MacroArgumentPayload {
  PrepEngine *Engine = nullptr;
  std::vector<Token> Tokens;
  std::vector<uint64_t> Offsets;
  std::vector<uint64_t> Lengths;
  bool VarargsElided = false;
};

struct TokenBuilderPayload {
  PrepEngine *Engine = nullptr;
  SourceLocation Location;
  IdentifierInfo *Identifier = nullptr;
  NevercTokenKind Kind = NEVERC_TOKEN_UNKNOWN;
  NevercTokenFlags Flags = 0;
  std::string Spelling;
  bool HasKind = false;
  bool HasLocation = false;
  bool HasIdentifier = false;
  bool HasLiteral = false;
  bool Committed = false;
};

NevercStatus status(NevercStatusCode Code);
bool sameHandle(NevercHandle Left, NevercHandle Right);
NevercStringView stringView(llvm::StringRef Value);

template <typename T>
NevercStatus writeCallerBuffer(T *OutValue, const T &Value) {
  if (!OutValue)
    return status(NEVERC_STATUS_INVALID_ARGUMENT);
  const uint32_t Capacity = OutValue->Header.StructSize;
  if (Capacity < sizeof(NevercABITableHeader))
    return status(NEVERC_STATUS_ABI_MISMATCH);
  const size_t Writable = std::min<size_t>(Capacity, sizeof(Value));
  std::memset(OutValue, 0, Writable);
  std::memcpy(OutValue, &Value, Writable);
  if (Capacity < sizeof(Value))
    return status(NEVERC_STATUS_ABI_MISMATCH);
  return neverc_status_ok();
}

NevercTokenKind stableTokenKind(tok::TokenKind Kind);
bool nativeTokenKind(NevercTokenKind Stable, tok::TokenKind *OutKind);
NevercPPKeywordKind stablePPKeywordKind(tok::PPKeywordKind Kind);
NevercTokenCategory tokenCategory(NevercTokenKind Kind);
bool tokenKindConstructible(NevercTokenKind Kind);
NevercTokenFlags tokenFlags(const Token &Value);
NevercTokenOriginKind tokenOrigin(PrepEngine &Prep, const Token &Value);
std::string tokenSpelling(PrepEngine &Prep, const Token &Value);

} // namespace neverc::plugin::prep_bridge_detail

#endif
