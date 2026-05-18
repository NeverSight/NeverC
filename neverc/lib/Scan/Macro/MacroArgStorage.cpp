#include "neverc/Scan/MacroArgStorage.h"
#include "neverc/Scan/LexDiag.h"
#include "neverc/Scan/MacroRecord.h"
#include "neverc/Scan/PrepEngine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SaveAndRestore.h"
#include <algorithm>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Construction & destruction
// ===----------------------------------------------------------------------===

MacroArgStorage *MacroArgStorage::create(const MacroRecord *MI,
                                         llvm::ArrayRef<Token> UnexpArgTokens,
                                         bool VarargsElided, PrepEngine &PP) {
  assert(MI->isFunctionLike() && "Can't have args for an object-like macro!");
  MacroArgStorage **ResultEnt = nullptr;

  // LRU first-fit: recently released entries are at the head. Take the first
  // entry with sufficient capacity rather than scanning for the best fit.
  for (MacroArgStorage **Entry = &PP.MacroArgCache; *Entry;
       Entry = &(*Entry)->ArgCache) {
    if ((*Entry)->NumUnexpArgTokens >= UnexpArgTokens.size()) {
      ResultEnt = Entry;
      break;
    }
  }
  MacroArgStorage *Result;
  if (LLVM_UNLIKELY(!ResultEnt)) {
    Result =
        new (llvm::safe_malloc(totalSizeToAlloc<Token>(UnexpArgTokens.size())))
            MacroArgStorage(UnexpArgTokens.size(), VarargsElided,
                            MI->getNumParams());
  } else {
    Result = *ResultEnt;
    // Unlink this node from the preprocessors singly linked list.
    *ResultEnt = Result->ArgCache;
    Result->NumUnexpArgTokens = UnexpArgTokens.size();
    Result->VarargsElided = VarargsElided;
    Result->NumMacroArgStorage = MI->getNumParams();
    Result->ArgStartOffsets.clear();
    Result->ArgAccessCount = 0;
  }

  // Copy the actual unexpanded tokens to immediately after the result ptr.
  if (!UnexpArgTokens.empty()) {
    static_assert(std::is_trivial_v<Token>,
                  "assume trivial copyability if copying into the "
                  "uninitialized array (as opposed to reusing a cached "
                  "MacroArgStorage)");
    std::copy(UnexpArgTokens.begin(), UnexpArgTokens.end(),
              Result->getTrailingObjects<Token>());
  }

  return Result;
}

void MacroArgStorage::destroy(PrepEngine &PP) {
  for (unsigned i = 0, e = PreExpArgTokens.size(); i != e; ++i)
    PreExpArgTokens[i].clear();

  // Add this to the preprocessor's free list.
  ArgCache = PP.MacroArgCache;
  PP.MacroArgCache = this;
}

MacroArgStorage *MacroArgStorage::deallocate() {
  MacroArgStorage *Next = ArgCache;

  // Run the dtor to deallocate the vectors.
  this->~MacroArgStorage();
  // Release the memory for the object.
  static_assert(std::is_trivially_destructible_v<Token>,
                "assume trivially destructible and forego destructors");
  free(this);

  return Next;
}

unsigned MacroArgStorage::getArgLength(const Token *ArgPtr) {
  const Token *Start = ArgPtr;
  while (LLVM_LIKELY(ArgPtr[0].isNot(tok::eof))) {
    if (LLVM_UNLIKELY(ArgPtr[1].is(tok::eof))) {
      ++ArgPtr;
      break;
    }
    if (LLVM_UNLIKELY(ArgPtr[2].is(tok::eof))) {
      ArgPtr += 2;
      break;
    }
    if (LLVM_UNLIKELY(ArgPtr[3].is(tok::eof))) {
      ArgPtr += 3;
      break;
    }
    if (LLVM_UNLIKELY(ArgPtr[4].is(tok::eof))) {
      ArgPtr += 4;
      break;
    }
    if (LLVM_UNLIKELY(ArgPtr[5].is(tok::eof))) {
      ArgPtr += 5;
      break;
    }
    if (LLVM_UNLIKELY(ArgPtr[6].is(tok::eof))) {
      ArgPtr += 6;
      break;
    }
    if (LLVM_UNLIKELY(ArgPtr[7].is(tok::eof))) {
      ArgPtr += 7;
      break;
    }
    __builtin_prefetch(ArgPtr + 16, 0, 1);
    ArgPtr += 8;
  }
  return static_cast<unsigned>(ArgPtr - Start);
}

// ===----------------------------------------------------------------------===
// Argument access & pre-expansion
// ===----------------------------------------------------------------------===

const Token *MacroArgStorage::getUnexpArgument(unsigned Arg) const {
  assert(Arg < getNumMacroArguments() && "Invalid arg #");
  const Token *Start = getTrailingObjects<Token>();

  if (LLVM_LIKELY(!ArgStartOffsets.empty())) {
    assert(Arg < ArgStartOffsets.size());
    return Start + ArgStartOffsets[Arg];
  }

  ++ArgAccessCount;

  // First access: linear scan without building the offset table.
  // Most macros with few arguments are only accessed once per expansion.
  if (ArgAccessCount < 2) {
    const Token *Ptr = Start;
    for (unsigned I = 0; I != Arg; ++I)
      while (Ptr++->isNot(tok::eof)) {
      }
    return Ptr;
  }

  // Second+ access: build the full offset table for future lookups.
  ArgStartOffsets.reserve(getNumMacroArguments());
  ArgStartOffsets.push_back(0);
  for (unsigned Idx = 0; Idx < NumUnexpArgTokens; ++Idx) {
    if (Start[Idx].is(tok::eof) &&
        ArgStartOffsets.size() < getNumMacroArguments())
      ArgStartOffsets.push_back(Idx + 1);
  }
  assert(ArgStartOffsets.size() == getNumMacroArguments());

  return Start + ArgStartOffsets[Arg];
}

bool MacroArgStorage::invokedWithVariadicArgument(const MacroRecord *const MI,
                                                  PrepEngine &PP) {
  if (!MI->isVariadic())
    return false;
  const int VariadicArgIndex = getNumMacroArguments() - 1;
  return getPreExpArgument(VariadicArgIndex, PP).front().isNot(tok::eof);
}

bool MacroArgStorage::ArgNeedsPreexpansion(const Token *ArgTok,
                                           PrepEngine &PP) const {
  for (; ArgTok->isNot(tok::eof); ++ArgTok) {
    if (LLVM_LIKELY(ArgTok + 1 != nullptr))
      __builtin_prefetch(ArgTok + 2, 0, 1);
    if (const IdentifierInfo *II = ArgTok->getIdentifierInfo())
      if (LLVM_UNLIKELY(II->hasMacroDefinition()))
        return true;
  }
  return false;
}

const std::vector<Token> &MacroArgStorage::getPreExpArgument(unsigned Arg,
                                                             PrepEngine &PP) {
  assert(Arg < getNumMacroArguments() && "Invalid argument number!");

  // If we have already computed this, return it.
  if (PreExpArgTokens.size() < getNumMacroArguments())
    PreExpArgTokens.resize(getNumMacroArguments());

  std::vector<Token> &Result = PreExpArgTokens[Arg];
  if (!Result.empty())
    return Result;

  SaveAndRestore PreExpandingMacroArgStorage(PP.InMacroArgPreExpansion, true);

  const Token *AT = getUnexpArgument(Arg);
  unsigned NumToks = getArgLength(AT) + 1; // Include the EOF.

  PP.PushTokenStream(AT, NumToks, false /*disable expand*/,
                     false /*owns tokens*/, false /*is reinject*/);

  Result.reserve(NumToks);
  do {
    Result.emplace_back();
    PP.Lex(Result.back());
  } while (Result.back().isNot(tok::eof));

  // Pop the token stream before its backing storage goes away.
  if (PP.IsCaching())
    PP.DisableCaching();
  PP.PopLexerLevel();
  return Result;
}

// ===----------------------------------------------------------------------===
// Stringification
// ===----------------------------------------------------------------------===

Token MacroArgStorage::EscapeArgToLiteral(const Token *ArgToks, PrepEngine &PP,
                                          bool Charify,
                                          SourceLocation ExpansionLocStart,
                                          SourceLocation ExpansionLocEnd) {
  Token Tok;
  Tok.startToken();
  Tok.setKind(Charify ? tok::char_constant : tok::string_literal);

  const Token *ArgTokStart = ArgToks;

  // Escape all tokens into a string literal.
  llvm::SmallString<256> Result;
  Result += "\"";

  bool isFirst = true;
  for (; ArgToks->isNot(tok::eof); ++ArgToks) {
    const Token &Tok = *ArgToks;
    if (!isFirst && (Tok.hasLeadingSpace() || Tok.isAtStartOfLine()))
      Result += ' ';
    isFirst = false;

    // Escape backslash and quote in string/char tokens for stringification.
    if (tok::isStringLiteral(Tok.getKind()) || Tok.is(tok::char_constant) ||
        Tok.is(tok::wide_char_constant) || Tok.is(tok::utf8_char_constant) ||
        Tok.is(tok::utf16_char_constant) || Tok.is(tok::utf32_char_constant)) {
      bool Invalid = false;
      llvm::SmallString<256> SpellingBuf;
      llvm::StringRef TokStr = PP.getSpelling(Tok, SpellingBuf, &Invalid);
      if (!Invalid) {
        // Inline escape directly into Result (avoids temporary string).
        for (char C : TokStr) {
          if (C == '\\' || C == '"')
            Result.push_back('\\');
          Result.push_back(C);
        }
      }
    } else {
      unsigned CurStrLen = Result.size();
      Result.resize(CurStrLen + Tok.getLength());
      const char *BufPtr = Result.data() + CurStrLen;
      bool Invalid = false;
      unsigned ActualTokLen = PP.getSpelling(Tok, BufPtr, &Invalid);

      if (!Invalid) {
        if (ActualTokLen && BufPtr != &Result[CurStrLen])
          memcpy(&Result[CurStrLen], BufPtr, ActualTokLen);
        if (ActualTokLen != Tok.getLength())
          Result.resize(CurStrLen + ActualTokLen);
      }
    }
  }

  // Trailing unescaped backslash means invalid literal (C99).
  if (Result.back() == '\\') {
    unsigned FirstNonSlash = Result.size() - 2;
    while (Result[FirstNonSlash] == '\\')
      --FirstNonSlash;
    if ((Result.size() - 1 - FirstNonSlash) & 1) {
      PP.Diag(ArgToks[-1], diag::pp_invalid_string_literal);
      Result.pop_back();
    }
  }
  Result += '"';

  if (Charify) {
    Result[0] = '\'';
    Result[Result.size() - 1] = '\'';

    bool isBad = false;
    if (Result.size() == 3)
      isBad = Result[1] == '\'';
    else
      isBad = (Result.size() != 4 || Result[1] != '\\');

    if (isBad) {
      PP.Diag(ArgTokStart[0], diag::err_invalid_character_to_charify);
      Result = "' '";
    }
  }

  PP.WriteScratch(Result, Tok, ExpansionLocStart, ExpansionLocEnd);
  return Tok;
}
