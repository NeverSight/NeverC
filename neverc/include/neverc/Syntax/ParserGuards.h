#ifndef NEVERC_SYNTAX_PARSERGUARDS_H
#define NEVERC_SYNTAX_PARSERGUARDS_H

#include "neverc/Analyze/DelayedDiagnostic.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Syntax/ParseDiag.h"
#include "neverc/Syntax/SyntaxParser.h"

namespace neverc {
class ParsingDeclDiagnosticScope {
  Sema &S;
  sema::DelayedDiagnosticPool DiagnosticPool;
  Sema::ParsingDeclState State;
  bool Active;

public:
  ParsingDeclDiagnosticScope(Parser &P, bool activate = true)
      : S(P.getActions()), DiagnosticPool(nullptr) {
    if (activate) {
      State = S.PushParsingDeclaration(DiagnosticPool);
      Active = true;
    } else {
      Active = false;
    }
  }
  ParsingDeclDiagnosticScope(ParsingDeclDiagnosticScope &&Other)
      : S(Other.S), DiagnosticPool(std::move(Other.DiagnosticPool)),
        State(Other.State), Active(Other.Active) {
    Other.Active = false;
  }
  void operator=(ParsingDeclDiagnosticScope &&Other) = delete;

  void done() {
    assert(Active && "trying to end an inactive suppression");
    S.PopParsingDeclaration(State, nullptr);
    Active = false;
  }

  void redelay() {
    assert(!Active && "redelaying without having ended first");
    if (!DiagnosticPool.pool_empty())
      S.redelayDiagnostics(DiagnosticPool);
    assert(DiagnosticPool.pool_empty());
  }

  ~ParsingDeclDiagnosticScope() {
    if (Active)
      done();
  }
};

class ParsingDeclRAIIObject {
  Sema &Actions;
  sema::DelayedDiagnosticPool DiagnosticPool;
  Sema::ParsingDeclState State;
  bool Popped;

  ParsingDeclRAIIObject(const ParsingDeclRAIIObject &) = delete;
  void operator=(const ParsingDeclRAIIObject &) = delete;

public:
  enum NoParent_t { NoParent };
  ParsingDeclRAIIObject(Parser &P, NoParent_t _)
      : Actions(P.getActions()), DiagnosticPool(nullptr) {
    push();
  }

  ParsingDeclRAIIObject(Parser &P,
                        const sema::DelayedDiagnosticPool *parentPool)
      : Actions(P.getActions()), DiagnosticPool(parentPool) {
    push();
  }

  ParsingDeclRAIIObject(Parser &P, ParsingDeclRAIIObject *other)
      : Actions(P.getActions()),
        DiagnosticPool(other ? other->DiagnosticPool.getParent() : nullptr) {
    if (other) {
      DiagnosticPool.steal(other->DiagnosticPool);
      other->abort();
    }
    push();
  }

  ~ParsingDeclRAIIObject() { abort(); }

  sema::DelayedDiagnosticPool &getDelayedDiagnosticPool() {
    return DiagnosticPool;
  }
  const sema::DelayedDiagnosticPool &getDelayedDiagnosticPool() const {
    return DiagnosticPool;
  }

  void reset() {
    abort();
    push();
  }

  void abort() { pop(nullptr); }

  void complete(Decl *D) {
    assert(!Popped && "ParsingDeclaration has already been popped!");
    pop(D);
  }

private:
  void push() {
    State = Actions.PushParsingDeclaration(DiagnosticPool);
    Popped = false;
  }

  void pop(Decl *D) {
    if (!Popped) {
      Actions.PopParsingDeclaration(State, D);
      Popped = true;
    }
  }
};

class ParsingDeclSpec : public DeclSpec {
  ParsingDeclRAIIObject ParsingRAII;

public:
  ParsingDeclSpec(Parser &P)
      : DeclSpec(P.getAttrFactory()),
        ParsingRAII(P, ParsingDeclRAIIObject::NoParent) {}
  ParsingDeclSpec(Parser &P, ParsingDeclRAIIObject *RAII)
      : DeclSpec(P.getAttrFactory()), ParsingRAII(P, RAII) {}

  const sema::DelayedDiagnosticPool &getDelayedDiagnosticPool() const {
    return ParsingRAII.getDelayedDiagnosticPool();
  }

  void complete(Decl *D) { ParsingRAII.complete(D); }

  void abort() { ParsingRAII.abort(); }
};

class ParsingDeclarator : public Declarator {
  ParsingDeclRAIIObject ParsingRAII;

public:
  ParsingDeclarator(Parser &P, const ParsingDeclSpec &DS,
                    const ParsedAttributes &DeclarationAttrs,
                    DeclaratorContext C)
      : Declarator(DS, DeclarationAttrs, C),
        ParsingRAII(P, &DS.getDelayedDiagnosticPool()) {}

  const ParsingDeclSpec &getDeclSpec() const {
    return static_cast<const ParsingDeclSpec &>(Declarator::getDeclSpec());
  }

  ParsingDeclSpec &getMutableDeclSpec() const {
    return const_cast<ParsingDeclSpec &>(getDeclSpec());
  }

  void clear() {
    Declarator::clear();
    ParsingRAII.reset();
  }

  void complete(Decl *D) { ParsingRAII.complete(D); }
};

class ParsingFieldDeclarator : public FieldDeclarator {
  ParsingDeclRAIIObject ParsingRAII;

public:
  ParsingFieldDeclarator(Parser &P, const ParsingDeclSpec &DS,
                         const ParsedAttributes &DeclarationAttrs)
      : FieldDeclarator(DS, DeclarationAttrs),
        ParsingRAII(P, &DS.getDelayedDiagnosticPool()) {}

  void complete(Decl *D) { ParsingRAII.complete(D); }
};

class ExtensionRAIIObject {
  ExtensionRAIIObject(const ExtensionRAIIObject &) = delete;
  void operator=(const ExtensionRAIIObject &) = delete;

  DiagnosticsEngine &Diags;

public:
  ExtensionRAIIObject(DiagnosticsEngine &diags) : Diags(diags) {
    Diags.IncrementAllExtensionsSilenced();
  }

  ~ExtensionRAIIObject() { Diags.DecrementAllExtensionsSilenced(); }
};

class ColonProtectionRAIIObject {
  Parser &P;
  bool OldVal;

public:
  ColonProtectionRAIIObject(Parser &p, bool Value = true)
      : P(p), OldVal(P.ColonIsSacred) {
    P.ColonIsSacred = Value;
  }

  void restore() { P.ColonIsSacred = OldVal; }

  ~ColonProtectionRAIIObject() { restore(); }
};

class OffsetOfStateRAIIObject {
  Sema::OffsetOfKind &OffsetOfState;
  Sema::OffsetOfKind OldValue;

public:
  OffsetOfStateRAIIObject(Parser &P, Sema::OffsetOfKind Value)
      : OffsetOfState(P.OffsetOfState), OldValue(P.OffsetOfState) {
    OffsetOfState = Value;
  }

  ~OffsetOfStateRAIIObject() { OffsetOfState = OldValue; }
};

class ParenBraceBracketBalancer {
  Parser &P;
  unsigned short ParenCount, BracketCount, BraceCount;

public:
  ParenBraceBracketBalancer(Parser &p)
      : P(p), ParenCount(p.ParenCount), BracketCount(p.BracketCount),
        BraceCount(p.BraceCount) {}

  ~ParenBraceBracketBalancer() {
    P.ParenCount = ParenCount;
    P.BracketCount = BracketCount;
    P.BraceCount = BraceCount;
  }
};

class BalancedDelimiterTracker {
  Parser &P;
  tok::TokenKind Kind, Close, FinalToken;
  SourceLocation (Parser::*Consumer)();
  SourceLocation LOpen, LClose;

  unsigned short &getDepth() {
    switch (Kind) {
    case tok::l_brace:
      return P.BraceCount;
    case tok::l_square:
      return P.BracketCount;
    case tok::l_paren:
      return P.ParenCount;
    default:
      llvm_unreachable("Wrong token kind");
    }
  }

  bool diagnoseOverflow();
  bool diagnoseMissingClose();

public:
  BalancedDelimiterTracker(Parser &p, tok::TokenKind k,
                           tok::TokenKind FinalToken = tok::semi)
      : P(p), Kind(k), FinalToken(FinalToken) {
    switch (Kind) {
    default:
      llvm_unreachable("Unexpected balanced token");
    case tok::l_brace:
      Close = tok::r_brace;
      Consumer = &Parser::ConsumeBrace;
      break;
    case tok::l_paren:
      Close = tok::r_paren;
      Consumer = &Parser::ConsumeParen;
      break;

    case tok::l_square:
      Close = tok::r_square;
      Consumer = &Parser::ConsumeBracket;
      break;
    }
  }

  SourceLocation getOpenLocation() const { return LOpen; }
  SourceLocation getCloseLocation() const { return LClose; }
  SourceRange getRange() const { return SourceRange(LOpen, LClose); }

  bool consumeOpen() {
    if (!P.Tok.is(Kind))
      return true;

    if (getDepth() < P.getLangOpts().BracketDepth) {
      LOpen = (P.*Consumer)();
      return false;
    }

    return diagnoseOverflow();
  }

  bool expectAndConsume(unsigned DiagID = diag::err_expected,
                        const char *Msg = "",
                        tok::TokenKind SkipToTok = tok::unknown);
  bool consumeClose() {
    if (P.Tok.is(Close)) {
      LClose = (P.*Consumer)();
      return false;
    } else if (P.Tok.is(tok::semi) && P.NextToken().is(Close)) {
      SourceLocation SemiLoc = P.ConsumeToken();
      P.Diag(SemiLoc, diag::err_unexpected_semi)
          << Close << FixItHint::CreateRemoval(SourceRange(SemiLoc, SemiLoc));
      LClose = (P.*Consumer)();
      return false;
    }

    return diagnoseMissingClose();
  }
  void skipToEnd();
};
} // end namespace neverc

#endif
