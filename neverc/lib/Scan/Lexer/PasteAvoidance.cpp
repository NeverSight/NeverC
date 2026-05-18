#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Scan/PasteGuard.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstring>
using namespace neverc;

namespace {

LLVM_ATTRIBUTE_ALWAYS_INLINE
bool isEncodingPrefix(const char *Ptr, unsigned Len) {
  if (LLVM_UNLIKELY(Len == 0 || Len > 3))
    return false;
  switch (Ptr[0]) {
  case 'L':
    return Len == 1;
  case 'u':
    return (Len >= 2 && Ptr[1] == '8') &&
           (Len == 2 || (Len == 3 && Ptr[2] == 'R'));
  default:
    return false;
  }
}
} // namespace

bool PasteGuard::isIdentifierStringPrefix(const Token &Tok) const {
  const unsigned TokLen = Tok.getLength();
  if (LLVM_UNLIKELY(TokLen < 1 || TokLen > 3))
    return false;

  if (LLVM_LIKELY(!Tok.needsCleaning())) {
    SourceManager &SM = PP.getSourceManager();
    const char *Ptr = SM.getCharacterData(SM.getSpellingLoc(Tok.getLocation()));
    return isEncodingPrefix(Ptr, TokLen);
  }

  char Buffer[4];
  const char *TokPtr = Buffer;
  unsigned Len = PP.getSpelling(Tok, TokPtr);
  return isEncodingPrefix(TokPtr, Len);
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
char inspectFirstChar(const PrepEngine &PP, const Token &Tok) {
  if (const IdentifierInfo *II = Tok.getIdentifierInfo())
    return II->getNameStart()[0];

  if (Tok.isLiteral()) {
    const char *LD = Tok.getLiteralData();
    if (LLVM_LIKELY(LD != nullptr))
      return *LD;
  }

  if (LLVM_LIKELY(!Tok.needsCleaning())) {
    SourceManager &SM = PP.getSourceManager();
    return *SM.getCharacterData(SM.getSpellingLoc(Tok.getLocation()));
  }

  char Buffer[4];
  const char *TokPtr = Buffer;
  PP.getSpelling(Tok, TokPtr);
  return TokPtr[0];
}
} // namespace

__attribute__((hot)) bool PasteGuard::avoidConcat(const Token &PrevPrevTok,
                                                  const Token &PrevTok,
                                                  const Token &Tok) const {
  if (LLVM_UNLIKELY(PrevTok.isAnnotation()))
    return true;

  SourceManager &SM = PP.getSourceManager();
  SourceLocation PrevSpellLoc = SM.getSpellingLoc(PrevTok.getLocation());
  SourceLocation SpellLoc = SM.getSpellingLoc(Tok.getLocation());
  if (PrevSpellLoc.getLocWithOffset(PrevTok.getLength()) == SpellLoc)
    return false;

  tok::TokenKind PrevKind = PrevTok.getKind();
  if (PrevTok.getIdentifierInfo())
    PrevKind = tok::identifier;

  unsigned ConcatInfo = detail::ConcatLookup[PrevKind];

  if (LLVM_LIKELY(ConcatInfo == 0))
    return false;

  if (ConcatInfo & detail::aci_avoid_equal) {
    if (Tok.isOneOf(tok::equal, tok::equalequal))
      return true;
    ConcatInfo &= ~detail::aci_avoid_equal;
  }
  if (LLVM_UNLIKELY(Tok.isAnnotation()))
    ConcatInfo = 0;

  if (ConcatInfo == 0)
    return false;

  if (ConcatInfo & detail::aci_custom_firstchar) {
    const char FirstChar = inspectFirstChar(PP, Tok);
    const auto &DC = detail::DangerCharLookup[PrevKind];
    uint32_t Packed;
    std::memcpy(&Packed, DC.C, 4);
    uint32_t Spread = static_cast<uint8_t>(FirstChar) * 0x01010101u;
    uint32_t Xor = Packed ^ Spread;
    uint32_t HasZero = (Xor - 0x01010101u) & ~Xor & 0x80808080u;
    return (Packed != 0) & (HasZero != 0);
  }

  switch (PrevKind) {
  default:
    llvm_unreachable("PasteGuard table built wrong");
  case tok::identifier:
    if (Tok.is(tok::numeric_constant))
      return inspectFirstChar(PP, Tok) != '.';
    if (Tok.getIdentifierInfo() ||
        Tok.isOneOf(tok::wide_string_literal, tok::utf8_string_literal,
                    tok::utf16_string_literal, tok::utf32_string_literal,
                    tok::wide_char_constant, tok::utf8_char_constant,
                    tok::utf16_char_constant, tok::utf32_char_constant))
      return true;
    if (Tok.isNot(tok::char_constant) && Tok.isNot(tok::string_literal))
      return false;
    return isIdentifierStringPrefix(PrevTok);
  case tok::numeric_constant: {
    const char FC = inspectFirstChar(PP, Tok);
    return isPreprocessingNumberBody(FC) | (FC == '+') | (FC == '-');
  }
  case tok::period: {
    const char FC = inspectFirstChar(PP, Tok);
    return (FC == '.' && PrevPrevTok.is(tok::period)) | isDigit(FC);
  }
  }
}
