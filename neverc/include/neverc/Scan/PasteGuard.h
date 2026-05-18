#ifndef NEVERC_LEX_PASTEGUARD_H
#define NEVERC_LEX_PASTEGUARD_H

#include "neverc/Foundation/Core/TokenKinds.h"
#include <array>

namespace neverc {
class PrepEngine;
class Token;

namespace detail {

enum avoidConcatInfo : char {
  aci_never_avoid_concat = 0,
  aci_custom_firstchar = 1,
  aci_custom = 2,
  aci_avoid_equal = 4
};

inline constexpr std::array<char, tok::NUM_TOKENS> buildConcatTable() {
  std::array<char, tok::NUM_TOKENS> T{};
  T[tok::identifier] |= aci_custom;
  T[tok::numeric_constant] |= aci_custom;
  T[tok::period] |= aci_custom;
  T[tok::amp] |= aci_custom_firstchar;
  T[tok::plus] |= aci_custom_firstchar;
  T[tok::minus] |= aci_custom_firstchar;
  T[tok::slash] |= aci_custom_firstchar;
  T[tok::less] |= aci_custom_firstchar;
  T[tok::greater] |= aci_custom_firstchar;
  T[tok::pipe] |= aci_custom_firstchar;
  T[tok::percent] |= aci_custom_firstchar;
  T[tok::colon] |= aci_custom_firstchar;
  T[tok::hash] |= aci_custom_firstchar;

  T[tok::amp] |= aci_avoid_equal;
  T[tok::plus] |= aci_avoid_equal;
  T[tok::minus] |= aci_avoid_equal;
  T[tok::slash] |= aci_avoid_equal;
  T[tok::less] |= aci_avoid_equal;
  T[tok::greater] |= aci_avoid_equal;
  T[tok::pipe] |= aci_avoid_equal;
  T[tok::percent] |= aci_avoid_equal;
  T[tok::star] |= aci_avoid_equal;
  T[tok::exclaim] |= aci_avoid_equal;
  T[tok::lessless] |= aci_avoid_equal;
  T[tok::greatergreater] |= aci_avoid_equal;
  T[tok::caret] |= aci_avoid_equal;
  T[tok::equal] |= aci_avoid_equal;
  return T;
}

inline constexpr auto ConcatLookup = buildConcatTable();

struct DangerCharEntry {
  char C[4];
};

inline constexpr std::array<DangerCharEntry, tok::NUM_TOKENS>
buildDangerCharTable() {
  std::array<DangerCharEntry, tok::NUM_TOKENS> T{};
  T[tok::amp] = {{'&'}};
  T[tok::plus] = {{'+'}};
  T[tok::minus] = {{'-', '>'}};
  T[tok::slash] = {{'*', '/'}};
  T[tok::less] = {{'<', ':', '%'}};
  T[tok::greater] = {{'>'}};
  T[tok::pipe] = {{'|'}};
  T[tok::percent] = {{'>', ':'}};
  T[tok::colon] = {{'>'}};
  T[tok::hash] = {{'#', '@', '%'}};
  return T;
}

inline constexpr auto DangerCharLookup = buildDangerCharTable();

} // namespace detail

class PasteGuard {
  const PrepEngine &PP;

public:
  explicit PasteGuard(const PrepEngine &PP) : PP(PP) {}

  bool avoidConcat(const Token &PrevPrevTok, const Token &PrevTok,
                   const Token &Tok) const;

private:
  bool isIdentifierStringPrefix(const Token &Tok) const;
};
} // namespace neverc

#endif
