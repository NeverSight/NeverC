#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Lookup.h"
#include "neverc/Foundation/Attr/AttributeCommonInfo.h"
#include "neverc/Foundation/Attr/Attributes.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/PrettyDeclStackTrace.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include <optional>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Special type specifiers & static_assert
// ===----------------------------------------------------------------------===

void Parser::ParseTypeofSpecifier(DeclSpec &DS) {
  assert(Tok.isOneOf(tok::kw_typeof, tok::kw_typeof_unqual) &&
         "Not a typeof specifier");

  bool IsUnqual = Tok.is(tok::kw_typeof_unqual);
  const IdentifierInfo *II = Tok.getIdentifierInfo();
  if (getLangOpts().C23 && !II->getName().starts_with("__"))
    Diag(Tok.getLocation(), diag::warn_c23_compat_keyword) << Tok.getName();

  Token OpTok = Tok;
  SourceLocation StartLoc = ConsumeToken();
  bool HasParens = Tok.is(tok::l_paren);

  EnterExpressionEvaluationContext Unevaluated(
      Actions, Sema::ExpressionEvaluationContext::Unevaluated);

  bool isCastExpr;
  ParsedType CastTy;
  SourceRange CastRange;
  ExprResult Operand = Actions.CorrectDelayedTyposInExpr(
      ParseExprAfterUnaryExprOrTypeTrait(OpTok, isCastExpr, CastTy, CastRange));
  if (HasParens)
    DS.setTypeArgumentRange(CastRange);

  if (CastRange.getEnd().isInvalid())
    DS.SetRangeEnd(Tok.getLocation());
  else
    DS.SetRangeEnd(CastRange.getEnd());

  if (isCastExpr) {
    if (!CastTy) {
      DS.SetTypeSpecError();
      return;
    }

    const char *PrevSpec = nullptr;
    unsigned DiagID;

    if (DS.SetTypeSpecType(IsUnqual ? DeclSpec::TST_typeof_unqualType
                                    : DeclSpec::TST_typeofType,
                           StartLoc, PrevSpec, DiagID, CastTy,
                           Actions.getTreeContext().getPrintingPolicy()))
      Diag(StartLoc, DiagID) << PrevSpec;
    return;
  }

  // If we get here, the operand to the typeof was an expression.
  if (Operand.isInvalid()) {
    DS.SetTypeSpecError();
    return;
  }

  // We might need to transform the operand if it is potentially evaluated.
  Operand = Actions.ResolveExprEvaluationContextForTypeof(Operand.get());
  if (Operand.isInvalid()) {
    DS.SetTypeSpecError();
    return;
  }

  const char *PrevSpec = nullptr;
  unsigned DiagID;
  if (DS.SetTypeSpecType(IsUnqual ? DeclSpec::TST_typeof_unqualExpr
                                  : DeclSpec::TST_typeofExpr,
                         StartLoc, PrevSpec, DiagID, Operand.get(),
                         Actions.getTreeContext().getPrintingPolicy()))
    Diag(StartLoc, DiagID) << PrevSpec;
}

void Parser::ParseAtomicSpecifier(DeclSpec &DS) {
  assert(Tok.is(tok::kw__Atomic) && NextToken().is(tok::l_paren) &&
         "Not an atomic specifier");

  SourceLocation StartLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen())
    return;

  TypeResult Result = ParseTypeName();
  if (Result.isInvalid()) {
    SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch);
    SourceLocation EndLoc = Tok.getLocation();
    if (Tok.is(tok::r_paren)) {
      DS.SetRangeEnd(EndLoc);
      ConsumeParen();
    }
    Diag(EndLoc, diag::warn_missing_type_specifier)
        << SourceRange(StartLoc, EndLoc);
    DS.SetTypeSpecError();
    return;
  }

  // Match the ')'
  T.consumeClose();

  if (T.getCloseLocation().isInvalid())
    return;

  DS.setTypeArgumentRange(T.getRange());
  DS.SetRangeEnd(T.getCloseLocation());

  const char *PrevSpec = nullptr;
  unsigned DiagID;
  if (DS.SetTypeSpecType(DeclSpec::TST_atomic, StartLoc, PrevSpec, DiagID,
                         Result.get(),
                         Actions.getTreeContext().getPrintingPolicy()))
    Diag(StartLoc, DiagID) << PrevSpec;
}

void Parser::DiagnoseBitIntUse(const Token &Tok) {
  // If the token is for _ExtInt, diagnose it as being deprecated. Otherwise,
  // the token is about _BitInt and gets (potentially) diagnosed as use of an
  // extension.
  assert(Tok.isOneOf(tok::kw__ExtInt, tok::kw__BitInt) &&
         "expected either an _ExtInt or _BitInt token!");

  SourceLocation Loc = Tok.getLocation();
  if (Tok.is(tok::kw__ExtInt)) {
    Diag(Loc, diag::warn_ext_int_deprecated) << FixItHint::CreateReplacement(
        Loc, tok::getKeywordSpelling(tok::kw__BitInt));
  } else {
    if (getLangOpts().C23)
      Diag(Loc, diag::warn_c23_compat_keyword) << Tok.getName();
    else
      Diag(Loc, diag::ext_bit_int);
  }
}

namespace {
FixItHint buildStaticAssertMessageFix(const Expr *AssertExpr,
                                      SourceLocation EndExprLoc) {
  if (const auto *BO = dyn_cast_or_null<BinaryOperator>(AssertExpr)) {
    if (BO->getOpcode() == BO_LAnd &&
        isa<StringLiteral>(BO->getRHS()->IgnoreImpCasts()))
      return FixItHint::CreateReplacement(BO->getOperatorLoc(), ",");
  }
  return FixItHint::CreateInsertion(EndExprLoc, ", \"\"");
}
} // namespace

Decl *Parser::ParseStaticAssertDeclaration(SourceLocation &DeclEnd) {
  assert(Tok.isOneOf(tok::kw_static_assert, tok::kw__Static_assert) &&
         "Not a static_assert declaration");

  const char *TokName = Tok.getName();

  if (Tok.is(tok::kw__Static_assert) && !getLangOpts().C11)
    Diag(Tok, diag::ext_c11_feature) << Tok.getName();
  if (Tok.is(tok::kw_static_assert)) {
    if (getLangOpts().C23)
      Diag(Tok, diag::warn_c23_compat_keyword) << Tok.getName();
    else
      Diag(Tok, diag::ext_ms_static_assert) << FixItHint::CreateReplacement(
          Tok.getLocation(), tok::getKeywordSpelling(tok::kw__Static_assert));
  }

  SourceLocation StaticAssertLoc = ConsumeToken();

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen()) {
    Diag(Tok, diag::err_expected) << tok::l_paren;
    SkipBrokenDecl();
    return nullptr;
  }

  EnterExpressionEvaluationContext ConstantEvaluated(
      Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
  ExprResult AssertExpr(ParseConstantExpressionInExprEvalContext());
  if (AssertExpr.isInvalid()) {
    SkipBrokenDecl();
    return nullptr;
  }

  ExprResult AssertMessage;
  if (Tok.is(tok::r_paren)) {
    unsigned DiagVal;
    if (getLangOpts().C23)
      DiagVal = diag::warn_c17_compat_static_assert_no_message;
    else
      DiagVal = diag::ext_c_static_assert_no_message;
    Diag(Tok, DiagVal) << buildStaticAssertMessageFix(AssertExpr.get(),
                                                      Tok.getLocation());
  } else {
    if (RequireToken(tok::comma)) {
      SkipUntil(tok::semi);
      return nullptr;
    }

    if (tokenIsLikeStringLiteral(Tok, getLangOpts()))
      AssertMessage = ParseUnevaluatedStringLiteralExpression();
    else {
      Diag(Tok, diag::err_expected_string_literal)
          << /*Source='static_assert'*/ 1;
      SkipBrokenDecl();
      return nullptr;
    }

    if (AssertMessage.isInvalid()) {
      SkipBrokenDecl();
      return nullptr;
    }
  }

  T.consumeClose();
  DeclEnd = Tok.getLocation();
  RequireSemicolon(diag::err_expected_semi_after_static_assert, TokName);

  return Actions.OnStaticAssertDeclaration(StaticAssertLoc, AssertExpr.get(),
                                           AssertMessage.get(),
                                           T.getCloseLocation());
}

bool Parser::isValidAfterTypeSpecifier(bool CouldBeBitfield) {
  switch (Tok.getKind()) {
  default:
    if (Tok.isRegularKeywordAttribute())
      return true;
    break;
  case tok::semi:
  case tok::star:
  case tok::amp:
  case tok::ampamp:
  case tok::identifier:
  case tok::r_paren:
  case tok::annot_typename:
  case tok::l_paren:
  case tok::comma:
  case tok::kw___declspec:
  case tok::l_square:
  case tok::ellipsis:
  case tok::kw___attribute:
  case tok::annot_pragma_pack:
  case tok::annot_pragma_ms_pragma:
    return true;
  case tok::colon:
    return CouldBeBitfield || ColonIsSacred;
  case tok::kw___cdecl:
  case tok::kw_cdecl:
  case tok::kw___fastcall:
  case tok::kw___stdcall:
  case tok::kw___vectorcall:
    return getLangOpts().MicrosoftExt;
  case tok::kw_const:
  case tok::kw_volatile:
  case tok::kw_restrict:
  case tok::kw__Atomic:
  case tok::kw___unaligned:
  case tok::kw_inline:
  case tok::kw_static:
  case tok::kw_extern:
  case tok::kw_typedef:
  case tok::kw_register:
  case tok::kw_auto:
  case tok::kw_thread_local:
  case tok::kw_constexpr:
    if (!isKnownToBeTypeSpecifier(NextToken()))
      return true;
    break;
  case tok::r_brace:
    return true;
  case tok::greater:
    return false;
  }
  return false;
}

// ===----------------------------------------------------------------------===
// Struct, union & enum
// ===----------------------------------------------------------------------===

void Parser::ParseStructOrUnionSpecifier(tok::TokenKind TagTokKind,
                                         SourceLocation StartLoc, DeclSpec &DS,
                                         AccessSpecifier AS,
                                         DeclSpecContext DSC,
                                         ParsedAttributes &Attributes) {
  DeclSpec::TST TagType;
  if (TagTokKind == tok::kw_struct)
    TagType = DeclSpec::TST_struct;
  else {
    assert(TagTokKind == tok::kw_union && "Not a struct/union specifier");
    TagType = DeclSpec::TST_union;
  }

  ParsedAttributes attrs(AttrFactory);
  MaybeParseAttributes(PAKM_Bracket | PAKM_Declspec | PAKM_GNU, attrs);

  SourceLocation AttrFixitLoc = Tok.getLocation();

  struct PreserveAtomicIdentifierInfoRAII {
    PreserveAtomicIdentifierInfoRAII(Token &Tok, bool Enabled)
        : AtomicII(nullptr) {
      if (!Enabled)
        return;
      assert(Tok.is(tok::kw__Atomic));
      AtomicII = Tok.getIdentifierInfo();
      AtomicII->revertTokenIDToIdentifier();
      Tok.setKind(tok::identifier);
    }
    ~PreserveAtomicIdentifierInfoRAII() {
      if (!AtomicII)
        return;
      AtomicII->revertIdentifierToTokenID(tok::kw__Atomic);
    }
    IdentifierInfo *AtomicII;
  };

  bool ShouldChangeAtomicToIdentifier = getLangOpts().MSVCCompat &&
                                        Tok.is(tok::kw__Atomic) &&
                                        TagType == DeclSpec::TST_struct;
  PreserveAtomicIdentifierInfoRAII AtomicTokenGuard(
      Tok, ShouldChangeAtomicToIdentifier);

  IdentifierInfo *Name = nullptr;
  SourceLocation NameLoc;
  if (Tok.is(tok::identifier)) {
    Name = Tok.getIdentifierInfo();
    NameLoc = ConsumeToken();
  }

  MaybeParseBracketAttributes(Attributes);

  const PrintingPolicy &Policy{Actions.getTreeContext().getPrintingPolicy()};
  Sema::TagUseKind TUK;
  if (isDefiningTypeSpecifierContext(DSC) == AllowDefiningTypeSpec::No)
    TUK = Sema::TUK_Reference;
  else if (Tok.is(tok::l_brace)) {
    TUK = Sema::TUK_Definition;
  } else if (!isTypeSpecifier(DSC) &&
             (Tok.is(tok::semi) ||
              (Tok.isAtStartOfLine() && !isValidAfterTypeSpecifier(false)))) {
    TUK = Sema::TUK_Declaration;
    if (Tok.isNot(tok::semi)) {
      const PrintingPolicy &PPol{Actions.getTreeContext().getPrintingPolicy()};
      RequireToken(tok::semi, diag::err_expected_after,
                   DeclSpec::getSpecifierName(TagType, PPol));
      PP.InjectToken(Tok, /*IsReinject*/ true);
      Tok.setKind(tok::semi);
    }
  } else
    TUK = Sema::TUK_Reference;

  if (TUK != Sema::TUK_Reference) {
    SourceRange AttrRange = Attributes.Range;
    if (AttrRange.isValid()) {
      auto *FirstAttr = Attributes.empty() ? nullptr : &Attributes.front();
      auto Loc = AttrRange.getBegin();
      (FirstAttr && FirstAttr->isRegularKeywordAttribute()
           ? Diag(Loc, diag::err_keyword_not_allowed) << FirstAttr
           : Diag(Loc, diag::err_attributes_not_allowed))
          << AttrRange
          << FixItHint::CreateInsertionFromRange(
                 AttrFixitLoc, CharSourceRange(AttrRange, true))
          << FixItHint::CreateRemoval(AttrRange);
      attrs.takeAllFrom(Attributes);
    }
  }

  if (!Name && (DS.getTypeSpecType() == DeclSpec::TST_error ||
                TUK != Sema::TUK_Definition)) {
    if (DS.getTypeSpecType() != DeclSpec::TST_error)
      Diag(StartLoc, diag::err_anon_type_definition)
          << DeclSpec::getSpecifierName(TagType, Policy);
    SkipUntil(tok::comma, StopAtSemi);
    return;
  }

  DeclResult TagOrTempResult = true;
  TypeResult TypeResult = true;
  bool Owned = false;
  Sema::SkipBodyInfo SkipBody;

  {
    if (TUK != Sema::TUK_Declaration && TUK != Sema::TUK_Definition)
      ProhibitBracketAttributes(attrs, diag::err_attributes_not_allowed,
                                diag::err_keyword_not_allowed,
                                /* DiagnoseEmptyAttrs=*/true);

    stripTypeAttributesOffDeclSpec(attrs, DS, TUK);

    TagOrTempResult = Actions.OnTag(
        getCurScope(), TagType, TUK, StartLoc, Name, NameLoc, attrs, AS, Owned,
        neverc::TypeResult(), OffsetOfState, &SkipBody);
  }

  if (TUK == Sema::TUK_Definition) {
    assert(Tok.is(tok::l_brace));
    if (SkipBody.ShouldSkip)
      SkipBraceEnclosedBody();
    else {
      Decl *D =
          SkipBody.CheckSameAsPrevious ? SkipBody.New : TagOrTempResult.get();
      ParseStructUnionBody(StartLoc, TagType, cast<RecordDecl>(D));
      if (SkipBody.CheckSameAsPrevious) {
        DS.SetTypeSpecError();
        return;
      }
    }
  }

  if (!TagOrTempResult.isInvalid())
    Actions.ProcessDeclAttributeDelayed(TagOrTempResult.get(), attrs);

  const char *PrevSpec = nullptr;
  unsigned DiagID;
  bool Result;
  if (!TypeResult.isInvalid()) {
    Result = DS.SetTypeSpecType(DeclSpec::TST_typename, StartLoc,
                                NameLoc.isValid() ? NameLoc : StartLoc,
                                PrevSpec, DiagID, TypeResult.get(), Policy);
  } else if (!TagOrTempResult.isInvalid()) {
    Result = DS.SetTypeSpecType(
        TagType, StartLoc, NameLoc.isValid() ? NameLoc : StartLoc, PrevSpec,
        DiagID, TagOrTempResult.get(), Owned, Policy);
  } else {
    DS.SetTypeSpecError();
    return;
  }

  if (Result)
    Diag(StartLoc, DiagID) << PrevSpec;

  if (TUK == Sema::TUK_Definition && !isTypeSpecifier(DSC) &&
      !isValidAfterTypeSpecifier(false)) {
    if (Tok.isNot(tok::semi)) {
      const PrintingPolicy &PPol{Actions.getTreeContext().getPrintingPolicy()};
      RequireToken(tok::semi, diag::err_expected_after,
                   DeclSpec::getSpecifierName(TagType, PPol));
      PP.InjectToken(Tok, /*IsReinject=*/true);
      Tok.setKind(tok::semi);
    }
  }
}

void Parser::SkipBraceEnclosedBody() {
  assert(Tok.is(tok::l_brace));
  BalancedDelimiterTracker T(*this, tok::l_brace);
  T.consumeOpen();
  T.skipToEnd();

  if (Tok.is(tok::kw___attribute)) {
    ParsedAttributes Attrs(AttrFactory);
    MaybeParseGNUAttributes(Attrs);
  }
}

IdentifierInfo *
Parser::TryParseBracketAttributeIdentifier(SourceLocation &Loc) {
  switch (Tok.getKind()) {
  default:
    if (!Tok.isAnnotation()) {
      if (IdentifierInfo *II = Tok.getIdentifierInfo()) {
        Loc = ConsumeToken();
        return II;
      }
    }
    return nullptr;

  case tok::numeric_constant: {
    if (Tok.getLocation().isMacroID()) {
      llvm::SmallString<8> ExpansionBuf;
      SourceLocation ExpansionLoc =
          PP.getSourceManager().getExpansionLoc(Tok.getLocation());
      llvm::StringRef Spelling = PP.getSpelling(ExpansionLoc, ExpansionBuf);
      if (Spelling == "__neverc__") {
        SourceRange TokRange(
            ExpansionLoc,
            PP.getSourceManager().getExpansionLoc(Tok.getEndLoc()));
        Diag(Tok, diag::warn_wrong_attr_namespace)
            << FixItHint::CreateReplacement(TokRange, "_NeverC");
        Loc = ConsumeToken();
        return &PP.getIdentifierTable().get("_NeverC");
      }
    }
    return nullptr;
  }

  case tok::ampamp:
  case tok::pipe:
  case tok::pipepipe:
  case tok::caret:
  case tok::tilde:
  case tok::amp:
  case tok::ampequal:
  case tok::pipeequal:
  case tok::caretequal:
  case tok::exclaim:
  case tok::exclaimequal:
    llvm::SmallString<8> SpellingBuf;
    SourceLocation SpellingLoc =
        PP.getSourceManager().getSpellingLoc(Tok.getLocation());
    llvm::StringRef Spelling = PP.getSpelling(SpellingLoc, SpellingBuf);
    if (isLetter(Spelling[0])) {
      Loc = ConsumeToken();
      return &PP.getIdentifierTable().get(Spelling);
    }
    return nullptr;
  }
}
