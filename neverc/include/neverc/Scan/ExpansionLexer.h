#ifndef NEVERC_SCAN_EXPANSIONLEXER_H
#define NEVERC_SCAN_EXPANSIONLEXER_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Scan/Token.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

namespace neverc {

class MacroArgStorage;
class MacroRecord;
class PrepEngine;
class VAOptExpansionContext;

class ExpansionLexer {
  friend class PrepEngine;

  MacroRecord *Macro = nullptr;
  MacroArgStorage *ActualArgs = nullptr;
  PrepEngine &PP;

  const Token *Tokens;
  unsigned NumTokens;
  unsigned CurTokenIdx;

  SourceLocation ExpandLocStart, ExpandLocEnd;
  SourceLocation MacroExpansionStart;
  unsigned MacroStartSLocOffset;
  SourceLocation MacroDefStart;
  unsigned MacroDefLength;

  bool AtStartOfLine : 1;
  bool HasLeadingSpace : 1;
  bool NextTokGetsSpace : 1;
  bool OwnsTokens : 1;
  bool DisableMacroExpansion : 1;
  bool IsReinject : 1;

public:
  ExpansionLexer(Token &Tok, SourceLocation ILEnd, MacroRecord *MI,
                 MacroArgStorage *ActualArgs, PrepEngine &pp)
      : PP(pp), OwnsTokens(false) {
    Init(Tok, ILEnd, MI, ActualArgs);
  }

  ExpansionLexer(const Token *TokArray, unsigned NumToks, bool DisableExpansion,
                 bool ownsTokens, bool isReinject, PrepEngine &pp)
      : PP(pp), OwnsTokens(false) {
    Init(TokArray, NumToks, DisableExpansion, ownsTokens, isReinject);
  }

  ExpansionLexer(const ExpansionLexer &) = delete;
  ExpansionLexer &operator=(const ExpansionLexer &) = delete;
  ~ExpansionLexer() { destroy(); }

  void Init(Token &Tok, SourceLocation ELEnd, MacroRecord *MI,
            MacroArgStorage *Actuals);

  void Init(const Token *TokArray, unsigned NumToks, bool DisableMacroExpansion,
            bool OwnsTokens, bool IsReinject);

  unsigned isNextTokenLParen() const;
  bool Lex(Token &Tok);
  bool isParsingDirective() const;

private:
  void destroy();

  bool isAtEnd() const { return CurTokenIdx == NumTokens; }

  // Token-pasting helper used by both normal expansion and __VA_OPT__ handling.
  bool pasteTokens(Token &LHSTok, llvm::ArrayRef<Token> TokenStream,
                   unsigned int &CurIdx);

  bool pasteTokens(Token &Tok);

  void genVAOptStringified(llvm::SmallVectorImpl<Token> &ResultToks,
                           const VAOptExpansionContext &VCtx,
                           SourceLocation VAOPTClosingParenLoc);

  void substituteFunctionParams();
  bool needsParamSubstitution() const;
  bool processVAOpt(unsigned &I, llvm::SmallVectorImpl<Token> &ResultToks,
                    VAOptExpansionContext &VCtx, bool &MadeChange,
                    std::optional<bool> &CalledWithVariadicArguments);

  void skipPastedComment(Token &Tok, SourceLocation OpLoc);

  SourceLocation getExpansionLocForMacroDefLoc(SourceLocation loc) const;

  // Remap argument tokens from spelling locations to expansion locations.
  void updateLocForMacroArgTokens(SourceLocation ArgIdSpellLoc,
                                  Token *begin_tokens, Token *end_tokens);

  bool tryOmitCommaForVarargs(llvm::SmallVectorImpl<Token> &ResultToks,
                              bool HasPasteOperator, MacroRecord *Macro,
                              unsigned MacroArgNo, PrepEngine &PP);

  void carryLineFlags(Token &Result) {
    AtStartOfLine = Result.isAtStartOfLine();
    HasLeadingSpace = Result.hasLeadingSpace();
  }
};

} // namespace neverc

#endif // NEVERC_SCAN_EXPANSIONLEXER_H
