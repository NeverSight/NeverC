#ifndef NEVERC_FOUNDATION_CHARINFO_H
#define NEVERC_FOUNDATION_CHARINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"

namespace neverc {
namespace charinfo {
extern const uint16_t InfoTable[256];

enum {
  CHAR_HORZ_WS = 0x0001, // '\t', '\f', '\v'.  Note, no '\0'
  CHAR_VERT_WS = 0x0002, // '\r', '\n'
  CHAR_SPACE = 0x0004,   // ' '
  CHAR_DIGIT = 0x0008,   // 0-9
  CHAR_XLETTER = 0x0010, // a-f,A-F
  CHAR_UPPER = 0x0020,   // A-Z
  CHAR_LOWER = 0x0040,   // a-z
  CHAR_UNDER = 0x0080,   // _
  CHAR_PERIOD = 0x0100,  // .
  CHAR_RAWDEL = 0x0200,  // {}[]#<>%:;?*+-/^&|~!=,"'
  CHAR_PUNCT = 0x0400    // `$@()
};

enum {
  CHAR_XUPPER = CHAR_XLETTER | CHAR_UPPER,
  CHAR_XLOWER = CHAR_XLETTER | CHAR_LOWER
};
} // end namespace charinfo

LLVM_READNONE inline bool isASCII(char c) {
  return static_cast<unsigned char>(c) <= 127;
}

LLVM_READNONE inline bool isASCII(unsigned char c) { return c <= 127; }

LLVM_READNONE inline bool isASCII(uint32_t c) { return c <= 127; }
LLVM_READNONE inline bool isASCII(int64_t c) { return 0 <= c && c <= 127; }

LLVM_READONLY inline bool isAsciiIdentifierStart(unsigned char c,
                                                 bool AllowDollar = false) {
  using namespace charinfo;
  if (InfoTable[c] & (CHAR_UPPER | CHAR_LOWER | CHAR_UNDER))
    return true;
  return AllowDollar && c == '$';
}

LLVM_READONLY inline bool isAsciiIdentifierContinue(unsigned char c,
                                                    bool AllowDollar = false) {
  using namespace charinfo;
  if (InfoTable[c] & (CHAR_UPPER | CHAR_LOWER | CHAR_DIGIT | CHAR_UNDER))
    return true;
  return AllowDollar && c == '$';
}

LLVM_READONLY inline bool isHorizontalWhitespace(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_HORZ_WS | CHAR_SPACE)) != 0;
}

LLVM_READONLY inline bool isVerticalWhitespace(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & CHAR_VERT_WS) != 0;
}

LLVM_READONLY inline bool isWhitespace(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_HORZ_WS | CHAR_VERT_WS | CHAR_SPACE)) != 0;
}

LLVM_READONLY inline bool isDigit(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & CHAR_DIGIT) != 0;
}

LLVM_READONLY inline bool isLowercase(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & CHAR_LOWER) != 0;
}

LLVM_READONLY inline bool isUppercase(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & CHAR_UPPER) != 0;
}

LLVM_READONLY inline bool isLetter(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_UPPER | CHAR_LOWER)) != 0;
}

LLVM_READONLY inline bool isAlphanumeric(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_DIGIT | CHAR_UPPER | CHAR_LOWER)) != 0;
}

LLVM_READONLY inline bool isHexDigit(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_DIGIT | CHAR_XLETTER)) != 0;
}

LLVM_READONLY inline bool isPunctuation(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] &
          (CHAR_UNDER | CHAR_PERIOD | CHAR_RAWDEL | CHAR_PUNCT)) != 0;
}

LLVM_READONLY inline bool isPrintable(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] &
          (CHAR_UPPER | CHAR_LOWER | CHAR_PERIOD | CHAR_PUNCT | CHAR_DIGIT |
           CHAR_UNDER | CHAR_RAWDEL | CHAR_SPACE)) != 0;
}

LLVM_READONLY inline bool isPreprocessingNumberBody(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_UPPER | CHAR_LOWER | CHAR_DIGIT | CHAR_UNDER |
                          CHAR_PERIOD)) != 0;
}

LLVM_READONLY inline bool isRawStringDelimBody(unsigned char c) {
  using namespace charinfo;
  return (InfoTable[c] & (CHAR_UPPER | CHAR_LOWER | CHAR_PERIOD | CHAR_DIGIT |
                          CHAR_UNDER | CHAR_RAWDEL)) != 0;
}

enum class EscapeChar {
  Single = 1,
  Double = 2,
  SingleAndDouble = static_cast<int>(Single) | static_cast<int>(Double),
};

template <EscapeChar Opt, class CharT>
LLVM_READONLY inline auto escapeCStyle(CharT Ch) -> llvm::StringRef {
  switch (Ch) {
  case '\\':
    return "\\\\";
  case '\'':
    if ((static_cast<int>(Opt) & static_cast<int>(EscapeChar::Single)) == 0)
      break;
    return "\\'";
  case '"':
    if ((static_cast<int>(Opt) & static_cast<int>(EscapeChar::Double)) == 0)
      break;
    return "\\\"";
  case '\a':
    return "\\a";
  case '\b':
    return "\\b";
  case '\f':
    return "\\f";
  case '\n':
    return "\\n";
  case '\r':
    return "\\r";
  case '\t':
    return "\\t";
  case '\v':
    return "\\v";
  }
  return {};
}

LLVM_READONLY inline char toLowercase(char c) {
  if (isUppercase(c))
    return c + 'a' - 'A';
  return c;
}

LLVM_READONLY inline char toUppercase(char c) {
  if (isLowercase(c))
    return c + 'A' - 'a';
  return c;
}

LLVM_READONLY inline bool isValidAsciiIdentifier(llvm::StringRef S,
                                                 bool AllowDollar = false) {
  if (S.empty() || !isAsciiIdentifierStart(S[0], AllowDollar))
    return false;

  for (llvm::StringRef::iterator I = S.begin(), E = S.end(); I != E; ++I)
    if (!isAsciiIdentifierContinue(*I, AllowDollar))
      return false;

  return true;
}

} // end namespace neverc

#endif
