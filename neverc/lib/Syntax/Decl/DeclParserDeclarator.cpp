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
// Declarator parsing
// ===----------------------------------------------------------------------===

void Parser::ParseDeclarator(Declarator &D) {
  Actions.runWithSufficientStackSpace(D.getBeginLoc(), [&] {
    ParseDeclaratorInternal(D, &Parser::ParseDirectDeclarator);
  });
}

namespace {
bool isPointerOpToken(tok::TokenKind Kind, const LangOptions &Lang,
                      DeclaratorContext TheContext) {
  return Kind == tok::star;
}
} // namespace

void Parser::ParseDeclaratorInternal(Declarator &D,
                                     DirectDeclParseFunction DirectDeclParser) {
  tok::TokenKind Kind = Tok.getKind();

  if (!isPointerOpToken(Kind, getLangOpts(), D.getContext())) {
    if (DirectDeclParser)
      (this->*DirectDeclParser)(D);
    return;
  }

  SourceLocation Loc = ConsumeToken();
  D.SetRangeEnd(Loc);

  assert(Kind == tok::star && "Only pointer operator expected");
  DeclSpec DS(AttrFactory);

  unsigned Reqs = AR_BracketAttributesParsed | AR_DeclspecAttributesParsed |
                  AR_GNUAttributesParsed;
  ParseTypeQualifierListOpt(DS, Reqs, true, !D.mayOmitIdentifier());
  D.ExtendWithDeclSpec(DS);

  Actions.runWithSufficientStackSpace(
      D.getBeginLoc(), [&] { ParseDeclaratorInternal(D, DirectDeclParser); });
  D.AddTypeInfo(DeclaratorChunk::getPointer(
                    DS.getTypeQualifiers(), Loc, DS.getConstSpecLoc(),
                    DS.getVolatileSpecLoc(), DS.getRestrictSpecLoc(),
                    DS.getAtomicSpecLoc(), DS.getUnalignedSpecLoc()),
                std::move(DS.getAttributes()), SourceLocation());
}

namespace {
SourceLocation locateMissingDeclaratorName(Declarator &D, SourceLocation Loc) {
  if (D.getName().getBeginLoc().isInvalid() &&
      D.getName().getEndLoc().isValid())
    return D.getName().getEndLoc();

  return Loc;
}
} // namespace

NEVERC_HOT void Parser::ParseDirectDeclarator(Declarator &D) {
  if (Tok.is(tok::identifier) && D.mayHaveIdentifier()) {
    assert(Tok.getIdentifierInfo() && "Not an identifier?");
    D.SetIdentifier(Tok.getIdentifierInfo(), Tok.getLocation());
    D.SetRangeEnd(Tok.getLocation());
    ConsumeToken();
    goto PastIdentifier;
  } else if (Tok.is(tok::identifier) && !D.mayHaveIdentifier()) {
    // We're not allowed an identifier here, but we got one. Try to figure out
    // if the user was trying to attach a name to the type, or whether the name
    // is some unrelated trailing syntax.
    bool DiagnoseIdentifier = false;
    if (D.hasGroupingParens())
      // An identifier within parens is unlikely to be intended to be anything
      // other than a name being "declared".
      DiagnoseIdentifier = true;
    if (DiagnoseIdentifier) {
      Diag(Tok.getLocation(), diag::err_unexpected_unqualified_id)
          << FixItHint::CreateRemoval(Tok.getLocation());
      D.SetIdentifier(nullptr, Tok.getLocation());
      ConsumeToken();
      goto PastIdentifier;
    }
  }

  if (Tok.is(tok::l_paren)) {
    // If this might be an abstract-declarator followed by a direct-initializer,
    // check whether this is a valid declarator chunk. If it can't be, assume
    // that it's an initializer instead.

    // direct-declarator: '(' declarator ')'
    // direct-declarator: '(' attributes declarator ')'
    // Example: 'char (*X)'   or 'int (*XX)(void)'
    ParseParenDeclarator(D);

  } else if (D.mayOmitIdentifier()) {
    // This could be something simple like "int" (in which case the declarator
    // portion is empty), if an abstract-declarator is allowed.
    D.SetIdentifier(nullptr, Tok.getLocation());

  } else {
    if (Tok.getKind() == tok::annot_pragma_parser_crash)
      LLVM_BUILTIN_TRAP;
    if (Tok.is(tok::l_square))
      return ParseMisplacedBracketDeclarator(D);
    if (D.getContext() == DeclaratorContext::Member) {
      // Detect C keywords and try to prevent further errors by
      // treating these keyword as valid member names.
      const DeclSpec &MDS = D.getDeclSpec();
      Diag(locateMissingDeclaratorName(D, Tok.getLocation()),
           diag::err_expected_member_name_or_semi)
          << (MDS.isEmpty() ? SourceRange() : MDS.getSourceRange());
    } else {
      if (Tok.getKind() == tok::TokenKind::kw_while) {
        Diag(Tok, diag::err_while_loop_outside_of_a_function);
      } else {
        Diag(locateMissingDeclaratorName(D, Tok.getLocation()),
             diag::err_expected_either)
            << tok::identifier << tok::l_paren;
      }
    }
    D.SetIdentifier(nullptr, Tok.getLocation());
    D.setInvalidType(true);
  }

PastIdentifier:
  assert(D.isPastIdentifier() &&
         "Haven't past the location of the identifier yet?");

  // Don't parse attributes unless we have parsed an unparenthesized name.
  if (D.hasName() && !D.getNumTypeObjects())
    MaybeParseBracketAttributes(D);

  while (true) {
    if (Tok.is(tok::l_paren)) {
      bool IsFunctionDeclaration = D.isFunctionDeclaratorAFunctionDeclaration();
      // Enter function-declaration scope, limiting any declarators to the
      // function prototype scope, including parameter declarators.
      ParseScope PrototypeScope(
          this,
          Scope::FunctionPrototypeScope | Scope::DeclScope |
              (IsFunctionDeclaration ? Scope::FunctionDeclarationScope : 0));

      // Parentheses may start a function parameter list or wrap a nested
      // declarator; \c ParseFunctionDeclarator disambiguates.
      bool IsAmbiguous = false;
      ParsedAttributes attrs(AttrFactory);
      BalancedDelimiterTracker T(*this, tok::l_paren);
      T.consumeOpen();
      ParseFunctionDeclarator(D, attrs, T, IsAmbiguous);
      PrototypeScope.Exit();
    } else if (Tok.is(tok::l_square)) {
      ParseBracketDeclarator(D);
    } else if (Tok.isRegularKeywordAttribute()) {
      // For consistency with attribute parsing.
      Diag(Tok, diag::err_keyword_not_allowed) << Tok.getIdentifierInfo();
      ConsumeToken();
    } else {
      break;
    }
  }
}

void Parser::ParseParenDeclarator(Declarator &D) {
  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  assert(!D.isPastIdentifier() && "Should be called before passing identifier");

  // Eat any attributes before we look at whether this is a grouping or function
  // declarator paren.  If this is a grouping paren, the attribute applies to
  // the type being built up, for example:
  //     int (__attribute__(()) *x)(long y)
  // If this ends up not being a grouping paren, the attribute applies to the
  // first argument, for example:
  //     int (__attribute__(()) int x)
  // In either case, we need to eat any attributes to be able to determine what
  // sort of paren this is.
  //
  ParsedAttributes attrs(AttrFactory);
  bool RequiresArg = false;
  if (Tok.is(tok::kw___attribute)) {
    ParseGNUAttributes(attrs);

    // We require that the argument list (if this is a non-grouping paren) be
    // present even if the attribute list was empty.
    RequiresArg = true;
  }

  // Eat any Microsoft extensions.
  ParseMicrosoftTypeAttributes(attrs);

  // If we haven't past the identifier yet (or where the identifier would be
  // stored, if this is an abstract declarator), then this is probably just
  // grouping parens. However, if this could be an abstract-declarator, then
  // this could also be the start of function arguments (consider 'void()').
  bool isGrouping;

  if (!D.mayOmitIdentifier()) {
    // If this can't be an abstract-declarator, this *must* be a grouping
    // paren, because we haven't seen the identifier yet.
    isGrouping = true;
  } else if (Tok.is(tok::r_paren) ||          // 'int()' is a function.
             isDeclarationSpecifier() ||      // 'int(int)' is a function.
             isBracketAttributeSpecifier()) { // 'int([[]]int)' is a function.
    // This handles C99 6.7.5.3p11: in "typedef int X; void foo(X)", X is
    // considered to be a type, not a K&R identifier-list.
    isGrouping = false;
  } else {
    // Otherwise, this is a grouping paren, e.g. 'int (*X)' or 'int(X)'.
    isGrouping = true;
  }

  // If this is a grouping paren, handle:
  // direct-declarator: '(' declarator ')'
  // direct-declarator: '(' attributes declarator ')'
  if (isGrouping) {
    bool hadGroupingParens = D.hasGroupingParens();
    D.setGroupingParens(true);
    ParseDeclaratorInternal(D, &Parser::ParseDirectDeclarator);
    // Match the ')'.
    T.consumeClose();
    D.AddTypeInfo(
        DeclaratorChunk::getParen(T.getOpenLocation(), T.getCloseLocation()),
        std::move(attrs), T.getCloseLocation());

    D.setGroupingParens(hadGroupingParens);

    return;
  }

  // Okay, if this wasn't a grouping paren, it must be the start of a function
  // argument list.  Recognize that this declarator will never have an
  // identifier (and remember where it would have been), then call into
  // ParseFunctionDeclarator to handle of argument list.
  D.SetIdentifier(nullptr, Tok.getLocation());

  // Enter function-declaration scope, limiting any declarators to the
  // function prototype scope, including parameter declarators.
  ParseScope PrototypeScope(this,
                            Scope::FunctionPrototypeScope | Scope::DeclScope |
                                (D.isFunctionDeclaratorAFunctionDeclaration()
                                     ? Scope::FunctionDeclarationScope
                                     : 0));
  ParseFunctionDeclarator(D, attrs, T, false, RequiresArg);
  PrototypeScope.Exit();
}

void Parser::ParseFunctionDeclarator(Declarator &D,
                                     ParsedAttributes &FirstArgAttrs,
                                     BalancedDelimiterTracker &Tracker,
                                     bool IsAmbiguous, bool RequiresArg) {
  assert(getCurScope()->isFunctionPrototypeScope() &&
         "Should call from a Function scope");
  // lparen is already consumed!
  assert(D.isPastIdentifier() && "Should not call before identifier!");

  // This should be true when the function has typed arguments.
  // Otherwise, it is treated as a K&R-style function.
  bool HasProto = false;
  llvm::SmallVector<DeclaratorChunk::ParamInfo, 16> ParamInfo;
  // Remember where we see an ellipsis, if any.
  SourceLocation EllipsisLoc;

  DeclSpec DS(AttrFactory);
  ParsedAttributes FnAttrs(AttrFactory);

  /* LocalEndLoc is the end location for the local FunctionTypeLoc.
     EndLoc is the end location for the function declarator. */
  SourceLocation StartLoc, LocalEndLoc, EndLoc;
  SourceLocation LParenLoc, RParenLoc;
  LParenLoc = Tracker.getOpenLocation();
  StartLoc = LParenLoc;

  if (isFunctionDeclaratorIdentifierList()) {
    if (RequiresArg)
      Diag(Tok, diag::err_argument_required_after_attribute);

    ParseFunctionDeclaratorIdentifierList(D, ParamInfo);

    Tracker.consumeClose();
    RParenLoc = Tracker.getCloseLocation();
    LocalEndLoc = RParenLoc;
    EndLoc = RParenLoc;

    // If there are attributes following the identifier list, parse them and
    // prohibit them.
    MaybeParseBracketAttributes(FnAttrs);
    ProhibitAttributes(FnAttrs);
  } else {
    if (Tok.isNot(tok::r_paren))
      ParseParameterDeclarationClause(FirstArgAttrs, ParamInfo, EllipsisLoc);
    else if (RequiresArg)
      Diag(Tok, diag::err_argument_required_after_attribute);

    // C23 requires strict prototypes.
    HasProto = ParamInfo.size() || getLangOpts().requiresStrictPrototypes();

    // If we have the closing ')', eat it.
    Tracker.consumeClose();
    RParenLoc = Tracker.getCloseLocation();
    LocalEndLoc = RParenLoc;
    EndLoc = RParenLoc;

    MaybeParseBracketAttributes(FnAttrs);
  }

  // Collect non-parameter declarations from the prototype if this is a function
  // declaration; they are moved into the scope of the function.
  llvm::SmallVector<NamedDecl *, 0> DeclsInPrototype;
  if (getCurScope()->isFunctionDeclarationScope()) {
    for (Decl *D : getCurScope()->decls()) {
      NamedDecl *ND = dyn_cast<NamedDecl>(D);
      if (!ND || isa<ParmVarDecl>(ND))
        continue;
      DeclsInPrototype.push_back(ND);
    }
    // Sort DeclsInPrototype based on raw encoding of the source location.
    // Scope::decls() is iterating over a SmallPtrSet so sort the Decls before
    // moving to DeclContext. This provides a stable ordering for traversing
    // Decls in DeclContext, which is important for deterministic output.
    llvm::sort(DeclsInPrototype, [](Decl *D1, Decl *D2) {
      return D1->getLocation().getRawEncoding() <
             D2->getLocation().getRawEncoding();
    });
  }

  // Remember that we parsed a function type, and remember the attributes.
  D.AddTypeInfo(DeclaratorChunk::getFunction(
                    HasProto, IsAmbiguous, LParenLoc, ParamInfo.data(),
                    ParamInfo.size(), EllipsisLoc, RParenLoc, DeclsInPrototype,
                    StartLoc, LocalEndLoc, D, &DS),
                std::move(FnAttrs), EndLoc);
}

bool Parser::isFunctionDeclaratorIdentifierList() {
  if (getLangOpts().requiresStrictPrototypes() || !Tok.is(tok::identifier))
    return false;
  // K&R identifier lists can't have typedefs as identifiers, per C99
  // 6.7.5.3p11.
  TryResolveTypeAnnotation();
  if (Tok.is(tok::annot_typename))
    return false;
  // Identifier lists follow a really simple grammar: the identifiers can
  // be followed *only* by a ", identifier" or ")".  However, K&R
  // identifier lists are really rare in the brave new modern world, and
  // it is very common for someone to typo a type in a non-K&R style
  // list.  If we are presented with something like: "void foo(intptr x,
  // float y)", we don't want to start parsing the function declarator as
  // though it is a K&R style declarator just because intptr is an
  // invalid type.
  //
  // To handle this, we check to see if the token after the first
  // identifier is a "," or ")".  Only then do we parse it as an
  // identifier list.
  return !Tok.is(tok::eof) &&
         (NextToken().is(tok::comma) || NextToken().is(tok::r_paren));
}

void Parser::ParseFunctionDeclaratorIdentifierList(
    Declarator &D,
    llvm::SmallVectorImpl<DeclaratorChunk::ParamInfo> &ParamInfo) {
  assert(
      !getLangOpts().requiresStrictPrototypes() &&
      "Cannot parse a K&R identifier list when strict prototypes are required");

  // If there was no identifier specified for the declarator, either we are in
  // an abstract-declarator, or we are in a parameter declarator which was found
  // to be abstract.  In abstract-declarators, identifier lists are not valid:
  // diagnose this.
  if (!D.getIdentifier())
    Diag(Tok, diag::ext_ident_list_in_param);

  // Maintain an efficient lookup of params we have seen so far.
  llvm::SmallSet<const IdentifierInfo *, 16> ParamsSoFar;

  do {
    // If this isn't an identifier, report the error and skip until ')'.
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected) << tok::identifier;
      SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch);
      // Forget we parsed anything.
      ParamInfo.clear();
      return;
    }

    IdentifierInfo *ParmII = Tok.getIdentifierInfo();

    // Reject 'typedef int y; int test(x, y)', but continue parsing.
    if (Actions.getTypeName(*ParmII, Tok.getLocation(), getCurScope()))
      Diag(Tok, diag::err_unexpected_typedef_ident) << ParmII;

    // Verify that the argument identifier has not already been mentioned.
    if (!ParamsSoFar.insert(ParmII).second) {
      Diag(Tok, diag::err_param_redefinition) << ParmII;
    } else {
      // Remember this identifier in ParamInfo.
      ParamInfo.push_back(
          DeclaratorChunk::ParamInfo(ParmII, Tok.getLocation(), nullptr));
    }

    // Eat the identifier.
    ConsumeToken();
    // The list continues if we see a comma.
  } while (TryConsumeToken(tok::comma));
}

void Parser::ParseParameterDeclarationClause(
    ParsedAttributes &FirstArgAttrs,
    llvm::SmallVectorImpl<DeclaratorChunk::ParamInfo> &ParamInfo,
    SourceLocation &EllipsisLoc) {

  // Avoid exceeding the maximum function scope depth.
  // See https://bugs.llvm.org/show_bug.cgi?id=19607
  // Note Sema::OnParamDeclarator calls ParmVarDecl::setScopeInfo with
  // getFunctionPrototypeDepth() - 1.
  if (getCurScope()->getFunctionPrototypeDepth() - 1 >
      ParmVarDecl::getMaxFunctionScopeDepth()) {
    Diag(Tok.getLocation(), diag::err_function_scope_depth_exceeded)
        << ParmVarDecl::getMaxFunctionScopeDepth();
    cutOffParsing();
    return;
  }

  do {
    if (TryConsumeToken(tok::ellipsis, EllipsisLoc))
      break;

    // Just use the ParsingDeclaration "scope" of the declarator.
    DeclSpec DS(AttrFactory);

    ParsedAttributes ArgDeclAttrs(AttrFactory);
    ParsedAttributes ArgDeclSpecAttrs(AttrFactory);

    if (FirstArgAttrs.Range.isValid()) {
      // If the caller parsed attributes for the first argument, add them now.
      // Take them so that we only apply the attributes to the first parameter.
      // We have already started parsing the decl-specifier sequence, so don't
      // parse any parameter-declaration pieces that precede it.
      ArgDeclSpecAttrs.takeAllFrom(FirstArgAttrs);
    } else {
      MaybeParseBracketAttributes(ArgDeclAttrs);

      // Skip any Microsoft attributes before a param.
      MaybeParseMicrosoftAttributes(ArgDeclSpecAttrs);
    }

    SourceLocation DSStart = Tok.getLocation();

    ParseDeclarationSpecifiers(DS, AS_none, DeclSpecContext::DSC_normal,
                               /*LateAttrs=*/nullptr);

    DS.takeAttributesFrom(ArgDeclSpecAttrs);

    // Parameter lists accept either a declarator or an abstract declarator.
    Declarator ParmDeclarator(DS, ArgDeclAttrs, DeclaratorContext::Prototype);
    ParseDeclarator(ParmDeclarator);

    MaybeParseGNUAttributes(ParmDeclarator);

    // Remember this parsed parameter in ParamInfo.
    IdentifierInfo *ParmII = ParmDeclarator.getIdentifier();

    // If no parameter was specified, verify that *something* was specified,
    // otherwise we have a missing type and identifier.
    if (DS.isEmpty() && ParmDeclarator.getIdentifier() == nullptr &&
        ParmDeclarator.getNumTypeObjects() == 0) {
      // Completely missing, emit error.
      Diag(DSStart, diag::err_missing_param);
    } else {
      // Otherwise, we have something.  Add it and let semantic analysis try
      // to grok it and add the result to the ParamInfo we are building.

      // Now we are at the point where declarator parsing is finished.
      //
      // Try to catch keywords in place of the identifier in a declarator, and
      // in particular the common case where:
      //   1 identifier comes at the end of the declarator
      //   2 if the identifier is dropped, the declarator is valid but anonymous
      //     (no identifier)
      //   3 declarator parsing succeeds, and then we have a trailing keyword,
      //     which is never valid in a param list (e.g. missing a ',')
      // And we can't handle this in ParseDeclarator because in general keywords
      // may be allowed to follow the declarator. (And in some cases there'd be
      // better recovery like inserting punctuation). ParseDeclarator is just
      // treating this as an anonymous parameter, and fortunately at this point
      // we've already almost done that.
      //
      // We care about case 1) where the declarator type should be known, and
      // the identifier should be null.
      if (!ParmDeclarator.isInvalidType() && !ParmDeclarator.hasName() &&
          Tok.isNot(tok::raw_identifier) && !Tok.isAnnotation() &&
          Tok.getIdentifierInfo() &&
          Tok.getIdentifierInfo()->isKeyword(getLangOpts())) {
        Diag(Tok, diag::err_keyword_as_parameter) << PP.getSpelling(Tok);
        // Consume the keyword.
        ConsumeToken();
      }
      // Inform the actions module about the parameter declarator, so it gets
      // added to the current scope.
      Decl *Param = Actions.OnParamDeclarator(getCurScope(), ParmDeclarator);

      ParamInfo.push_back(DeclaratorChunk::ParamInfo(
          ParmII, ParmDeclarator.getIdentifierLoc(), Param, nullptr));
    }

    if (TryConsumeToken(tok::ellipsis, EllipsisLoc)) {
      Diag(EllipsisLoc, diag::err_missing_comma_before_ellipsis)
          << FixItHint::CreateInsertion(EllipsisLoc, ", ");

      break;
    }

    // If the next token is a comma, consume it and keep reading arguments.
  } while (TryConsumeToken(tok::comma));
}

void Parser::ParseBracketDeclarator(Declarator &D) {
  if (CheckProhibitedBracketAttribute())
    return;

  BalancedDelimiterTracker T(*this, tok::l_square);
  T.consumeOpen();

  // C array syntax has many features, but by-far the most common is [] and [4].
  // This code does a fast path to handle some of the most obvious cases.
  if (Tok.getKind() == tok::r_square) {
    T.consumeClose();
    ParsedAttributes attrs(AttrFactory);
    MaybeParseBracketAttributes(attrs);

    // Remember that we parsed the empty array type.
    D.AddTypeInfo(DeclaratorChunk::getArray(0, false, false, nullptr,
                                            T.getOpenLocation(),
                                            T.getCloseLocation()),
                  std::move(attrs), T.getCloseLocation());
    return;
  } else if (Tok.getKind() == tok::numeric_constant &&
             GetLookAheadToken(1).is(tok::r_square)) {
    // [4] is very common.  Parse the numeric constant expression.
    ExprResult ExprRes(Actions.OnNumericConstant(Tok));
    ConsumeToken();

    T.consumeClose();
    ParsedAttributes attrs(AttrFactory);
    MaybeParseBracketAttributes(attrs);

    // Remember that we parsed a array type, and remember its features.
    D.AddTypeInfo(DeclaratorChunk::getArray(0, false, false, ExprRes.get(),
                                            T.getOpenLocation(),
                                            T.getCloseLocation()),
                  std::move(attrs), T.getCloseLocation());
    return;
  }

  // If valid, this location is the position where we read the 'static' keyword.
  SourceLocation StaticLoc;
  TryConsumeToken(tok::kw_static, StaticLoc);

  // If there is a type-qualifier-list, read it now.
  // Type qualifiers in an array subscript are a C99 feature.
  DeclSpec DS(AttrFactory);
  ParseTypeQualifierListOpt(DS, AR_BracketAttributesParsed);

  // If we haven't already read 'static', check to see if there is one after the
  // type-qualifier-list.
  if (!StaticLoc.isValid())
    TryConsumeToken(tok::kw_static, StaticLoc);

  // Handle "direct-declarator [ type-qual-list[opt] * ]".
  bool isStar = false;
  ExprResult NumElements;

  // Handle the case where we have '[*]' as the array size.  However, a leading
  // star could be the start of an expression, for example 'X[*p + 4]'.  Verify
  // the token after the star is a ']'.  Since stars in arrays are
  // infrequent, use of lookahead is not costly here.
  if (Tok.is(tok::star) && GetLookAheadToken(1).is(tok::r_square)) {
    ConsumeToken(); // Eat the '*'.

    if (StaticLoc.isValid()) {
      Diag(StaticLoc, diag::err_unspecified_vla_size_with_static);
      StaticLoc = SourceLocation(); // Drop the static.
    }
    isStar = true;
  } else if (Tok.isNot(tok::r_square)) {
    // Note, in C89, this production uses the constant-expr production instead
    // of assignment-expr.  The only difference is that assignment-expr allows
    // things like '=' and '*='.  Sema rejects these in C89 mode because they
    // are not i-c-e's, so we don't need to distinguish between the two here.

    {
      EnterExpressionEvaluationContext Unevaluated(
          Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
      NumElements =
          Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression());
    }
  } else {
    if (StaticLoc.isValid()) {
      Diag(StaticLoc, diag::err_unspecified_size_with_static);
      StaticLoc = SourceLocation(); // Drop the static.
    }
  }

  // If there was an error parsing the assignment-expression, recover.
  if (NumElements.isInvalid()) {
    D.setInvalidType(true);
    // If the expression was invalid, skip it.
    SkipUntil(tok::r_square, StopAtSemi);
    return;
  }

  T.consumeClose();

  MaybeParseBracketAttributes(DS.getAttributes());

  // Remember that we parsed a array type, and remember its features.
  D.AddTypeInfo(
      DeclaratorChunk::getArray(DS.getTypeQualifiers(), StaticLoc.isValid(),
                                isStar, NumElements.get(), T.getOpenLocation(),
                                T.getCloseLocation()),
      std::move(DS.getAttributes()), T.getCloseLocation());
}

void Parser::ParseMisplacedBracketDeclarator(Declarator &D) {
  assert(Tok.is(tok::l_square) && "Missing opening bracket");
  assert(!D.mayOmitIdentifier() && "Declarator cannot omit identifier");

  SourceLocation StartBracketLoc = Tok.getLocation();
  Declarator TempDeclarator(D.getDeclSpec(), ParsedAttributesView::none(),
                            D.getContext());

  while (Tok.is(tok::l_square)) {
    ParseBracketDeclarator(TempDeclarator);
  }

  // Stuff the location of the start of the brackets into the Declarator.
  // The diagnostics from ParseDirectDeclarator will make more sense if
  // they use this location instead.
  if (Tok.is(tok::semi))
    D.getName().setEndLoc(StartBracketLoc);

  SourceLocation SuggestParenLoc = Tok.getLocation();

  // Now that the brackets are removed, try parsing the declarator again.
  ParseDeclaratorInternal(D, &Parser::ParseDirectDeclarator);

  // Something went wrong parsing the brackets, in which case,
  // ParseBracketDeclarator has emitted an error, and we don't need to emit
  // one here.
  if (TempDeclarator.getNumTypeObjects() == 0)
    return;

  // Determine if parens will need to be suggested in the diagnostic.
  bool NeedParens = false;
  if (D.getNumTypeObjects() != 0) {
    switch (D.getTypeObject(D.getNumTypeObjects() - 1).Kind) {
    case DeclaratorChunk::Pointer:
      NeedParens = true;
      break;
    case DeclaratorChunk::Array:
    case DeclaratorChunk::Function:
    case DeclaratorChunk::Paren:
      break;
    }
  }

  if (NeedParens) {
    SourceLocation EndLoc = PP.getLocForEndOfToken(D.getEndLoc());
    D.AddTypeInfo(DeclaratorChunk::getParen(SuggestParenLoc, EndLoc),
                  SourceLocation());
  }

  // Adding back the bracket info to the end of the Declarator.
  for (unsigned i = 0, e = TempDeclarator.getNumTypeObjects(); i < e; ++i) {
    const DeclaratorChunk &Chunk = TempDeclarator.getTypeObject(i);
    D.AddTypeInfo(Chunk, SourceLocation());
  }

  // The missing identifier would have been diagnosed in ParseDirectDeclarator.
  // If parentheses are required, always suggest them.
  if (!D.getIdentifier() && !NeedParens)
    return;

  SourceLocation EndBracketLoc = TempDeclarator.getEndLoc();

  // Generate the move bracket error message.
  SourceRange BracketRange(StartBracketLoc, EndBracketLoc);
  SourceLocation EndLoc = PP.getLocForEndOfToken(D.getEndLoc());

  if (NeedParens) {
    Diag(EndLoc, diag::err_brackets_go_after_unqualified_id)
        << false << FixItHint::CreateInsertion(SuggestParenLoc, "(")
        << FixItHint::CreateInsertion(EndLoc, ")")
        << FixItHint::CreateInsertionFromRange(
               EndLoc, CharSourceRange(BracketRange, true))
        << FixItHint::CreateRemoval(BracketRange);
  } else {
    Diag(EndLoc, diag::err_brackets_go_after_unqualified_id)
        << false
        << FixItHint::CreateInsertionFromRange(
               EndLoc, CharSourceRange(BracketRange, true))
        << FixItHint::CreateRemoval(BracketRange);
  }
}
