#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Syntax/ParseDiag.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "llvm/Support/TimeProfiler.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// Parser utilities
// ===----------------------------------------------------------------------===

IdentifierInfo *Parser::getSEHExceptKeyword() {
  if (!Ident__except && getLangOpts().MicrosoftExt)
    Ident__except = PP.getIdentifierInfo("__except");

  return Ident__except;
}

Parser::Parser(PrepEngine &pp, Sema &actions)
    : PP(pp), Actions(actions), Diags(PP.getDiagnostics()),
      ColonIsSacred(false) {
  Tok.startToken();
  Tok.setKind(tok::eof);
  Actions.CurScope = nullptr;
  NumCachedScopes = 0;

  initializePragmaHandlers();
}

DiagnosticBuilder Parser::Diag(SourceLocation Loc, unsigned DiagID) {
  return Diags.Report(Loc, DiagID);
}

DiagnosticBuilder Parser::Diag(const Token &Tok, unsigned DiagID) {
  return Diag(Tok.getLocation(), DiagID);
}

void Parser::SuggestParentheses(SourceLocation Loc, unsigned DK,
                                SourceRange ParenRange) {
  SourceLocation EndLoc = PP.getLocForEndOfToken(ParenRange.getEnd());
  if (!ParenRange.getEnd().isFileID() || EndLoc.isInvalid()) {
    Diag(Loc, DK);
    return;
  }

  Diag(Loc, DK) << FixItHint::CreateInsertion(ParenRange.getBegin(), "(")
                << FixItHint::CreateInsertion(EndLoc, ")");
}

namespace {
LLVM_ATTRIBUTE_ALWAYS_INLINE
bool isMistypedPunctuation(tok::TokenKind ExpectedTok, const Token &Tok) {
  if (LLVM_LIKELY(ExpectedTok != tok::semi))
    return false;
  const auto K = Tok.getKind();
  return K == tok::colon || K == tok::comma;
}
} // namespace

bool Parser::RequireToken(tok::TokenKind ExpectedTok, unsigned DiagID,
                          llvm::StringRef Msg) {
  if (LLVM_LIKELY(Tok.is(ExpectedTok))) {
    ConsumeAnyToken();
    return false;
  }

  if (isMistypedPunctuation(ExpectedTok, Tok)) {
    SourceLocation Loc = Tok.getLocation();
    {
      DiagnosticBuilder DB = Diag(Loc, DiagID);
      DB << FixItHint::CreateReplacement(
          SourceRange(Loc), tok::getPunctuatorSpelling(ExpectedTok));
      if (DiagID == diag::err_expected)
        DB << ExpectedTok;
      else if (DiagID == diag::err_expected_after)
        DB << Msg << ExpectedTok;
      else
        DB << Msg;
    }

    ConsumeAnyToken();
    return false;
  }

  SourceLocation EndLoc = PP.getLocForEndOfToken(PrevTokLocation);
  const char *Spelling = nullptr;
  if (EndLoc.isValid())
    Spelling = tok::getPunctuatorSpelling(ExpectedTok);

  DiagnosticBuilder DB =
      Spelling
          ? Diag(EndLoc, DiagID) << FixItHint::CreateInsertion(EndLoc, Spelling)
          : Diag(Tok, DiagID);
  if (DiagID == diag::err_expected)
    DB << ExpectedTok;
  else if (DiagID == diag::err_expected_after)
    DB << Msg << ExpectedTok;
  else
    DB << Msg;

  return true;
}

bool Parser::RequireSemicolon(unsigned DiagID, llvm::StringRef TokenUsed) {
  if (LLVM_LIKELY(TryConsumeToken(tok::semi)))
    return false;

  if (LLVM_UNLIKELY((Tok.is(tok::r_paren) || Tok.is(tok::r_square)) &&
                    NextToken().is(tok::semi))) {
    Diag(Tok, diag::err_extraneous_token_before_semi)
        << PP.getSpelling(Tok) << FixItHint::CreateRemoval(Tok.getLocation());
    ConsumeAnyToken(); // The ')' or ']'.
    ConsumeToken();    // The ';'.
    return false;
  }

  return RequireToken(tok::semi, DiagID, TokenUsed);
}

void Parser::ConsumeRedundantSemicolons(ExtraSemiKind Kind, DeclSpec::TST TST) {
  if (!Tok.is(tok::semi))
    return;

  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc = Tok.getLocation();
  ConsumeToken();

  while ((Tok.is(tok::semi) && !Tok.isAtStartOfLine())) {
    EndLoc = Tok.getLocation();
    ConsumeToken();
  }

  Diag(StartLoc, diag::ext_extra_semi)
      << Kind
      << DeclSpec::getSpecifierName(
             TST, Actions.getTreeContext().getPrintingPolicy())
      << FixItHint::CreateRemoval(SourceRange(StartLoc, EndLoc));
}

void Parser::checkCompoundToken(SourceLocation FirstTokLoc,
                                tok::TokenKind FirstTokKind, CompoundToken Op) {
  if (FirstTokLoc.isInvalid())
    return;
  SourceLocation SecondTokLoc = Tok.getLocation();

  if ((FirstTokLoc.isMacroID() || SecondTokLoc.isMacroID()) &&
      PP.getSourceManager().getFileID(FirstTokLoc) !=
          PP.getSourceManager().getFileID(SecondTokLoc)) {
    Diag(FirstTokLoc, diag::warn_compound_token_split_by_macro)
        << (FirstTokKind == Tok.getKind()) << FirstTokKind << Tok.getKind()
        << static_cast<int>(Op) << SourceRange(FirstTokLoc);
    Diag(SecondTokLoc, diag::note_compound_token_split_second_token_here)
        << (FirstTokKind == Tok.getKind()) << Tok.getKind()
        << SourceRange(SecondTokLoc);
    return;
  }

  if (Tok.hasLeadingSpace() || Tok.isAtStartOfLine()) {
    SourceLocation SpaceLoc = PP.getLocForEndOfToken(FirstTokLoc);
    if (SpaceLoc.isInvalid())
      SpaceLoc = FirstTokLoc;
    Diag(SpaceLoc, diag::warn_compound_token_split_by_whitespace)
        << (FirstTokKind == Tok.getKind()) << FirstTokKind << Tok.getKind()
        << static_cast<int>(Op) << SourceRange(FirstTokLoc, SecondTokLoc);
    return;
  }
}

namespace {
bool hasSkipFlag(Parser::SkipUntilFlags L, Parser::SkipUntilFlags R) {
  return (static_cast<unsigned>(L) & static_cast<unsigned>(R)) != 0;
}
} // namespace

bool Parser::SkipUntil(llvm::ArrayRef<tok::TokenKind> Toks,
                       SkipUntilFlags Flags) {
  bool isFirstTokenSkipped = true;
  while (true) {
    tok::TokenKind CurKind = Tok.getKind();
    if (Toks.size() == 1) {
      if (LLVM_UNLIKELY(CurKind == Toks[0])) {
        if (!hasSkipFlag(Flags, StopBeforeMatch))
          ConsumeAnyToken();
        return true;
      }
    } else {
      for (unsigned i = 0, NumToks = Toks.size(); i != NumToks; ++i) {
        if (CurKind == Toks[i]) {
          if (!hasSkipFlag(Flags, StopBeforeMatch))
            ConsumeAnyToken();
          return true;
        }
      }
    }

    // Fast bail: skip to EOF without recursion.
    if (Toks.size() == 1 && Toks[0] == tok::eof &&
        !hasSkipFlag(Flags, StopAtSemi)) {
      while (Tok.isNot(tok::eof))
        ConsumeAnyToken();
      return true;
    }

    switch (Tok.getKind()) {
    case tok::eof:
      // Ran out of tokens.
      return false;

    case tok::l_paren:
      ConsumeParen();
      SkipUntil(tok::r_paren);
      break;
    case tok::l_square:
      ConsumeBracket();
      SkipUntil(tok::r_square);
      break;
    case tok::l_brace:
      ConsumeBrace();
      SkipUntil(tok::r_brace);
      break;
    case tok::question:
      ConsumeToken();
      SkipUntil(tok::colon,
                SkipUntilFlags(unsigned(Flags) & unsigned(StopAtSemi)));
      break;

    // Okay, we found a ']' or '}' or ')', which we think should be balanced.
    // Since the user wasn't looking for this token (if they were, it would
    // already be handled), this isn't balanced.  If there is a LHS token at a
    // higher level, we will assume that this matches the unbalanced token
    // and return it.  Otherwise, this is a spurious RHS token, which we skip.
    case tok::r_paren:
      if (ParenCount && !isFirstTokenSkipped)
        return false; // Matches something.
      ConsumeParen();
      break;
    case tok::r_square:
      if (BracketCount && !isFirstTokenSkipped)
        return false; // Matches something.
      ConsumeBracket();
      break;
    case tok::r_brace:
      if (BraceCount && !isFirstTokenSkipped)
        return false; // Matches something.
      ConsumeBrace();
      break;

    case tok::semi:
      if (hasSkipFlag(Flags, StopAtSemi))
        return false;
      [[fallthrough]];
    default:
      // Skip this token.
      ConsumeAnyToken();
      break;
    }
    isFirstTokenSkipped = false;
  }
}

void Parser::PushScope(unsigned ScopeFlags) {
  if (NumCachedScopes) {
    Scope *N = ScopeCache[--NumCachedScopes];
    N->Init(getCurScope(), ScopeFlags);
    Actions.CurScope = N;
  } else {
    Actions.CurScope = new Scope(getCurScope(), ScopeFlags, Diags);
  }
}

void Parser::PopScope() {
  assert(getCurScope() && "Scope imbalance!");

  // Inform the actions module that this scope is going away if there are any
  // decls in it.
  Actions.OnPopScope(Tok.getLocation(), getCurScope());

  Scope *OldScope = getCurScope();
  Actions.CurScope = OldScope->getParent();

  if (NumCachedScopes == ScopeCacheSize)
    delete OldScope;
  else
    ScopeCache[NumCachedScopes++] = OldScope;
}

Parser::ParseScopeFlags::ParseScopeFlags(Parser *Self, unsigned ScopeFlags,
                                         bool ManageFlags)
    : CurScope(ManageFlags ? Self->getCurScope() : nullptr) {
  if (CurScope) {
    OldFlags = CurScope->getFlags();
    CurScope->setFlags(ScopeFlags);
  }
}

Parser::ParseScopeFlags::~ParseScopeFlags() {
  if (CurScope)
    CurScope->setFlags(OldFlags);
}

Parser::~Parser() {
  // If we still have scopes active, delete the scope tree.
  delete getCurScope();
  Actions.CurScope = nullptr;

  // Free the scope cache.
  for (unsigned i = 0, e = NumCachedScopes; i != e; ++i)
    delete ScopeCache[i];

  resetPragmaHandlers();
}

void Parser::Initialize() {
  assert(getCurScope() == nullptr && "A scope is already active?");
  PushScope(Scope::DeclScope);
  Actions.OnTranslationUnitScope(getCurScope());

  Ident_introduced = nullptr;
  Ident_deprecated = nullptr;
  Ident_obsoleted = nullptr;
  Ident_unavailable = nullptr;
  Ident_strict = nullptr;
  Ident_replacement = nullptr;

  Ident__except = nullptr;

  Actions.Initialize();

  // Prime the lexer look-ahead.
  ConsumeToken();
}

bool Parser::ParseFirstTopLevelDecl(DeclGroupPtrTy &Result) {
  bool NoTopLevelDecls = ParseTopLevelDecl(Result);

  // Translation units must have at least one top-level declaration.
  // If the main file is a header, we're only pretending it's a TU; don't warn.
  if (NoTopLevelDecls && !getLangOpts().IsHeaderFile)
    Diag(diag::ext_empty_translation_unit);

  return NoTopLevelDecls;
}

bool Parser::ParseTopLevelDecl(DeclGroupPtrTy &Result) {
  Result = nullptr;
  switch (Tok.getKind()) {
  case tok::annot_pragma_unused:
    ProcessPragmaUnused();
    return false;

  case tok::eof:
    if (PP.getMaxTokens() != 0 && PP.getTokenCount() > PP.getMaxTokens()) {
      PP.Diag(Tok.getLocation(), diag::warn_max_tokens_total)
          << PP.getTokenCount() << PP.getMaxTokens();
      SourceLocation OverrideLoc = PP.getMaxTokensOverrideLoc();
      if (OverrideLoc.isValid()) {
        PP.Diag(OverrideLoc, diag::note_max_tokens_total_override);
      }
    }

    Actions.OnEndOfTranslationUnit();
    return true;

  case tok::identifier:
    break;

  default:
    break;
  }

  ParsedAttributes DeclAttrs(AttrFactory);
  ParsedAttributes DeclSpecAttrs(AttrFactory);
  // GNU attributes are applied to the declaration specification while the
  // standard attributes are applied to the declaration.  We parse the two
  // attribute sets into different containters so we can apply them during
  // the regular parsing process.
  while (MaybeParseBracketAttributes(DeclAttrs) ||
         MaybeParseGNUAttributes(DeclSpecAttrs))
    ;

  Result = ParseExternalDeclaration(DeclAttrs, DeclSpecAttrs);
  return false;
}

__attribute__((hot)) Parser::DeclGroupPtrTy
Parser::ParseExternalDeclaration(ParsedAttributes &Attrs,
                                 ParsedAttributes &DeclSpecAttrs,
                                 ParsingDeclSpec *DS) {
  ParenBraceBracketBalancer BalancerRAIIObj(*this);

  Decl *SingleDecl = nullptr;
  switch (Tok.getKind()) {
  case tok::annot_pragma_vis:
    ProcessPragmaVisibility();
    return nullptr;
  case tok::annot_pragma_pack:
    ProcessPragmaPack();
    return nullptr;
  case tok::annot_pragma_msstruct:
    ProcessPragmaMSStruct();
    return nullptr;
  case tok::annot_pragma_align:
    ProcessPragmaAlign();
    return nullptr;
  case tok::annot_pragma_weak:
    ProcessPragmaWeak();
    return nullptr;
  case tok::annot_pragma_weakalias:
    ProcessPragmaWeakAlias();
    return nullptr;
  case tok::annot_pragma_fp_contract:
    ProcessPragmaFPContract();
    return nullptr;
  case tok::annot_pragma_fenv_access:
  case tok::annot_pragma_fenv_access_ms:
    ProcessPragmaFEnvAccess();
    return nullptr;
  case tok::annot_pragma_fenv_round:
    ProcessPragmaFEnvRound();
    return nullptr;
  case tok::annot_pragma_cx_limited_range:
    ProcessPragmaCXLimitedRange();
    return nullptr;
  case tok::annot_pragma_float_control:
    ProcessPragmaFloatControl();
    return nullptr;
  case tok::annot_pragma_fp:
    ProcessPragmaFP();
    break;
  case tok::annot_pragma_ms_pragma:
    ProcessPragmaMSPragma();
    return nullptr;
  case tok::annot_pragma_dump:
    ProcessPragmaDump();
    return nullptr;
  case tok::annot_pragma_attribute:
    ProcessPragmaAttribute();
    return nullptr;
  case tok::semi:
    // Empty declaration or attribute-only declaration (e.g. `;` or `[[...]];`).
    SingleDecl =
        Actions.OnEmptyDeclaration(getCurScope(), Attrs, Tok.getLocation());
    ConsumeRedundantSemicolons(OutsideFunction);
    break;
  case tok::r_brace:
    Diag(Tok, diag::err_extraneous_closing_brace);
    ConsumeBrace();
    return nullptr;
  case tok::eof:
    Diag(Tok, diag::err_expected_external_declaration);
    return nullptr;
  case tok::kw___extension__: {
    // __extension__ silences extension warnings in the subexpression.
    ExtensionRAIIObject O(Diags); // Use RAII to do this.
    ConsumeToken();
    return ParseExternalDeclaration(Attrs, DeclSpecAttrs);
  }
  case tok::kw_asm: {
    ProhibitAttributes(Attrs);

    SourceLocation StartLoc = Tok.getLocation();
    SourceLocation EndLoc;

    ExprResult Result(ParseSimpleAsm(/*ForAsmLabel*/ false, &EndLoc));

    // Empty asm string is allowed — it introduces no assembly code.
    if (!(getLangOpts().GNUAsm || Result.isInvalid())) {
      const auto *SL = cast<StringLiteral>(Result.get());
      if (!SL->getString().trim().empty())
        Diag(StartLoc, diag::err_gnu_inline_asm_disabled);
    }

    RequireToken(tok::semi, diag::err_expected_after, "top-level asm block");

    if (Result.isInvalid())
      return nullptr;
    SingleDecl = Actions.OnFileScopeAsmDecl(Result.get(), StartLoc, EndLoc);
    break;
  }
  case tok::minus:
  case tok::plus:
    Diag(Tok, diag::err_expected_external_declaration);
    ConsumeToken();
    return nullptr;
  case tok::kw_typedef:
  case tok::kw_static_assert:
  case tok::kw__Static_assert:
    // A function definition cannot start with any of these keywords.
    {
      SourceLocation DeclEnd;
      return ParseDeclaration(DeclaratorContext::File, DeclEnd, Attrs,
                              DeclSpecAttrs);
    }

  case tok::kw_static:
    goto dont_know;

  case tok::kw_inline:
    goto dont_know;

  case tok::kw_extern:
    goto dont_know;

  case tok::kw___if_exists:
  case tok::kw___if_not_exists:
    ParseMicrosoftIfExistsExternalDeclaration();
    return nullptr;

  default:
  dont_know:
    // We can't tell whether this is a function-definition or declaration yet.
    if (!SingleDecl)
      return ParseDeclarationOrFunctionDefinition(Attrs, DeclSpecAttrs, DS);
  }

  // This routine returns a DeclGroup, if the thing we parsed only contains a
  // single decl, convert it now.
  return Actions.WrapDeclAsGroup(SingleDecl);
}

bool Parser::isDeclarationAfterDeclarator() {

  return Tok.is(tok::equal) ||       // int X()=  -> not a function def
         Tok.is(tok::comma) ||       // int X(),  -> not a function def
         Tok.is(tok::semi) ||        // int X();  -> not a function def
         Tok.is(tok::kw_asm) ||      // int X() __asm__ -> not a function def
         Tok.is(tok::kw___attribute) // int X() __attr__ -> not a function def
      ;
}

bool Parser::isStartOfFunctionDefinition(const ParsingDeclarator &Declarator) {
  assert(Declarator.isFunctionDeclarator() && "Isn't a function declarator");
  if (Tok.is(tok::l_brace)) // int X() {}
    return true;

  // Handle K&R C argument lists: int X(f) int f; {}
  if (Declarator.getFunctionTypeInfo().isKNRPrototype())
    return isDeclarationSpecifier();

  return false;
}

Parser::DeclGroupPtrTy Parser::ParseDeclOrFunctionDefInternal(
    ParsedAttributes &Attrs, ParsedAttributes &DeclSpecAttrs,
    ParsingDeclSpec &DS, AccessSpecifier AS) {
  // Because we assume that the DeclSpec has not yet been initialised, we simply
  // overwrite the source range and attribute the provided leading declspec
  // attributes.
  assert(DS.getSourceRange().isInvalid() &&
         "expected uninitialised source range");
  DS.SetRangeStart(DeclSpecAttrs.Range.getBegin());
  DS.SetRangeEnd(DeclSpecAttrs.Range.getEnd());
  DS.takeAttributesFrom(DeclSpecAttrs);

  MaybeParseMicrosoftAttributes(DS.getAttributes());
  ParseDeclarationSpecifiers(DS, AS, DeclSpecContext::DSC_top_level);

  if (DS.hasTagDefinition() && DiagnoseMissingSemiAfterTagDefinition(
                                   DS, AS, DeclSpecContext::DSC_top_level))
    return nullptr;

  if (Tok.is(tok::semi)) {
    auto LengthOfTSTToken = [](DeclSpec::TST TKind) {
      assert(DeclSpec::isDeclRep(TKind));
      switch (TKind) {
      case DeclSpec::TST_struct:
        return 6;
      case DeclSpec::TST_union:
        return 5;
      case DeclSpec::TST_enum:
        return 4;
      default:
        return 0;
      }
    };
    // Suggest correct location to fix '[[attrib]] struct' to 'struct
    // [[attrib]]'
    SourceLocation CorrectLocationForAttributes =
        DeclSpec::isDeclRep(DS.getTypeSpecType())
            ? DS.getTypeSpecTypeLoc().getLocWithOffset(
                  LengthOfTSTToken(DS.getTypeSpecType()))
            : SourceLocation();
    ProhibitAttributes(Attrs, CorrectLocationForAttributes);
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

  return ParseDeclGroup(DS, DeclaratorContext::File, Attrs);
}

Parser::DeclGroupPtrTy Parser::ParseDeclarationOrFunctionDefinition(
    ParsedAttributes &Attrs, ParsedAttributes &DeclSpecAttrs,
    ParsingDeclSpec *DS, AccessSpecifier AS) {
  // Add an enclosing time trace scope for a bunch of small scopes with
  // "EvaluateAsConstExpr".
  llvm::TimeTraceScope TimeScope(
      "ParseDeclarationOrFunctionDefinition", [&]() -> llvm::SmallString<64> {
        return llvm::SmallString<64>(Tok.getLocation().printToString(
            Actions.getTreeContext().getSourceManager()));
      });

  if (DS) {
    return ParseDeclOrFunctionDefInternal(Attrs, DeclSpecAttrs, *DS, AS);
  } else {
    ParsingDeclSpec PDS(*this);
    return ParseDeclOrFunctionDefInternal(Attrs, DeclSpecAttrs, PDS, AS);
  }
}

Decl *Parser::ParseFunctionDefinition(ParsingDeclarator &D,
                                      LateParsedAttrList *LateParsedAttrs) {
  llvm::TimeTraceScope TimeScope(
      "ParseFunctionDefinition", [&]() -> llvm::SmallString<64> {
        return llvm::SmallString<64>(
            Actions.GetNameForDeclarator(D).getName().getAsString());
      });

  const DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();

  // If this is C89 and the declspecs were completely missing, fudge in an
  // implicit int.  We do this here because this is the only place where
  // declaration-specifiers are completely optional in the grammar.
  if (getLangOpts().isImplicitIntRequired()) {
    const DeclSpec &IDS = D.getDeclSpec();
    if (IDS.isEmpty()) {
      Diag(D.getIdentifierLoc(), diag::warn_missing_type_specifier)
          << IDS.getSourceRange();
      const char *PrevSpec;
      unsigned DiagID;
      const PrintingPolicy &Policy{
          Actions.getTreeContext().getPrintingPolicy()};
      D.getMutableDeclSpec().SetTypeSpecType(
          DeclSpec::TST_int, D.getIdentifierLoc(), PrevSpec, DiagID, Policy);
      D.SetRangeBegin(D.getDeclSpec().getSourceRange().getBegin());
    }
  }

  // If this declaration was formed with a K&R-style identifier list for the
  // arguments, parse declarations for all of the args next.
  // int foo(a,b) int a; float b; {}
  if (FTI.isKNRPrototype())
    ParseOldStyleParams(D);

  // We should have an opening brace for the function body.
  if (Tok.isNot(tok::l_brace)) {
    Diag(Tok, diag::err_expected_fn_body);

    // Skip over garbage, until we get to '{'.  Don't eat the '{'.
    SkipUntil(tok::l_brace, StopAtSemi | StopBeforeMatch);

    // If we didn't find the '{', bail out.
    if (Tok.isNot(tok::l_brace))
      return nullptr;
  }

  if (Tok.isNot(tok::equal)) {
    for (const ParsedAttr &AL : D.getAttributes())
      if (AL.isKnownToGCC() && !AL.isStandardAttributeSyntax())
        Diag(AL.getLoc(), diag::warn_attribute_on_function_definition) << AL;
  }

  // Enter a scope for the function body.
  ParseScope BodyScope(this, Scope::FnScope | Scope::DeclScope |
                                 Scope::CompoundStmtScope);

  // Tell the actions module that we have entered a function definition with the
  // specified Declarator for the function.
  Sema::SkipBodyInfo SkipBody;
  Decl *Res = Actions.OnStartOfFunctionDef(getCurScope(), D, &SkipBody);

  if (SkipBody.ShouldSkip) {
    SkipFunctionBody();
    Actions.PopExpressionEvaluationContext();
    return Res;
  }

  // Break out of the ParsingDeclarator context before we parse the body.
  D.complete(Res);

  // Break out of the ParsingDeclSpec context, too.  This const_cast is
  // safe because we're always the sole owner.
  D.getMutableDeclSpec().abort();

  // Late attributes are parsed in the same scope as the function body.
  if (LateParsedAttrs)
    ParseLexedAttributeList(*LateParsedAttrs, Res, false, true);

  return ParseFunctionStatementBody(Res, BodyScope);
}

void Parser::SkipFunctionBody() {
  if (Tok.is(tok::equal)) {
    SkipUntil(tok::semi);
    return;
  }

  CachedTokens Skipped;
  if (CacheFunctionPrologue(Skipped))
    SkipBrokenDecl();
  else {
    SkipUntil(tok::r_brace);
  }
}

void Parser::ParseOldStyleParams(Declarator &D) {
  // We know that the top-level of this declarator is a function.
  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();

  // Enter function-declaration scope, limiting any declarators to the
  // function prototype scope, including parameter declarators.
  ParseScope PrototypeScope(this, Scope::FunctionPrototypeScope |
                                      Scope::FunctionDeclarationScope |
                                      Scope::DeclScope);

  // Read all the argument declarations.
  while (isDeclarationSpecifier()) {
    SourceLocation DSStart = Tok.getLocation();

    // Parse the common declaration-specifiers piece.
    DeclSpec DS(AttrFactory);
    ParseDeclarationSpecifiers(DS);

    // Each declaration in the K&R param list must have a declarator.
    // NOTE: GCC just makes this an ext-warn.  It's not clear what it does with
    // the declarations though.  It's trivial to ignore them, really hard to do
    // anything else with them.
    if (TryConsumeToken(tok::semi)) {
      Diag(DSStart, diag::err_declaration_does_not_declare_param);
      continue;
    }

    // K&R parameter declarations only allow 'register' storage class.
    if (DS.getStorageClassSpec() != DeclSpec::SCS_unspecified &&
        DS.getStorageClassSpec() != DeclSpec::SCS_register) {
      Diag(DS.getStorageClassSpecLoc(),
           diag::err_invalid_storage_class_in_func_decl);
      DS.ClearStorageClassSpecs();
    }
    if (DS.getThreadStorageClassSpec() != DeclSpec::TSCS_unspecified) {
      Diag(DS.getThreadStorageClassSpecLoc(),
           diag::err_invalid_storage_class_in_func_decl);
      DS.ClearStorageClassSpecs();
    }

    // Parse the first declarator attached to this declspec.
    Declarator ParmDeclarator(DS, ParsedAttributesView::none(),
                              DeclaratorContext::KNRTypeList);
    ParseDeclarator(ParmDeclarator);

    while (true) {
      // If attributes are present, parse them.
      MaybeParseGNUAttributes(ParmDeclarator);

      // Ask the actions module to compute the type for this declarator.
      Decl *Param = Actions.OnParamDeclarator(getCurScope(), ParmDeclarator);

      if (Param &&
          // A missing identifier has already been diagnosed.
          ParmDeclarator.getIdentifier()) {

        // Scan the argument list looking for the correct param to apply this
        // type.
        for (unsigned i = 0;; ++i) {
          // Declarators must name parameters from the identifier list.
          if (i == FTI.NumParams) {
            Diag(ParmDeclarator.getIdentifierLoc(), diag::err_no_matching_param)
                << ParmDeclarator.getIdentifier();
            break;
          }

          if (FTI.Params[i].Ident == ParmDeclarator.getIdentifier()) {
            // Reject redefinitions of parameters.
            if (FTI.Params[i].Param) {
              Diag(ParmDeclarator.getIdentifierLoc(),
                   diag::err_param_redefinition)
                  << ParmDeclarator.getIdentifier();
            } else {
              FTI.Params[i].Param = Param;
            }
            break;
          }
        }
      }

      // If we don't have a comma, it is either the end of the list (a ';') or
      // an error, bail out.
      if (Tok.isNot(tok::comma))
        break;

      ParmDeclarator.clear();

      // Consume the comma.
      ConsumeToken();

      // Parse the next declarator.
      ParseDeclarator(ParmDeclarator);
    }

    // Consume ';' and continue parsing.
    if (!RequireSemicolon(diag::err_expected_semi_declaration))
      continue;

    // Otherwise recover by skipping to next semi or mandatory function body.
    if (SkipUntil(tok::l_brace, StopAtSemi | StopBeforeMatch))
      break;
    TryConsumeToken(tok::semi);
  }

  // The actions module must verify that all arguments were declared.
  Actions.OnFinishKNRParamDeclarations(getCurScope(), D, Tok.getLocation());
}

ExprResult Parser::ParseAsmStringLiteral(bool ForAsmLabel) {
  if (!isTokenStringLiteral()) {
    Diag(Tok, diag::err_expected_string_literal)
        << /*Source='in...'*/ 0 << "'asm'";
    return ExprError();
  }

  ExprResult AsmString(ParseStringLiteralExpression());
  if (!AsmString.isInvalid()) {
    const auto *SL = cast<StringLiteral>(AsmString.get());
    if (!SL->isOrdinary()) {
      Diag(Tok, diag::err_asm_operand_wide_string_literal)
          << SL->isWide() << SL->getSourceRange();
      return ExprError();
    }
    if (ForAsmLabel && SL->getString().empty()) {
      Diag(Tok, diag::err_asm_operand_wide_string_literal)
          << 2 /* an empty */ << SL->getSourceRange();
      return ExprError();
    }
  }
  return AsmString;
}

ExprResult Parser::ParseSimpleAsm(bool ForAsmLabel, SourceLocation *EndLoc) {
  assert(Tok.is(tok::kw_asm) && "Not an asm!");
  SourceLocation Loc = ConsumeToken();

  if (isGNUAsmQualifier(Tok)) {
    // Remove from the end of 'asm' to the end of the asm qualifier.
    SourceRange RemovalRange(PP.getLocForEndOfToken(Loc),
                             PP.getLocForEndOfToken(Tok.getLocation()));
    Diag(Tok, diag::err_global_asm_qualifier_ignored)
        << GNUAsmQualifiers::getQualifierName(getGNUAsmQualifier(Tok))
        << FixItHint::CreateRemoval(RemovalRange);
    ConsumeToken();
  }

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen()) {
    Diag(Tok, diag::err_expected_lparen_after)
        << tok::getKeywordSpelling(tok::kw_asm);
    return ExprError();
  }

  ExprResult Result(ParseAsmStringLiteral(ForAsmLabel));

  if (!Result.isInvalid()) {
    // Close the paren and get the location of the end bracket
    T.consumeClose();
    if (EndLoc)
      *EndLoc = T.getCloseLocation();
  } else if (SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch)) {
    if (EndLoc)
      *EndLoc = Tok.getLocation();
    ConsumeParen();
  }

  return Result;
}

Parser::AnnotatedNameKind
Parser::TryAnnotateName(CorrectionCandidateCallback *CCC) {
  assert(Tok.is(tok::identifier));

  IdentifierInfo *Name = Tok.getIdentifierInfo();
  if (isPendingDeclaration(Name)) {
    TryResolveTypeAnnotation();
    return Tok.is(tok::annot_typename) ? ANK_Success : ANK_TentativeDecl;
  }

  return TryAnnotateName(NextToken(), CCC);
}

Parser::AnnotatedNameKind
Parser::TryAnnotateName(const Token &Next, CorrectionCandidateCallback *CCC) {
  assert(Tok.is(tok::identifier));

  IdentifierInfo *Name = Tok.getIdentifierInfo();
  SourceLocation NameLoc = Tok.getLocation();

  if (isPendingDeclaration(Name)) {
    TryResolveTypeAnnotation();
    return Tok.is(tok::annot_typename) ? ANK_Success : ANK_TentativeDecl;
  }

  Sema::NameClassification Classification =
      Actions.ClassifyName(getCurScope(), Name, NameLoc, Next, CCC);

  switch (Classification.getKind()) {
  case Sema::NC_Error:
    return ANK_Error;

  case Sema::NC_Keyword:
    Tok.setIdentifierInfo(Name);
    Tok.setKind(Name->getTokenID());
    PP.TypoCorrectToken(Tok);
    return ANK_Success;

  case Sema::NC_Unknown:
    break;

  case Sema::NC_Type: {
    ParsedType Ty = Classification.getType();

    Tok.setKind(tok::annot_typename);
    setTypeAnnotation(Tok, Ty);
    Tok.setAnnotationEndLoc(Tok.getLocation());
    Tok.setLocation(NameLoc);
    PP.AnnotateCachedTokens(Tok);
    return ANK_Success;
  }

  case Sema::NC_NonType:
    break;
  }

  return ANK_Unresolved;
}

bool Parser::TryKeywordIdentFallback(bool DisableKeyword) {
  assert(Tok.isNot(tok::identifier));
  Diag(Tok, diag::ext_keyword_as_ident)
      << PP.getSpelling(Tok) << DisableKeyword;
  if (DisableKeyword)
    Tok.getIdentifierInfo()->revertTokenIDToIdentifier();
  Tok.setKind(tok::identifier);
  return true;
}

void Parser::TryResolveTypeAnnotation() {
  assert(Tok.is(tok::identifier) &&
         "TryResolveTypeAnnotation is only used for identifier tokens");

  if (ParsedType Ty = Actions.getTypeName(*Tok.getIdentifierInfo(),
                                          Tok.getLocation(), getCurScope(),
                                          NextToken().is(tok::period), true)) {
    // This is a typename. Replace the current token in-place with an
    // annotation type token.
    Tok.setKind(tok::annot_typename);
    setTypeAnnotation(Tok, Ty);
    Tok.setAnnotationEndLoc(Tok.getLocation());

    // In case the tokens were cached, have PrepEngine replace
    // them with the annotation token.
    PP.AnnotateCachedTokens(Tok);
  }
}

bool Parser::isTokenEqualOrEqualTypo() {
  tok::TokenKind Kind = Tok.getKind();
  switch (Kind) {
  default:
    return false;
  case tok::ampequal:            // &=
  case tok::starequal:           // *=
  case tok::plusequal:           // +=
  case tok::minusequal:          // -=
  case tok::exclaimequal:        // !=
  case tok::slashequal:          // /=
  case tok::percentequal:        // %=
  case tok::lessequal:           // <=
  case tok::lesslessequal:       // <<=
  case tok::greaterequal:        // >=
  case tok::greatergreaterequal: // >>=
  case tok::caretequal:          // ^=
  case tok::pipeequal:           // |=
  case tok::equalequal:          // ==
    Diag(Tok, diag::err_invalid_token_after_declarator_suggest_equal)
        << Kind
        << FixItHint::CreateReplacement(SourceRange(Tok.getLocation()), "=");
    [[fallthrough]];
  case tok::equal:
    return true;
  }
}

bool Parser::ParseMicrosoftIfExistsCondition(IfExistsCondition &Result) {
  assert((Tok.is(tok::kw___if_exists) || Tok.is(tok::kw___if_not_exists)) &&
         "Expected '__if_exists' or '__if_not_exists'");
  Result.IsIfExists = Tok.is(tok::kw___if_exists);
  Result.KeywordLoc = ConsumeToken();

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen()) {
    Diag(Tok, diag::err_expected_lparen_after)
        << (Result.IsIfExists
                ? tok::getKeywordSpelling(tok::kw___if_exists)
                : tok::getKeywordSpelling(tok::kw___if_not_exists));
    return true;
  }

  // Parse the unqualified-id.
  if (ParseUnqualifiedId(Result.Name)) {
    T.skipToEnd();
    return true;
  }

  if (T.consumeClose())
    return true;

  switch (Actions.CheckMicrosoftIfExistsSymbol(
      getCurScope(), Result.KeywordLoc, Result.IsIfExists, Result.Name)) {
  case Sema::IER_Exists:
    Result.Behavior = Result.IsIfExists ? IEB_Parse : IEB_Skip;
    break;

  case Sema::IER_DoesNotExist:
    Result.Behavior = !Result.IsIfExists ? IEB_Parse : IEB_Skip;
    break;

  case Sema::IER_Error:
    return true;
  }

  return false;
}

void Parser::ParseMicrosoftIfExistsExternalDeclaration() {
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

  // Parse the declarations.
  while (Tok.isNot(tok::r_brace) && !isEofOrEom()) {
    ParsedAttributes Attrs(AttrFactory);
    MaybeParseBracketAttributes(Attrs);
    ParsedAttributes EmptyDeclSpecAttrs(AttrFactory);
    DeclGroupPtrTy Result = ParseExternalDeclaration(Attrs, EmptyDeclSpecAttrs);
    if (Result && !getCurScope()->getParent())
      Actions.getTreeConsumer().ProcessTopLevelDecl(Result.get());
  }
  Braces.consumeClose();
}

bool BalancedDelimiterTracker::diagnoseOverflow() {
  P.Diag(P.Tok, diag::err_bracket_depth_exceeded)
      << P.getLangOpts().BracketDepth;
  P.Diag(P.Tok, diag::note_bracket_depth);
  P.cutOffParsing();
  return true;
}

bool BalancedDelimiterTracker::expectAndConsume(unsigned DiagID,
                                                const char *Msg,
                                                tok::TokenKind SkipToTok) {
  LOpen = P.Tok.getLocation();
  if (P.RequireToken(Kind, DiagID, Msg)) {
    if (SkipToTok != tok::unknown)
      P.SkipUntil(SkipToTok, Parser::StopAtSemi);
    return true;
  }

  if (getDepth() < P.getLangOpts().BracketDepth)
    return false;

  return diagnoseOverflow();
}

bool BalancedDelimiterTracker::diagnoseMissingClose() {
  assert(!P.Tok.is(Close) && "Should have consumed closing delimiter");

  P.Diag(P.Tok, diag::err_expected) << Close;
  P.Diag(LOpen, diag::note_matching) << Kind;

  // If we're not already at some kind of closing bracket, skip to our closing
  // token.
  if (P.Tok.isNot(tok::r_paren) && P.Tok.isNot(tok::r_brace) &&
      P.Tok.isNot(tok::r_square) &&
      P.SkipUntil(Close, FinalToken,
                  Parser::StopAtSemi | Parser::StopBeforeMatch) &&
      P.Tok.is(Close))
    LClose = P.ConsumeAnyToken();
  return true;
}

void BalancedDelimiterTracker::skipToEnd() {
  P.SkipUntil(Close, Parser::StopBeforeMatch);
  consumeClose();
}
