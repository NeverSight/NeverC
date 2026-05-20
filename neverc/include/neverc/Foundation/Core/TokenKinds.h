#ifndef NEVERC_FOUNDATION_TOKENKINDS_H
#define NEVERC_FOUNDATION_TOKENKINDS_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/Compiler.h"

namespace neverc {

namespace tok {

enum TokenKind : unsigned short {
#define TOK(X) X,
#include "neverc/Foundation/Core/TokenKinds.def"
  NUM_TOKENS
};

enum PPKeywordKind {
#define PPKEYWORD(X) pp_##X,
#include "neverc/Foundation/Core/TokenKinds.def"
  NUM_PP_KEYWORDS
};

enum InterestingIdentifierKind {
#define INTERESTING_IDENTIFIER(X) X,
#include "neverc/Foundation/Core/TokenKinds.def"
  NUM_INTERESTING_IDENTIFIERS
};

enum OnOffSwitch { OOS_ON, OOS_OFF, OOS_DEFAULT };

const char *getTokenName(TokenKind Kind) LLVM_READNONE;

const char *getPunctuatorSpelling(TokenKind Kind) LLVM_READNONE;

const char *getKeywordSpelling(TokenKind Kind) LLVM_READNONE;

const char *getPPKeywordSpelling(PPKeywordKind Kind) LLVM_READNONE;

inline bool isAnyIdentifier(TokenKind K) {
  return (K == tok::identifier) || (K == tok::raw_identifier);
}

inline bool isStringLiteral(TokenKind K) {
  return K == tok::string_literal || K == tok::wide_string_literal ||
         K == tok::utf8_string_literal || K == tok::utf16_string_literal ||
         K == tok::utf32_string_literal;
}

inline bool isLiteral(TokenKind K) {
  return K == tok::numeric_constant || K == tok::char_constant ||
         K == tok::wide_char_constant || K == tok::utf8_char_constant ||
         K == tok::utf16_char_constant || K == tok::utf32_char_constant ||
         isStringLiteral(K) || K == tok::header_name;
}

bool isAnnotation(TokenKind K);

bool isPragmaAnnotation(TokenKind K);

inline constexpr bool isRegularKeywordAttribute(TokenKind K) {
  return (false
#define KEYWORD_ATTRIBUTE(X) || (K == tok::kw_##X)
#include "neverc/Foundation/AttrTokenKinds.td.h"
  );
}

} // end namespace tok
} // end namespace neverc

namespace llvm {
template <> struct DenseMapInfo<neverc::tok::PPKeywordKind> {
  static inline neverc::tok::PPKeywordKind getEmptyKey() {
    return neverc::tok::PPKeywordKind::pp_not_keyword;
  }
  static inline neverc::tok::PPKeywordKind getTombstoneKey() {
    return neverc::tok::PPKeywordKind::NUM_PP_KEYWORDS;
  }
  static unsigned getHashValue(const neverc::tok::PPKeywordKind &Val) {
    return static_cast<unsigned>(Val);
  }
  static bool isEqual(const neverc::tok::PPKeywordKind &LHS,
                      const neverc::tok::PPKeywordKind &RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

#endif
