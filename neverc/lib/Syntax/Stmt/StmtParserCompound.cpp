#include "neverc/Foundation/Core/PrettyStackTrace.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Tree/Decl/PrettyDeclStackTrace.h"
#include "llvm/ADT/STLExtras.h"

using namespace neverc;


// ===----------------------------------------------------------------------===
// Compound statements
// ===----------------------------------------------------------------------===

StmtResult Parser::ParseCompoundStatement(bool isStmtExpr) {
  return ParseCompoundStatement(isStmtExpr,
                                Scope::DeclScope | Scope::CompoundStmtScope);
}

StmtResult Parser::ParseCompoundStatement(bool isStmtExpr,
                                          unsigned ScopeFlags) {
  assert(Tok.is(tok::l_brace) && "Not a compound stmt!");

  // Enter a scope to hold everything within the compound stmt.  Compound
  // statements can always hold declarations.
  ParseScope CompoundScope(this, ScopeFlags);

  return ParseCompoundStatementBody(isStmtExpr);
}

void Parser::ParseCompoundStatementLeadingPragmas() {
  for (;;) {
    switch (Tok.getKind()) {
    case tok::annot_pragma_vis:
      ProcessPragmaVisibility();
      continue;
    case tok::annot_pragma_pack:
      ProcessPragmaPack();
      continue;
    case tok::annot_pragma_msstruct:
      ProcessPragmaMSStruct();
      continue;
    case tok::annot_pragma_align:
      ProcessPragmaAlign();
      continue;
    case tok::annot_pragma_weak:
      ProcessPragmaWeak();
      continue;
    case tok::annot_pragma_weakalias:
      ProcessPragmaWeakAlias();
      continue;
    case tok::annot_pragma_fp_contract:
      ProcessPragmaFPContract();
      continue;
    case tok::annot_pragma_fp:
      ProcessPragmaFP();
      continue;
    case tok::annot_pragma_fenv_access:
    case tok::annot_pragma_fenv_access_ms:
      ProcessPragmaFEnvAccess();
      continue;
    case tok::annot_pragma_fenv_round:
      ProcessPragmaFEnvRound();
      continue;
    case tok::annot_pragma_cx_limited_range:
      ProcessPragmaCXLimitedRange();
      continue;
    case tok::annot_pragma_float_control:
      ProcessPragmaFloatControl();
      continue;
    case tok::annot_pragma_ms_pragma:
      ProcessPragmaMSPragma();
      continue;
    case tok::annot_pragma_dump:
      ProcessPragmaDump();
      continue;
    default:
      return;
    }
  }
}

void Parser::WarnTrailingLabel() {
  Diag(Tok, getLangOpts().C23
                ? diag::warn_c23_compat_label_end_of_compound_statement
                : diag::ext_c_label_end_of_compound_statement);
}

bool Parser::SkipEmptyStatement(StmtVector &Stmts) {
  if (!Tok.is(tok::semi))
    return false;

  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc;

  while (Tok.is(tok::semi) && !Tok.hasLeadingEmptyMacro() &&
         Tok.getLocation().isValid() && !Tok.getLocation().isMacroID()) {
    EndLoc = Tok.getLocation();

    // Don't just ConsumeToken() this tok::semi, do store it in AST.
    StmtResult R =
        ParseStatementOrDeclaration(Stmts, ParsedStmtContext::SubStmt);
    if (R.isUsable())
      Stmts.push_back(R.get());
  }

  // Did not consume any extra semi.
  if (EndLoc.isInvalid())
    return false;

  Diag(StartLoc, diag::warn_null_statement)
      << FixItHint::CreateRemoval(SourceRange(StartLoc, EndLoc));
  return true;
}

StmtResult Parser::finalizeExprStmt(ExprResult E, ParsedStmtContext StmtCtx) {
  bool IsStmtExprResult = false;
  if ((StmtCtx & ParsedStmtContext::InStmtExpr) != ParsedStmtContext()) {
    // For GCC compatibility we skip past NullStmts.
    unsigned LookAhead = 0;
    while (GetLookAheadToken(LookAhead).is(tok::semi)) {
      ++LookAhead;
    }
    // Then look to see if the next two tokens close the statement expression;
    // if so, this expression statement is the last statement in a statement
    // expression.
    IsStmtExprResult = GetLookAheadToken(LookAhead).is(tok::r_brace) &&
                       GetLookAheadToken(LookAhead + 1).is(tok::r_paren);
  }

  if (IsStmtExprResult)
    E = Actions.OnStmtExprResult(E);
  return Actions.OnExprStmt(E, /*DiscardedValue=*/!IsStmtExprResult);
}

NEVERC_HOT StmtResult
Parser::ParseCompoundStatementBody(bool isStmtExpr) {
  PrettyStackTraceLoc CrashInfo(PP.getSourceManager(), Tok.getLocation(),
                                "in compound statement ('{}')");

  // Record the current FPFeatures, restore on leaving the
  // compound statement.
  Sema::FPFeaturesStateRAII SaveFPFeatures(Actions);

  BalancedDelimiterTracker T(*this, tok::l_brace);
  if (T.consumeOpen())
    return StmtError();

  Sema::CompoundScopeRAII CompoundScope(Actions, isStmtExpr);

  // Parse any pragmas at the beginning of the compound statement.
  ParseCompoundStatementLeadingPragmas();
  Actions.OnAfterCompoundStatementLeadingPragmas();

  StmtVector Stmts;

  // "__label__ X, Y, Z;" is the GNU "Local Label" extension.  These are
  // only allowed at the start of a compound stmt regardless of the language.
  while (Tok.is(tok::kw___label__)) {
    SourceLocation LabelLoc = ConsumeToken();

    llvm::SmallVector<Decl *, 8> DeclsInGroup;
    while (true) {
      if (Tok.isNot(tok::identifier)) {
        Diag(Tok, diag::err_expected) << tok::identifier;
        break;
      }

      IdentifierInfo *II = Tok.getIdentifierInfo();
      SourceLocation IdLoc = ConsumeToken();
      DeclsInGroup.push_back(Actions.LookupOrCreateLabel(II, IdLoc, LabelLoc));

      if (!TryConsumeToken(tok::comma))
        break;
    }

    DeclSpec DS(AttrFactory);
    DeclGroupPtrTy Res =
        Actions.FinalizeDeclaratorGroup(getCurScope(), DS, DeclsInGroup);
    StmtResult R = Actions.OnDeclStmt(Res, LabelLoc, Tok.getLocation());

    RequireSemicolon(diag::err_expected_semi_declaration);
    if (R.isUsable())
      Stmts.push_back(R.get());
  }

  ParsedStmtContext SubStmtCtx =
      ParsedStmtContext::Compound |
      (isStmtExpr ? ParsedStmtContext::InStmtExpr : ParsedStmtContext());

  while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
    if (LLVM_UNLIKELY(Tok.is(tok::annot_pragma_unused))) {
      ProcessPragmaUnused();
      continue;
    }

    if (LLVM_UNLIKELY(SkipEmptyStatement(Stmts)))
      continue;

    StmtResult R;
    if (LLVM_LIKELY(Tok.isNot(tok::kw___extension__))) {
      R = ParseStatementOrDeclaration(Stmts, SubStmtCtx);
    } else {
      // __extension__ can start declarations and it can also be a unary
      // operator for expressions.  Consume multiple __extension__ markers here
      // until we can determine which is which.
      SourceLocation ExtLoc = ConsumeToken();
      while (Tok.is(tok::kw___extension__))
        ConsumeToken();

      ParsedAttributes attrs(AttrFactory);
      MaybeParseBracketAttributes(attrs);

      // If this is the start of a declaration, parse it as such.
      if (isDeclarationStatement()) {
        // __extension__ silences extension warnings in the subdeclaration.
        ExtensionRAIIObject O(Diags);

        SourceLocation DeclStart = Tok.getLocation(), DeclEnd;
        ParsedAttributes DeclSpecAttrs(AttrFactory);
        DeclGroupPtrTy Res = ParseDeclaration(DeclaratorContext::Block, DeclEnd,
                                              attrs, DeclSpecAttrs);
        R = Actions.OnDeclStmt(Res, DeclStart, DeclEnd);
      } else {
        // Otherwise this was a unary __extension__ marker.
        ExprResult Res(ParseExpressionWithLeadingExtension(ExtLoc));

        if (Res.isInvalid()) {
          SkipUntil(tok::semi);
          continue;
        }

        // Eat the semicolon at the end of stmt and convert the expr into a
        // statement.
        RequireSemicolon(diag::err_expected_semi_after_expr);
        R = finalizeExprStmt(Res, SubStmtCtx);
        if (R.isUsable())
          R = Actions.OnAttributedStmt(attrs, R.get());
      }
    }

    if (R.isUsable())
      Stmts.push_back(R.get());
  }
  // Warn the user that using `-ffp-eval-method=source` with `sse` disabled
  // is not supported.
  if (!PP.getTargetInfo().supportSourceEvalMethod() &&
      (PP.getLastFPEvalPragmaLocation().isValid() ||
       PP.getCurrentFPEvalMethod() ==
           LangOptions::FPEvalMethodKind::FEM_Source))
    Diag(Tok.getLocation(),
         diag::warn_no_support_for_eval_method_source_on_m32);

  SourceLocation CloseLoc = Tok.getLocation();

  // We broke out of the while loop because we found a '}' or EOF.
  if (!T.consumeClose()) {
    // If this is the '})' of a statement expression, check that it's written
    // in a sensible way.
    if (isStmtExpr && Tok.is(tok::r_paren))
      checkCompoundToken(CloseLoc, tok::r_brace, CompoundToken::StmtExprEnd);
  } else {
    // Recover by creating a compound statement with what we parsed so far,
    // instead of dropping everything and returning StmtError().
  }

  if (T.getCloseLocation().isValid())
    CloseLoc = T.getCloseLocation();

  return Actions.OnCompoundStmt(T.getOpenLocation(), CloseLoc, Stmts,
                                isStmtExpr);
}

bool Parser::ParseParenExprOrCondition(Sema::ConditionResult &Cond,
                                       SourceLocation Loc,
                                       Sema::ConditionKind CK,
                                       SourceLocation &LParenLoc,
                                       SourceLocation &RParenLoc) {
  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();
  SourceLocation Start = Tok.getLocation();

  ExprResult CondExpr = ParseExpression();

  // If required, convert to a boolean value.
  if (CondExpr.isInvalid())
    Cond = Sema::ConditionError();
  else
    Cond = Actions.OnCondition(getCurScope(), Loc, CondExpr.get(), CK,
                               /*MissingOK=*/false);

  // If the parser was confused by the condition and we don't have a ')', try to
  // recover by skipping ahead to a semi and bailing out.  If condexp is
  // semantically invalid but we have well formed code, keep going.
  if (Cond.isInvalid() && Tok.isNot(tok::r_paren)) {
    SkipUntil(tok::semi);
    // Skipping may have stopped if it found the containing ')'.  If so, we can
    // continue parsing the if statement.
    if (Tok.isNot(tok::r_paren))
      return true;
  }

  if (Cond.isInvalid()) {
    ExprResult CondExpr = Actions.CreateRecoveryExpr(
        Start, Tok.getLocation() == Start ? Start : PrevTokLocation, {},
        Actions.PreferredConditionType(CK));
    if (!CondExpr.isInvalid())
      Cond = Actions.OnCondition(getCurScope(), Loc, CondExpr.get(), CK,
                                 /*MissingOK=*/false);
  }

  // Either the condition is valid or the rparen is present.
  T.consumeClose();
  LParenLoc = T.getOpenLocation();
  RParenLoc = T.getCloseLocation();

  // Check for extraneous ')'s to catch things like "if (foo())) {".  We know
  // that all callers are looking for a statement after the condition, so ")"
  // isn't valid.
  while (Tok.is(tok::r_paren)) {
    Diag(Tok, diag::err_extraneous_rparen_in_condition)
        << FixItHint::CreateRemoval(Tok.getLocation());
    ConsumeParen();
  }

  return false;
}
