//===-- GlobPattern.h - glob pattern matcher implementation -*- C++ -*-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a glob pattern matcher.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GLOBPATTERN_H
#define LLVM_SUPPORT_GLOBPATTERN_H

#include "csupport/lglob_lpattern.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <optional>
#include <system_error>

namespace llvm {

/// This class implements a glob pattern matcher similar to the one found in
/// bash, but with some key differences. Namely, that \p "*" matches all
/// characters and does not exclude path separators.
///
/// * \p "?" matches a single character.
/// * \p "*" matches zero or more characters.
/// * \p "[<chars>]" matches one character in the bracket. Character ranges,
///   e.g., \p "[a-z]", and negative sets via \p "[^ab]" or \p "[!ab]" are also
///   supported.
/// * \p "{<glob>,...}" matches one of the globs in the list. Nested brace
///   expansions are not supported. If \p MaxSubPatterns is empty then
///   brace expansions are not supported and characters \p "{,}" are treated as
///   literals.
/// * \p "\" escapes the next character so it is treated as a literal.
///
///
/// Some known edge cases are:
/// * \p "]" is allowed as the first character in a character class, i.e.,
///   \p "[]]" is valid and matches the literal \p "]".
/// * The empty character class, i.e., \p "[]", is invalid.
/// * Empty or singleton brace expansions, e.g., \p "{}", \p "{a}", are invalid.
/// * \p "}" and \p "," that are not inside a brace expansion are taken as
///   literals, e.g., \p ",}" is valid but \p "{" is not.
///
///
/// For example, \p "*[/\\]foo.{c,cpp}" will match (unix or windows) paths to
/// all files named \p "foo.c" or \p "foo.cpp".
class GlobPattern {
public:
  /// \param Pat the pattern to match against
  /// \param MaxSubPatterns if provided limit the number of allowed subpatterns
  ///                       created from expanding braces otherwise disable
  ///                       brace expansion
  static Expected<GlobPattern> create(StringRef Pat,
                                      size_t MaxSubPatterns = SIZE_MAX);
  /// \returns \p true if \p S matches this glob pattern
  bool match(StringRef S) const;

  // Returns true for glob pattern "*". Can be used to avoid expensive
  // preparation/acquisition of the input for match().
  bool isTrivialMatchAll() const {
    if (!Prefix.empty())
      return false;
    if (SubGlobs.size() != 1)
      return false;
    return SubGlobs[0].getPat() == "*";
  }

private:
  StringRef Prefix;

  struct SubGlobPattern {
    /// \param Pat the pattern to match against
    static Expected<SubGlobPattern> create(StringRef Pat);
    /// \returns \p true if \p S matches this glob pattern
    bool match(StringRef S) const;
    StringRef getPat() const { return StringRef(Pat.data(), Pat.size()); }

    // Brackets with their end position and matched bytes.
    struct Bracket {
      size_t NextOffset;
      BitVector Bytes;
    };
    SmallVector<Bracket, 0> Brackets;
    SmallVector<char, 0> Pat;
  };
  SmallVector<SubGlobPattern, 1> SubGlobs;
};
} // namespace llvm

/*== Inline implementations (moved from cpp_bridge.cpp) ==*/

inline static llvm::Expected<llvm::BitVector> expand(llvm::StringRef S,
                                                     llvm::StringRef Original) {
  uint8_t bitmap[256];
  if (csupport_glob_expand_charset(S.data(), S.size(), bitmap) < 0)
    return llvm::make_error<llvm::StringError>(
        "invalid glob pattern: " + Original,
        std::make_error_code(std::errc::invalid_argument));
  llvm::BitVector BV(256, false);
  for (int i = 0; i < 256; ++i)
    if (bitmap[i])
      BV.set(i);
  return BV;
}

inline static llvm::Expected<llvm::SmallVector<llvm::SmallString<256>, 1>>
parseBraceExpansions(llvm::StringRef S, size_t MaxSubPatterns) {
  llvm::SmallVector<llvm::SmallString<256>, 4> SubPatterns;
  SubPatterns.emplace_back(S);
  if (MaxSubPatterns == SIZE_MAX || !S.contains('{'))
    return SubPatterns;

  csupport_brace_expansion_t exps[16];
  const char *errmsg = 0;
  int nexp = csupport_glob_parse_brace_expansions(S.data(), S.size(), exps, 16,
                                                  &errmsg);
  if (nexp < 0)
    return llvm::make_error<llvm::StringError>(
        errmsg, std::make_error_code(std::errc::invalid_argument));
  if (nexp == 0)
    return SubPatterns;

  size_t NumSubPatterns = csupport_glob_count_sub_patterns(exps, nexp);
  if (NumSubPatterns > MaxSubPatterns)
    return llvm::make_error<llvm::StringError>(
        "too many brace expansions",
        std::make_error_code(std::errc::invalid_argument));

  for (int i = nexp - 1; i >= 0; --i) {
    llvm::SmallVector<llvm::SmallString<256>, 4> OrigSubPatterns;
    SubPatterns.swap(OrigSubPatterns);
    for (size_t t = 0; t < exps[i].num_terms; ++t) {
      llvm::StringRef Term(S.data() + exps[i].term_offsets[t],
                           exps[i].term_lengths[t]);
      for (llvm::StringRef Orig : OrigSubPatterns) {
        llvm::SmallString<256> NewPat;
        NewPat.append(Orig.data(), Orig.data() + exps[i].start);
        NewPat.append(Term.begin(), Term.end());
        NewPat.append(Orig.data() + exps[i].start + exps[i].length,
                      Orig.data() + Orig.size());
        SubPatterns.push_back(NewPat);
      }
    }
  }
  return SubPatterns;
}

namespace llvm {

inline Expected<GlobPattern> GlobPattern::create(StringRef S,
                                                 size_t MaxSubPatterns) {
  GlobPattern Pat;

  size_t PrefixSize = S.find_first_of("?*[{\\");
  Pat.Prefix = S.substr(0, PrefixSize);
  if (PrefixSize == StringRef::npos)
    return Pat;
  S = S.substr(PrefixSize);

  SmallVector<SmallString<256>, 1> SubPats;
  if (auto Err = parseBraceExpansions(S, MaxSubPatterns).moveInto(SubPats))
    return Err;
  for (StringRef SubPat : SubPats) {
    auto SubGlobOrErr = SubGlobPattern::create(SubPat);
    if (!SubGlobOrErr)
      return SubGlobOrErr.takeError();
    Pat.SubGlobs.push_back(*SubGlobOrErr);
  }

  return Pat;
}

inline Expected<GlobPattern::SubGlobPattern>
GlobPattern::SubGlobPattern::create(StringRef S) {
  SubGlobPattern Pat;

  Pat.Pat.assign(S.begin(), S.end());
  for (size_t I = 0, E = S.size(); I != E; ++I) {
    if (S[I] == '[') {
      ++I;
      size_t J = S.find(']', I + 1);
      if (J == StringRef::npos)
        return make_error<StringError>(
            "invalid glob pattern, unmatched '['",
            std::make_error_code(std::errc::invalid_argument));
      StringRef Chars = S.substr(I, J - I);
      bool Invert = S[I] == '^' || S[I] == '!';
      Expected<BitVector> BV =
          Invert ? expand(Chars.substr(1), S) : expand(Chars, S);
      if (!BV)
        return BV.takeError();
      if (Invert)
        BV->flip();
      Pat.Brackets.push_back(Bracket{J + 1, *BV});
      I = J;
    } else if (S[I] == '\\') {
      if (++I == E)
        return make_error<StringError>(
            "invalid glob pattern, stray '\\'",
            std::make_error_code(std::errc::invalid_argument));
    }
  }
  return Pat;
}

inline bool GlobPattern::match(StringRef S) const {
  if (!S.consume_front(Prefix))
    return false;
  if (SubGlobs.empty() && S.empty())
    return true;
  for (auto &Glob : SubGlobs)
    if (Glob.match(S))
      return true;
  return false;
}

inline bool GlobPattern::SubGlobPattern::match(StringRef Str) const {
  const char *P = Pat.data(), *SegmentBegin = 0, *S = Str.data(), *SavedS = S;
  const char *const PEnd = P + Pat.size(), *const End = S + Str.size();
  size_t B = 0, SavedB = 0;
  while (S != End) {
    if (P == PEnd)
      ;
    else if (*P == '*') {
      SegmentBegin = ++P;
      SavedS = S;
      SavedB = B;
      continue;
    } else if (*P == '[') {
      if (Brackets[B].Bytes[uint8_t(*S)]) {
        P = Pat.data() + Brackets[B++].NextOffset;
        ++S;
        continue;
      }
    } else if (*P == '\\') {
      if (*++P == *S) {
        ++P;
        ++S;
        continue;
      }
    } else if (*P == *S || *P == '?') {
      ++P;
      ++S;
      continue;
    }
    if (!SegmentBegin)
      return false;
    P = SegmentBegin;
    S = ++SavedS;
    B = SavedB;
  }
  return getPat().find_first_not_of('*', P - Pat.data()) == StringRef::npos;
}

} // namespace llvm

#endif // LLVM_SUPPORT_GLOBPATTERN_H
