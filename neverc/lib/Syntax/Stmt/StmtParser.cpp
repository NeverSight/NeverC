#include "neverc/Foundation/Core/PrettyStackTrace.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Tree/Decl/PrettyDeclStackTrace.h"
#include "llvm/ADT/STLExtras.h"

using namespace neverc;

// ===----------------------------------------------------------------------===
// Statement parsing entry
// ===----------------------------------------------------------------------===

__attribute__((hot)) StmtResult Parser::ParseStatement(
    SourceLocation *TrailingElseLoc, ParsedStmtContext StmtCtx) {
  StmtResult Res;
  StmtVector Stmts;
  do {
    Res = ParseStatementOrDeclaration(Stmts, StmtCtx, TrailingElseLoc);
  } while (!Res.isInvalid() && !Res.get());

  return Res;
}

__attribute__((hot)) StmtResult Parser::ParseStatementOrDeclaration(
    StmtVector &Stmts, ParsedStmtContext StmtCtx,
    SourceLocation *TrailingElseLoc) {

  ParenBraceBracketBalancer BalancerRAIIObj(*this);

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

__attribute__((hot)) StmtResult
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

__attribute__((hot)) StmtResult
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
