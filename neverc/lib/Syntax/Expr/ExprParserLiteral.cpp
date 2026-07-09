#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Std/StdModule.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
using namespace neverc;


// ===----------------------------------------------------------------------===
// Builtin, paren & literal expressions
// ===----------------------------------------------------------------------===

ExprResult Parser::ParseBuiltinPrimaryExpression() {
  ExprResult Res;
  const IdentifierInfo *BuiltinII = Tok.getIdentifierInfo();

  tok::TokenKind T = Tok.getKind();
  SourceLocation StartLoc = ConsumeToken(); // Eat the builtin identifier.

  // All of these start with an open paren.
  if (Tok.isNot(tok::l_paren))
    return ExprError(Diag(Tok, diag::err_expected_after)
                     << BuiltinII << tok::l_paren);

  BalancedDelimiterTracker PT(*this, tok::l_paren);
  PT.consumeOpen();

  switch (T) {
  default:
    return ExprError();
  case tok::kw___builtin_types_compatible_p: {
    TypeResult Ty1 = ParseTypeName();
    if (RequireToken(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
    TypeResult Ty2 = ParseTypeName();
    if (Ty1.isInvalid() || Ty2.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
    PT.consumeClose();
    QualType T1 = Actions.GetTypeFromParser(Ty1.get()).getUnqualifiedType();
    QualType T2 = Actions.GetTypeFromParser(Ty2.get()).getUnqualifiedType();
    bool Compatible = Actions.Context.typesAreCompatible(T1, T2);
    Res = IntegerLiteral::Create(Actions.Context,
                                 llvm::APInt(32, Compatible ? 1 : 0),
                                 Actions.Context.IntTy, StartLoc);
    break;
  }
  case tok::kw___builtin_va_arg: {
    ExprResult Expr(ParseAssignmentExpression());

    if (RequireToken(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      Expr = ExprError();
    }

    TypeResult Ty = ParseTypeName();

    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      Expr = ExprError();
    }

    if (Expr.isInvalid() || Ty.isInvalid())
      Res = ExprError();
    else
      Res = Actions.OnVAArg(StartLoc, Expr.get(), Ty.get(), ConsumeParen());
    break;
  }
  case tok::kw___builtin_offsetof: {
    SourceLocation TypeLoc = Tok.getLocation();
    auto OOK = Sema::OffsetOfKind::OOK_Builtin;
    if (Tok.getLocation().isMacroID()) {
      llvm::StringRef MacroName =
          SourceScanner::getImmediateMacroNameForDiagnostics(
              Tok.getLocation(), PP.getSourceManager(), getLangOpts());
      if (MacroName == "offsetof")
        OOK = Sema::OffsetOfKind::OOK_Macro;
    }
    TypeResult Ty;
    {
      OffsetOfStateRAIIObject InOffsetof(*this, OOK);
      Ty = ParseTypeName();
      if (Ty.isInvalid()) {
        SkipUntil(tok::r_paren, StopAtSemi);
        return ExprError();
      }
    }

    if (RequireToken(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // We must have at least one identifier here.
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected) << tok::identifier;
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // Keep track of the various subcomponents we see.
    llvm::SmallVector<Sema::OffsetOfComponent, 4> Comps;

    Comps.push_back(Sema::OffsetOfComponent());
    Comps.back().isBrackets = false;
    Comps.back().U.IdentInfo = Tok.getIdentifierInfo();
    Comps.back().LocStart = Comps.back().LocEnd = ConsumeToken();

    while (true) {
      if (Tok.is(tok::period)) {
        // offsetof-member-designator: offsetof-member-designator '.' identifier
        Comps.push_back(Sema::OffsetOfComponent());
        Comps.back().isBrackets = false;
        Comps.back().LocStart = ConsumeToken();

        if (Tok.isNot(tok::identifier)) {
          Diag(Tok, diag::err_expected) << tok::identifier;
          SkipUntil(tok::r_paren, StopAtSemi);
          return ExprError();
        }
        Comps.back().U.IdentInfo = Tok.getIdentifierInfo();
        Comps.back().LocEnd = ConsumeToken();
      } else if (Tok.is(tok::l_square)) {
        if (CheckProhibitedBracketAttribute())
          return ExprError();

        // offsetof-member-designator: offsetof-member-design '[' expression ']'
        Comps.push_back(Sema::OffsetOfComponent());
        Comps.back().isBrackets = true;
        BalancedDelimiterTracker ST(*this, tok::l_square);
        ST.consumeOpen();
        Comps.back().LocStart = ST.getOpenLocation();
        Res = ParseExpression();
        if (Res.isInvalid()) {
          SkipUntil(tok::r_paren, StopAtSemi);
          return Res;
        }
        Comps.back().U.E = Res.get();

        ST.consumeClose();
        Comps.back().LocEnd = ST.getCloseLocation();
      } else {
        if (Tok.isNot(tok::r_paren)) {
          PT.consumeClose();
          Res = ExprError();
        } else if (Ty.isInvalid()) {
          Res = ExprError();
        } else {
          PT.consumeClose();
          Res =
              Actions.OnBuiltinOffsetOf(getCurScope(), StartLoc, TypeLoc,
                                        Ty.get(), Comps, PT.getCloseLocation());
        }
        break;
      }
    }
    break;
  }
  case tok::kw___builtin_choose_expr: {
    ExprResult Cond(ParseAssignmentExpression());
    if (Cond.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return Cond;
    }
    if (RequireToken(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    ExprResult Expr1(ParseAssignmentExpression());
    if (Expr1.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return Expr1;
    }
    if (RequireToken(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    ExprResult Expr2(ParseAssignmentExpression());
    if (Expr2.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return Expr2;
    }
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      return ExprError();
    }
    Res = Actions.OnChooseExpr(StartLoc, Cond.get(), Expr1.get(), Expr2.get(),
                               ConsumeParen());
    break;
  }
  case tok::kw___builtin_convertvector: {
    // The first argument is an expression to be converted, followed by a comma.
    ExprResult Expr(ParseAssignmentExpression());
    if (Expr.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    if (RequireToken(tok::comma)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    // Second argument is the type to bitcast to.
    TypeResult DestTy = ParseTypeName();
    if (DestTy.isInvalid())
      return ExprError();

    // Attempt to consume the r-paren.
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    Res = Actions.OnConvertVectorExpr(Expr.get(), DestTy.get(), StartLoc,
                                      ConsumeParen());
    break;
  }
  case tok::kw___builtin_COLUMN:
  case tok::kw___builtin_FILE:
  case tok::kw___builtin_FILE_NAME:
  case tok::kw___builtin_FUNCTION:
  case tok::kw___builtin_FUNCSIG:
  case tok::kw___builtin_LINE: {
    // Attempt to consume the r-paren.
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected) << tok::r_paren;
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
    SourceLocIdentKind Kind = [&] {
      switch (T) {
      case tok::kw___builtin_FILE:
        return SourceLocIdentKind::File;
      case tok::kw___builtin_FILE_NAME:
        return SourceLocIdentKind::FileName;
      case tok::kw___builtin_FUNCTION:
        return SourceLocIdentKind::Function;
      case tok::kw___builtin_FUNCSIG:
        return SourceLocIdentKind::FuncSig;
      case tok::kw___builtin_LINE:
        return SourceLocIdentKind::Line;
      case tok::kw___builtin_COLUMN:
        return SourceLocIdentKind::Column;
      default:
        return SourceLocIdentKind::File;
      }
    }();
    Res = Actions.OnSourceLocExpr(Kind, StartLoc, ConsumeParen());
    break;
  }
  }

  if (Res.isInvalid())
    return ExprError();

  // These can be followed by postfix-expr pieces because they are
  // primary-expressions.
  return ParsePostfixExpressionSuffix(Res.get());
}

ExprResult Parser::ParseParenExpression(ParenParseOption &ExprType,
                                        bool stopIfCastExpr, bool isTypeCast,
                                        ParsedType &CastTy,
                                        SourceLocation &RParenLoc) {
  assert(Tok.is(tok::l_paren) && "Not a paren expr!");
  ColonProtectionRAIIObject ColonProtection(*this, false);
  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen())
    return ExprError();
  SourceLocation OpenLoc = T.getOpenLocation();

  ExprResult Result(true);
  bool isAmbiguousTypeId;
  CastTy = nullptr;

  // None of these cases should fall through with an invalid Result
  // unless they've already reported an error.
  if (ExprType >= CompoundStmt && Tok.is(tok::l_brace)) {
    Diag(Tok, OpenLoc.isMacroID() ? diag::ext_gnu_statement_expr_macro
                                  : diag::ext_gnu_statement_expr);

    checkCompoundToken(OpenLoc, tok::l_paren, CompoundToken::StmtExprBegin);

    if (!getCurScope()->getFnParent()) {
      Result = ExprError(Diag(OpenLoc, diag::err_stmtexpr_file_scope));
    } else {
      // Find the nearest non-record decl context. Variables declared in a
      // statement expression behave as if they were declared in the enclosing
      // function, block, or other code construct.
      DeclContext *CodeDC = Actions.CurContext;
      while (CodeDC->isRecord() || isa<EnumDecl>(CodeDC)) {
        CodeDC = CodeDC->getParent();
        assert(CodeDC && !CodeDC->isFileContext() &&
               "statement expr not in code context");
      }
      Sema::ContextRAII SavedContext(Actions, CodeDC, /*NewThisContext=*/false);

      Actions.OnStartStmtExpr();

      StmtResult Stmt(ParseCompoundStatement(true));
      ExprType = CompoundStmt;

      // If the substmt parsed correctly, build the AST node.
      if (!Stmt.isInvalid()) {
        Result = Actions.OnStmtExpr(getCurScope(), OpenLoc, Stmt.get(),
                                    Tok.getLocation());
      } else {
        Actions.OnStmtExprError();
      }
    }
  } else if (ExprType >= CompoundLiteral &&
             isTypeIdInParens(isAmbiguousTypeId)) {

    // Otherwise, this is a compound literal expression or cast expression.

    // Parse the type declarator.
    DeclSpec DS(AttrFactory);
    ParseSpecifierQualifierList(DS);
    Declarator DeclaratorInfo(DS, ParsedAttributesView::none(),
                              DeclaratorContext::TypeName);
    ParseDeclarator(DeclaratorInfo);

    {
      // Match the ')'.
      T.consumeClose();
      ColonProtection.restore();
      RParenLoc = T.getCloseLocation();
      if (Tok.is(tok::l_brace)) {
        ExprType = CompoundLiteral;
        TypeResult Ty = Actions.OnTypeName(getCurScope(), DeclaratorInfo);
        return ParseCompoundLiteralExpression(Ty.get(), OpenLoc, RParenLoc);
      }

      if (ExprType == CastExpr) {
        // We parsed '(' type-name ')' and the thing after it wasn't a '{'.

        if (DeclaratorInfo.isInvalidType())
          return ExprError();

        // Note that this doesn't parse the subsequent cast-expression, it just
        // returns the parsed type to the callee.
        if (stopIfCastExpr) {
          TypeResult Ty = Actions.OnTypeName(getCurScope(), DeclaratorInfo);
          CastTy = Ty.get();
          return ExprResult();
        }

        // Parse the cast-expression that follows it next.
        Result = ParseCastExpression(/*isUnaryExpression=*/AnyCastExpr,
                                     /*isAddressOfOperand=*/false,
                                     /*isTypeCast=*/IsTypeCast);
        if (!Result.isInvalid()) {
          Result = Actions.OnCastExpr(getCurScope(), OpenLoc, DeclaratorInfo,
                                      CastTy, RParenLoc, Result.get());
        }
        return Result;
      }

      Diag(Tok, diag::err_expected_lbrace_in_compound_literal);
      return ExprError();
    }
  } else if (isTypeCast) {
    // Parse the expression-list.
    ExprVector ArgExprs;

    if (!ParseSimpleExpressionList(ArgExprs)) {
      ExprType = SimpleExpr;
      Result = Actions.OnParenListExpr(OpenLoc, Tok.getLocation(), ArgExprs);
    }
  } else {
    Result = ParseExpression(MaybeTypeCast);
    if (Result.isUsable()) {
      // Correct typos early so implicit-cast-like expressions parse correctly.
      Result = Actions.CorrectDelayedTyposInExpr(Result);
    }

    ExprType = SimpleExpr;

    // Don't build a paren expression unless we actually match a ')'.
    if (!Result.isInvalid() && Tok.is(tok::r_paren))
      Result = Actions.OnParenExpr(OpenLoc, Tok.getLocation(), Result.get());
  }

  // Match the ')'.
  if (Result.isInvalid()) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  T.consumeClose();
  RParenLoc = T.getCloseLocation();
  return Result;
}

ExprResult Parser::ParseCompoundLiteralExpression(ParsedType Ty,
                                                  SourceLocation LParenLoc,
                                                  SourceLocation RParenLoc) {
  assert(Tok.is(tok::l_brace) && "Not a compound literal!");
  if (!getLangOpts().C99) // Compound literals don't exist in C90.
    Diag(LParenLoc, diag::ext_c99_compound_literal);
  ExprResult Result = ParseInitializer();
  if (!Result.isInvalid() && Ty)
    return Actions.OnCompoundLiteral(LParenLoc, Ty, RParenLoc, Result.get());
  return Result;
}

ExprResult Parser::ParseStringLiteralExpression() {
  return ParseStringLiteralExpression(/*Unevaluated=*/false);
}

ExprResult Parser::ParseUnevaluatedStringLiteralExpression() {
  return ParseStringLiteralExpression(/*Unevaluated=*/true);
}

ExprResult Parser::ParseStringLiteralExpression(bool Unevaluated) {
  assert(tokenIsLikeStringLiteral(Tok, getLangOpts()) &&
         "Not a string-literal-like token!");

  // String concatenation.
  // Note: some keywords like __FUNCTION__ are not considered to be strings
  // for concatenation purposes, unless Microsoft extensions are enabled.
  llvm::SmallVector<Token, 4> StringToks;

  do {
    StringToks.push_back(Tok);
    ConsumeAnyToken();
  } while (tokenIsLikeStringLiteral(Tok, getLangOpts()));

  if (Unevaluated)
    return Actions.OnUnevaluatedStringLiteral(StringToks);

  return Actions.OnStringLiteral(StringToks);
}

ExprResult Parser::ParseGenericSelectionExpression() {
  assert(Tok.is(tok::kw__Generic) && "_Generic keyword expected");
  if (!getLangOpts().C11)
    Diag(Tok, diag::ext_c11_feature) << Tok.getName();

  SourceLocation KeyLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.expectAndConsume())
    return ExprError();

  // We either have a controlling expression or we have a controlling type, and
  // we need to figure out which it is.
  TypeResult ControllingType;
  ExprResult ControllingExpr;
  if (isTypeIdForGenericSelection()) {
    ControllingType = ParseTypeName();
    if (ControllingType.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
    const auto *LIT = cast<LocInfoType>(ControllingType.get().get());
    SourceLocation Loc = LIT->getTypeSourceInfo()->getTypeLoc().getBeginLoc();
    Diag(Loc, diag::ext_generic_with_type_arg);
  } else {
    // C11 6.5.1.1p3 "The controlling expression of a generic selection is
    // not evaluated."
    EnterExpressionEvaluationContext Unevaluated(
        Actions, Sema::ExpressionEvaluationContext::Unevaluated);
    ControllingExpr =
        Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression());
    if (ControllingExpr.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
  }

  if (RequireToken(tok::comma)) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return ExprError();
  }

  SourceLocation DefaultLoc;
  llvm::SmallVector<ParsedType, 12> Types;
  ExprVector Exprs;
  do {
    ParsedType Ty;
    if (Tok.is(tok::kw_default)) {
      // C11 6.5.1.1p2 "A generic selection shall have no more than one default
      // generic association."
      if (!DefaultLoc.isInvalid()) {
        Diag(Tok, diag::err_duplicate_default_assoc);
        Diag(DefaultLoc, diag::note_previous_default_assoc);
        SkipUntil(tok::r_paren, StopAtSemi);
        return ExprError();
      }
      DefaultLoc = ConsumeToken();
      Ty = nullptr;
    } else {
      ColonProtectionRAIIObject X(*this);
      TypeResult TR = ParseTypeName(nullptr, DeclaratorContext::Association);
      if (TR.isInvalid()) {
        SkipUntil(tok::r_paren, StopAtSemi);
        return ExprError();
      }
      Ty = TR.get();
    }
    Types.push_back(Ty);

    if (RequireToken(tok::colon)) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }

    ExprResult ER(
        Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression()));
    if (ER.isInvalid()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return ExprError();
    }
    Exprs.push_back(ER.get());
  } while (TryConsumeToken(tok::comma));

  T.consumeClose();
  if (T.getCloseLocation().isInvalid())
    return ExprError();

  void *ExprOrTy = ControllingExpr.isUsable()
                       ? ControllingExpr.get()
                       : ControllingType.get().getAsOpaquePtr();

  return Actions.OnGenericSelectionExpr(
      KeyLoc, DefaultLoc, T.getCloseLocation(), ControllingExpr.isUsable(),
      ExprOrTy, Types, Exprs);
}

bool Parser::ParseExpressionList(llvm::SmallVectorImpl<Expr *> &Exprs,
                                 llvm::function_ref<void()> ExpressionStarts,
                                 bool FailImmediatelyOnInvalidExpr,
                                 bool EarlyTypoCorrection) {
  bool SawError = false;
  while (true) {
    ExprResult Expr;
    Expr = ParseAssignmentExpression();

    if (EarlyTypoCorrection)
      Expr = Actions.CorrectDelayedTyposInExpr(Expr);

    if (Expr.isInvalid()) {
      SawError = true;
      if (FailImmediatelyOnInvalidExpr)
        break;
      SkipUntil(tok::comma, tok::r_paren, StopBeforeMatch);
    } else {
      Exprs.push_back(Expr.get());
    }

    if (Tok.isNot(tok::comma))
      break;
    // Move to the next argument, remember where the comma was.
    ConsumeToken();
  }
  if (SawError) {
    for (auto &E : Exprs) {
      ExprResult Expr = Actions.CorrectDelayedTyposInExpr(E);
      if (Expr.isUsable())
        E = Expr.get();
    }
  }
  return SawError;
}

bool Parser::ParseSimpleExpressionList(llvm::SmallVectorImpl<Expr *> &Exprs) {
  while (true) {
    ExprResult Expr = ParseAssignmentExpression();
    if (Expr.isInvalid())
      return true;

    Exprs.push_back(Expr.get());

    // Stop the list before a `, ...` sequence (not valid in C).
    if (Tok.isNot(tok::comma) || NextToken().is(tok::ellipsis))
      return false;

    ConsumeToken();
  }
}

ExprResult Parser::ParseAvailabilityCheckExpr(SourceLocation BeginLoc) {
  assert(Tok.is(tok::kw___builtin_available));
  Diag(BeginLoc, diag::err_unsupported_builtin) << "__builtin_available";
  ConsumeToken();
  BalancedDelimiterTracker Parens(*this, tok::l_paren);
  if (Parens.expectAndConsume())
    return ExprError();
  Parens.skipToEnd();
  return ExprError();
}

bool Parser::ParseUnqualifiedId(UnqualifiedId &Result) {
  if (Tok.is(tok::identifier)) {
    // Capture identifier BEFORE ConsumeToken() — C++ does not guarantee
    // function-argument evaluation order, so merging these into
    //   setIdentifier(Tok.getIdentifierInfo(), ConsumeToken())
    // lets the optimizer call ConsumeToken() first, advancing Tok and
    // returning nullptr from getIdentifierInfo().  LTO builds on Windows
    // reliably trigger this, producing "no member named ''" for every
    // struct member access.
    IdentifierInfo *II = Tok.getIdentifierInfo();
    SourceLocation Loc = ConsumeToken();
    Result.setIdentifier(II, Loc);
    return false;
  }
  Diag(Tok, diag::err_expected_unqualified_id) << 0;
  return true;
}

bool Parser::isPendingDeclaration(IdentifierInfo *II) {
  return llvm::is_contained(TentativelyDeclaredIdentifiers, II);
}

Parser::BracketAttributeKind
Parser::isBracketAttributeSpecifier(bool Disambiguate) {
  if (Tok.is(tok::kw_alignas))
    return CAK_AttributeSpecifier;
  if (Tok.isRegularKeywordAttribute())
    return CAK_AttributeSpecifier;
  if (Tok.isNot(tok::l_square) || NextToken().isNot(tok::l_square))
    return CAK_NotAttributeSpecifier;
  if (!Disambiguate)
    return CAK_AttributeSpecifier;
  {
    const Token &MaybeUsing = GetLookAheadToken(2);
    if (MaybeUsing.is(tok::identifier) &&
        MaybeUsing.getIdentifierInfo()->isStr("using"))
      return CAK_AttributeSpecifier;
  }
  RevertingTentativeParsingAction PA(*this);
  ConsumeBracket();
  ConsumeBracket();
  bool IsAttribute = SkipUntil(tok::r_square);
  IsAttribute &= Tok.is(tok::r_square);
  return IsAttribute ? CAK_AttributeSpecifier : CAK_InvalidAttributeSpecifier;
}

Parser::LateParsedDeclaration::~LateParsedDeclaration() {}
void Parser::LateParsedDeclaration::ParseLexedAttributes() {}
void Parser::LateParsedDeclaration::ParseLexedPragmas() {}

void Parser::LateParsedAttribute::ParseLexedAttributes() {
  Self->ParseLexedAttribute(*this, true, false);
}
void Parser::ParseLexedAttributeList(LateParsedAttrList &LAs, Decl *D,
                                     bool PushScope, bool OnDefinition) {
  assert(LAs.parseSoon() &&
         "Attribute list should be marked for immediate parsing.");
  for (unsigned i = 0, ni = LAs.size(); i < ni; ++i) {
    if (D)
      LAs[i]->addDecl(D);
    ParseLexedAttribute(*LAs[i], PushScope, OnDefinition);
    delete LAs[i];
  }
  LAs.clear();
}

void Parser::ParseLexedAttribute(LateParsedAttribute &LA, bool PushScope,
                                 bool OnDefinition) {
  Token AttrEnd;
  AttrEnd.startToken();
  AttrEnd.setKind(tok::eof);
  AttrEnd.setLocation(Tok.getLocation());
  AttrEnd.setEofData(LA.Toks.data());
  LA.Toks.push_back(AttrEnd);
  LA.Toks.push_back(Tok);
  PP.PushTokenStream(LA.Toks, true, /*IsReinject=*/true);
  ConsumeAnyToken();

  ParsedAttributes Attrs(AttrFactory);
  if (LA.Decls.size() > 0) {
    Decl *D = LA.Decls[0];
    if (LA.Decls.size() == 1) {
      bool HasFunScope = PushScope && D->getAsFunction();
      if (HasFunScope)
        Actions.OnReenterFunctionContext(Actions.CurScope, D);
      ParseGNUAttributeArgs(&LA.AttrName, LA.AttrNameLoc, Attrs, nullptr,
                            nullptr, SourceLocation(), ParsedAttr::Form::GNU(),
                            nullptr);
      if (HasFunScope)
        Actions.OnExitFunctionContext();
    } else {
      ParseGNUAttributeArgs(&LA.AttrName, LA.AttrNameLoc, Attrs, nullptr,
                            nullptr, SourceLocation(), ParsedAttr::Form::GNU(),
                            nullptr);
    }
  } else {
    Diag(Tok, diag::warn_attribute_no_decl) << LA.AttrName.getName();
  }

  if (OnDefinition && !Attrs.empty() && Attrs.begin()->isGNUAttribute() &&
      Attrs.begin()->isKnownToGCC())
    Diag(Tok, diag::warn_attribute_on_function_definition) << &LA.AttrName;

  for (unsigned i = 0, ni = LA.Decls.size(); i < ni; ++i)
    Actions.OnFinishDelayedAttribute(getCurScope(), LA.Decls[i], Attrs);

  while (Tok.isNot(tok::eof))
    ConsumeAnyToken();
  if (Tok.is(tok::eof) && Tok.getEofData() == AttrEnd.getEofData())
    ConsumeAnyToken();
}

bool Parser::CacheTokensUntil(tok::TokenKind T1, tok::TokenKind T2,
                              CachedTokens &Toks, bool StopAtSemi,
                              bool ConsumeFinalToken) {
  bool isFirstTokenConsumed = true;
  while (true) {
    if (Tok.is(T1) || Tok.is(T2)) {
      if (ConsumeFinalToken) {
        Toks.push_back(Tok);
        ConsumeAnyToken();
      }
      return true;
    }
    switch (Tok.getKind()) {
    case tok::eof:
      return false;
    case tok::l_paren:
      Toks.push_back(Tok);
      ConsumeParen();
      CacheTokensUntil(tok::r_paren, Toks, /*StopAtSemi=*/false);
      break;
    case tok::l_square:
      Toks.push_back(Tok);
      ConsumeBracket();
      CacheTokensUntil(tok::r_square, Toks, /*StopAtSemi=*/false);
      break;
    case tok::l_brace:
      Toks.push_back(Tok);
      ConsumeBrace();
      CacheTokensUntil(tok::r_brace, Toks, /*StopAtSemi=*/false);
      break;
    case tok::r_paren:
      if (ParenCount && !isFirstTokenConsumed)
        return false;
      Toks.push_back(Tok);
      ConsumeParen();
      break;
    case tok::r_square:
      if (BracketCount && !isFirstTokenConsumed)
        return false;
      Toks.push_back(Tok);
      ConsumeBracket();
      break;
    case tok::r_brace:
      if (BraceCount && !isFirstTokenConsumed)
        return false;
      Toks.push_back(Tok);
      ConsumeBrace();
      break;
    case tok::semi:
      if (StopAtSemi)
        return false;
      [[fallthrough]];
    default:
      Toks.push_back(Tok);
      ConsumeAnyToken();
      break;
    }
    isFirstTokenConsumed = false;
  }
}

bool Parser::CacheFunctionPrologue(CachedTokens &Toks) {
  CacheTokensUntil(tok::l_brace, tok::r_brace, Toks, true, false);
  if (Tok.isNot(tok::l_brace))
    return Diag(Tok.getLocation(), diag::err_expected) << tok::l_brace;
  Toks.push_back(Tok);
  ConsumeBrace();
  return false;
}

bool Parser::ConsumeAndStoreInitializer(CachedTokens &Toks,
                                        CachedInitKind CIK) {
  while (true) {
    switch (Tok.getKind()) {
    case tok::comma:
      return true;
    case tok::semi:
      if (CIK == CIK_DefaultInitializer)
        return true;
      Toks.push_back(Tok);
      ConsumeToken();
      break;
    case tok::r_paren:
    case tok::r_square:
      if (CIK == CIK_DefaultArgument)
        return true;
      Toks.push_back(Tok);
      ConsumeAnyToken();
      break;
    case tok::l_paren:
      Toks.push_back(Tok);
      ConsumeParen();
      if (!CacheTokensUntil(tok::r_paren, Toks, true))
        return true;
      break;
    case tok::l_square:
      Toks.push_back(Tok);
      ConsumeBracket();
      if (!CacheTokensUntil(tok::r_square, Toks, true))
        return true;
      break;
    case tok::l_brace:
      Toks.push_back(Tok);
      ConsumeBrace();
      if (!CacheTokensUntil(tok::r_brace, Toks, true))
        return true;
      break;
    case tok::eof:
      return true;
    default:
      Toks.push_back(Tok);
      ConsumeAnyToken();
      break;
    }
  }
}
