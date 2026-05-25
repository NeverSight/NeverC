#ifndef NEVERC_SCAN_SOURCESCANNER_H
#define NEVERC_SCAN_SOURCESCANNER_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Scan/LexerCore.h"
#include "neverc/Scan/Token.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>

namespace llvm {

class MemoryBufferRef;

} // namespace llvm

namespace neverc {

class DiagnosticBuilder;
class PrepEngine;
class SourceManager;
class LangOptions;

enum ConflictMarkerKind { CMK_None, CMK_Normal, CMK_Perforce };

class SourceScanner : public LexerCore {
  friend class PrepEngine;

  void anchor() override;

  const char *BufferStart;
  const char *BufferEnd;
  SourceLocation FileLoc;

  const LangOptions &LangOpts;

  bool LineComment;
  bool Is_PragmaLexer;

  unsigned char ExtendedTokenMode;

  const char *BufferPtr;
  bool IsAtStartOfLine;
  bool IsAtPhysicalStartOfLine;
  bool HasLeadingSpace;
  bool HasLeadingEmptyMacro;
  bool IsFirstTimeLexingFile;
  const char *NewLinePtr;
  ConflictMarkerKind CurrentConflictMarkerState;

  void initScanState(const char *BufStart, const char *BufPtr,
                     const char *BufEnd);

public:
  SourceScanner(FileID FID, const llvm::MemoryBufferRef &InputFile,
                PrepEngine &PP, bool IsFirstIncludeOfFile = true);

  SourceScanner(SourceLocation FileLoc, const LangOptions &LangOpts,
                const char *BufStart, const char *BufPtr, const char *BufEnd,
                bool IsFirstIncludeOfFile = true);

  SourceScanner(FileID FID, const llvm::MemoryBufferRef &FromFile,
                const SourceManager &SM, const LangOptions &LangOpts,
                bool IsFirstIncludeOfFile = true);

  SourceScanner(const SourceScanner &) = delete;
  SourceScanner &operator=(const SourceScanner &) = delete;

  static SourceScanner *CreatePragmaScanner(SourceLocation SpellingLoc,
                                            SourceLocation ExpansionLocStart,
                                            SourceLocation ExpansionLocEnd,
                                            unsigned TokLen, PrepEngine &PP);

  SourceLocation getFileLoc() const { return FileLoc; }

private:
  bool Lex(Token &Result);

public:
  bool isPragmaLexer() const { return Is_PragmaLexer; }

private:
  void IndirectLex(Token &Result) override { Lex(Result); }

public:
  bool LexFromRawLexer(Token &Result) {
    assert(LexingRawMode && "Not already in raw mode!");
    Lex(Result);
    return BufferPtr == BufferEnd;
  }

  bool isKeepWhitespaceMode() const { return ExtendedTokenMode > 1; }

  void SetKeepWhitespaceMode(bool Val) {
    assert((!Val || LexingRawMode || LangOpts.TraditionalCPP) &&
           "Can only retain whitespace in raw mode or -traditional-cpp");
    ExtendedTokenMode = Val ? 2 : 0;
  }

  bool inKeepCommentMode() const { return ExtendedTokenMode > 0; }

  void SetCommentRetentionState(bool Mode) {
    assert(!isKeepWhitespaceMode() &&
           "Can't play with comment retention state when retaining whitespace");
    ExtendedTokenMode = Mode ? 1 : 0;
  }

  void resetExtendedTokenMode();

  llvm::StringRef getBuffer() const {
    return llvm::StringRef(BufferStart, BufferEnd - BufferStart);
  }

  void drainDirectiveLine(llvm::SmallVectorImpl<char> *Result = nullptr);

  DiagnosticBuilder Diag(const char *Loc, unsigned DiagID) const;

  SourceLocation getSourceLocation(const char *Loc, unsigned TokLen = 1) const;

  SourceLocation getSourceLocation() override {
    return getSourceLocationFast(BufferPtr, 1);
  }

  const char *getBufferLocation() const { return BufferPtr; }

  unsigned getCurrentBufferOffset() {
    assert(BufferPtr >= BufferStart && "Invalid buffer state");
    return BufferPtr - BufferStart;
  }

  void seek(unsigned Offset, bool IsAtStartOfLine);

  static std::string escapeStringLiteral(llvm::StringRef Str,
                                         bool Charify = false);
  static void escapeStringLiteral(llvm::SmallVectorImpl<char> &Str);

  static unsigned getSpelling(const Token &Tok, const char *&Buffer,
                              const SourceManager &SourceMgr,
                              const LangOptions &LangOpts,
                              bool *Invalid = nullptr);

  static std::string getSpelling(const Token &Tok,
                                 const SourceManager &SourceMgr,
                                 const LangOptions &LangOpts,
                                 bool *Invalid = nullptr);

  static llvm::StringRef getSpelling(SourceLocation loc,
                                     llvm::SmallVectorImpl<char> &buffer,
                                     const SourceManager &SM,
                                     const LangOptions &options,
                                     bool *invalid = nullptr);

  static unsigned measureTokenLength(SourceLocation Loc,
                                     const SourceManager &SM,
                                     const LangOptions &LangOpts);

  static bool scanRawToken(SourceLocation Loc, Token &Result,
                           const SourceManager &SM, const LangOptions &LangOpts,
                           bool IgnoreWhiteSpace = false);

  static SourceLocation locateTokenOrigin(SourceLocation Loc,
                                          const SourceManager &SM,
                                          const LangOptions &LangOpts);

  static unsigned getTokenPrefixLength(SourceLocation TokStart, unsigned CharNo,
                                       const SourceManager &SM,
                                       const LangOptions &LangOpts);

  static SourceLocation AdvanceToTokenCharacter(SourceLocation TokStart,
                                                unsigned Characters,
                                                const SourceManager &SM,
                                                const LangOptions &LangOpts) {
    return TokStart.getLocWithOffset(
        getTokenPrefixLength(TokStart, Characters, SM, LangOpts));
  }

  static SourceLocation getLocForEndOfToken(SourceLocation Loc, unsigned Offset,
                                            const SourceManager &SM,
                                            const LangOptions &LangOpts);

  static CharSourceRange getAsCharRange(SourceRange Range,
                                        const SourceManager &SM,
                                        const LangOptions &LangOpts) {
    SourceLocation End = getLocForEndOfToken(Range.getEnd(), 0, SM, LangOpts);
    return End.isInvalid()
               ? CharSourceRange()
               : CharSourceRange::getCharRange(Range.getBegin(), End);
  }
  static CharSourceRange getAsCharRange(CharSourceRange Range,
                                        const SourceManager &SM,
                                        const LangOptions &LangOpts) {
    return Range.isTokenRange()
               ? getAsCharRange(Range.getAsRange(), SM, LangOpts)
               : Range;
  }

  static bool isAtStartOfMacroExpansion(SourceLocation loc,
                                        const SourceManager &SM,
                                        const LangOptions &LangOpts,
                                        SourceLocation *MacroBegin = nullptr);

  static bool isAtEndOfMacroExpansion(SourceLocation loc,
                                      const SourceManager &SM,
                                      const LangOptions &LangOpts,
                                      SourceLocation *MacroEnd = nullptr);

  static CharSourceRange makeFileCharRange(CharSourceRange Range,
                                           const SourceManager &SM,
                                           const LangOptions &LangOpts);

  static llvm::StringRef getSourceText(CharSourceRange Range,
                                       const SourceManager &SM,
                                       const LangOptions &LangOpts,
                                       bool *Invalid = nullptr);

  static llvm::StringRef getImmediateMacroName(SourceLocation Loc,
                                               const SourceManager &SM,
                                               const LangOptions &LangOpts);

  static llvm::StringRef getImmediateMacroNameForDiagnostics(
      SourceLocation Loc, const SourceManager &SM, const LangOptions &LangOpts);

  static std::optional<Token> peekNextToken(SourceLocation Loc,
                                            const SourceManager &SM,
                                            const LangOptions &LangOpts);

  static SourceLocation locateAfterToken(SourceLocation loc,
                                         tok::TokenKind TKind,
                                         const SourceManager &SM,
                                         const LangOptions &LangOpts,
                                         bool SkipTrailingWhitespaceAndNewLine);

  static bool isAsciiIdentifierContinueChar(char c,
                                            const LangOptions &LangOpts);

  static bool isEscapedNewline(const char *BufferStart, const char *Str);

  struct SizedChar {
    char Char;
    unsigned Size;
  };

  static inline SizedChar getCharAndSizeNoWarn(const char *Ptr,
                                               const LangOptions &LangOpts) {
    if (isTrivialCharUnit(Ptr[0]))
      return {*Ptr, 1u};
    return decodeCharSlowSilent(Ptr, LangOpts);
  }

  static llvm::StringRef getIndentationForLine(SourceLocation Loc,
                                               const SourceManager &SM);

  bool isFirstTimeLexingFile() const { return IsFirstTimeLexingFile; }

private:
  bool scanToken(Token &Result, bool TokAtPhysicalStartOfLine);
  int dispatchSlowPath(Token &Result, const char *CurPtr, char Char,
                       bool &TokAtPhysicalStartOfLine);
  bool checkUnicodeWhitespace(Token &Result, uint32_t C, const char *CurPtr);
  bool processUnicodeIdentStart(Token &Result, uint32_t C, const char *CurPtr);

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  SourceLocation getSourceLocationFast(const char *Loc, unsigned TokLen) const {
    unsigned CharNo = Loc - BufferStart;
    if (LLVM_LIKELY(FileLoc.isFileID()))
      return FileLoc.getLocWithOffset(CharNo);
    return getSourceLocation(Loc, TokLen);
  }

  void emitToken(Token &Result, const char *TokEnd, tok::TokenKind Kind) {
    unsigned TokLen = TokEnd - BufferPtr;
    Result.setKind(Kind);
    Result.setLength(TokLen);
    Result.setLocation(getSourceLocationFast(BufferPtr, TokLen));
    BufferPtr = TokEnd;
  }

  unsigned probeLeftParen();

  static bool isTrivialCharUnit(char C) { return C != '?' && C != '\\'; }

  inline char readChar(const char *&Ptr, Token &Tok) {
    if (isTrivialCharUnit(Ptr[0]))
      return *Ptr++;
    auto [C, Size] = decodeCharSlow(Ptr, &Tok);
    Ptr += Size;
    return C;
  }

  const char *consumeChar(const char *Ptr, unsigned Size, Token &Tok) {
    if (Size == 1)
      return Ptr + Size;
    return Ptr + decodeCharSlow(Ptr, &Tok).Size;
  }

  inline char peekCharSize(const char *Ptr, unsigned &Size) {
    if (isTrivialCharUnit(Ptr[0])) {
      Size = 1;
      return *Ptr;
    }
    auto CharAndSize = decodeCharSlow(Ptr);
    Size = CharAndSize.Size;
    return CharAndSize.Char;
  }

  SizedChar decodeCharSlow(const char *Ptr, Token *Tok = nullptr);
  static unsigned probeLineSplice(const char *P);
  static const char *skipSplices(const char *P);
  static SizedChar decodeCharSlowSilent(const char *Ptr,
                                        const LangOptions &LangOpts);

  void setByteOffset(unsigned Offset, bool StartOfLine);

  void carryLineFlags(Token &Result) {
    IsAtStartOfLine = Result.isAtStartOfLine();
    HasLeadingSpace = Result.hasLeadingSpace();
    HasLeadingEmptyMacro = Result.hasLeadingEmptyMacro();
  }

  bool scanIdentRest(Token &Result, const char *CurPtr);

  bool scanNumber(Token &Result, const char *CurPtr);
  bool isExponentSignContinuation(char C, char PrevCh, const char *CurPtr);
  bool scanQuotedLiteral(Token &Result, const char *CurPtr, char Quote,
                         tok::TokenKind Kind);
  bool scanStringLit(Token &Result, const char *CurPtr, tok::TokenKind Kind) {
    return scanQuotedLiteral(Result, CurPtr, '"', Kind);
  }
  bool scanAnglePath(Token &Result, const char *CurPtr) {
    return scanQuotedLiteral(Result, CurPtr, '>', tok::header_name);
  }
  bool scanCharLit(Token &Result, const char *CurPtr, tok::TokenKind Kind) {
    return scanQuotedLiteral(Result, CurPtr, '\'', Kind);
  }
  bool onBufferTerminator(Token &Result, const char *CurPtr);
  bool consumeSpacing(Token &Result, const char *CurPtr,
                      bool &TokAtPhysicalStartOfLine);
  bool skipNonToken(Token &Result, bool &TokAtPhysicalStartOfLine);
  bool consumeLineComment(Token &Result, const char *CurPtr,
                          bool &TokAtPhysicalStartOfLine);
  bool consumeBlockComment(Token &Result, const char *CurPtr,
                           bool &TokAtPhysicalStartOfLine);
  bool finalizeLineComment(Token &Result, const char *CurPtr);

  bool matchConflictBegin(const char *CurPtr);
  bool matchConflictClose(const char *CurPtr);

  void cutOffLexing() { BufferPtr = BufferEnd; }

  bool hasHexPrefix(const char *Start, const LangOptions &LangOpts);

  std::optional<uint32_t>
  tryReadNumericUCN(const char *&StartPtr, const char *SlashLoc, Token *Result);
  std::optional<uint32_t> tryReadNamedUCN(const char *&StartPtr,
                                          const char *SlashLoc, Token *Result);

  uint32_t tryReadUCN(const char *&StartPtr, const char *SlashLoc,
                      Token *Result);
  bool tryConsumeIdentifierUCN(const char *&CurPtr, unsigned Size,
                               Token &Result);
  bool tryConsumeIdentifierUTF8Char(const char *&CurPtr, Token &Result);
};

} // namespace neverc

#endif // NEVERC_SCAN_SOURCESCANNER_H
