#include "neverc/Foundation/Core/PrettyStackTrace.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Tree/Decl/PrettyDeclStackTrace.h"
#include "llvm/ADT/STLExtras.h"

using namespace neverc;


namespace {

enum MisleadingStatementKind { MSK_if, MSK_else, MSK_for, MSK_while };

struct MisleadingIndentationChecker {
  Parser &P;
  SourceLocation StmtLoc;
  SourceLocation PrevLoc;
  unsigned NumDirectives;
  MisleadingStatementKind Kind;
  bool ShouldSkip;
  MisleadingIndentationChecker(Parser &P, MisleadingStatementKind K,
                               SourceLocation SL)
      : P(P), StmtLoc(SL), PrevLoc(P.getCurToken().getLocation()),
        NumDirectives(P.getPrepEngine().getNumDirectives()), Kind(K),
        ShouldSkip(P.getCurToken().is(tok::l_brace)) {
    if (!P.MisleadingIndentationElseLoc.isInvalid()) {
      StmtLoc = P.MisleadingIndentationElseLoc;
      P.MisleadingIndentationElseLoc = SourceLocation();
    }
    if (Kind == MSK_else && !ShouldSkip)
      P.MisleadingIndentationElseLoc = SL;
  }

  static unsigned getVisualIndentation(SourceManager &SM, SourceLocation Loc) {
    unsigned TabStop = SM.getDiagnostics().getDiagnosticOptions().TabStop;

    unsigned ColNo = SM.getSpellingColumnNumber(Loc);
    if (ColNo == 0 || TabStop == 1)
      return ColNo;

    auto [FID, Offset] = SM.getDecomposedLoc(Loc);

    bool Invalid;
    llvm::StringRef BufData = SM.getBufferData(FID, &Invalid);
    if (LLVM_UNLIKELY(Invalid))
      return 0;

    const char *EndPos = BufData.data() + Offset;
    assert(Offset + 1 >= ColNo && "Column number smaller than file offset?");

    const char *LineStart = EndPos - (ColNo - 1);
    unsigned Remaining = ColNo - 1;

    unsigned VisualColumn = 0;
    const char *P = LineStart;
    while (Remaining >= 4) {
      bool AnyTab =
          P[0] == '\t' || P[1] == '\t' || P[2] == '\t' || P[3] == '\t';
      if (LLVM_LIKELY(!AnyTab)) {
        VisualColumn += 4;
        P += 4;
        Remaining -= 4;
        continue;
      }
      for (unsigned i = 0; i < 4; ++i, ++P, --Remaining) {
        if (*P == '\t')
          VisualColumn += (TabStop - VisualColumn % TabStop);
        else
          ++VisualColumn;
      }
    }
    for (; Remaining; --Remaining, ++P) {
      if (*P == '\t')
        VisualColumn += (TabStop - VisualColumn % TabStop);
      else
        ++VisualColumn;
    }
    return VisualColumn + 1;
  }

  void Check() {
    Token Tok = P.getCurToken();
    if (P.getActions().getDiagnostics().isIgnored(
            diag::warn_misleading_indentation, Tok.getLocation()) ||
        ShouldSkip || NumDirectives != P.getPrepEngine().getNumDirectives() ||
        Tok.isOneOf(tok::semi, tok::r_brace) || Tok.isAnnotation() ||
        Tok.getLocation().isMacroID() || PrevLoc.isMacroID() ||
        StmtLoc.isMacroID() ||
        (Kind == MSK_else && P.MisleadingIndentationElseLoc.isInvalid())) {
      P.MisleadingIndentationElseLoc = SourceLocation();
      return;
    }
    if (Kind == MSK_else)
      P.MisleadingIndentationElseLoc = SourceLocation();

    SourceManager &SM = P.getPrepEngine().getSourceManager();
    unsigned PrevColNum = getVisualIndentation(SM, PrevLoc);
    unsigned CurColNum = getVisualIndentation(SM, Tok.getLocation());
    unsigned StmtColNum = getVisualIndentation(SM, StmtLoc);

    if (PrevColNum != 0 && CurColNum != 0 && StmtColNum != 0 &&
        ((PrevColNum > StmtColNum && PrevColNum == CurColNum) ||
         !Tok.isAtStartOfLine()) &&
        SM.getPresumedLineNumber(StmtLoc) !=
            SM.getPresumedLineNumber(Tok.getLocation()) &&
        (Tok.isNot(tok::identifier) ||
         P.getPrepEngine().LookAhead(0).isNot(tok::colon))) {
      P.Diag(Tok.getLocation(), diag::warn_misleading_indentation) << Kind;
      P.Diag(StmtLoc, diag::note_previous_statement);
    }
  }
};

} // namespace

// ===----------------------------------------------------------------------===
// Control flow statements
// ===----------------------------------------------------------------------===

StmtResult Parser::ParseIfStatement(SourceLocation *TrailingElseLoc) {
  assert(Tok.is(tok::kw_if) && "Not an if stmt!");
  SourceLocation IfLoc = ConsumeToken(); // eat the 'if'.

  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after)
        << tok::getKeywordSpelling(tok::kw_if);
    SkipUntil(tok::semi);
    return StmtError();
  }

  bool C99 = getLangOpts().C99;

  // In C99+ the if-statement introduces a block scope.
  ParseScope IfScope(this, Scope::DeclScope | Scope::ControlScope, C99);

  Sema::ConditionResult Cond;
  SourceLocation LParen;
  SourceLocation RParen;
  if (ParseParenExprOrCondition(Cond, IfLoc, Sema::ConditionKind::Boolean,
                                LParen, RParen))
    return StmtError();

  bool IsBracedThen = Tok.is(tok::l_brace);

  // C99+ body scope (skipped when body is already a compound statement).
  ParseScope InnerScope(this, Scope::DeclScope, C99, IsBracedThen);

  MisleadingIndentationChecker MIChecker(*this, MSK_if, IfLoc);

  // Read the 'then' stmt.
  SourceLocation ThenStmtLoc = Tok.getLocation();

  SourceLocation InnerStatementTrailingElseLoc;
  StmtResult ThenStmt;
  ThenStmt = ParseStatement(&InnerStatementTrailingElseLoc);

  if (Tok.isNot(tok::kw_else))
    MIChecker.Check();

  // Pop the 'if' scope if needed.
  InnerScope.Exit();

  // If it has an else, parse it.
  SourceLocation ElseLoc;
  SourceLocation ElseStmtLoc;
  StmtResult ElseStmt;

  if (Tok.is(tok::kw_else)) {
    if (TrailingElseLoc)
      *TrailingElseLoc = Tok.getLocation();

    ElseLoc = ConsumeToken();
    ElseStmtLoc = Tok.getLocation();

    // C99+ body scope for else-branch (skipped when braced).
    ParseScope InnerScope(this, Scope::DeclScope, C99, Tok.is(tok::l_brace));

    MisleadingIndentationChecker MIChecker(*this, MSK_else, ElseLoc);
    ElseStmt = ParseStatement();

    if (ElseStmt.isUsable())
      MIChecker.Check();

    // Pop the 'else' scope if needed.
    InnerScope.Exit();
  } else if (InnerStatementTrailingElseLoc.isValid()) {
    Diag(InnerStatementTrailingElseLoc, diag::warn_dangling_else);
  }

  IfScope.Exit();

  // If the then or else stmt is invalid and the other is valid (and present),
  // turn the invalid one into a null stmt to avoid dropping the other
  // part.  If both are invalid, return error.
  if ((ThenStmt.isInvalid() && ElseStmt.isInvalid()) ||
      (ThenStmt.isInvalid() && ElseStmt.get() == nullptr) ||
      (ThenStmt.get() == nullptr && ElseStmt.isInvalid())) {
    // Both invalid, or one is invalid and other is non-present: return error.
    return StmtError();
  }

  // Now if either are invalid, replace with a ';'.
  if (ThenStmt.isInvalid())
    ThenStmt = Actions.OnNullStmt(ThenStmtLoc);
  if (ElseStmt.isInvalid())
    ElseStmt = Actions.OnNullStmt(ElseStmtLoc);

  return Actions.OnIfStmt(IfLoc, LParen, /*InitStmt=*/nullptr, Cond, RParen,
                          ThenStmt.get(), ElseLoc, ElseStmt.get());
}

StmtResult Parser::ParseSwitchStatement(SourceLocation *TrailingElseLoc) {
  assert(Tok.is(tok::kw_switch) && "Not a switch stmt!");
  SourceLocation SwitchLoc = ConsumeToken(); // eat the 'switch'.

  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after)
        << tok::getKeywordSpelling(tok::kw_switch);
    SkipUntil(tok::semi);
    return StmtError();
  }

  bool C99 = getLangOpts().C99;

  // C99+ switch introduces a block scope.
  unsigned ScopeFlags = Scope::SwitchScope;
  if (C99)
    ScopeFlags |= Scope::DeclScope | Scope::ControlScope;
  ParseScope SwitchScope(this, ScopeFlags);

  Sema::ConditionResult Cond;
  SourceLocation LParen;
  SourceLocation RParen;
  if (ParseParenExprOrCondition(Cond, SwitchLoc, Sema::ConditionKind::Switch,
                                LParen, RParen))
    return StmtError();

  StmtResult Switch = Actions.OnStartOfSwitchStmt(
      SwitchLoc, LParen, /*InitStmt=*/nullptr, Cond, RParen);

  if (Switch.isInvalid()) {
    // Skip the switch body.
    // Not optimal recovery, but parsing the body is more
    // dangerous due to the presence of case and default statements, which
    // will have no place to connect back with the switch.
    if (Tok.is(tok::l_brace)) {
      ConsumeBrace();
      SkipUntil(tok::r_brace);
    } else
      SkipUntil(tok::semi);
    return Switch;
  }

  // C99+ body scope for switch (skipped when braced).
  getCurScope()->AddFlags(Scope::BreakScope);
  ParseScope InnerScope(this, Scope::DeclScope, C99, Tok.is(tok::l_brace));

  if (C99)
    getCurScope()->decrementMSManglingNumber();

  // Read the body statement.
  StmtResult Body(ParseStatement(TrailingElseLoc));

  // Pop the scopes.
  InnerScope.Exit();
  SwitchScope.Exit();

  return Actions.OnFinishSwitchStmt(SwitchLoc, Switch.get(), Body.get());
}

StmtResult Parser::ParseWhileStatement(SourceLocation *TrailingElseLoc) {
  assert(Tok.is(tok::kw_while) && "Not a while stmt!");
  SourceLocation WhileLoc = Tok.getLocation();
  ConsumeToken(); // eat the 'while'.

  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after)
        << tok::getKeywordSpelling(tok::kw_while);
    SkipUntil(tok::semi);
    return StmtError();
  }

  bool C99 = getLangOpts().C99;

  // C99+ while introduces a block scope.
  unsigned ScopeFlags;
  if (C99)
    ScopeFlags = Scope::BreakScope | Scope::ContinueScope | Scope::DeclScope |
                 Scope::ControlScope;
  else
    ScopeFlags = Scope::BreakScope | Scope::ContinueScope;
  ParseScope WhileScope(this, ScopeFlags);

  Sema::ConditionResult Cond;
  SourceLocation LParen;
  SourceLocation RParen;
  if (ParseParenExprOrCondition(Cond, WhileLoc, Sema::ConditionKind::Boolean,
                                LParen, RParen))
    return StmtError();

  // C99+ body scope for while (skipped when braced).
  ParseScope InnerScope(this, Scope::DeclScope, C99, Tok.is(tok::l_brace));

  MisleadingIndentationChecker MIChecker(*this, MSK_while, WhileLoc);

  // Read the body statement.
  StmtResult Body(ParseStatement(TrailingElseLoc));

  if (Body.isUsable())
    MIChecker.Check();
  // Pop the body scope if needed.
  InnerScope.Exit();
  WhileScope.Exit();

  if (Cond.isInvalid() || Body.isInvalid())
    return StmtError();

  return Actions.OnWhileStmt(WhileLoc, LParen, Cond, RParen, Body.get());
}

StmtResult Parser::ParseDoStatement() {
  assert(Tok.is(tok::kw_do) && "Not a do stmt!");
  SourceLocation DoLoc = ConsumeToken(); // eat the 'do'.

  // C99+ do introduces a block scope.
  unsigned ScopeFlags;
  if (getLangOpts().C99)
    ScopeFlags = Scope::BreakScope | Scope::ContinueScope | Scope::DeclScope;
  else
    ScopeFlags = Scope::BreakScope | Scope::ContinueScope;

  ParseScope DoScope(this, ScopeFlags);

  // C99+ body scope for do (skipped when braced).
  bool C99 = getLangOpts().C99;
  ParseScope InnerScope(this, Scope::DeclScope, C99, Tok.is(tok::l_brace));

  // Read the body statement.
  StmtResult Body(ParseStatement());

  // Pop the body scope if needed.
  InnerScope.Exit();

  if (Tok.isNot(tok::kw_while)) {
    if (!Body.isInvalid()) {
      Diag(Tok, diag::err_expected_while);
      Diag(DoLoc, diag::note_matching) << "'do'";
      SkipUntil(tok::semi, StopBeforeMatch);
    }
    return StmtError();
  }
  SourceLocation WhileLoc = ConsumeToken();

  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after) << "do/while";
    SkipUntil(tok::semi, StopBeforeMatch);
    return StmtError();
  }

  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  // A do-while expression is not a condition, so can't have attributes.
  DiagnoseAndSkipBracketAttributes();

  SourceLocation Start = Tok.getLocation();
  ExprResult Cond = ParseExpression();
  // Correct the typos in condition before closing the scope.
  if (Cond.isUsable())
    Cond = Actions.CorrectDelayedTyposInExpr(Cond, /*InitDecl=*/nullptr,
                                             /*RecoverUncorrectedTypos=*/true);
  else {
    if (!Tok.isOneOf(tok::r_paren, tok::r_square, tok::r_brace))
      SkipUntil(tok::semi);
    Cond = Actions.CreateRecoveryExpr(
        Start, Start == Tok.getLocation() ? Start : PrevTokLocation, {},
        Actions.getTreeContext().BoolTy);
  }
  T.consumeClose();
  DoScope.Exit();

  if (Cond.isInvalid() || Body.isInvalid())
    return StmtError();

  return Actions.OnDoStmt(DoLoc, Body.get(), WhileLoc, T.getOpenLocation(),
                          Cond.get(), T.getCloseLocation());
}

StmtResult Parser::ParseForStatement(SourceLocation *TrailingElseLoc) {
  assert(Tok.is(tok::kw_for) && "Not a for stmt!");
  SourceLocation ForLoc = ConsumeToken(); // eat the 'for'.

  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after)
        << tok::getKeywordSpelling(tok::kw_for);
    SkipUntil(tok::semi);
    return StmtError();
  }

  bool HasForDeclScope = getLangOpts().C99;

  // C99+ for introduces a block scope.
  unsigned ScopeFlags = 0;
  if (HasForDeclScope)
    ScopeFlags = Scope::DeclScope | Scope::ControlScope;

  ParseScope ForScope(this, ScopeFlags);

  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  ExprResult Value;

  StmtResult FirstPart;
  Sema::ConditionResult SecondPart;
  FullExprArg ThirdPart;

  ParsedAttributes attrs(AttrFactory);
  MaybeParseBracketAttributes(attrs);

  SourceLocation EmptyInitStmtSemiLoc;

  if (Tok.is(tok::semi)) { // for (;
    ProhibitAttributes(attrs);
    // no first part, eat the ';'.
    SourceLocation SemiLoc = Tok.getLocation();
    if (!Tok.hasLeadingEmptyMacro() && !SemiLoc.isMacroID())
      EmptyInitStmtSemiLoc = SemiLoc;
    ConsumeToken();
  } else if (isForInitDeclaration()) { // for (int X = 4;
    ParenBraceBracketBalancer BalancerRAIIObj(*this);

    // Parse declaration, which eats the ';'.
    if (!HasForDeclScope) { // Use of C99-style for loops in C90 mode?
      Diag(Tok, diag::ext_c99_variable_decl_in_for_loop);
      Diag(Tok, diag::warn_gcc_variable_decl_in_for_loop);
    }
    DeclGroupPtrTy DG;
    SourceLocation DeclStart = Tok.getLocation(), DeclEnd;
    {
      ColonProtectionRAIIObject ColonProtection(*this, false);
      ParsedAttributes DeclSpecAttrs(AttrFactory);
      DG = ParseSimpleDeclaration(DeclaratorContext::ForInit, DeclEnd, attrs,
                                  DeclSpecAttrs, false);
      FirstPart = Actions.OnDeclStmt(DG, DeclStart, Tok.getLocation());
      if (Tok.is(tok::semi)) {
        ConsumeToken();
      } else {
        Diag(Tok, diag::err_expected_semi_for);
      }
    }
  } else {
    ProhibitAttributes(attrs);
    Value = Actions.CorrectDelayedTyposInExpr(ParseExpression());

    // Turn the expression into a stmt (discarded-value / void expression).
    if (!Value.isInvalid())
      FirstPart = Actions.OnExprStmt(Value);

    if (Tok.is(tok::semi)) {
      ConsumeToken();
    } else {
      if (!Value.isInvalid()) {
        Diag(Tok, diag::err_expected_semi_for);
      } else {
        // Skip until semicolon or rparen, don't consume it.
        SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch);
        if (Tok.is(tok::semi))
          ConsumeToken();
      }
    }
  }

  if (!SecondPart.isInvalid()) {
    if (Tok.is(tok::semi)) { // for (...;;
      // no second part.
    } else if (Tok.is(tok::r_paren)) {
      // missing both semicolons.
    } else {
      {
        // We permit 'continue' and 'break' in the condition of a for loop.
        getCurScope()->AddFlags(Scope::BreakScope | Scope::ContinueScope);

        ExprResult SecondExpr = ParseExpression();
        if (SecondExpr.isInvalid())
          SecondPart = Sema::ConditionError();
        else
          SecondPart = Actions.OnCondition(
              getCurScope(), ForLoc, SecondExpr.get(),
              Sema::ConditionKind::Boolean, /*MissingOK=*/true);
      }
    }
  }

  // Enter a break / continue scope, if we didn't already enter one while
  // parsing the second part.
  if (!getCurScope()->isContinueScope())
    getCurScope()->AddFlags(Scope::BreakScope | Scope::ContinueScope);

  if (Tok.isNot(tok::semi)) {
    if (!SecondPart.isInvalid())
      Diag(Tok, diag::err_expected_semi_for);
    SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch);
  }

  if (Tok.is(tok::semi)) {
    ConsumeToken();
  }

  if (Tok.isNot(tok::r_paren)) { // for (...;...;)
    ExprResult Third = ParseExpression();
    // Increment expression is discarded (void context).
    ThirdPart = Actions.MakeFullDiscardedValueExpr(Third.get());
  }
  // Match the ')'.
  T.consumeClose();

  // C99+ body scope for for-loop (skipped when braced).
  ParseScope InnerScope(this, Scope::DeclScope, HasForDeclScope,
                        Tok.is(tok::l_brace));

  // The body of the for loop has the same local mangling number as the
  // for-init-statement.
  // It will only be incremented if the body contains other things that would
  // normally increment the mangling number (like a compound statement).
  if (HasForDeclScope)
    getCurScope()->decrementMSManglingNumber();

  MisleadingIndentationChecker MIChecker(*this, MSK_for, ForLoc);

  // Read the body statement.
  StmtResult Body(ParseStatement(TrailingElseLoc));

  if (Body.isUsable())
    MIChecker.Check();

  // Pop the body scope if needed.
  InnerScope.Exit();

  // Leave the for-scope.
  ForScope.Exit();

  if (Body.isInvalid())
    return StmtError();

  return Actions.OnForStmt(ForLoc, T.getOpenLocation(), FirstPart.get(),
                           SecondPart, ThirdPart, T.getCloseLocation(),
                           Body.get());
}

StmtResult Parser::ParseGotoStatement() {
  assert(Tok.is(tok::kw_goto) && "Not a goto stmt!");
  SourceLocation GotoLoc = ConsumeToken(); // eat the 'goto'.

  StmtResult Res;
  if (Tok.is(tok::identifier)) {
    LabelDecl *LD =
        Actions.LookupOrCreateLabel(Tok.getIdentifierInfo(), Tok.getLocation());
    Res = Actions.OnGotoStmt(GotoLoc, Tok.getLocation(), LD);
    ConsumeToken();
  } else if (Tok.is(tok::star)) {
    // GNU indirect goto extension.
    Diag(Tok, diag::ext_gnu_indirect_goto);
    SourceLocation StarLoc = ConsumeToken();
    ExprResult R(ParseExpression());
    if (R.isInvalid()) { // Skip to the semicolon, but don't consume it.
      SkipUntil(tok::semi, StopBeforeMatch);
      return StmtError();
    }
    Res = Actions.OnIndirectGotoStmt(GotoLoc, StarLoc, R.get());
  } else {
    Diag(Tok, diag::err_expected) << tok::identifier;
    return StmtError();
  }

  return Res;
}

StmtResult Parser::ParseContinueStatement() {
  SourceLocation ContinueLoc = ConsumeToken(); // eat the 'continue'.
  return Actions.OnContinueStmt(ContinueLoc, getCurScope());
}

StmtResult Parser::ParseBreakStatement() {
  SourceLocation BreakLoc = ConsumeToken(); // eat the 'break'.
  return Actions.OnBreakStmt(BreakLoc, getCurScope());
}

StmtResult Parser::ParseReturnStatement() {
  assert(Tok.is(tok::kw_return) && "Not a return stmt!");
  SourceLocation ReturnLoc = ConsumeToken();

  ExprResult R;
  if (Tok.isNot(tok::semi)) {
    R = ParseExpression();
    if (R.isInvalid()) {
      SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
      return StmtError();
    }
  }
  return Actions.OnReturnStmt(ReturnLoc, R.get(), getCurScope());
}

Decl *Parser::ParseFunctionStatementBody(Decl *Decl, ParseScope &BodyScope) {
  assert(Tok.is(tok::l_brace));
  SourceLocation LBraceLoc = Tok.getLocation();

  PrettyDeclStackTraceEntry CrashInfo(Actions.Context, Decl, LBraceLoc,
                                      "parsing function body");

  Sema::PragmaStackSentinelRAII PragmaStackSentinel(
      Actions, "InternalPragmaState", false);

  // Do not enter a scope for the brace, as the arguments are in the same scope
  // (the function body) as the body itself.  Instead, just read the statement
  // list and put it into a CompoundStmt for safe keeping.
  StmtResult FnBody(ParseCompoundStatementBody());

  // If the function body could not be parsed, make a bogus compoundstmt.
  if (FnBody.isInvalid()) {
    Sema::CompoundScopeRAII CompoundScope(Actions);
    FnBody = Actions.OnCompoundStmt(LBraceLoc, LBraceLoc, std::nullopt, false);
  }

  BodyScope.Exit();
  return Actions.OnFinishFunctionBody(Decl, FnBody.get());
}

void Parser::ParseMicrosoftIfExistsStatement(StmtVector &Stmts) {
  IfExistsCondition Result;
  if (ParseMicrosoftIfExistsCondition(Result))
    return;

  BalancedDelimiterTracker Braces(*this, tok::l_brace);
  if (Braces.consumeOpen()) {
    Diag(Tok, diag::err_expected) << tok::l_brace;
    return;
  }

  if (Result.Behavior == IEB_Skip) {
    Braces.skipToEnd();
    return;
  }
  assert(Result.Behavior == IEB_Parse);

  // Condition is true, parse the statements.
  while (Tok.isNot(tok::r_brace)) {
    StmtResult R =
        ParseStatementOrDeclaration(Stmts, ParsedStmtContext::Compound);
    if (R.isUsable())
      Stmts.push_back(R.get());
  }
  Braces.consumeClose();
}
