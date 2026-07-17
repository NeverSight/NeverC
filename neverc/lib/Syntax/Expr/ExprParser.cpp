#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Foundation/Std/StdModule.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/ParserPluginHooks.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// Expression parsing — assignment & binary operators
// ===----------------------------------------------------------------------===

namespace {
bool isNeverCStringMethodName(const UnqualifiedId &Name) {
  IdentifierInfo *II = Name.getIdentifierInfo();
  return II && BuiltinString::isMethodName(II->getName());
}
} // namespace

NEVERC_HOT ExprResult
Parser::ParseExpression(TypeCastState isTypeCast) {
  ExprResult LHS(ParseAssignmentExpression(isTypeCast));
  return ParseRHSOfBinaryExpression(LHS, prec::Comma);
}

ExprResult Parser::ParseExpressionWithLeadingExtension(SourceLocation ExtLoc) {
  ExprResult LHS(true);
  {
    ExtensionRAIIObject O(Diags);

    LHS = ParseCastExpression(AnyCastExpr);
  }

  if (!LHS.isInvalid())
    LHS = Actions.OnUnaryOp(getCurScope(), ExtLoc, tok::kw___extension__,
                            LHS.get());

  return ParseRHSOfBinaryExpression(LHS, prec::Comma);
}

ExprResult Parser::ParseAssignmentExpression(TypeCastState isTypeCast) {
  ExprResult LHS =
      ParseCastExpression(AnyCastExpr,
                          /*isAddressOfOperand=*/false, isTypeCast);
  return ParseRHSOfBinaryExpression(LHS, prec::Assignment);
}

ExprResult
Parser::ParseConstantExpressionInExprEvalContext(TypeCastState isTypeCast) {
  assert(Actions.ExprEvalContexts.back().Context ==
             Sema::ExpressionEvaluationContext::ConstantEvaluated &&
         "Call this function only if your ExpressionEvaluationContext is "
         "already ConstantEvaluated");
  ExprResult LHS(ParseCastExpression(AnyCastExpr, false, isTypeCast));
  ExprResult Res(ParseRHSOfBinaryExpression(LHS, prec::Conditional));
  return Actions.OnConstantExpression(Res);
}

ExprResult Parser::ParseConstantExpression() {
  EnterExpressionEvaluationContext ConstantEvaluated(
      Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  return ParseConstantExpressionInExprEvalContext(NotTypeCast);
}

ExprResult Parser::ParseCaseExpression(SourceLocation CaseLoc) {
  EnterExpressionEvaluationContext ConstantEvaluated(
      Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  ExprResult LHS(ParseCastExpression(AnyCastExpr, false, NotTypeCast));
  ExprResult Res(ParseRHSOfBinaryExpression(LHS, prec::Conditional));
  return Actions.OnCaseExpr(CaseLoc, Res);
}

bool Parser::isNotExpressionStart() {
  const tok::TokenKind K = Tok.getKind();
  switch (K) {
  case tok::l_brace:
  case tok::r_brace:
  case tok::kw_for:
  case tok::kw_while:
  case tok::kw_if:
  case tok::kw_else:
  case tok::kw_goto:
    return true;
  default:
    return isKnownToBeDeclarationSpecifier();
  }
}

NEVERC_HOT ExprResult
Parser::ParseRHSOfBinaryExpression(ExprResult LHS, prec::Level MinPrec) {
  prec::Level NextTokPrec = getBinOpPrecedence(Tok.getKind());
  SourceLocation ColonLoc;

  while (true) {
    if (NextTokPrec < MinPrec)
      return LHS;

    Token OpToken = Tok;
    ConsumeToken();

    if (LLVM_UNLIKELY(OpToken.is(tok::comma) && isNotExpressionStart())) {
      PP.InjectToken(Tok, /*IsReinject*/ true);
      Tok = OpToken;
      return LHS;
    }

    ExprResult TernaryMiddle(true);
    if (LLVM_UNLIKELY(NextTokPrec == prec::Conditional)) {
      if (Tok.isNot(tok::colon)) {
        ColonProtectionRAIIObject X(*this);
        TernaryMiddle = ParseExpression();
      } else {
        TernaryMiddle = nullptr;
        Diag(Tok, diag::ext_gnu_conditional_expr);
      }

      if (TernaryMiddle.isInvalid()) {
        Actions.CorrectDelayedTyposInExpr(LHS);
        LHS = ExprError();
        TernaryMiddle = nullptr;
      }

      if (!TryConsumeToken(tok::colon, ColonLoc)) {
        SourceLocation FILoc = Tok.getLocation();
        const char *FIText = ": ";
        const SourceManager &SM = PP.getSourceManager();
        if (FILoc.isFileID() || PP.isAtStartOfMacroExpansion(FILoc, &FILoc)) {
          assert(FILoc.isFileID());
          bool IsInvalid = false;
          const char *SourcePtr =
              SM.getCharacterData(FILoc.getLocWithOffset(-1), &IsInvalid);
          if (!IsInvalid && *SourcePtr == ' ') {
            SourcePtr =
                SM.getCharacterData(FILoc.getLocWithOffset(-2), &IsInvalid);
            if (!IsInvalid && *SourcePtr == ' ') {
              FILoc = FILoc.getLocWithOffset(-1);
              FIText = ":";
            }
          }
        }

        Diag(Tok, diag::err_expected)
            << tok::colon << FixItHint::CreateInsertion(FILoc, FIText);
        Diag(OpToken, diag::note_matching) << tok::question;
        ColonLoc = Tok.getLocation();
      }
    }

    ExprResult RHS;
    bool RHSIsInitList = false;
    RHS = ParseCastExpression(AnyCastExpr);

    if (RHS.isInvalid()) {
      Actions.CorrectDelayedTyposInExpr(LHS);
      if (TernaryMiddle.isUsable())
        TernaryMiddle = Actions.CorrectDelayedTyposInExpr(TernaryMiddle);
      LHS = ExprError();
    }

    prec::Level ThisPrec = NextTokPrec;
    NextTokPrec = getBinOpPrecedence(Tok.getKind());

    bool isRightAssoc =
        ThisPrec == prec::Conditional || ThisPrec == prec::Assignment;

    if (ThisPrec < NextTokPrec || (ThisPrec == NextTokPrec && isRightAssoc)) {
      if (!RHS.isInvalid() && RHSIsInitList) {
        Diag(Tok, diag::err_init_list_bin_op)
            << /*LHS*/ 0 << PP.getSpelling(Tok)
            << Actions.getExprRange(RHS.get());
        RHS = ExprError();
      }
      RHS = ParseRHSOfBinaryExpression(
          RHS, static_cast<prec::Level>(ThisPrec + !isRightAssoc));
      RHSIsInitList = false;

      if (RHS.isInvalid()) {
        Actions.CorrectDelayedTyposInExpr(LHS);
        if (TernaryMiddle.isUsable())
          TernaryMiddle = Actions.CorrectDelayedTyposInExpr(TernaryMiddle);
        LHS = ExprError();
      }

      NextTokPrec = getBinOpPrecedence(Tok.getKind());
    }

    if (!RHS.isInvalid() && RHSIsInitList) {
      if (ThisPrec == prec::Assignment) {
      } else if (ColonLoc.isValid()) {
        Diag(ColonLoc, diag::err_init_list_bin_op)
            << /*RHS*/ 1 << ":" << Actions.getExprRange(RHS.get());
        LHS = ExprError();
      } else {
        Diag(OpToken, diag::err_init_list_bin_op)
            << /*RHS*/ 1 << PP.getSpelling(OpToken)
            << Actions.getExprRange(RHS.get());
        LHS = ExprError();
      }
    }

    ExprResult OrigLHS = LHS;
    if (!LHS.isInvalid()) {
      if (TernaryMiddle.isInvalid()) {
        ExprResult BinOp =
            Actions.OnBinOp(getCurScope(), OpToken.getLocation(),
                            OpToken.getKind(), LHS.get(), RHS.get());
        if (BinOp.isInvalid())
          BinOp = Actions.CreateRecoveryExpr(LHS.get()->getBeginLoc(),
                                             RHS.get()->getEndLoc(),
                                             {LHS.get(), RHS.get()});

        LHS = BinOp;
      } else {
        ExprResult CondOp =
            Actions.OnConditionalOp(OpToken.getLocation(), ColonLoc, LHS.get(),
                                    TernaryMiddle.get(), RHS.get());
        if (CondOp.isInvalid()) {
          std::vector<neverc::Expr *> Args;
          // TernaryMiddle can be null for the GNU conditional expr extension.
          if (TernaryMiddle.get())
            Args = {LHS.get(), TernaryMiddle.get(), RHS.get()};
          else
            Args = {LHS.get(), RHS.get()};
          CondOp = Actions.CreateRecoveryExpr(LHS.get()->getBeginLoc(),
                                              RHS.get()->getEndLoc(), Args);
        }

        LHS = CondOp;
      }
      continue;
    }

    if (LHS.isInvalid()) {
      Actions.CorrectDelayedTyposInExpr(OrigLHS);
      Actions.CorrectDelayedTyposInExpr(TernaryMiddle);
      Actions.CorrectDelayedTyposInExpr(RHS);
    }
  }
}

NEVERC_HOT ExprResult Parser::ParseCastExpression(
    CastParseKind ParseKind, bool isAddressOfOperand, TypeCastState isTypeCast,
    bool isVectorLiteral, bool *NotPrimaryExpression) {
  bool NotCastExpr;
  ExprResult Res =
      ParseCastExpression(ParseKind, isAddressOfOperand, NotCastExpr,
                          isTypeCast, isVectorLiteral, NotPrimaryExpression);
  if (NotCastExpr)
    Diag(Tok, diag::err_expected_expression);
  return Res;
}

namespace {
class CastExpressionIdValidator final : public CorrectionCandidateCallback {
public:
  CastExpressionIdValidator(Token Next, bool AllowTypes, bool AllowNonTypes)
      : NextToken(Next), AllowNonTypes(AllowNonTypes) {
    WantTypeSpecifiers = WantFunctionLikeCasts = AllowTypes;
  }

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    NamedDecl *ND = candidate.getCorrectionDecl();
    if (!ND)
      return candidate.isKeyword();

    if (isa<TypeDecl>(ND))
      return WantTypeSpecifiers;

    if (!AllowNonTypes ||
        !CorrectionCandidateCallback::ValidateCandidate(candidate))
      return false;

    if (!NextToken.isOneOf(tok::equal, tok::arrow, tok::period))
      return true;

    for (auto *C : candidate) {
      NamedDecl *ND = C->getUnderlyingDecl();
      if (isa<ValueDecl>(ND) && !isa<FunctionDecl>(ND))
        return true;
    }
    return false;
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<CastExpressionIdValidator>(*this);
  }

private:
  Token NextToken;
  bool AllowNonTypes;
};
} // namespace

NEVERC_HOT NEVERC_FLATTEN ExprResult
Parser::ParseCastExpression(CastParseKind ParseKind, bool isAddressOfOperand,
                            bool &NotCastExpr, TypeCastState isTypeCast,
                            bool isVectorLiteral, bool *NotPrimaryExpression) {
  ExprResult Res;
  NotCastExpr = false;

  if (PluginHooks && ParseKind == AnyCastExpr) {
    Expr *Extension = nullptr;
    ParserPluginOutcome Outcome =
        PluginHooks->parseExpression(*this, Extension);
    if (Outcome == ParserPluginOutcome::Error) {
      // Cursor is restored on failure; advance once to avoid retry loops.
      if (!Tok.is(tok::eof))
        ConsumeAnyToken();
      return ExprError();
    }
    if (Outcome == ParserPluginOutcome::Handled) {
      Res = Extension;
      if (!isPostfixExpressionSuffixStart())
        return Res;
      return ParsePostfixExpressionSuffix(Res);
    }

    if (Tok.is(tok::identifier)) {
      Outcome = PluginHooks->parseKeyword(*this, Extension);
      if (Outcome == ParserPluginOutcome::Error) {
        if (!Tok.is(tok::eof))
          ConsumeAnyToken();
        return ExprError();
      }
      if (Outcome == ParserPluginOutcome::Handled) {
        Res = Extension;
        if (!isPostfixExpressionSuffixStart())
          return Res;
        return ParsePostfixExpressionSuffix(Res);
      }
    }
  }

  tok::TokenKind SavedKind = Tok.getKind();
  if (LLVM_LIKELY(SavedKind == tok::numeric_constant)) {
    Res = Actions.OnNumericConstant(Tok);
    ConsumeToken();
    if (LLVM_LIKELY(!isPostfixExpressionSuffixStart()))
      return Res;
    return ParsePostfixExpressionSuffix(Res);
  }

  if (LLVM_LIKELY(SavedKind == tok::identifier) &&
      LLVM_LIKELY(ParseKind == AnyCastExpr)) {
    IdentifierInfo &II = *Tok.getIdentifierInfo();
    SourceLocation ILoc = ConsumeToken();

    if (isAddressOfOperand && isPostfixExpressionSuffixStart())
      isAddressOfOperand = false;

    UnqualifiedId Name;
    Token Replacement;
    Name.setIdentifier(&II, ILoc);
    Res = Actions.OnIdExpression(getCurScope(), Name, Tok.is(tok::l_paren),
                                 isAddressOfOperand, nullptr,
                                 /*IsInlineAsmIdentifier=*/false,
                                 Tok.is(tok::r_paren) ? nullptr : &Replacement);
    if (!Res.isInvalid() && Res.isUnset()) {
      UnconsumeToken(Replacement);
      return ParseCastExpression(
          ParseKind, isAddressOfOperand, NotCastExpr, isTypeCast,
          /*isVectorLiteral=*/false, NotPrimaryExpression);
    }
    if (LLVM_LIKELY(!isPostfixExpressionSuffixStart()))
      return Res;
    return ParsePostfixExpressionSuffix(Res);
  }

  bool AllowSuffix = true;

  switch (SavedKind) {
  case tok::l_paren: {
    ParenParseOption ParenExprType;
    switch (ParseKind) {
    case CastParseKind::UnaryExprOnly:
      [[fallthrough]];
    case CastParseKind::AnyCastExpr:
      ParenExprType = ParenParseOption::CastExpr;
      break;
    case CastParseKind::PrimaryExprOnly:
      ParenExprType = SimpleExpr;
      break;
    }
    ParsedType CastTy;
    SourceLocation RParenLoc;
    Res = ParseParenExpression(ParenExprType, false /*stopIfCastExr*/,
                               isTypeCast == IsTypeCast, CastTy, RParenLoc);

    if (isVectorLiteral)
      return Res;

    switch (ParenExprType) {
    case SimpleExpr:
      break;
    case CompoundStmt:
      break;
    case CompoundLiteral:
      break;
    case CastExpr:
      return Res;
    }

    break;
  }

    // primary-expression
  case tok::numeric_constant:
    llvm_unreachable("handled by fast path above");

  case tok::kw_true:
  case tok::kw_false: {
    tok::TokenKind BoolKind = Tok.getKind();
    Res = Actions.OnBoolLiteral(ConsumeToken(), BoolKind);
    break;
  }

  case tok::kw_nullptr:
    Diag(Tok, getLangOpts().C23 ? diag::warn_c23_compat_keyword
                                : diag::ext_c_nullptr)
        << Tok.getName();

    Res = Actions.OnNullPtrLiteral(ConsumeToken());
    break;

  case tok::identifier: { // primary-expression: identifier
                          // unqualified-id: identifier
    // constant: enumeration-constant

    IdentifierInfo &II = *Tok.getIdentifierInfo();
    SourceLocation ILoc = ConsumeToken();

    if (isAddressOfOperand && isPostfixExpressionSuffixStart())
      isAddressOfOperand = false;

    UnqualifiedId Name;
    Token Replacement;
    Name.setIdentifier(&II, ILoc);
    Res = Actions.OnIdExpression(getCurScope(), Name, Tok.is(tok::l_paren),
                                 isAddressOfOperand, nullptr,
                                 /*IsInlineAsmIdentifier=*/false,
                                 Tok.is(tok::r_paren) ? nullptr : &Replacement);
    if (!Res.isInvalid() && Res.isUnset()) {
      UnconsumeToken(Replacement);
      return ParseCastExpression(
          ParseKind, isAddressOfOperand, NotCastExpr, isTypeCast,
          /*isVectorLiteral=*/false, NotPrimaryExpression);
    }
    break;
  }
  case tok::char_constant: // constant: character-constant
  case tok::wide_char_constant:
  case tok::utf8_char_constant:
  case tok::utf16_char_constant:
  case tok::utf32_char_constant:
    Res = Actions.OnCharacterConstant(Tok);
    ConsumeToken();
    break;
  case tok::kw___func__:      // primary-expression: __func__ [C99 6.4.2.2]
  case tok::kw___FUNCTION__:  // primary-expression: __FUNCTION__ [GNU]
  case tok::kw___FUNCDNAME__: // primary-expression: __FUNCDNAME__ [MS]
  case tok::kw___FUNCSIG__:   // primary-expression: __FUNCSIG__ [MS]
  case tok::kw_L__FUNCTION__: // primary-expression: L__FUNCTION__ [MS]
  case tok::kw_L__FUNCSIG__:  // primary-expression: L__FUNCSIG__ [MS]
  case tok::kw___PRETTY_FUNCTION__: // primary-expression: __P..Y_F..N__ [GNU]
    // Function local predefined macros are represented by PredefinedExpr except
    // when Microsoft extensions are enabled and one of these macros is adjacent
    // to a string literal or another one of these macros.
    if (!(getLangOpts().MicrosoftExt &&
          tokenIsLikeStringLiteral(Tok, getLangOpts()) &&
          tokenIsLikeStringLiteral(NextToken(), getLangOpts()))) {
      Res = Actions.OnPredefinedExpr(Tok.getLocation(), SavedKind);
      ConsumeToken();
      break;
    }
    [[fallthrough]]; // treat MS function local macros as concatenable strings
  case tok::string_literal: // primary-expression: string-literal
  case tok::wide_string_literal:
  case tok::utf8_string_literal:
  case tok::utf16_string_literal:
  case tok::utf32_string_literal:
    Res = ParseStringLiteralExpression();
    break;
  case tok::kw__Generic: // primary-expression: generic-selection [C11 6.5.1]
    Res = ParseGenericSelectionExpression();
    break;
  case tok::kw___builtin_available:
    Res = ParseAvailabilityCheckExpr(Tok.getLocation());
    break;
  case tok::kw___builtin_types_compatible_p:
  case tok::kw___builtin_va_arg:
  case tok::kw___builtin_offsetof:
  case tok::kw___builtin_choose_expr:
  case tok::kw___builtin_convertvector:
  case tok::kw___builtin_COLUMN:
  case tok::kw___builtin_FILE:
  case tok::kw___builtin_FILE_NAME:
  case tok::kw___builtin_FUNCTION:
  case tok::kw___builtin_FUNCSIG:
  case tok::kw___builtin_LINE:
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    // This parses the complete suffix; we can return early.
    return ParseBuiltinPrimaryExpression();

  case tok::plusplus:     // unary-expression: '++' unary-expression [C99]
  case tok::minusminus: { // unary-expression: '--' unary-expression [C99]
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    // C99 §6.5.3: prefix ++/-- apply to unary-expression; we use
    // cast-expression.
    Token SavedTok = Tok;
    ConsumeToken();

    // One special case is implicitly handled here: if the preceding tokens are
    // an ambiguous cast expression, such as "(T())++", then we recurse to
    // determine whether the '++' is prefix or postfix.
    Res = ParseCastExpression(AnyCastExpr, /*isAddressOfOperand*/ false,
                              NotCastExpr, NotTypeCast);
    if (NotCastExpr) {
      // If we return with NotCastExpr = true, we must not consume any tokens,
      // so put the token back where we found it.
      assert(Res.isInvalid());
      UnconsumeToken(SavedTok);
      return ExprError();
    }
    if (!Res.isInvalid()) {
      Expr *Arg = Res.get();
      Res = Actions.OnUnaryOp(getCurScope(), SavedTok.getLocation(), SavedKind,
                              Arg);
      if (Res.isInvalid())
        Res = Actions.CreateRecoveryExpr(SavedTok.getLocation(),
                                         Arg->getEndLoc(), Arg);
    }
    return Res;
  }
  case tok::amp: { // unary-expression: '&' cast-expression
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    // Address-of expression
    SourceLocation SavedLoc = ConsumeToken();
    Res = ParseCastExpression(AnyCastExpr, /*isAddressOfOperand=*/true);
    if (!Res.isInvalid()) {
      Expr *Arg = Res.get();
      Res = Actions.OnUnaryOp(getCurScope(), SavedLoc, SavedKind, Arg);
      if (Res.isInvalid())
        Res = Actions.CreateRecoveryExpr(Tok.getLocation(), Arg->getEndLoc(),
                                         Arg);
    }
    return Res;
  }

  case tok::star:        // unary-expression: '*' cast-expression
  case tok::plus:        // unary-expression: '+' cast-expression
  case tok::minus:       // unary-expression: '-' cast-expression
  case tok::tilde:       // unary-expression: '~' cast-expression
  case tok::exclaim:     // unary-expression: '!' cast-expression
  case tok::kw___real:   // unary-expression: '__real' cast-expression [GNU]
  case tok::kw___imag: { // unary-expression: '__imag' cast-expression [GNU]
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    SourceLocation SavedLoc = ConsumeToken();
    Res = ParseCastExpression(AnyCastExpr);
    if (!Res.isInvalid()) {
      Expr *Arg = Res.get();
      Res = Actions.OnUnaryOp(getCurScope(), SavedLoc, SavedKind, Arg,
                              isAddressOfOperand);
      if (Res.isInvalid())
        Res = Actions.CreateRecoveryExpr(SavedLoc, Arg->getEndLoc(), Arg);
    }
    return Res;
  }

  case tok::kw___extension__: { // unary-expression:'__extension__' cast-expr
                                // [GNU]
    // __extension__ silences extension warnings in the subexpression.
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    ExtensionRAIIObject O(Diags); // Use RAII to do this.
    SourceLocation SavedLoc = ConsumeToken();
    Res = ParseCastExpression(AnyCastExpr);
    if (!Res.isInvalid())
      Res = Actions.OnUnaryOp(getCurScope(), SavedLoc, SavedKind, Res.get());
    return Res;
  }
  case tok::kw__Alignof: // unary-expression: '_Alignof' '(' type-name ')'
    if (!getLangOpts().C11)
      Diag(Tok, diag::ext_c11_feature) << Tok.getName();
    [[fallthrough]];
  case tok::kw_alignof:   // unary-expression: 'alignof' '(' type-id ')'
  case tok::kw___alignof: // unary-expression: '__alignof' unary-expression
                          // unary-expression: '__alignof' '(' type-name ')'
  case tok::kw_sizeof:    // unary-expression: 'sizeof' unary-expression
                          // unary-expression: 'sizeof' '(' type-name ')'
  case tok::kw___builtin_vectorelements:
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    AllowSuffix = false;
    Res = ParseUnaryExprOrTypeTraitExpression();
    break;
  case tok::ampamp: { // unary-expression: '&&' identifier
    if (NotPrimaryExpression)
      *NotPrimaryExpression = true;
    SourceLocation AmpAmpLoc = ConsumeToken();
    if (Tok.isNot(tok::identifier))
      return ExprError(Diag(Tok, diag::err_expected) << tok::identifier);

    if (getCurScope()->getFnParent() == nullptr)
      return ExprError(Diag(Tok, diag::err_address_of_label_outside_fn));

    Diag(AmpAmpLoc, diag::ext_gnu_address_of_label);
    LabelDecl *LD =
        Actions.LookupOrCreateLabel(Tok.getIdentifierInfo(), Tok.getLocation());
    Res = Actions.OnAddrLabel(AmpAmpLoc, Tok.getLocation(), LD);
    ConsumeToken();
    AllowSuffix = false;
    break;
  }

  case tok::l_square:
    [[fallthrough]];
  default:
    NotCastExpr = true;
    return ExprError();
  }

  // Check to see whether Res is a function designator only.

  if (ParseKind == PrimaryExprOnly)
    // This is strictly a primary-expression - no postfix-expr pieces should be
    // parsed.
    return Res;

  if (!AllowSuffix) {
    if (Res.isInvalid())
      return Res;

    switch (Tok.getKind()) {
    case tok::l_square:
    case tok::l_paren:
    case tok::plusplus:
    case tok::minusminus:
      // "expected ';'" or similar is probably the right diagnostic here. Let
      // the caller decide what to do.
      if (Tok.isAtStartOfLine())
        return Res;

      [[fallthrough]];
    case tok::period:
    case tok::arrow:
      break;

    default:
      return Res;
    }

    // This was a unary-expression for which a postfix-expression suffix is
    // not permitted by the grammar (eg, a sizeof expression).
    // Diagnose but parse the suffix anyway.
    Diag(Tok.getLocation(), diag::err_postfix_after_unary_requires_parens)
        << Tok.getKind() << Res.get()->getSourceRange()
        << FixItHint::CreateInsertion(Res.get()->getBeginLoc(), "(")
        << FixItHint::CreateInsertion(PP.getLocForEndOfToken(PrevTokLocation),
                                      ")");
  }

  // These can be followed by postfix-expr pieces.
  Res = ParsePostfixExpressionSuffix(Res);
  return Res;
}

NEVERC_HOT
// ===----------------------------------------------------------------------===
// Postfix, unary & sizeof expressions
// ===----------------------------------------------------------------------===

ExprResult Parser::ParsePostfixExpressionSuffix(ExprResult LHS) {
  // Now that the primary-expression piece of the postfix-expression has been
  // parsed, see if there are any postfix-expression pieces here.
  SourceLocation Loc;
  while (true) {
    switch (Tok.getKind()) {
    case tok::identifier:
      // Stray identifier after an expression — not a postfix operator here.
      [[fallthrough]];

    default: // Not a postfix-expression suffix.
      return LHS;
    case tok::l_square: { // postfix-expression: p-e '[' expression ']'
      // '[[' is reserved for attributes.
      if (CheckProhibitedBracketAttribute()) {
        (void)Actions.CorrectDelayedTyposInExpr(LHS);
        return ExprError();
      }
      BalancedDelimiterTracker T(*this, tok::l_square);
      T.consumeOpen();
      Loc = T.getOpenLocation();
      ExprVector ArgExprs;
      bool HasError = false;
      {
        ExprResult Idx = ParseExpression();
        LHS = Actions.CorrectDelayedTyposInExpr(LHS);
        Idx = Actions.CorrectDelayedTyposInExpr(Idx);
        if (Idx.isInvalid()) {
          HasError = true;
        } else {
          ArgExprs.push_back(Idx.get());
        }
      }

      SourceLocation RLoc = Tok.getLocation();
      LHS = Actions.CorrectDelayedTyposInExpr(LHS);

      if (!LHS.isInvalid() && !HasError && Tok.is(tok::r_square)) {
        LHS = Actions.OnArraySubscriptExpr(getCurScope(), LHS.get(), Loc,
                                           ArgExprs, RLoc);
      } else {
        LHS = ExprError();
      }

      // Match the ']'.
      T.consumeClose();
      break;
    }

    case tok::l_paren: { // p-e: p-e '(' argument-expression-list[opt] ')'
      BalancedDelimiterTracker PT(*this, tok::l_paren);

      {
        PT.consumeOpen();
        Loc = PT.getOpenLocation();
      }

      ExprVector ArgExprs;
      if (!LHS.isInvalid()) {
        if (Tok.isNot(tok::r_paren)) {
          if (ParseExpressionList(ArgExprs)) {
            (void)Actions.CorrectDelayedTyposInExpr(LHS);
            LHS = ExprError();
          } else if (LHS.isInvalid()) {
            for (auto &E : ArgExprs)
              Actions.CorrectDelayedTyposInExpr(E);
          }
        }
      }

      // Match the ')'.
      if (LHS.isInvalid()) {
        SkipUntil(tok::r_paren, StopAtSemi);
      } else if (Tok.isNot(tok::r_paren)) {
        bool HadDelayedTypo = false;
        if (Actions.CorrectDelayedTyposInExpr(LHS).get() != LHS.get())
          HadDelayedTypo = true;
        for (auto &E : ArgExprs)
          if (Actions.CorrectDelayedTyposInExpr(E).get() != E)
            HadDelayedTypo = true;
        // If there were delayed typos in the LHS or ArgExprs, call SkipUntil
        // instead of PT.consumeClose() to avoid emitting extra diagnostics for
        // the unmatched l_paren.
        if (HadDelayedTypo)
          SkipUntil(tok::r_paren, StopAtSemi);
        else
          PT.consumeClose();
        LHS = ExprError();
      } else {
        Expr *Fn = LHS.get();
        SourceLocation RParLoc = Tok.getLocation();
        LHS = Actions.OnCallExpr(getCurScope(), Fn, Loc, ArgExprs, RParLoc);
        const bool IsMSCompat =
            getLangOpts().MicrosoftExt || getLangOpts().MSVCCompat;
        if (LHS.isInvalid() &&
            (!IsMSCompat || Fn->getStmtClass() != Expr::ParenExprClass)) {
          ArgExprs.insert(ArgExprs.begin(), Fn);
          LHS =
              Actions.CreateRecoveryExpr(Fn->getBeginLoc(), RParLoc, ArgExprs);
        }
        PT.consumeClose();
      }

      break;
    }
    case tok::arrow:
    case tok::period: {
      // postfix-expression: p-e '->' identifier
      // postfix-expression: p-e '.' identifier
      tok::TokenKind OpKind = Tok.getKind();
      SourceLocation OpLoc = ConsumeToken(); // Eat the "." or "->" token.

      Expr *OrigLHS = !LHS.isInvalid() ? LHS.get() : nullptr;

      UnqualifiedId Name;
      if (ParseUnqualifiedId(Name)) {
        (void)Actions.CorrectDelayedTyposInExpr(LHS);
        LHS = ExprError();
      }

      if (!LHS.isInvalid() && OpKind == tok::period && Tok.is(tok::l_paren) &&
          isNeverCStringMethodName(Name) &&
          Actions.isNeverCStringOrLiteral(LHS.get())) {
        BalancedDelimiterTracker PT(*this, tok::l_paren);
        PT.consumeOpen();
        SourceLocation LParenLoc = PT.getOpenLocation();

        ExprVector ArgExprs;
        if (Tok.isNot(tok::r_paren) && ParseExpressionList(ArgExprs)) {
          (void)Actions.CorrectDelayedTyposInExpr(LHS);
          LHS = ExprError();
        }

        if (LHS.isInvalid()) {
          SkipUntil(tok::r_paren, StopAtSemi);
        } else if (Tok.isNot(tok::r_paren)) {
          bool HadDelayedTypo = false;
          if (Actions.CorrectDelayedTyposInExpr(LHS).get() != LHS.get())
            HadDelayedTypo = true;
          for (auto &E : ArgExprs)
            if (Actions.CorrectDelayedTyposInExpr(E).get() != E)
              HadDelayedTypo = true;
          if (HadDelayedTypo)
            SkipUntil(tok::r_paren, StopAtSemi);
          else
            PT.consumeClose();
          LHS = ExprError();
        } else {
          SourceLocation RParenLoc = Tok.getLocation();
          LHS = Actions.OnBuiltinStringMethodCall(
              getCurScope(), LHS.get(), OpLoc, OpKind, Name, LParenLoc,
              ArgExprs, RParenLoc);
          PT.consumeClose();
        }
        break;
      }

      if (!LHS.isInvalid() && OpKind == tok::period && Tok.is(tok::l_paren) &&
          Name.getIdentifierInfo() &&
          Actions.isStdModuleDotSyntax(LHS.get(),
                                       Name.getIdentifierInfo())) {
        BalancedDelimiterTracker PT(*this, tok::l_paren);
        PT.consumeOpen();
        SourceLocation LParenLoc = PT.getOpenLocation();

        ExprVector ArgExprs;
        if (Tok.isNot(tok::r_paren) && ParseExpressionList(ArgExprs)) {
          (void)Actions.CorrectDelayedTyposInExpr(LHS);
          LHS = ExprError();
        }

        if (LHS.isInvalid()) {
          SkipUntil(tok::r_paren, StopAtSemi);
        } else if (Tok.isNot(tok::r_paren)) {
          PT.consumeClose();
          LHS = ExprError();
        } else {
          SourceLocation RParenLoc = Tok.getLocation();
          LHS = Actions.OnStdModuleMethodCall(
              getCurScope(), LHS.get(), OpLoc, Name, LParenLoc, ArgExprs,
              RParenLoc);
          PT.consumeClose();
        }
        break;
      }

      if (!LHS.isInvalid())
        LHS = Actions.OnMemberAccessExpr(getCurScope(), LHS.get(), OpLoc,
                                         OpKind, Name);
      if (LHS.isInvalid() && OrigLHS && Name.isValid()) {
        // Preserve the LHS if the RHS is an invalid member.
        LHS = Actions.CreateRecoveryExpr(OrigLHS->getBeginLoc(),
                                         Name.getEndLoc(), {OrigLHS});
      }
      break;
    }
    case tok::plusplus:   // postfix-expression: postfix-expression '++'
    case tok::minusminus: // postfix-expression: postfix-expression '--'
      if (!LHS.isInvalid()) {
        Expr *Arg = LHS.get();
        LHS = Actions.OnPostfixUnaryOp(getCurScope(), Tok.getLocation(),
                                       Tok.getKind(), Arg);
        if (LHS.isInvalid())
          LHS = Actions.CreateRecoveryExpr(Arg->getBeginLoc(),
                                           Tok.getLocation(), Arg);
      }
      ConsumeToken();
      break;
    }
  }
}

ExprResult Parser::ParseExprAfterUnaryExprOrTypeTrait(const Token &OpTok,
                                                      bool &isCastExpr,
                                                      ParsedType &CastTy,
                                                      SourceRange &CastRange) {

  assert(OpTok.isOneOf(tok::kw_typeof, tok::kw_typeof_unqual, tok::kw_sizeof,
                       tok::kw___alignof, tok::kw_alignof, tok::kw__Alignof,
                       tok::kw___builtin_vectorelements) &&
         "Not a typeof/sizeof/alignof expression!");

  ExprResult Operand;

  // If the operand doesn't start with an '(', it must be an expression.
  if (Tok.isNot(tok::l_paren)) {
    // If construct allows a form without parenthesis, user may forget to put
    // pathenthesis around type name.
    if (OpTok.isOneOf(tok::kw_sizeof, tok::kw___alignof, tok::kw_alignof,
                      tok::kw__Alignof)) {
      if (isTypeIdUnambiguously()) {
        DeclSpec DS(AttrFactory);
        ParseSpecifierQualifierList(DS);
        Declarator DeclaratorInfo(DS, ParsedAttributesView::none(),
                                  DeclaratorContext::TypeName);
        ParseDeclarator(DeclaratorInfo);

        SourceLocation LParenLoc = PP.getLocForEndOfToken(OpTok.getLocation());
        SourceLocation RParenLoc = PP.getLocForEndOfToken(PrevTokLocation);
        if (LParenLoc.isInvalid() || RParenLoc.isInvalid()) {
          Diag(OpTok.getLocation(),
               diag::err_expected_parentheses_around_typename)
              << OpTok.getName();
        } else {
          Diag(LParenLoc, diag::err_expected_parentheses_around_typename)
              << OpTok.getName() << FixItHint::CreateInsertion(LParenLoc, "(")
              << FixItHint::CreateInsertion(RParenLoc, ")");
        }
        isCastExpr = true;
        return ExprEmpty();
      }
    }

    isCastExpr = false;
    if (OpTok.isOneOf(tok::kw_typeof, tok::kw_typeof_unqual)) {
      Diag(Tok, diag::err_expected_after)
          << OpTok.getIdentifierInfo() << tok::l_paren;
      return ExprError();
    }

    Operand = ParseCastExpression(UnaryExprOnly);
  } else {
    // If it starts with a '(', we know that it is either a parenthesized
    // type-name, or it is a unary-expression that starts with a compound
    // literal, or starts with a primary-expression that is a parenthesized
    // expression.
    ParenParseOption ExprType = CastExpr;
    SourceLocation LParenLoc = Tok.getLocation(), RParenLoc;

    Operand = ParseParenExpression(ExprType, true /*stopIfCastExpr*/, false,
                                   CastTy, RParenLoc);
    CastRange = SourceRange(LParenLoc, RParenLoc);

    // If ParseParenExpression parsed a '(typename)' sequence only, then this is
    // a type.
    if (ExprType == CastExpr) {
      isCastExpr = true;
      return ExprEmpty();
    }

    if (!OpTok.isOneOf(tok::kw_typeof, tok::kw_typeof_unqual)) {
      // GNU typeof in C requires the expression to be parenthesized. Not so for
      // sizeof/alignof. Therefore, the parenthesized expression is
      // the start of a unary-expression, but doesn't include any postfix
      // pieces. Parse these now if present.
      if (!Operand.isInvalid())
        Operand = ParsePostfixExpressionSuffix(Operand.get());
    }
  }

  // If we get here, the operand to the typeof/sizeof/alignof was an expression.
  isCastExpr = false;
  return Operand;
}

ExprResult Parser::ParseUnaryExprOrTypeTraitExpression() {
  assert(Tok.isOneOf(tok::kw_sizeof, tok::kw___alignof, tok::kw_alignof,
                     tok::kw__Alignof, tok::kw___builtin_vectorelements) &&
         "Not a sizeof/alignof expression!");
  Token OpTok = Tok;
  ConsumeToken();

  if (getLangOpts().C23 && OpTok.is(tok::kw_alignof))
    Diag(OpTok, diag::warn_c23_compat_keyword) << OpTok.getName();

  EnterExpressionEvaluationContext Unevaluated(
      Actions, Sema::ExpressionEvaluationContext::Unevaluated);

  bool isCastExpr;
  ParsedType CastTy;
  SourceRange CastRange;
  ExprResult Operand =
      ParseExprAfterUnaryExprOrTypeTrait(OpTok, isCastExpr, CastTy, CastRange);

  UnaryExprOrTypeTrait ExprKind = UETT_SizeOf;
  switch (OpTok.getKind()) {
  case tok::kw_alignof:
  case tok::kw__Alignof:
    ExprKind = UETT_AlignOf;
    break;
  case tok::kw___alignof:
    ExprKind = UETT_PreferredAlignOf;
    break;

  case tok::kw___builtin_vectorelements:
    ExprKind = UETT_VectorElements;
    break;
  default:
    break;
  }

  if (isCastExpr)
    return Actions.OnUnaryExprOrTypeTraitExpr(
        OpTok.getLocation(), ExprKind,
        /*IsType=*/true, CastTy.getAsOpaquePtr(), CastRange);

  if (OpTok.isOneOf(tok::kw_alignof, tok::kw__Alignof))
    Diag(OpTok, diag::ext_alignof_expr) << OpTok.getIdentifierInfo();

  // If we get here, the operand to the sizeof/alignof was an expression.
  if (!Operand.isInvalid())
    Operand = Actions.OnUnaryExprOrTypeTraitExpr(OpTok.getLocation(), ExprKind,
                                                 /*IsType=*/false,
                                                 Operand.get(), CastRange);
  return Operand;
}
