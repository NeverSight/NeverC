//===-- Regex.h - Regular Expression matcher implementation -*- C++ -*-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a POSIX regular expression matcher.  Both Basic and
// Extended POSIX regular expressions (ERE) are supported.  EREs were extended
// to support backreferences in matches.
// This implementation also supports matching strings with embedded NUL chars.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_REGEX_H
#define LLVM_SUPPORT_REGEX_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/SmallString.h"

#include <cstdlib>

#include "csupport/lregex.h"
#include "regex_impl.h"

namespace llvm {
class StringRef;
template <typename T> class SmallVectorImpl;

class Regex {
public:
  enum RegexFlags : unsigned {
    NoFlags = 0,
    /// Compile for matching that ignores upper/lower case distinctions.
    IgnoreCase = 1,
    /// Compile for newline-sensitive matching. With this flag '[^' bracket
    /// expressions and '.' never match newline. A ^ anchor matches the
    /// null string after any newline in the string in addition to its normal
    /// function, and the $ anchor matches the null string before any
    /// newline in the string in addition to its normal function.
    Newline = 2,
    /// By default, the POSIX extended regular expression (ERE) syntax is
    /// assumed. Pass this flag to turn on basic regular expressions (BRE)
    /// instead.
    BasicRegex = 4,

    LLVM_MARK_AS_BITMASK_ENUM(BasicRegex)
  };

  Regex();
  /// Compiles the given regular expression \p Regex.
  ///
  /// \param Regex - referenced string is no longer needed after this
  /// constructor does finish.  Only its compiled form is kept stored.
  Regex(StringRef Regex, RegexFlags Flags = NoFlags);
  Regex(StringRef Regex, unsigned Flags);
  Regex(const Regex &) = delete;
  Regex &operator=(Regex regex) {
    std::swap(preg, regex.preg);
    std::swap(error, regex.error);
    return *this;
  }
  Regex(Regex &&regex);
  ~Regex();

  /// isValid - returns the error encountered during regex compilation, if
  /// any.
  bool isValid(SmallVectorImpl<char> &Error) const;
  bool isValid() const { return !error; }

  /// getNumMatches - In a valid regex, return the number of parenthesized
  /// matches it contains.  The number filled in by match will include this
  /// many entries plus one for the whole regex (as element 0).
  unsigned getNumMatches() const;

  /// matches - Match the regex against a given \p String.
  ///
  /// \param Matches - If given, on a successful match this will be filled in
  /// with references to the matched group expressions (inside \p String),
  /// the first group is always the entire pattern.
  ///
  /// \param Error - If non-null, any errors in the matching will be recorded
  /// as a non-empty string. If there is no error, it will be an empty string.
  ///
  /// This returns true on a successful match.
  bool match(StringRef String, SmallVectorImpl<StringRef> *Matches = nullptr,
             SmallVectorImpl<char> *Error = nullptr) const;

  /// sub - Return the result of replacing the first match of the regex in
  /// \p String with the \p Repl string. Backreferences like "\0" and "\g<1>"
  /// in the replacement string are replaced with the appropriate match
  /// substring.
  ///
  /// Note that the replacement string has backslash escaping performed on
  /// it. Invalid backreferences are ignored (replaced by empty strings).
  ///
  /// \param Error If non-null, any errors in the substitution (invalid
  /// backreferences, trailing backslashes) will be recorded as a non-empty
  /// string. If there is no error, it will be an empty string.
  SmallString<256> sub(StringRef Repl, StringRef String,
                       SmallVectorImpl<char> *Error = nullptr) const;

  /// If this function returns true, ^Str$ is an extended regular
  /// expression that matches Str and only Str.
  static bool isLiteralERE(StringRef Str);

  /// Turn String into a regex by escaping its special characters.
  static SmallString<256> escape(StringRef String);

private:
  struct llvm_regex *preg;
  int error;
};
} // namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

namespace llvm {

inline Regex::Regex() : preg(0), error(REG_BADPAT) {}

inline Regex::Regex(StringRef regex, RegexFlags Flags) {
  unsigned flags = 0;
  preg = (llvm_regex *)calloc(1, sizeof(llvm_regex));
  preg->re_endp = regex.end();
  if (Flags & IgnoreCase)
    flags |= REG_ICASE;
  if (Flags & Newline)
    flags |= REG_NEWLINE;
  if (!(Flags & BasicRegex))
    flags |= REG_EXTENDED;
  error = llvm_regcomp(preg, regex.data(), flags | REG_PEND);
}

inline Regex::Regex(StringRef regex, unsigned Flags)
    : Regex(regex, (RegexFlags)(Flags)) {}

inline Regex::Regex(Regex &&regex) {
  preg = regex.preg;
  error = regex.error;
  regex.preg = 0;
  regex.error = REG_BADPAT;
}

inline Regex::~Regex() {
  if (preg) {
    llvm_regfree(preg);
    free(preg);
  }
}

namespace {

/// Utility to convert a regex error code into a human-readable string.
inline void RegexErrorToString(int error, struct llvm_regex *preg,
                               SmallVectorImpl<char> &Error) {
  size_t len = llvm_regerror(error, preg, 0, 0);
  Error.resize(len - 1);
  llvm_regerror(error, preg, Error.data(), len);
}

} // namespace

inline bool Regex::isValid(SmallVectorImpl<char> &Error) const {
  if (!error)
    return true;

  RegexErrorToString(error, preg, Error);
  return false;
}

/// getNumMatches - In a valid regex, return the number of parenthesized
/// matches it contains.
inline unsigned Regex::getNumMatches() const { return preg->re_nsub; }

inline bool Regex::match(StringRef String, SmallVectorImpl<StringRef> *Matches,
                         SmallVectorImpl<char> *Error) const {
  if (Error && !Error->empty())
    Error->clear();

  // Check if the regex itself didn't successfully compile.
  if (Error ? !isValid(*Error) : !isValid())
    return false;

  unsigned nmatch = Matches ? preg->re_nsub + 1 : 0;

  // Update null string to empty string.
  if (String.data() == 0)
    String = "";

  // pmatch needs to have at least one element.
  SmallVector<llvm_regmatch_t, 8> pm;
  pm.resize(nmatch > 0 ? nmatch : 1);
  pm[0].rm_so = 0;
  pm[0].rm_eo = String.size();

  int rc = llvm_regexec(preg, String.data(), nmatch, pm.data(), REG_STARTEND);

  // Failure to match is not an error, it's just a normal return value.
  // Any other error code is considered abnormal, and is logged in the Error.
  if (rc == REG_NOMATCH)
    return false;
  if (rc != 0) {
    if (Error)
      RegexErrorToString(error, preg, *Error);
    return false;
  }

  // There was a match.

  if (Matches) { // match position requested
    Matches->clear();

    for (unsigned i = 0; i != nmatch; ++i) {
      if (pm[i].rm_so == -1) {
        // this group didn't match
        Matches->push_back(StringRef());
        continue;
      }
      assert(pm[i].rm_eo >= pm[i].rm_so);
      Matches->push_back(
          StringRef(String.data() + pm[i].rm_so, pm[i].rm_eo - pm[i].rm_so));
    }
  }

  return true;
}

inline SmallString<256> Regex::sub(StringRef Repl, StringRef String,
                                   SmallVectorImpl<char> *Error) const {
  SmallVector<StringRef, 8> Matches;
  if (!match(String, &Matches, Error))
    return SmallString<256>(String);

  size_t starts[16], ends[16];
  size_t nmatches = Matches.size();
  if (nmatches > 16)
    nmatches = 16;
  for (size_t i = 0; i < nmatches; i++) {
    starts[i] = (size_t)(Matches[i].data() - String.data());
    ends[i] = starts[i] + Matches[i].size();
  }

  char err_buf[256] = {0};
  char buf[8192];
  size_t n = csupport_regex_sub(Repl.data(), Repl.size(), String.data(),
                                String.size(), starts, ends, nmatches, buf,
                                sizeof(buf), err_buf, sizeof(err_buf));
  if (err_buf[0] && Error && Error->empty()) {
    StringRef ErrRef(err_buf);
    Error->assign(ErrRef.begin(), ErrRef.end());
  }
  if (n < sizeof(buf))
    return SmallString<256>(StringRef(buf, n));
  SmallVector<char, 16384> big(n + 1);
  csupport_regex_sub(Repl.data(), Repl.size(), String.data(), String.size(),
                     starts, ends, nmatches, big.data(), big.size(), 0, 0);
  return SmallString<256>(StringRef(big.data(), n));
}

// These are the special characters matched in functions like "p_ere_exp".
static const char RegexMetachars[] = "()^$|*+?.[]\\{}";

inline bool Regex::isLiteralERE(StringRef Str) {
  // Check for regex metacharacters.  This list was derived from our regex
  // implementation in regcomp.c and double checked against the POSIX extended
  // regular expression specification.
  return Str.find_first_of(RegexMetachars) == StringRef::npos;
}

inline SmallString<256> Regex::escape(StringRef String) {
  char buf[4096];
  size_t n = 0;
  csupport_regex_escape(String.data(), String.size(), buf, sizeof(buf), &n);
  return SmallString<256>(StringRef(buf, n));
}

} // namespace llvm

#endif // LLVM_SUPPORT_REGEX_H
