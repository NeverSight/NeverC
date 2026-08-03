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
// Declaration parsing
// ===----------------------------------------------------------------------===

Parser::DeclGroupPtrTy Parser::ParseDeclaration(DeclaratorContext Context,
                                                SourceLocation &DeclEnd,
                                                ParsedAttributes &DeclAttrs,
                                                ParsedAttributes &DeclSpecAttrs,
                                                SourceLocation *DeclSpecStart) {
  ParenBraceBracketBalancer BalancerRAIIObj(*this);

  Decl *SingleDecl = nullptr;
  switch (Tok.getKind()) {
  case tok::kw_inline:
    return ParseSimpleDeclaration(Context, DeclEnd, DeclAttrs, DeclSpecAttrs,
                                  true, DeclSpecStart);

  case tok::kw_module:
      return ParseModuleDecl(ConsumeToken());
    case tok::kw_import:
      return ParseModuleDecl(ConsumeToken());
    case tok::kw_template: {
      if (!getLangOpts().CPlusPlus) {
        Diag(Tok, diag::err_expected_type);
        ConsumeToken();
        return nullptr;
      }
      SourceLocation TemplateLoc = ConsumeToken();
      return ParseTemplateDeclaration(TemplateLoc);
    }
    case tok::kw_namespace:
    if (!getLangOpts().CPlusPlus) {
      Diag(Tok, diag::err_expected_external_declaration);
      ConsumeToken();
      return nullptr;
    }
    ProhibitAttributes(DeclSpecAttrs);
    SingleDecl = ParseNamespace(DeclEnd, DeclAttrs);
    break;

  case tok::kw_using:
    if (!getLangOpts().CPlusPlus) {
      Diag(Tok, diag::err_expected_external_declaration);
      ConsumeToken();
      return nullptr;
    }
    ProhibitAttributes(DeclSpecAttrs);
    SingleDecl = ParseUsingDirectiveOrDeclaration(DeclEnd, DeclAttrs);
    break;

  case tok::kw_static_assert:
  case tok::kw__Static_assert:
    ProhibitAttributes(DeclAttrs);
    ProhibitAttributes(DeclSpecAttrs);
    SingleDecl = ParseStaticAssertDeclaration(DeclEnd);
    break;
  default:
    return ParseSimpleDeclaration(Context, DeclEnd, DeclAttrs, DeclSpecAttrs,
                                  true, DeclSpecStart);
  }

  return Actions.WrapDeclAsGroup(SingleDecl);
}

Parser::DeclGroupPtrTy Parser::ParseSimpleDeclaration(
    DeclaratorContext Context, SourceLocation &DeclEnd,
    ParsedAttributes &DeclAttrs, ParsedAttributes &DeclSpecAttrs,
    bool RequireSemi, SourceLocation *DeclSpecStart) {
  ParsedAttributesView OriginalDeclSpecAttrs;
  OriginalDeclSpecAttrs.addAll(DeclSpecAttrs.begin(), DeclSpecAttrs.end());
  OriginalDeclSpecAttrs.Range = DeclSpecAttrs.Range;

  ParsingDeclSpec DS(*this);
  DS.takeAttributesFrom(DeclSpecAttrs);

  DeclSpecContext DSContext = getDeclSpecContextFromDeclaratorContext(Context);
  ParseDeclarationSpecifiers(DS, AS_none, DSContext);

  if (DS.hasTagDefinition() &&
      DiagnoseMissingSemiAfterTagDefinition(DS, AS_none, DSContext))
    return nullptr;

  // C99 6.7.2.3p6: Handle "struct-or-union identifier;", "enum { X };"
  // declaration-specifiers init-declarator-list[opt] ';'
  if (Tok.is(tok::semi)) {
    ProhibitAttributes(DeclAttrs);
    DeclEnd = Tok.getLocation();
    if (RequireSemi)
      ConsumeToken();
    RecordDecl *AnonRecord = nullptr;
    Decl *TheDecl = Actions.ParsedFreeStandingDeclSpec(
        getCurScope(), AS_none, DS, ParsedAttributesView::none(), AnonRecord);
    DS.complete(TheDecl);
    if (AnonRecord) {
      Decl *decls[] = {AnonRecord, TheDecl};
      return Actions.FormDeclaratorGroup(decls);
    }
    return Actions.WrapDeclAsGroup(TheDecl);
  }

  if (DeclSpecStart)
    DS.SetRangeStart(*DeclSpecStart);

  return ParseDeclGroup(DS, Context, DeclAttrs, &DeclEnd);
}

bool Parser::CouldBeDeclarator(DeclaratorContext Context) {
  switch (Tok.getKind()) {
  case tok::ellipsis:
  case tok::kw___attribute:
  case tok::l_paren:
  case tok::star:
    return true;

  case tok::amp:
  case tok::ampamp:
    // C++ reference declarators.
    return getLangOpts().CPlusPlus;

  case tok::l_square: // Might be an attribute on an unnamed bit-field.
    return false;

  case tok::colon: // Might be a typo for '::' or an unnamed bit-field.
    return Context == DeclaratorContext::Member;

  case tok::identifier:
    switch (NextToken().getKind()) {
    case tok::comma:
    case tok::equal:
    case tok::equalequal: // Might be a typo for '='.
    case tok::kw_alignas:
    case tok::kw_asm:
    case tok::kw___attribute:
    case tok::l_brace:
    case tok::l_paren:
    case tok::l_square:
    case tok::less:
    case tok::r_brace:
    case tok::r_paren:
    case tok::r_square:
    case tok::semi:
      return true;

    case tok::colon:
      // ':' after an identifier: label at block scope; bit-field in struct
      // member list.
      return Context == DeclaratorContext::Member;

    case tok::identifier:
      return false;

    default:
      return Tok.isRegularKeywordAttribute();
    }

  default:
    return Tok.isRegularKeywordAttribute();
  }
}

void Parser::SkipBrokenDecl() {
  while (true) {
    switch (Tok.getKind()) {
    case tok::l_brace:
      // Skip until matching }, then stop. We've probably skipped over
      // a malformed function body or compound construct.
      ConsumeBrace();
      SkipUntil(tok::r_brace);
      if (Tok.isOneOf(tok::comma, tok::l_brace)) {
        // This declaration isn't over yet. Keep skipping.
        continue;
      }
      TryConsumeToken(tok::semi);
      return;

    case tok::l_square:
      ConsumeBracket();
      SkipUntil(tok::r_square);
      continue;

    case tok::l_paren:
      ConsumeParen();
      SkipUntil(tok::r_paren);
      continue;

    case tok::r_brace:
      return;

    case tok::semi:
      ConsumeToken();
      return;

    case tok::eof:
      return;

    default:
      break;
    }

    ConsumeAnyToken();
  }
}

NEVERC_HOT Parser::DeclGroupPtrTy
Parser::ParseDeclGroup(ParsingDeclSpec &DS, DeclaratorContext Context,
                       ParsedAttributes &Attrs, SourceLocation *DeclEnd) {
  // Consume all of the attributes from `Attrs` by moving them to our own local
  // list. This ensures that we will not attempt to interpret them as statement
  // attributes higher up the callchain.
  ParsedAttributes LocalAttrs(AttrFactory);
  LocalAttrs.takeAllFrom(Attrs);
  ParsingDeclarator D(*this, DS, LocalAttrs, Context);
  ParseDeclarator(D);

  // Bail out if the first declarator didn't seem well-formed.
  if (!D.hasName() && !D.mayOmitIdentifier()) {
    SkipBrokenDecl();
    return nullptr;
  }

  // Save late-parsed attributes for now; they need to be parsed in the
  // appropriate function scope after the function Decl has been constructed.
  // These will be parsed in ParseFunctionDefinition or ParseLexedAttrList.
  LateParsedAttrList LateParsedAttrs(true);
  if (D.isFunctionDeclarator()) {
    MaybeParseGNUAttributes(D, &LateParsedAttrs);

    // The _Noreturn keyword can't appear here, unlike the GNU noreturn
    // attribute. If we find the keyword here, tell the user to put it
    // at the start instead.
    if (Tok.is(tok::kw__Noreturn)) {
      SourceLocation Loc = ConsumeToken();
      const char *PrevSpec;
      unsigned DiagID;

      // We can offer a fixit if it's valid to mark this function as _Noreturn
      // and we don't have any other declarators in this declaration.
      bool Fixit = !DS.setFunctionSpecNoreturn(Loc, PrevSpec, DiagID);
      MaybeParseGNUAttributes(D, &LateParsedAttrs);
      Fixit &= Tok.isOneOf(tok::semi, tok::l_brace);

      llvm::SmallString<16> NoreturnKw(
          tok::getKeywordSpelling(tok::kw__Noreturn));
      NoreturnKw += ' ';
      Diag(Loc, diag::err_c11_noreturn_misplaced)
          << (Fixit ? FixItHint::CreateRemoval(Loc) : FixItHint())
          << (Fixit ? FixItHint::CreateInsertion(D.getBeginLoc(), NoreturnKw)
                    : FixItHint());
    }

    // Check for a function *definition* (body required). Past end of
    // declarator. Next token might be \c __attribute__ (GCC K&R) or '\{' —
    // distinguish from a mere declaration.
    if (!isDeclarationAfterDeclarator()) {

      // In C, function definitions appear at file scope (or local functions as
      // extensions); handle the file-scope '\{' case here.
      if (Context == DeclaratorContext::File) {
        if (isStartOfFunctionDefinition(D)) {
          if (DS.getStorageClassSpec() == DeclSpec::SCS_typedef) {
            Diag(Tok, diag::err_function_declared_typedef);

            // Recover by treating the 'typedef' as spurious.
            DS.ClearStorageClassSpecs();
          }

          Decl *TheDecl = ParseFunctionDefinition(D, &LateParsedAttrs);
          return Actions.WrapDeclAsGroup(TheDecl);
        }

        if (isDeclarationSpecifier()) {
          // Missing ';' after a prototype: what follows looks like a new
          // declaration, not a function body. Fall through and diagnose later.
        } else {
          Diag(Tok, diag::err_expected_fn_body);
          SkipUntil(tok::semi);
          return nullptr;
        }
      } else {
        if (Tok.is(tok::l_brace)) {
          Diag(Tok, diag::err_function_definition_not_allowed);
          SkipBrokenDecl();
          return nullptr;
        }
      }
    }
  }

  if (ParseAsmAttributesAfterDeclarator(D))
    return nullptr;

  llvm::SmallVector<Decl *, 8> DeclsInGroup;
  Decl *FirstDecl = ParseDeclarationAfterDeclaratorAndAttributes(D);
  if (LateParsedAttrs.size() > 0)
    ParseLexedAttributeList(LateParsedAttrs, FirstDecl, true, false);
  D.complete(FirstDecl);
  if (FirstDecl)
    DeclsInGroup.push_back(FirstDecl);

  bool ExpectSemi = Context != DeclaratorContext::ForInit;

  // If we don't have a comma, it is either the end of the list (a ';') or an
  // error, bail out.
  SourceLocation CommaLoc;
  while (TryConsumeToken(tok::comma, CommaLoc)) {
    if (Tok.isAtStartOfLine() && ExpectSemi && !CouldBeDeclarator(Context)) {
      // This comma was followed by a line-break and something which can't be
      // the start of a declarator. The comma was probably a typo for a
      // semicolon.
      Diag(CommaLoc, diag::err_expected_semi_declaration)
          << FixItHint::CreateReplacement(CommaLoc, ";");
      ExpectSemi = false;
      break;
    }

    D.clear();

    // Accept attributes in an init-declarator.  In the first declarator in a
    // declaration, these would be part of the declspec.  In subsequent
    // declarators, they become part of the declarator itself, so that they
    // don't apply to declarators after *this* one.  Examples:
    //    short __attribute__((common)) var;    -> declspec
    //    short var __attribute__((common));    -> declarator
    //    short x, __attribute__((common)) var;    -> declarator
    MaybeParseGNUAttributes(D);

    // MSVC parses but ignores qualifiers after the comma as an extension.
    if (getLangOpts().MicrosoftExt)
      DiagnoseAndSkipExtendedMicrosoftTypeAttributes();

    ParseDeclarator(D);

    if (!D.isInvalidType()) {
      // init-declarator: declarator initializer[opt]
      Decl *ThisDecl = ParseDeclarationAfterDeclarator(D);
      D.complete(ThisDecl);
      if (ThisDecl)
        DeclsInGroup.push_back(ThisDecl);
    }
  }

  if (DeclEnd)
    *DeclEnd = Tok.getLocation();

  if (ExpectSemi &&
      RequireSemicolon(Context == DeclaratorContext::File
                           ? diag::err_invalid_token_after_toplevel_declarator
                           : diag::err_expected_semi_declaration)) {
    // Okay, there was no semicolon and one was expected.  If we see a
    // declaration specifier, just assume it was missing and continue parsing.
    // Otherwise things are very confused and we skip to recover.
    if (!isDeclarationSpecifier())
      SkipBrokenDecl();
  }

  return Actions.FinalizeDeclaratorGroup(getCurScope(), DS, DeclsInGroup);
}

bool Parser::ParseAsmAttributesAfterDeclarator(Declarator &D) {
  // If a simple-asm-expr is present, parse it.
  if (Tok.is(tok::kw_asm)) {
    SourceLocation Loc;
    ExprResult AsmLabel(ParseSimpleAsm(/*ForAsmLabel*/ true, &Loc));
    if (AsmLabel.isInvalid()) {
      SkipUntil(tok::semi, StopBeforeMatch);
      return true;
    }

    D.setAsmLabel(AsmLabel.get());
    D.SetRangeEnd(Loc);
  }

  MaybeParseGNUAttributes(D);
  return false;
}

Decl *Parser::ParseDeclarationAfterDeclarator(Declarator &D) {
  if (ParseAsmAttributesAfterDeclarator(D))
    return nullptr;

  return ParseDeclarationAfterDeclaratorAndAttributes(D);
}

Decl *Parser::ParseDeclarationAfterDeclaratorAndAttributes(Declarator &D) {
  enum class InitKind {
    Uninitialized,
    Equal,
  };
  InitKind TheInitKind;
  // If a '==' or '+=' is found, suggest a fixit to '='.
  if (isTokenEqualOrEqualTypo())
    TheInitKind = InitKind::Equal;
  else
    TheInitKind = InitKind::Uninitialized;
  if (TheInitKind != InitKind::Uninitialized)
    D.setHasInitializer();

  // Inform Sema that we just parsed this declarator.
  Decl *ThisDecl = Actions.OnDeclarator(getCurScope(), D);

  switch (TheInitKind) {
  // Parse declarator '=' initializer.
  case InitKind::Equal: {
    ConsumeToken();

    ExprResult Init = ParseInitializer();

    if (Init.isInvalid()) {
      llvm::SmallVector<tok::TokenKind, 2> StopTokens;
      StopTokens.push_back(tok::comma);
      if (D.getContext() == DeclaratorContext::ForInit)
        StopTokens.push_back(tok::r_paren);
      SkipUntil(StopTokens, StopAtSemi | StopBeforeMatch);
      Actions.OnInitializerError(ThisDecl);
    } else
      Actions.AttachInitializerToDecl(ThisDecl, Init.get(),
                                      /*DirectInit=*/false);
    break;
  }
  case InitKind::Uninitialized: {
    Actions.OnUninitializedDecl(ThisDecl);
    break;
  }
  }

  Actions.FinalizeDeclaration(ThisDecl);
  return ThisDecl;
}

void Parser::ParseSpecifierQualifierList(DeclSpec &DS, AccessSpecifier AS,
                                         DeclSpecContext DSC) {
  ParseDeclarationSpecifiers(DS, AS, DSC, nullptr);

  // Validate declspec for type-name.
  unsigned Specs = DS.getParsedSpecifiers();
  if (isTypeSpecifier(DSC) && !DS.hasTypeSpecifier()) {
    Diag(Tok, diag::err_expected_type);
    DS.SetTypeSpecError();
  } else if (Specs == DeclSpec::PQ_None && !DS.hasAttributes()) {
    Diag(Tok, diag::err_typename_requires_specqual);
    if (!DS.hasTypeSpecifier())
      DS.SetTypeSpecError();
  }

  // Issue diagnostic and remove storage class if present.
  if (Specs & DeclSpec::PQ_StorageClassSpecifier) {
    if (DS.getStorageClassSpecLoc().isValid())
      Diag(DS.getStorageClassSpecLoc(),
           diag::err_typename_invalid_storageclass);
    else
      Diag(DS.getThreadStorageClassSpecLoc(),
           diag::err_typename_invalid_storageclass);
    DS.ClearStorageClassSpecs();
  }

  // Issue diagnostic and remove function specifier if present.
  if (Specs & DeclSpec::PQ_FunctionSpecifier) {
    if (DS.isInlineSpecified())
      Diag(DS.getInlineSpecLoc(), diag::err_typename_invalid_functionspec);
    if (DS.isNoreturnSpecified())
      Diag(DS.getNoreturnSpecLoc(), diag::err_typename_invalid_functionspec);
    DS.ClearFunctionSpecs();
  }

  // Issue diagnostic and remove constexpr specifier if present.
  if (DS.hasConstexprSpecifier()) {
    Diag(DS.getConstexprSpecLoc(), diag::err_typename_invalid_constexpr)
        << static_cast<int>(DS.getConstexprSpecifier());
    DS.ClearConstexprSpec();
  }
}

namespace {
bool isPostDeclaratorToken(const Token &T) {
  return T.isOneOf(tok::l_square, tok::l_paren, tok::r_paren, tok::semi,
                   tok::comma, tok::equal, tok::kw_asm, tok::l_brace,
                   tok::colon);
}
} // namespace

bool Parser::ParseImpliedInt(DeclSpec &DS, AccessSpecifier AS,
                             DeclSpecContext DSC, ParsedAttributes &Attrs) {
  assert(Tok.is(tok::identifier) && "should have identifier");

  SourceLocation Loc = Tok.getLocation();
  // If we see an identifier that is not a type name, we normally would
  // parse it as the identifier being declared.  However, when a typename
  // is typo'd or the definition is not included, this will incorrectly
  // parse the typename as the identifier name and fall over misparsing
  // later parts of the diagnostic.
  //
  // As such, we try to do some look-ahead in cases where this would
  // otherwise be an "implicit-int" case to see if this is invalid.  For
  // example: "static foo_t x = 4;"  In this case, if we parsed foo_t as
  // an identifier with implicit int, we'd get a parse error because the
  // next token is obviously invalid for a type.  Parse these as a case
  // with an invalid type specifier.
  assert(!DS.hasTypeSpecifier() && "Type specifier checked above");

  // Since we know that this is either implicit int (rare) or an error, do
  // lookahead for better recovery. This never applies within a type specifier.
  // Implicit int is allowed only in language modes where the extension applies.
  if (!isTypeSpecifier(DSC) && getLangOpts().isImplicitIntAllowed() &&
      isPostDeclaratorToken(NextToken())) {
    // If this token is valid for implicit int, e.g. "static x = 4", then
    // we just avoid eating the identifier, so it will be parsed as the
    // identifier in the declarator.
    return false;
  }

  // Otherwise, if we don't consume this token, we are going to emit an
  // error anyway.  Try to recover from various common problems.  Check
  // to see if this was a reference to a tag name without a tag specified.
  // This is a common problem in C (saying 'foo' instead of 'struct foo').
  //
  tok::TokenKind TagKind = tok::unknown;

  switch (Actions.isTagName(*Tok.getIdentifierInfo(), getCurScope())) {
  default:
    break;
  case DeclSpec::TST_enum:
    TagKind = tok::kw_enum;
    break;
  case DeclSpec::TST_union:
    TagKind = tok::kw_union;
    break;
  case DeclSpec::TST_struct:
    TagKind = tok::kw_struct;
    break;
  case DeclSpec::TST_class:
    TagKind = tok::kw_class;
    break;
  }

  if (TagKind != tok::unknown) {
    llvm::StringRef TagName = tok::getKeywordSpelling(TagKind);
    llvm::SmallString<16> FixitTagName(TagName);
    FixitTagName += ' ';
    IdentifierInfo *TokenName = Tok.getIdentifierInfo();
    LookupResult R(Actions, TokenName, SourceLocation(),
                   neverc::ResolveOrdinary);

    Diag(Loc, diag::err_use_of_tag_name_without_tag)
        << TokenName << TagName << false
        << FixItHint::CreateInsertion(Tok.getLocation(), FixitTagName);

    if (Actions.LookupParsedName(R, getCurScope())) {
      for (LookupResult::iterator I = R.begin(), IEnd = R.end(); I != IEnd; ++I)
        Diag((*I)->getLocation(), diag::note_decl_hiding_tag_type)
            << TokenName << TagName;
    }

    // Parse this as a tag as if the missing tag were present.
    if (TagKind == tok::kw_enum)
      ParseEnumSpecifier(Loc, DS, AS, DeclSpecContext::DSC_normal);
    else
      ParseStructOrUnionSpecifier(TagKind, Loc, DS, AS,
                                  DeclSpecContext::DSC_normal, Attrs);
    return true;
  }

  // Determine whether this identifier could plausibly be the name of something
  // being declared (with a missing type).
  if (!isTypeSpecifier(DSC)) {
    // Look ahead to the next token to try to figure out what this declaration
    // was supposed to be.
    switch (NextToken().getKind()) {
    case tok::l_paren: {
      // static x(4); // 'x' is not a type
      // x(int n);    // 'x' is not a type
      // x (*p)[];    // 'x' is a type
      //
      // Fall through.
      [[fallthrough]];
    }
    case tok::comma:
    case tok::equal:
    case tok::kw_asm:
    case tok::l_brace:
    case tok::l_square:
    case tok::semi:
      // This looks like a variable or function declaration. The type is
      // probably missing. We're done parsing decl-specifiers.
      // But only if we are not in a function prototype scope.
      if (getCurScope()->isFunctionPrototypeScope())
        break;
      return false;

    default:
      // This is probably supposed to be a type. This includes cases like:
      //   int f(itn);
      //   struct S { unsigned : 4; };
      break;
    }
  }

  // This is almost certainly an invalid type name. Let Sema emit a diagnostic
  // and attempt to recover.
  ParsedType T;
  IdentifierInfo *II = Tok.getIdentifierInfo();
  Actions.DiagnoseUnknownTypeName(II, Loc, getCurScope(), T);
  if (T) {
    // The action has suggested that the type T could be used. Set that as
    // the type in the declaration specifiers, consume the would-be type
    // name token, and we're done.
    const char *PrevSpec;
    unsigned DiagID;
    DS.SetTypeSpecType(DeclSpec::TST_typename, Loc, PrevSpec, DiagID, T,
                       Actions.getTreeContext().getPrintingPolicy());
    DS.SetRangeEnd(Tok.getLocation());
    ConsumeToken();
    // There may be other declaration specifiers after this.
    return true;
  } else if (II != Tok.getIdentifierInfo()) {
    // If no type was suggested, the correction is to a keyword
    Tok.setKind(II->getTokenID());
    // There may be other declaration specifiers after this.
    return true;
  }

  // Otherwise, the action had no suggestion for us.  Mark this as an error.
  DS.SetTypeSpecError();
  DS.SetRangeEnd(Tok.getLocation());
  ConsumeToken();

  return true;
}

Parser::DeclSpecContext
Parser::getDeclSpecContextFromDeclaratorContext(DeclaratorContext Context) {
  switch (Context) {
  case DeclaratorContext::Member:
    return DeclSpecContext::DSC_record;
  case DeclaratorContext::File:
    return DeclSpecContext::DSC_top_level;
  case DeclaratorContext::Association:
    return DeclSpecContext::DSC_association;
  case DeclaratorContext::TypeName:
    return DeclSpecContext::DSC_type_specifier;
  default:
    return DeclSpecContext::DSC_normal;
  }
}

ExprResult Parser::ParseAlignArgument(llvm::StringRef KWName,
                                      SourceLocation Start, bool &IsType,
                                      ParsedType &TypeResult) {
  ExprResult ER;
  if (isTypeIdInParens()) {
    SourceLocation TypeLoc = Tok.getLocation();
    ParsedType Ty = ParseTypeName().get();
    SourceRange TypeRange(Start, Tok.getLocation());
    if (Actions.OnAlignasTypeArgument(KWName, Ty, TypeLoc, TypeRange))
      return ExprError();
    TypeResult = Ty;
    IsType = true;
  } else {
    ER = ParseConstantExpression();
    IsType = false;
  }

  return ER;
}

void Parser::ParseAlignmentSpecifier(ParsedAttributes &Attrs,
                                     SourceLocation *EndLoc) {
  assert(Tok.isOneOf(tok::kw_alignas, tok::kw__Alignas) &&
         "Not an alignment-specifier!");
  Token KWTok = Tok;
  IdentifierInfo *KWName = KWTok.getIdentifierInfo();
  auto Kind = KWTok.getKind();
  SourceLocation KWLoc = ConsumeToken();

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.expectAndConsume())
    return;

  bool IsType;
  ParsedType TypeResult;
  ExprResult ArgExpr = ParseAlignArgument(
      PP.getSpelling(KWTok), T.getOpenLocation(), IsType, TypeResult);
  if (ArgExpr.isInvalid()) {
    T.skipToEnd();
    return;
  }

  T.consumeClose();
  if (EndLoc)
    *EndLoc = T.getCloseLocation();

  if (IsType) {
    Attrs.addNewTypeAttr(KWName, KWLoc, nullptr, KWLoc, TypeResult, Kind);
  } else {
    ArgsVector ArgExprs;
    ArgExprs.push_back(ArgExpr.get());
    Attrs.addNew(KWName, KWLoc, nullptr, KWLoc, ArgExprs.data(), 1, Kind);
  }
}

ExprResult Parser::ParseExtIntegerArgument() {
  assert(Tok.isOneOf(tok::kw__ExtInt, tok::kw__BitInt) &&
         "Not an extended int type");
  ConsumeToken();

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.expectAndConsume())
    return ExprError();

  ExprResult ER = ParseConstantExpression();
  if (ER.isInvalid()) {
    T.skipToEnd();
    return ExprError();
  }

  if (T.consumeClose())
    return ExprError();
  return ER;
}

bool Parser::DiagnoseMissingSemiAfterTagDefinition(
    DeclSpec &DS, AccessSpecifier AS, DeclSpecContext DSContext,
    LateParsedAttrList *LateAttrs) {
  assert(DS.hasTagDefinition() && "shouldn't call this");

  bool HasScope = false;
  // Make a copy in case GetLookAheadToken invalidates the result of NextToken.
  Token AfterScope = HasScope ? NextToken() : Tok;

  // Determine whether the following tokens could possibly be a
  // declarator.
  bool CouldBeDeclarator = true;
  if (Tok.is(tok::annot_typename)) {
    // A declarator-id can't start with an annotated typedef name.
    CouldBeDeclarator = false;
  } else if (AfterScope.is(tok::identifier)) {
    const Token &Next = HasScope ? GetLookAheadToken(2) : NextToken();

    // These tokens cannot come after the declarator-id in a
    // simple-declaration, and are likely to come after a type-specifier.
    if (Next.isOneOf(tok::star, tok::identifier)) {
      // Missing a semicolon.
      CouldBeDeclarator = false;
    }
  }

  if (CouldBeDeclarator)
    return false;

  const PrintingPolicy &PPol{Actions.getTreeContext().getPrintingPolicy()};
  Diag(PP.getLocForEndOfToken(DS.getRepAsDecl()->getEndLoc()),
       diag::err_expected_after)
      << DeclSpec::getSpecifierName(DS.getTypeSpecType(), PPol) << tok::semi;

  // Try to recover from the typo, by dropping the tag definition and parsing
  // the problematic tokens as a type.
  //
  // Split the DeclSpec into pieces for the standalone
  // declaration and pieces for the following declaration, instead
  // of assuming that all the other pieces attach to new declaration,
  // and call ParsedFreeStandingDeclSpec as appropriate.
  DS.ClearTypeSpecType();
  ParseDeclarationSpecifiers(DS, AS, DSContext, LateAttrs);
  return false;
}
