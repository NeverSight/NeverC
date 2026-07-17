#include "neverc/Foundation/Core/PrettyStackTrace.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/ParserPluginHooks.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Tree/Decl/PrettyDeclStackTrace.h"
#include "llvm/ADT/STLExtras.h"

using namespace neverc;

// ===----------------------------------------------------------------------===
// Statement parsing entry
// ===----------------------------------------------------------------------===

NEVERC_HOT StmtResult Parser::ParseStatement(
    SourceLocation *TrailingElseLoc, ParsedStmtContext StmtCtx) {
  StmtResult Res;
  StmtVector Stmts;
  do {
    Res = ParseStatementOrDeclaration(Stmts, StmtCtx, TrailingElseLoc);
  } while (!Res.isInvalid() && !Res.get());

  return Res;
}

NEVERC_HOT StmtResult Parser::ParseStatementOrDeclaration(
    StmtVector &Stmts, ParsedStmtContext StmtCtx,
    SourceLocation *TrailingElseLoc) {

  ParenBraceBracketBalancer BalancerRAIIObj(*this);

  if (PluginHooks) {
    Stmt *Extension = nullptr;
    switch (PluginHooks->parseStatement(*this, Extension)) {
    case ParserPluginOutcome::Handled:
      return Extension;
    case ParserPluginOutcome::Error:
      SkipUntil(tok::semi, StopBeforeMatch);
      TryConsumeToken(tok::semi);
      return StmtError();
    case ParserPluginOutcome::NotHandled:
      break;
    }
  }

  ParsedAttributes BracketAttrs(AttrFactory);
  MaybeParseBracketAttributes(BracketAttrs);
  ParsedAttributes GNUAttrs(AttrFactory);

  StmtResult Res = ParseStatementOrDeclarationAfterAttributes(
      Stmts, StmtCtx, TrailingElseLoc, BracketAttrs, GNUAttrs);

  if (LLVM_LIKELY(BracketAttrs.empty() && GNUAttrs.empty()))
    return Res;

  ParsedAttributes Attrs(AttrFactory);
  takeAndConcatenateAttrs(BracketAttrs, GNUAttrs, Attrs);

  assert((Attrs.empty() || Res.isInvalid() || Res.isUsable()) &&
         "attributes on empty statement");

  if (Attrs.empty() || Res.isInvalid())
    return Res;

  return Actions.OnAttributedStmt(Attrs, Res.get());
}

namespace {
class StatementFilterCCC final : public CorrectionCandidateCallback {
public:
  StatementFilterCCC(Token nextTok) : NextToken(nextTok) {
    WantTypeSpecifiers = nextTok.isOneOf(tok::l_paren, tok::less, tok::l_square,
                                         tok::identifier, tok::star, tok::amp);
    WantExpressionKeywords =
        nextTok.isOneOf(tok::l_paren, tok::identifier, tok::arrow, tok::period);
    WantRemainingKeywords =
        nextTok.isOneOf(tok::l_paren, tok::semi, tok::identifier, tok::l_brace);
  }

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    if (candidate.getCorrectionDeclAs<FieldDecl>())
      return true;
    if (NextToken.is(tok::equal))
      return candidate.getCorrectionDeclAs<VarDecl>();
    return CorrectionCandidateCallback::ValidateCandidate(candidate);
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<StatementFilterCCC>(*this);
  }

private:
  Token NextToken;
};
} // namespace

NEVERC_HOT StmtResult
Parser::ParseStatementOrDeclarationAfterAttributes(
    StmtVector &Stmts, ParsedStmtContext StmtCtx,
    SourceLocation *TrailingElseLoc, ParsedAttributes &BracketAttrs,
    ParsedAttributes &GNUAttrs) {
  const char *SemiError = nullptr;
  StmtResult Res;
  SourceLocation GNUAttributeLoc;

  // Cases in this switch statement should fall through if the parser expects
  // the token to end in a semicolon (in which case SemiError should be set),
  // or they directly 'return;' if not.
  bool IdentAlreadyClassified = false;
Retry:
  tok::TokenKind Kind = Tok.getKind();
  IdentAlreadyClassified = false;
  switch (Kind) {
  case tok::identifier: {
    Token Next = NextToken();
    if (Next.is(tok::colon)) { // labeled-statement
      ParsedAttributes Attrs(AttrFactory);
      takeAndConcatenateAttrs(BracketAttrs, GNUAttrs, Attrs);
      return ParseLabeledStatement(Attrs, StmtCtx);
    }

    if (LLVM_LIKELY(Next.isOneOf(
            tok::equal, tok::plusequal, tok::minusequal, tok::starequal,
            tok::slashequal, tok::percentequal, tok::lesslessequal,
            tok::greatergreaterequal, tok::ampequal, tok::pipeequal,
            tok::caretequal, tok::plusplus, tok::minusminus, tok::period,
            tok::arrow, tok::l_square))) {
      IdentAlreadyClassified = true;
      [[fallthrough]];
    } else {
      StatementFilterCCC CCC(Next);
      if (TryAnnotateName(Next, &CCC) == ANK_Error) {
        SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
        if (Tok.is(tok::semi))
          ConsumeToken();
        return StmtError();
      }

      if (Tok.isNot(tok::identifier))
        goto Retry;

      IdentAlreadyClassified = true;
      [[fallthrough]];
    }
  }

  default: {
    bool HaveAttrs = !BracketAttrs.empty() || !GNUAttrs.empty();
    bool GNUAttrTrigger = false;
    if (LLVM_UNLIKELY(GNUAttributeLoc.isValid())) {
      auto IsStmtAttr = [](ParsedAttr &Attr) { return Attr.isStmtAttr(); };
      bool AllAttrsAreStmtAttrs = llvm::all_of(BracketAttrs, IsStmtAttr) &&
                                  llvm::all_of(GNUAttrs, IsStmtAttr);
      GNUAttrTrigger = !(HaveAttrs && AllAttrsAreStmtAttrs);
    }
    if ((GNUAttrTrigger ||
         (!IdentAlreadyClassified && isDeclarationStatement()))) {
      SourceLocation DeclStart = Tok.getLocation(), DeclEnd;
      DeclGroupPtrTy Decl;
      if (GNUAttributeLoc.isValid()) {
        DeclStart = GNUAttributeLoc;
        Decl = ParseDeclaration(DeclaratorContext::Block, DeclEnd, BracketAttrs,
                                GNUAttrs, &GNUAttributeLoc);
      } else {
        Decl = ParseDeclaration(DeclaratorContext::Block, DeclEnd, BracketAttrs,
                                GNUAttrs);
      }
      if (BracketAttrs.Range.getBegin().isValid()) {
        // The caller must guarantee that the BracketAttrs appear before the
        // GNUAttrs, and we rely on that here.
        assert(GNUAttrs.Range.getBegin().isInvalid() ||
               GNUAttrs.Range.getBegin() > BracketAttrs.Range.getBegin());
        DeclStart = BracketAttrs.Range.getBegin();
      } else if (GNUAttrs.Range.getBegin().isValid())
        DeclStart = GNUAttrs.Range.getBegin();
      return Actions.OnDeclStmt(Decl, DeclStart, DeclEnd);
    }

    if (Tok.is(tok::r_brace)) {
      Diag(Tok, diag::err_expected_statement);
      return StmtError();
    }

    return ParseExprStatement(StmtCtx);
  }

  case tok::kw___attribute: {
    GNUAttributeLoc = Tok.getLocation();
    ParseGNUAttributes(GNUAttrs);
    goto Retry;
  }

  case tok::kw_case:
    return ParseCaseStatement(StmtCtx);
  case tok::kw_default:
    return ParseDefaultStatement(StmtCtx);

  case tok::l_brace:
    return ParseCompoundStatement();
  case tok::semi: {
    bool HasLeadingEmptyMacro = Tok.hasLeadingEmptyMacro();
    return Actions.OnNullStmt(ConsumeToken(), HasLeadingEmptyMacro);
  }

  case tok::kw_if:
    return ParseIfStatement(TrailingElseLoc);
  case tok::kw_switch:
    return ParseSwitchStatement(TrailingElseLoc);

  case tok::kw_while:
    return ParseWhileStatement(TrailingElseLoc);
  case tok::kw_do:
    Res = ParseDoStatement();
    SemiError = "do/while";
    break;
  case tok::kw_for:
    return ParseForStatement(TrailingElseLoc);

  case tok::kw_goto:
    Res = ParseGotoStatement();
    SemiError = tok::getKeywordSpelling(tok::kw_goto);
    break;
  case tok::kw_continue:
    Res = ParseContinueStatement();
    SemiError = tok::getKeywordSpelling(tok::kw_continue);
    break;
  case tok::kw_break:
    Res = ParseBreakStatement();
    SemiError = tok::getKeywordSpelling(tok::kw_break);
    break;
  case tok::kw_return:
    Res = ParseReturnStatement();
    SemiError = tok::getKeywordSpelling(tok::kw_return);
    break;

  case tok::kw_asm: {
    for (const ParsedAttr &AL : BracketAttrs)
      // Could be relaxed if asm-related regular keyword attributes are
      // added later.
      (AL.isRegularKeywordAttribute()
           ? Diag(AL.getRange().getBegin(), diag::err_keyword_not_allowed)
           : Diag(AL.getRange().getBegin(), diag::warn_attribute_ignored))
          << AL;
    // Prevent these from being interpreted as statement attributes later on.
    BracketAttrs.clear();
    ProhibitAttributes(GNUAttrs);
    bool msAsm = false;
    Res = ParseAsmStatement(msAsm);
    if (msAsm)
      return Res;
    SemiError = tok::getKeywordSpelling(tok::kw_asm);
    break;
  }

  case tok::kw___if_exists:
  case tok::kw___if_not_exists:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    ParseMicrosoftIfExistsStatement(Stmts);
    // An __if_exists block is like a compound statement, but it doesn't create
    // a new scope.
    return StmtEmpty();

  case tok::kw___try:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    return ParseSEHTryBlock();

  case tok::kw___leave:
    Res = ParseSEHLeaveStatement();
    SemiError = tok::getKeywordSpelling(tok::kw___leave);
    break;

  case tok::annot_pragma_vis:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    ProcessPragmaVisibility();
    return StmtEmpty();

  case tok::annot_pragma_pack:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    ProcessPragmaPack();
    return StmtEmpty();

  case tok::annot_pragma_msstruct:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    ProcessPragmaMSStruct();
    return StmtEmpty();

  case tok::annot_pragma_align:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    ProcessPragmaAlign();
    return StmtEmpty();

  case tok::annot_pragma_weak:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    ProcessPragmaWeak();
    return StmtEmpty();

  case tok::annot_pragma_weakalias:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    ProcessPragmaWeakAlias();
    return StmtEmpty();

  case tok::annot_pragma_fp_contract:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope) << "fp_contract";
    ConsumeAnnotationToken();
    return StmtError();

  case tok::annot_pragma_fp:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope) << "neverc fp";
    ConsumeAnnotationToken();
    return StmtError();

  case tok::annot_pragma_fenv_access:
  case tok::annot_pragma_fenv_access_ms:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope)
        << (Kind == tok::annot_pragma_fenv_access ? "STDC FENV_ACCESS"
                                                  : "fenv_access");
    ConsumeAnnotationToken();
    return StmtEmpty();

  case tok::annot_pragma_fenv_round:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope) << "STDC FENV_ROUND";
    ConsumeAnnotationToken();
    return StmtError();

  case tok::annot_pragma_cx_limited_range:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope)
        << "STDC CX_LIMITED_RANGE";
    ConsumeAnnotationToken();
    return StmtError();

  case tok::annot_pragma_float_control:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    Diag(Tok, diag::err_pragma_file_or_compound_scope) << "float_control";
    ConsumeAnnotationToken();
    return StmtError();

  case tok::annot_pragma_ms_pragma:
    ProhibitAttributes(BracketAttrs);
    ProhibitAttributes(GNUAttrs);
    ProcessPragmaMSPragma();
    return StmtEmpty();

  case tok::annot_pragma_dump:
    ProcessPragmaDump();
    return StmtEmpty();

  case tok::annot_pragma_attribute:
    ProcessPragmaAttribute();
    return StmtEmpty();
  }

  if (!TryConsumeToken(tok::semi) && !Res.isInvalid()) {
    RequireToken(tok::semi, diag::err_expected_semi_after_stmt, SemiError);
    SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
  }

  return Res;
}

StmtResult Parser::ParseExprStatement(ParsedStmtContext StmtCtx) {
  Token OldToken = Tok;

  // expression[opt] ';'
  ExprResult Expr(ParseExpression());
  if (Expr.isInvalid()) {
    SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
    if (Tok.is(tok::semi))
      ConsumeToken();
    return Actions.OnExprStmtError();
  }

  if (Tok.is(tok::colon) && getCurScope()->isSwitchScope() &&
      Actions.CheckCaseExpression(Expr.get())) {
    // If a constant expression is followed by a colon inside a switch block,
    // suggest a missing case keyword.
    llvm::SmallString<8> CaseKw(tok::getKeywordSpelling(tok::kw_case));
    CaseKw += ' ';
    Diag(OldToken, diag::err_expected_case_before_expression)
        << FixItHint::CreateInsertion(OldToken.getLocation(), CaseKw);

    // Recover parsing as a case statement.
    return ParseCaseStatement(StmtCtx, /*MissingCase=*/true, Expr);
  }

  RequireSemicolon(diag::err_expected_semi_after_expr);
  return finalizeExprStmt(Expr, StmtCtx);
}

StmtResult Parser::ParseSEHTryBlock() {
  assert(Tok.is(tok::kw___try) && "Expected '__try'");
  SourceLocation TryLoc = ConsumeToken();

  if (Tok.isNot(tok::l_brace))
    return StmtError(Diag(Tok, diag::err_expected) << tok::l_brace);

  StmtResult TryBlock(ParseCompoundStatement(
      /*isStmtExpr=*/false,
      Scope::DeclScope | Scope::CompoundStmtScope | Scope::SEHTryScope));
  if (TryBlock.isInvalid())
    return TryBlock;

  StmtResult Handler;
  if (Tok.is(tok::identifier) &&
      Tok.getIdentifierInfo() == getSEHExceptKeyword()) {
    SourceLocation Loc = ConsumeToken();
    Handler = ParseSEHExceptBlock(Loc);
  } else if (Tok.is(tok::kw___finally)) {
    SourceLocation Loc = ConsumeToken();
    Handler = ParseSEHFinallyBlock(Loc);
  } else {
    return StmtError(Diag(Tok, diag::err_seh_expected_handler));
  }

  if (Handler.isInvalid())
    return Handler;

  return Actions.OnSEHTryBlock(TryLoc, TryBlock.get(), Handler.get());
}

StmtResult Parser::ParseSEHExceptBlock(SourceLocation ExceptLoc) {
  if (RequireToken(tok::l_paren))
    return StmtError();

  ParseScope ExpectScope(this, Scope::DeclScope | Scope::ControlScope |
                                   Scope::SEHExceptScope);

  ExprResult FilterExpr;
  {
    ParseScopeFlags FilterScope(this, getCurScope()->getFlags() |
                                          Scope::SEHFilterScope);
    FilterExpr = Actions.CorrectDelayedTyposInExpr(ParseExpression());
  }

  if (FilterExpr.isInvalid())
    return StmtError();

  if (RequireToken(tok::r_paren))
    return StmtError();

  if (Tok.isNot(tok::l_brace))
    return StmtError(Diag(Tok, diag::err_expected) << tok::l_brace);

  StmtResult Block(ParseCompoundStatement());

  if (Block.isInvalid())
    return Block;

  return Actions.OnSEHExceptBlock(ExceptLoc, FilterExpr.get(), Block.get());
}

StmtResult Parser::ParseSEHFinallyBlock(SourceLocation FinallyLoc) {
  if (Tok.isNot(tok::l_brace))
    return StmtError(Diag(Tok, diag::err_expected) << tok::l_brace);

  ParseScope FinallyScope(this, 0);
  Actions.OnStartSEHFinallyBlock();

  StmtResult Block(ParseCompoundStatement());
  if (Block.isInvalid()) {
    Actions.OnAbortSEHFinallyBlock();
    return Block;
  }

  return Actions.OnFinishSEHFinallyBlock(FinallyLoc, Block.get());
}

StmtResult Parser::ParseSEHLeaveStatement() {
  SourceLocation LeaveLoc = ConsumeToken(); // eat the '__leave'.
  return Actions.OnSEHLeaveStmt(LeaveLoc, getCurScope());
}

namespace {
void warnDeclAfterLabel(Parser &P, const Stmt *SubStmt) {
  // When in C mode (but not Microsoft extensions mode), diagnose use of a
  // label that is followed by a declaration rather than a statement.
  if (!P.getLangOpts().MicrosoftExt && isa<DeclStmt>(SubStmt)) {
    P.Diag(SubStmt->getBeginLoc(),
           P.getLangOpts().C23
               ? diag::warn_c23_compat_label_followed_by_declaration
               : diag::ext_c_label_followed_by_declaration);
  }
}
} // namespace

StmtResult Parser::ParseLabeledStatement(ParsedAttributes &Attrs,
                                         ParsedStmtContext StmtCtx) {
  assert(Tok.is(tok::identifier) && Tok.getIdentifierInfo() &&
         "Not an identifier!");

  Token IdentTok = Tok; // Save the whole token.
  ConsumeToken();       // eat the identifier.

  assert(Tok.is(tok::colon) && "Not a label!");

  // identifier ':' statement
  SourceLocation ColonLoc = ConsumeToken();

  // Read label attributes, if present.
  StmtResult SubStmt;
  if (Tok.is(tok::kw___attribute)) {
    ParsedAttributes TempAttrs(AttrFactory);
    ParseGNUAttributes(TempAttrs);

    // GNU label attributes; attach to the label before the following statement.
    Attrs.takeAllFrom(TempAttrs);
  }

  // The label may have no statement following it
  if (SubStmt.isUnset() && Tok.is(tok::r_brace)) {
    WarnTrailingLabel();
    SubStmt = Actions.OnNullStmt(ColonLoc);
  }

  // If we've not parsed a statement yet, parse one now.
  if (!SubStmt.isInvalid() && !SubStmt.isUsable())
    SubStmt = ParseStatement(nullptr, StmtCtx);

  // Broken substmt shouldn't prevent the label from being added to the AST.
  if (SubStmt.isInvalid())
    SubStmt = Actions.OnNullStmt(ColonLoc);

  warnDeclAfterLabel(*this, SubStmt.get());

  LabelDecl *LD = Actions.LookupOrCreateLabel(IdentTok.getIdentifierInfo(),
                                              IdentTok.getLocation());
  Actions.ProcessDeclAttributeList(Actions.CurScope, LD, Attrs);
  Attrs.clear();

  return Actions.OnLabelStmt(IdentTok.getLocation(), LD, ColonLoc,
                             SubStmt.get());
}

StmtResult Parser::ParseCaseStatement(ParsedStmtContext StmtCtx,
                                      bool MissingCase, ExprResult Expr) {
  assert((MissingCase || Tok.is(tok::kw_case)) && "Not a case stmt!");

  // It is very common for code to contain many case statements recursively
  // nested, as in (but usually without indentation):
  //  case 1:
  //    case 2:
  //      case 3:
  //         case 4:
  //           case 5: etc.
  //
  // Parsing this naively works, but is both inefficient and can cause us to run
  // out of stack space in our recursive descent parser.  As a special case,
  // flatten this recursion into an iterative loop.  This is complex and gross,
  // but all the grossness is constrained to ParseCaseStatement (and some
  // weirdness in the actions), so this is just local grossness :).

  // TopLevelCase - This is the highest level we have parsed.  'case 1' in the
  // example above.
  StmtResult TopLevelCase(true);

  // DeepestParsedCaseStmt - This is the deepest statement we have parsed, which
  // gets updated each time a new case is parsed, and whose body is unset so
  // far.  When parsing 'case 4', this is the 'case 3' node.
  Stmt *DeepestParsedCaseStmt = nullptr;

  // While we have case statements, eat and stack them.
  SourceLocation ColonLoc;
  do {
    SourceLocation CaseLoc = MissingCase ? Expr.get()->getExprLoc()
                                         : ConsumeToken(); // eat the 'case'.
    ColonLoc = SourceLocation();

    /// We don't want to treat 'case x : y' as a potential typo for 'case x::y'.
    /// Disable this form of error recovery while we're parsing the case
    /// expression.
    ColonProtectionRAIIObject ColonProtection(*this);

    ExprResult LHS;
    if (!MissingCase) {
      LHS = ParseCaseExpression(CaseLoc);
      if (LHS.isInvalid()) {
        // If constant-expression is parsed unsuccessfully, recover by skipping
        // current case statement (moving to the colon that ends it).
        if (!SkipUntil(tok::colon, tok::r_brace, StopAtSemi | StopBeforeMatch))
          return StmtError();
      }
    } else {
      LHS = Expr;
      MissingCase = false;
    }

    // GNU case range extension.
    SourceLocation DotDotDotLoc;
    ExprResult RHS;
    if (TryConsumeToken(tok::ellipsis, DotDotDotLoc)) {
      Diag(DotDotDotLoc, diag::ext_gnu_case_range);
      RHS = ParseCaseExpression(CaseLoc);
      if (RHS.isInvalid()) {
        if (!SkipUntil(tok::colon, tok::r_brace, StopAtSemi | StopBeforeMatch))
          return StmtError();
      }
    }

    ColonProtection.restore();

    if (TryConsumeToken(tok::colon, ColonLoc)) {
    } else if (TryConsumeToken(tok::semi, ColonLoc)) {
      // Treat "case blah;" as a typo for "case blah:".
      Diag(ColonLoc, diag::err_expected_after)
          << "'case'" << tok::colon
          << FixItHint::CreateReplacement(ColonLoc, ":");
    } else {
      SourceLocation ExpectedLoc = PP.getLocForEndOfToken(PrevTokLocation);
      Diag(ExpectedLoc, diag::err_expected_after)
          << "'case'" << tok::colon
          << FixItHint::CreateInsertion(ExpectedLoc, ":");
      ColonLoc = ExpectedLoc;
    }

    StmtResult Case =
        Actions.OnCaseStmt(CaseLoc, LHS, DotDotDotLoc, RHS, ColonLoc);

    // If we had a sema error parsing this case, then just ignore it and
    // continue parsing the sub-stmt.
    if (Case.isInvalid()) {
      if (TopLevelCase.isInvalid()) // No parsed case stmts.
        return ParseStatement(/*TrailingElseLoc=*/nullptr, StmtCtx);
      // Otherwise, just don't add it as a nested case.
    } else {
      // If this is the first case statement we parsed, it becomes TopLevelCase.
      // Otherwise we link it into the current chain.
      Stmt *NextDeepest = Case.get();
      if (TopLevelCase.isInvalid())
        TopLevelCase = Case;
      else
        Actions.OnCaseStmtBody(DeepestParsedCaseStmt, Case.get());
      DeepestParsedCaseStmt = NextDeepest;
    }

    // Handle all case statements.
  } while (Tok.is(tok::kw_case));

  // If we found a non-case statement, start by parsing it.
  StmtResult SubStmt;

  if (Tok.is(tok::r_brace)) {
    // "switch (X) { case 4: }", is valid and is treated as if label was
    // followed by a null statement.
    WarnTrailingLabel();
    SubStmt = Actions.OnNullStmt(ColonLoc);
  } else {
    SubStmt = ParseStatement(/*TrailingElseLoc=*/nullptr, StmtCtx);
  }

  // Install the body into the most deeply-nested case.
  if (DeepestParsedCaseStmt) {
    // Broken sub-stmt shouldn't prevent forming the case statement properly.
    if (SubStmt.isInvalid())
      SubStmt = Actions.OnNullStmt(SourceLocation());
    warnDeclAfterLabel(*this, SubStmt.get());
    Actions.OnCaseStmtBody(DeepestParsedCaseStmt, SubStmt.get());
  }

  return TopLevelCase;
}

StmtResult Parser::ParseDefaultStatement(ParsedStmtContext StmtCtx) {
  assert(Tok.is(tok::kw_default) && "Not a default stmt!");

  SourceLocation DefaultLoc = ConsumeToken(); // eat the 'default'.

  SourceLocation ColonLoc;
  if (TryConsumeToken(tok::colon, ColonLoc)) {
  } else if (TryConsumeToken(tok::semi, ColonLoc)) {
    // Treat "default;" as a typo for "default:".
    Diag(ColonLoc, diag::err_expected_after)
        << "'default'" << tok::colon
        << FixItHint::CreateReplacement(ColonLoc, ":");
  } else {
    SourceLocation ExpectedLoc = PP.getLocForEndOfToken(PrevTokLocation);
    Diag(ExpectedLoc, diag::err_expected_after)
        << "'default'" << tok::colon
        << FixItHint::CreateInsertion(ExpectedLoc, ":");
    ColonLoc = ExpectedLoc;
  }

  StmtResult SubStmt;

  if (Tok.is(tok::r_brace)) {
    // "switch (X) {... default: }", is valid and is treated as if label was
    // followed by a null statement.
    WarnTrailingLabel();
    SubStmt = Actions.OnNullStmt(ColonLoc);
  } else {
    SubStmt = ParseStatement(/*TrailingElseLoc=*/nullptr, StmtCtx);
  }

  // Broken sub-stmt shouldn't prevent forming the case statement properly.
  if (SubStmt.isInvalid())
    SubStmt = Actions.OnNullStmt(ColonLoc);

  warnDeclAfterLabel(*this, SubStmt.get());
  return Actions.OnDefaultStmt(DefaultLoc, ColonLoc, SubStmt.get(),
                               getCurScope());
}
