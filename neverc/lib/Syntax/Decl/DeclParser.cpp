#include "neverc/Analyze/EnterExpressionEvaluationContext.h"
#include "neverc/Analyze/Lookup.h"
#include "neverc/Foundation/Attr/AttributeCommonInfo.h"
#include "neverc/Foundation/Attr/Attributes.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/LiteralParser.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/ParserPluginHooks.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/PrettyDeclStackTrace.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include <optional>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Type name parsing
// ===----------------------------------------------------------------------===

TypeResult Parser::ParseTypeName(SourceRange *Range, DeclaratorContext Context,
                                 AccessSpecifier AS, Decl **OwnedType,
                                 ParsedAttributes *Attrs) {
  if (PluginHooks) {
    const SourceLocation Start = Tok.getLocation();
    QualType Extension;
    switch (PluginHooks->parseTypeName(*this, Extension)) {
    case ParserPluginOutcome::Handled:
      if (Range)
        *Range = SourceRange(Start, PrevTokLocation);
      if (OwnedType)
        *OwnedType = nullptr;
      return ParsedType::make(Extension);
    case ParserPluginOutcome::Error:
      if (!Tok.is(tok::eof))
        ConsumeAnyToken();
      return TypeError();
    case ParserPluginOutcome::NotHandled:
      break;
    }
  }

  DeclSpecContext DSC = getDeclSpecContextFromDeclaratorContext(Context);
  if (DSC == DeclSpecContext::DSC_normal)
    DSC = DeclSpecContext::DSC_type_specifier;

  DeclSpec DS(AttrFactory);
  if (Attrs)
    DS.addAttributes(*Attrs);
  ParseSpecifierQualifierList(DS, AS, DSC);
  if (OwnedType)
    *OwnedType = DS.isTypeSpecOwned() ? DS.getRepAsDecl() : nullptr;

  if (Attrs) {
    llvm::SmallVector<ParsedAttr *, 1> ToBeMoved;
    for (ParsedAttr &AL : DS.getAttributes()) {
      if (AL.isDeclspecAttribute())
        ToBeMoved.push_back(&AL);
    }

    for (ParsedAttr *AL : ToBeMoved)
      Attrs->takeOneFrom(DS.getAttributes(), AL);
  }

  Declarator DeclaratorInfo(DS, ParsedAttributesView::none(), Context);
  ParseDeclarator(DeclaratorInfo);
  if (Range)
    *Range = DeclaratorInfo.getSourceRange();

  if (DeclaratorInfo.isInvalidType())
    return true;

  return Actions.OnTypeName(getCurScope(), DeclaratorInfo);
}

// ===----------------------------------------------------------------------===
// Attribute parsing
// ===----------------------------------------------------------------------===

namespace {

LLVM_ATTRIBUTE_ALWAYS_INLINE
llvm::StringRef normalizeAttrName(llvm::StringRef Name) {
  unsigned Len = Name.size();
  if (LLVM_UNLIKELY(Len >= 4)) {
    const char *D = Name.data();
    if (D[0] == '_' && D[1] == '_' && D[Len - 1] == '_' && D[Len - 2] == '_')
      return Name.drop_front(2).drop_back(2);
  }
  return Name;
}

bool isDeferredAttr(const IdentifierInfo &II) {
#define NEVERC_ATTR_LATE_PARSED_LIST
  return llvm::StringSwitch<bool>(normalizeAttrName(II.getName()))
#include "neverc/Syntax/AttrParserStringSwitches.td.h"
      .Default(false);
#undef NEVERC_ATTR_LATE_PARSED_LIST
}

bool locateCommonFileRange(PrepEngine &PP, SourceLocation StartLoc,
                           SourceLocation EndLoc) {
  if (!StartLoc.isMacroID() || !EndLoc.isMacroID())
    return false;

  SourceManager &SM = PP.getSourceManager();
  if (SM.getFileID(StartLoc) != SM.getFileID(EndLoc))
    return false;

  bool AttrStartIsInMacro =
      SourceScanner::isAtStartOfMacroExpansion(StartLoc, SM, PP.getLangOpts());
  bool AttrEndIsInMacro =
      SourceScanner::isAtEndOfMacroExpansion(EndLoc, SM, PP.getLangOpts());
  return AttrStartIsInMacro && AttrEndIsInMacro;
}
} // namespace

void Parser::ParseAttributes(unsigned WhichAttrKinds, ParsedAttributes &Attrs,
                             LateParsedAttrList *LateAttrs) {
  bool MoreToParse;
  do {
    MoreToParse = false;
    if (WhichAttrKinds & PAKM_Bracket)
      MoreToParse |= MaybeParseBracketAttributes(Attrs);
    if (WhichAttrKinds & PAKM_GNU)
      MoreToParse |= MaybeParseGNUAttributes(Attrs, LateAttrs);
    if (WhichAttrKinds & PAKM_Declspec)
      MoreToParse |= MaybeParseMicrosoftDeclSpecs(Attrs);
  } while (MoreToParse);
}

void Parser::ParseGNUAttributes(ParsedAttributes &Attrs,
                                LateParsedAttrList *LateAttrs, Declarator *D) {
  assert(Tok.is(tok::kw___attribute) && "Not a GNU attribute list!");

  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc = StartLoc;

  while (Tok.is(tok::kw___attribute)) {
    SourceLocation AttrTokLoc = ConsumeToken();
    unsigned OldNumAttrs = Attrs.size();
    unsigned OldNumLateAttrs = LateAttrs ? LateAttrs->size() : 0;

    if (RequireToken(tok::l_paren, diag::err_expected_lparen_after,
                     "attribute")) {
      SkipUntil(tok::r_paren, StopAtSemi); // skip until ) or ;
      return;
    }
    if (RequireToken(tok::l_paren, diag::err_expected_lparen_after, "(")) {
      SkipUntil(tok::r_paren, StopAtSemi); // skip until ) or ;
      return;
    }
    do {
      while (TryConsumeToken(tok::comma))
        ;

      if (Tok.isAnnotation())
        break;
      IdentifierInfo *AttrName = Tok.getIdentifierInfo();
      if (!AttrName)
        break;

      SourceLocation AttrNameLoc = ConsumeToken();

      if (Tok.isNot(tok::l_paren)) {
        Attrs.addNew(AttrName, AttrNameLoc, nullptr, AttrNameLoc, nullptr, 0,
                     ParsedAttr::Form::GNU());
        continue;
      }

      if (!LateAttrs || !isDeferredAttr(*AttrName)) {
        ParseGNUAttributeArgs(AttrName, AttrNameLoc, Attrs, &EndLoc, nullptr,
                              SourceLocation(), ParsedAttr::Form::GNU(), D);
        continue;
      }

      LateParsedAttribute *LA =
          new LateParsedAttribute(this, *AttrName, AttrNameLoc);
      LateAttrs->push_back(LA);

      // Be sure CacheTokensUntil doesn't see the start l_paren, since it
      // recursively consumes balanced parens.
      LA->Toks.push_back(Tok);
      ConsumeParen();
      // Consume everything up to and including the matching right parens.
      CacheTokensUntil(tok::r_paren, LA->Toks, /*StopAtSemi=*/true);

      Token Eof;
      Eof.startToken();
      Eof.setLocation(Tok.getLocation());
      LA->Toks.push_back(Eof);
    } while (Tok.is(tok::comma));

    if (RequireToken(tok::r_paren))
      SkipUntil(tok::r_paren, StopAtSemi);
    SourceLocation Loc = Tok.getLocation();
    if (RequireToken(tok::r_paren))
      SkipUntil(tok::r_paren, StopAtSemi);
    EndLoc = Loc;

    // If this was declared in a macro, attach the macro IdentifierInfo to the
    // parsed attribute.
    auto &SM = PP.getSourceManager();
    if (!SM.isWrittenInBuiltinFile(SM.getSpellingLoc(AttrTokLoc)) &&
        locateCommonFileRange(PP, AttrTokLoc, Loc)) {
      CharSourceRange ExpansionRange = SM.getExpansionRange(AttrTokLoc);
      llvm::StringRef FoundName =
          SourceScanner::getSourceText(ExpansionRange, SM, PP.getLangOpts());
      IdentifierInfo *MacroII = PP.getIdentifierInfo(FoundName);

      for (unsigned i = OldNumAttrs; i < Attrs.size(); ++i)
        Attrs[i].setMacroIdentifier(MacroII, ExpansionRange.getBegin());

      if (LateAttrs) {
        for (unsigned i = OldNumLateAttrs; i < LateAttrs->size(); ++i)
          (*LateAttrs)[i]->MacroII = MacroII;
      }
    }
  }

  Attrs.Range = SourceRange(StartLoc, EndLoc);
}

namespace {
bool attrRequiresIdentArg(const IdentifierInfo &II) {
#define NEVERC_ATTR_IDENTIFIER_ARG_LIST
  return llvm::StringSwitch<bool>(normalizeAttrName(II.getName()))
#include "neverc/Syntax/AttrParserStringSwitches.td.h"
      .Default(false);
#undef NEVERC_ATTR_IDENTIFIER_ARG_LIST
}

ParsedAttributeArgumentsProperties
attributeStringLiteralListArg(const IdentifierInfo &II) {
#define NEVERC_ATTR_STRING_LITERAL_ARG_LIST
  return llvm::StringSwitch<uint32_t>(normalizeAttrName(II.getName()))
#include "neverc/Syntax/AttrParserStringSwitches.td.h"
      .Default(0);
#undef NEVERC_ATTR_STRING_LITERAL_ARG_LIST
}

bool attrRequiresVariadicIdentArg(const IdentifierInfo &II) {
#define NEVERC_ATTR_VARIADIC_IDENTIFIER_ARG_LIST
  return llvm::StringSwitch<bool>(normalizeAttrName(II.getName()))
#include "neverc/Syntax/AttrParserStringSwitches.td.h"
      .Default(false);
#undef NEVERC_ATTR_VARIADIC_IDENTIFIER_ARG_LIST
}

bool attrRequiresTypeArg(const IdentifierInfo &II) {
#define NEVERC_ATTR_TYPE_ARG_LIST
  return llvm::StringSwitch<bool>(normalizeAttrName(II.getName()))
#include "neverc/Syntax/AttrParserStringSwitches.td.h"
      .Default(false);
#undef NEVERC_ATTR_TYPE_ARG_LIST
}

bool attrHasUnevaluatedArgs(const IdentifierInfo &II) {
#define NEVERC_ATTR_ARG_CONTEXT_LIST
  return llvm::StringSwitch<bool>(normalizeAttrName(II.getName()))
#include "neverc/Syntax/AttrParserStringSwitches.td.h"
      .Default(false);
#undef NEVERC_ATTR_ARG_CONTEXT_LIST
}
} // namespace

IdentifierLoc *Parser::ParseIdentifierLoc() {
  assert(Tok.is(tok::identifier) && "expected an identifier");
  IdentifierLoc *IL = IdentifierLoc::create(Actions.Context, Tok.getLocation(),
                                            Tok.getIdentifierInfo());
  ConsumeToken();
  return IL;
}

void Parser::ParseAttributeWithTypeArg(IdentifierInfo &AttrName,
                                       SourceLocation AttrNameLoc,
                                       ParsedAttributes &Attrs,
                                       IdentifierInfo *ScopeName,
                                       SourceLocation ScopeLoc,
                                       ParsedAttr::Form Form) {
  BalancedDelimiterTracker Parens(*this, tok::l_paren);
  Parens.consumeOpen();

  TypeResult T;
  if (Tok.isNot(tok::r_paren))
    T = ParseTypeName();

  if (Parens.consumeClose())
    return;

  if (T.isInvalid())
    return;

  if (T.isUsable())
    Attrs.addNewTypeAttr(&AttrName,
                         SourceRange(AttrNameLoc, Parens.getCloseLocation()),
                         ScopeName, ScopeLoc, T.get(), Form);
  else
    Attrs.addNew(&AttrName, SourceRange(AttrNameLoc, Parens.getCloseLocation()),
                 ScopeName, ScopeLoc, nullptr, 0, Form);
}

ExprResult
Parser::ParseUnevaluatedStringInAttribute(const IdentifierInfo &AttrName) {
  if (Tok.is(tok::l_paren)) {
    BalancedDelimiterTracker Paren(*this, tok::l_paren);
    Paren.consumeOpen();
    ExprResult Res = ParseUnevaluatedStringInAttribute(AttrName);
    Paren.consumeClose();
    return Res;
  }
  if (!isTokenStringLiteral()) {
    Diag(Tok.getLocation(), diag::err_expected_string_literal)
        << /*in attribute...*/ 3 << AttrName.getName();
    return ExprError();
  }
  return ParseUnevaluatedStringLiteralExpression();
}

bool Parser::ParseAttributeArgumentList(
    const IdentifierInfo &AttrName, llvm::SmallVectorImpl<Expr *> &Exprs,
    ParsedAttributeArgumentsProperties ArgsProperties) {
  bool SawError = false;
  unsigned Arg = 0;
  while (true) {
    ExprResult Expr;
    if (ArgsProperties.isStringLiteralArg(Arg)) {
      Expr = ParseUnevaluatedStringInAttribute(AttrName);
    } else {
      Expr = ParseAssignmentExpression();
    }
    Expr = Actions.CorrectDelayedTyposInExpr(Expr);

    if (Expr.isInvalid()) {
      SawError = true;
      break;
    }

    Exprs.push_back(Expr.get());

    if (Tok.isNot(tok::comma))
      break;
    ConsumeToken();
    Arg++;
  }

  if (SawError) {
    // Ensure typos get diagnosed when errors were encountered while parsing the
    // expression list.
    for (auto &E : Exprs) {
      ExprResult Expr = Actions.CorrectDelayedTyposInExpr(E);
      if (Expr.isUsable())
        E = Expr.get();
    }
  }
  return SawError;
}

unsigned Parser::ParseAttributeArgsCommon(
    IdentifierInfo *AttrName, SourceLocation AttrNameLoc,
    ParsedAttributes &Attrs, SourceLocation *EndLoc, IdentifierInfo *ScopeName,
    SourceLocation ScopeLoc, ParsedAttr::Form Form) {
  // Ignore the left paren location for now.
  ConsumeParen();

  bool AttributeIsTypeArgAttr = attrRequiresTypeArg(*AttrName);
  bool AttributeHasVariadicIdentifierArg =
      attrRequiresVariadicIdentArg(*AttrName);

  ArgsVector ArgExprs;
  if (Tok.is(tok::identifier)) {
    // If this attribute wants an 'identifier' argument, make it so.
    bool IsIdentifierArg =
        AttributeHasVariadicIdentifierArg || attrRequiresIdentArg(*AttrName);
    ParsedAttr::Kind AttrKind =
        ParsedAttr::getParsedKind(AttrName, ScopeName, Form.getSyntax());

    // If we don't know how to parse this attribute, but this is the only
    // token in this argument, assume it's meant to be an identifier.
    if (AttrKind == ParsedAttr::UnknownAttribute ||
        AttrKind == ParsedAttr::IgnoredAttribute) {
      const Token &Next = NextToken();
      IsIdentifierArg = Next.isOneOf(tok::r_paren, tok::comma);
    }

    if (IsIdentifierArg)
      ArgExprs.push_back(ParseIdentifierLoc());
  }

  ParsedType TheParsedType;
  if (!ArgExprs.empty() ? Tok.is(tok::comma) : Tok.isNot(tok::r_paren)) {
    // Eat the comma.
    if (!ArgExprs.empty())
      ConsumeToken();

    if (AttributeIsTypeArgAttr) {
      TypeResult T = ParseTypeName();
      if (T.isInvalid()) {
        SkipUntil(tok::r_paren, StopAtSemi);
        return 0;
      }
      if (T.isUsable())
        TheParsedType = T.get();
    } else if (AttributeHasVariadicIdentifierArg) {
      // Parse variadic identifier arg. This can either consume identifiers or
      // expressions. Variadic identifier args do not support parameter packs
      // because those are typically used for attributes with enumeration
      // arguments, and those enumerations are not something the user could
      // express via a pack.
      do {
        ExprResult ArgExpr;
        if (Tok.is(tok::identifier)) {
          ArgExprs.push_back(ParseIdentifierLoc());
        } else {
          bool Uneval = attrHasUnevaluatedArgs(*AttrName);
          EnterExpressionEvaluationContext Unevaluated(
              Actions,
              Uneval ? Sema::ExpressionEvaluationContext::Unevaluated
                     : Sema::ExpressionEvaluationContext::ConstantEvaluated);

          ExprResult ArgExpr(
              Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression()));

          if (ArgExpr.isInvalid()) {
            SkipUntil(tok::r_paren, StopAtSemi);
            return 0;
          }
          ArgExprs.push_back(ArgExpr.get());
        }
        // Eat the comma, move to the next argument
      } while (TryConsumeToken(tok::comma));
    } else {
      // General case. Parse all available expressions.
      bool Uneval = attrHasUnevaluatedArgs(*AttrName);
      EnterExpressionEvaluationContext Unevaluated(
          Actions, Uneval
                       ? Sema::ExpressionEvaluationContext::Unevaluated
                       : Sema::ExpressionEvaluationContext::ConstantEvaluated);

      ExprVector ParsedExprs;
      ParsedAttributeArgumentsProperties ArgProperties =
          attributeStringLiteralListArg(*AttrName);
      if (ParseAttributeArgumentList(*AttrName, ParsedExprs, ArgProperties)) {
        SkipUntil(tok::r_paren, StopAtSemi);
        return 0;
      }

      ArgExprs.insert(ArgExprs.end(), ParsedExprs.begin(), ParsedExprs.end());
    }
  }

  SourceLocation RParen = Tok.getLocation();
  if (!RequireToken(tok::r_paren)) {
    SourceLocation AttrLoc = ScopeLoc.isValid() ? ScopeLoc : AttrNameLoc;

    if (AttributeIsTypeArgAttr && !TheParsedType.get().isNull()) {
      Attrs.addNewTypeAttr(AttrName, SourceRange(AttrNameLoc, RParen),
                           ScopeName, ScopeLoc, TheParsedType, Form);
    } else {
      Attrs.addNew(AttrName, SourceRange(AttrLoc, RParen), ScopeName, ScopeLoc,
                   ArgExprs.data(), ArgExprs.size(), Form);
    }
  }

  if (EndLoc)
    *EndLoc = RParen;

  return static_cast<unsigned>(ArgExprs.size() + !TheParsedType.get().isNull());
}

void Parser::ParseGNUAttributeArgs(
    IdentifierInfo *AttrName, SourceLocation AttrNameLoc,
    ParsedAttributes &Attrs, SourceLocation *EndLoc, IdentifierInfo *ScopeName,
    SourceLocation ScopeLoc, ParsedAttr::Form Form, Declarator *D) {

  assert(Tok.is(tok::l_paren) && "Attribute arg list not starting with '('");

  ParsedAttr::Kind AttrKind =
      ParsedAttr::getParsedKind(AttrName, ScopeName, Form.getSyntax());

  if (AttrKind == ParsedAttr::AT_Availability) {
    ParseAvailabilityAttribute(*AttrName, AttrNameLoc, Attrs, EndLoc, ScopeName,
                               ScopeLoc, Form);
    return;
  } else if (AttrKind == ParsedAttr::AT_TypeTagForDatatype) {
    ParseTypeTagForDatatypeAttribute(*AttrName, AttrNameLoc, Attrs, EndLoc,
                                     ScopeName, ScopeLoc, Form);
    return;
  } else if (attrRequiresTypeArg(*AttrName)) {
    ParseAttributeWithTypeArg(*AttrName, AttrNameLoc, Attrs, ScopeName,
                              ScopeLoc, Form);
    return;
  }

  // These may refer to the function arguments, but need to be parsed early to
  // participate in determining whether it's a redeclaration.
  std::optional<ParseScope> PrototypeScope;
  if (normalizeAttrName(AttrName->getName()) == "enable_if" && D &&
      D->isFunctionDeclarator()) {
    DeclaratorChunk::FunctionTypeInfo FTI = D->getFunctionTypeInfo();
    PrototypeScope.emplace(this, Scope::FunctionPrototypeScope |
                                     Scope::FunctionDeclarationScope |
                                     Scope::DeclScope);
    for (unsigned i = 0; i != FTI.NumParams; ++i) {
      ParmVarDecl *Param = cast<ParmVarDecl>(FTI.Params[i].Param);
      getCurScope()->AddDecl(Param);
    }
  }

  ParseAttributeArgsCommon(AttrName, AttrNameLoc, Attrs, EndLoc, ScopeName,
                           ScopeLoc, Form);
}

unsigned Parser::ParseCustomAttributeArgs(
    IdentifierInfo *AttrName, SourceLocation AttrNameLoc,
    ParsedAttributes &Attrs, SourceLocation *EndLoc, IdentifierInfo *ScopeName,
    SourceLocation ScopeLoc, ParsedAttr::Form Form) {
  assert(Tok.is(tok::l_paren) && "Attribute arg list not starting with '('");

  ParsedAttr::Kind AttrKind =
      ParsedAttr::getParsedKind(AttrName, ScopeName, Form.getSyntax());

  switch (AttrKind) {
  default:
    return ParseAttributeArgsCommon(AttrName, AttrNameLoc, Attrs, EndLoc,
                                    ScopeName, ScopeLoc, Form);
  case ParsedAttr::AT_Availability:
    ParseAvailabilityAttribute(*AttrName, AttrNameLoc, Attrs, EndLoc, ScopeName,
                               ScopeLoc, Form);
    break;
  case ParsedAttr::AT_TypeTagForDatatype:
    ParseTypeTagForDatatypeAttribute(*AttrName, AttrNameLoc, Attrs, EndLoc,
                                     ScopeName, ScopeLoc, Form);
    break;
  }
  return !Attrs.empty() ? Attrs.begin()->getNumArgs() : 0;
}

bool Parser::ParseMicrosoftDeclSpecArgs(IdentifierInfo *AttrName,
                                        SourceLocation AttrNameLoc,
                                        ParsedAttributes &Attrs) {
  unsigned ExistingAttrs = Attrs.size();

  // If the attribute isn't known, we will not attempt to parse any
  // arguments.
  if (!hasAttribute(AttributeCommonInfo::Syntax::AS_Declspec, nullptr, AttrName,
                    getTargetInfo(), getLangOpts())) {
    // Eat the left paren, then skip to the ending right paren.
    ConsumeParen();
    SkipUntil(tok::r_paren);
    return false;
  }

  SourceLocation OpenParenLoc = Tok.getLocation();

  unsigned NumArgs =
      ParseAttributeArgsCommon(AttrName, AttrNameLoc, Attrs, nullptr, nullptr,
                               SourceLocation(), ParsedAttr::Form::Declspec());

  // If this attribute's args were parsed, and it was expected to have
  // arguments but none were provided, emit a diagnostic.
  if (ExistingAttrs < Attrs.size() && Attrs.back().getMaxArgs() && !NumArgs) {
    Diag(OpenParenLoc, diag::err_attribute_requires_arguments) << AttrName;
    return false;
  }
  return true;
}

void Parser::ParseMicrosoftDeclSpecs(ParsedAttributes &Attrs) {
  assert(getLangOpts().DeclSpecKeyword && "__declspec keyword is not enabled");
  assert(Tok.is(tok::kw___declspec) && "Not a declspec!");

  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc = StartLoc;

  while (Tok.is(tok::kw___declspec)) {
    ConsumeToken();
    BalancedDelimiterTracker T(*this, tok::l_paren);
    if (T.expectAndConsume(diag::err_expected_lparen_after,
                           tok::getKeywordSpelling(tok::kw___declspec),
                           tok::r_paren))
      return;

    // An empty declspec is perfectly legal and should not warn.  Additionally,
    // you can specify multiple attributes per declspec.
    while (Tok.isNot(tok::r_paren)) {
      // Attribute not present.
      if (TryConsumeToken(tok::comma))
        continue;

      // We expect either a well-known identifier or a generic string.  Anything
      // else is a malformed declspec.
      bool IsString = Tok.getKind() == tok::string_literal;
      if (!IsString && Tok.getKind() != tok::identifier &&
          Tok.getKind() != tok::kw_restrict) {
        Diag(Tok, diag::err_ms_declspec_type);
        T.skipToEnd();
        return;
      }

      IdentifierInfo *AttrName;
      SourceLocation AttrNameLoc;
      if (IsString) {
        llvm::SmallString<8> StrBuffer;
        bool Invalid = false;
        llvm::StringRef Str = PP.getSpelling(Tok, StrBuffer, &Invalid);
        if (Invalid) {
          T.skipToEnd();
          return;
        }
        if (Str.front() == '"' && Str.back() == '"')
          Str = Str.drop_front(1).drop_back(1);
        AttrName = PP.getIdentifierInfo(Str);
        AttrNameLoc = ConsumeStringToken();
      } else {
        AttrName = Tok.getIdentifierInfo();
        AttrNameLoc = ConsumeToken();
      }

      bool AttrHandled = false;

      if (Tok.is(tok::l_paren))
        AttrHandled = ParseMicrosoftDeclSpecArgs(AttrName, AttrNameLoc, Attrs);
      else if (AttrName->getName() == "property")
        // The property attribute must have an argument list.
        Diag(Tok.getLocation(), diag::err_expected_lparen_after)
            << AttrName->getName();

      if (!AttrHandled)
        Attrs.addNew(AttrName, AttrNameLoc, nullptr, AttrNameLoc, nullptr, 0,
                     ParsedAttr::Form::Declspec());
    }
    T.consumeClose();
    EndLoc = T.getCloseLocation();
  }

  Attrs.Range = SourceRange(StartLoc, EndLoc);
}

void Parser::ParseMicrosoftTypeAttributes(ParsedAttributes &attrs) {
  // Treat these like attributes
  while (true) {
    // [MSVC Compatibility]
    auto Kind = Tok.getKind();
    switch (Kind) {
    case tok::kw___fastcall:
    case tok::kw___stdcall:
    case tok::kw___regcall:
    case tok::kw___cdecl:
    case tok::kw_cdecl:
    case tok::kw___vectorcall:
    case tok::kw___ptr64:
    case tok::kw___w64:
    case tok::kw___ptr32:
    case tok::kw___sptr:
    case tok::kw___uptr: {
      IdentifierInfo *AttrName = Tok.getIdentifierInfo();
      SourceLocation AttrNameLoc = ConsumeToken();
      attrs.addNew(AttrName, AttrNameLoc, nullptr, AttrNameLoc, nullptr, 0,
                   Kind);
      // [MSVC Compatibility]
      if (Kind == tok::kw___stdcall || Kind == tok::kw___cdecl ||
          Kind == tok::kw_cdecl || Kind == tok::kw___fastcall ||
          Kind == tok::kw___regcall || Kind == tok::kw___vectorcall) {
        if (Tok.is(tok::r_paren) && NextToken().is(tok::l_paren)) {
          ConsumeParen();
        }
      }
      break;
    }
    default:
      return;
    }
  }
}

void Parser::DiagnoseAndSkipExtendedMicrosoftTypeAttributes() {
  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc = SkipExtendedMicrosoftTypeAttributes();

  if (EndLoc.isValid()) {
    SourceRange Range(StartLoc, EndLoc);
    Diag(StartLoc, diag::warn_microsoft_qualifiers_ignored) << Range;
  }
}

SourceLocation Parser::SkipExtendedMicrosoftTypeAttributes() {
  SourceLocation EndLoc;

  while (true) {
    switch (Tok.getKind()) {
    case tok::kw_const:
    case tok::kw_volatile:
    case tok::kw___fastcall:
    case tok::kw___stdcall:
    case tok::kw___cdecl:
    case tok::kw_cdecl:
    case tok::kw___vectorcall:
    case tok::kw___ptr32:
    case tok::kw___ptr64:
    case tok::kw___w64:
    case tok::kw___unaligned:
    case tok::kw___sptr:
    case tok::kw___uptr:
      EndLoc = ConsumeToken();
      break;
    default:
      return EndLoc;
    }
  }
}

void Parser::ParseNullabilityTypeSpecifiers(ParsedAttributes &attrs) {
  // Treat these like attributes, even though they're type specifiers.
  while (true) {
    auto Kind = Tok.getKind();
    switch (Kind) {
    case tok::kw__Nonnull:
    case tok::kw__Nullable:
    case tok::kw__Null_unspecified: {
      IdentifierInfo *AttrName = Tok.getIdentifierInfo();
      SourceLocation AttrNameLoc = ConsumeToken();
      Diag(AttrNameLoc, diag::ext_nullability) << AttrName;
      attrs.addNew(AttrName, AttrNameLoc, nullptr, AttrNameLoc, nullptr, 0,
                   Kind);
      break;
    }
    default:
      return;
    }
  }
}

namespace {
bool isVersionDelimiter(const char Separator) {
  return (Separator == '.' || Separator == '_');
}
} // namespace

llvm::VersionTuple Parser::ParseVersionTuple(SourceRange &Range) {
  Range = SourceRange(Tok.getLocation(), Tok.getEndLoc());

  if (!Tok.is(tok::numeric_constant)) {
    Diag(Tok, diag::err_expected_version);
    SkipUntil(tok::comma, tok::r_paren, StopAtSemi | StopBeforeMatch);
    return llvm::VersionTuple();
  }

  // Parse the major (and possibly minor and subminor) versions, which
  // are stored in the numeric constant. We utilize a quirk of the
  // lexer, which is that it handles something like 1.2.3 as a single
  // numeric constant, rather than two separate tokens.
  llvm::SmallString<512> Buffer;
  Buffer.resize(Tok.getLength() + 1);
  const char *ThisTokBegin = &Buffer[0];

  bool Invalid = false;
  unsigned ActualLength = PP.getSpelling(Tok, ThisTokBegin, &Invalid);
  if (Invalid)
    return llvm::VersionTuple();

  unsigned AfterMajor = 0;
  unsigned Major = 0;
  while (AfterMajor < ActualLength && isDigit(ThisTokBegin[AfterMajor])) {
    Major = Major * 10 + ThisTokBegin[AfterMajor] - '0';
    ++AfterMajor;
  }

  if (AfterMajor == 0) {
    Diag(Tok, diag::err_expected_version);
    SkipUntil(tok::comma, tok::r_paren, StopAtSemi | StopBeforeMatch);
    return llvm::VersionTuple();
  }

  if (AfterMajor == ActualLength) {
    ConsumeToken();

    // We only had a single version component.
    if (Major == 0) {
      Diag(Tok, diag::err_zero_version);
      return llvm::VersionTuple();
    }

    return llvm::VersionTuple(Major);
  }

  const char AfterMajorSeparator = ThisTokBegin[AfterMajor];
  if (!isVersionDelimiter(AfterMajorSeparator) ||
      (AfterMajor + 1 == ActualLength)) {
    Diag(Tok, diag::err_expected_version);
    SkipUntil(tok::comma, tok::r_paren, StopAtSemi | StopBeforeMatch);
    return llvm::VersionTuple();
  }

  unsigned AfterMinor = AfterMajor + 1;
  unsigned Minor = 0;
  while (AfterMinor < ActualLength && isDigit(ThisTokBegin[AfterMinor])) {
    Minor = Minor * 10 + ThisTokBegin[AfterMinor] - '0';
    ++AfterMinor;
  }

  if (AfterMinor == ActualLength) {
    ConsumeToken();

    // We had major.minor.
    if (Major == 0 && Minor == 0) {
      Diag(Tok, diag::err_zero_version);
      return llvm::VersionTuple();
    }

    return llvm::VersionTuple(Major, Minor);
  }

  const char AfterMinorSeparator = ThisTokBegin[AfterMinor];
  // If what follows is not a '.' or '_', we have a problem.
  if (!isVersionDelimiter(AfterMinorSeparator)) {
    Diag(Tok, diag::err_expected_version);
    SkipUntil(tok::comma, tok::r_paren, StopAtSemi | StopBeforeMatch);
    return llvm::VersionTuple();
  }

  // Warn if separators, be it '.' or '_', do not match.
  if (AfterMajorSeparator != AfterMinorSeparator)
    Diag(Tok, diag::warn_expected_consistent_version_separator);

  unsigned AfterSubminor = AfterMinor + 1;
  unsigned Subminor = 0;
  while (AfterSubminor < ActualLength && isDigit(ThisTokBegin[AfterSubminor])) {
    Subminor = Subminor * 10 + ThisTokBegin[AfterSubminor] - '0';
    ++AfterSubminor;
  }

  if (AfterSubminor != ActualLength) {
    Diag(Tok, diag::err_expected_version);
    SkipUntil(tok::comma, tok::r_paren, StopAtSemi | StopBeforeMatch);
    return llvm::VersionTuple();
  }
  ConsumeToken();
  return llvm::VersionTuple(Major, Minor, Subminor);
}

void Parser::ParseAvailabilityAttribute(
    IdentifierInfo &Availability, SourceLocation AvailabilityLoc,
    ParsedAttributes &attrs, SourceLocation *endLoc, IdentifierInfo *ScopeName,
    SourceLocation ScopeLoc, ParsedAttr::Form Form) {
  enum { Introduced, Deprecated, Obsoleted, Unknown };
  AvailabilityChange Changes[Unknown];
  ExprResult MessageExpr, ReplacementExpr;

  // Opening '('.
  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen()) {
    Diag(Tok, diag::err_expected) << tok::l_paren;
    return;
  }

  if (Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::err_availability_expected_platform);
    SkipUntil(tok::r_paren, StopAtSemi);
    return;
  }
  IdentifierLoc *Platform = ParseIdentifierLoc();
  if (const IdentifierInfo *const Ident = Platform->Ident) {
    // Canonicalize platform name from "macosx" to "macos".
    if (Ident->getName() == "macosx")
      Platform->Ident = PP.getIdentifierInfo("macos");
    // Canonicalize platform name from "macosx_app_extension" to
    // "macos_app_extension".
    else if (Ident->getName() == "macosx_app_extension")
      Platform->Ident = PP.getIdentifierInfo("macos_app_extension");
    else
      Platform->Ident = PP.getIdentifierInfo(
          AvailabilityAttr::canonicalizePlatformName(Ident->getName()));
  }

  if (RequireToken(tok::comma)) {
    SkipUntil(tok::r_paren, StopAtSemi);
    return;
  }

  // If we haven't grabbed the pointers for the identifiers
  // "introduced", "deprecated", and "obsoleted", do so now.
  if (!Ident_introduced) {
    Ident_introduced = PP.getIdentifierInfo("introduced");
    Ident_deprecated = PP.getIdentifierInfo("deprecated");
    Ident_obsoleted = PP.getIdentifierInfo("obsoleted");
    Ident_unavailable = PP.getIdentifierInfo("unavailable");
    Ident_message = PP.getIdentifierInfo("message");
    Ident_strict = PP.getIdentifierInfo("strict");
    Ident_replacement = PP.getIdentifierInfo("replacement");
  }

  // Parse the optional "strict", the optional "replacement" and the set of
  // introductions/deprecations/removals.
  SourceLocation UnavailableLoc, StrictLoc;
  do {
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_availability_expected_change);
      SkipUntil(tok::r_paren, StopAtSemi);
      return;
    }
    IdentifierInfo *Keyword = Tok.getIdentifierInfo();
    SourceLocation KeywordLoc = ConsumeToken();

    if (Keyword == Ident_strict) {
      if (StrictLoc.isValid()) {
        Diag(KeywordLoc, diag::err_availability_redundant)
            << Keyword << SourceRange(StrictLoc);
      }
      StrictLoc = KeywordLoc;
      continue;
    }

    if (Keyword == Ident_unavailable) {
      if (UnavailableLoc.isValid()) {
        Diag(KeywordLoc, diag::err_availability_redundant)
            << Keyword << SourceRange(UnavailableLoc);
      }
      UnavailableLoc = KeywordLoc;
      continue;
    }

    if (Tok.isNot(tok::equal)) {
      Diag(Tok, diag::err_expected_after) << Keyword << tok::equal;
      SkipUntil(tok::r_paren, StopAtSemi);
      return;
    }
    ConsumeToken();
    if (Keyword == Ident_message || Keyword == Ident_replacement) {
      if (!isTokenStringLiteral()) {
        Diag(Tok, diag::err_expected_string_literal)
            << /*Source='availability attribute'*/ 2;
        SkipUntil(tok::r_paren, StopAtSemi);
        return;
      }
      if (Keyword == Ident_message) {
        MessageExpr = ParseUnevaluatedStringLiteralExpression();
        break;
      } else {
        ReplacementExpr = ParseUnevaluatedStringLiteralExpression();
        continue;
      }
    }

    // Special handling of 'NA' only when applied to introduced or
    // deprecated.
    if ((Keyword == Ident_introduced || Keyword == Ident_deprecated) &&
        Tok.is(tok::identifier)) {
      IdentifierInfo *NA = Tok.getIdentifierInfo();
      if (NA->getName() == "NA") {
        ConsumeToken();
        if (Keyword == Ident_introduced)
          UnavailableLoc = KeywordLoc;
        continue;
      }
    }

    SourceRange VersionRange;
    llvm::VersionTuple Version = ParseVersionTuple(VersionRange);

    if (Version.empty()) {
      SkipUntil(tok::r_paren, StopAtSemi);
      return;
    }

    unsigned Index;
    if (Keyword == Ident_introduced)
      Index = Introduced;
    else if (Keyword == Ident_deprecated)
      Index = Deprecated;
    else if (Keyword == Ident_obsoleted)
      Index = Obsoleted;
    else
      Index = Unknown;

    if (Index < Unknown) {
      if (!Changes[Index].KeywordLoc.isInvalid()) {
        Diag(KeywordLoc, diag::err_availability_redundant)
            << Keyword
            << SourceRange(Changes[Index].KeywordLoc,
                           Changes[Index].VersionRange.getEnd());
      }

      Changes[Index].KeywordLoc = KeywordLoc;
      Changes[Index].Version = Version;
      Changes[Index].VersionRange = VersionRange;
    } else {
      Diag(KeywordLoc, diag::err_availability_unknown_change)
          << Keyword << VersionRange;
    }

  } while (TryConsumeToken(tok::comma));

  // Closing ')'.
  if (T.consumeClose())
    return;

  if (endLoc)
    *endLoc = T.getCloseLocation();

  // The 'unavailable' availability cannot be combined with any other
  // availability changes. Make sure that hasn't happened.
  if (UnavailableLoc.isValid()) {
    bool Complained = false;
    for (unsigned Index = Introduced; Index != Unknown; ++Index) {
      if (Changes[Index].KeywordLoc.isValid()) {
        if (!Complained) {
          Diag(UnavailableLoc, diag::warn_availability_and_unavailable)
              << SourceRange(Changes[Index].KeywordLoc,
                             Changes[Index].VersionRange.getEnd());
          Complained = true;
        }

        // Clear out the availability.
        Changes[Index] = AvailabilityChange();
      }
    }
  }

  // Record this attribute
  attrs.addNew(&Availability,
               SourceRange(AvailabilityLoc, T.getCloseLocation()), ScopeName,
               ScopeLoc, Platform, Changes[Introduced], Changes[Deprecated],
               Changes[Obsoleted], UnavailableLoc, MessageExpr.get(), Form,
               StrictLoc, ReplacementExpr.get());
}

void Parser::ParseTypeTagForDatatypeAttribute(
    IdentifierInfo &AttrName, SourceLocation AttrNameLoc,
    ParsedAttributes &Attrs, SourceLocation *EndLoc, IdentifierInfo *ScopeName,
    SourceLocation ScopeLoc, ParsedAttr::Form Form) {
  assert(Tok.is(tok::l_paren) && "Attribute arg list not starting with '('");

  BalancedDelimiterTracker T(*this, tok::l_paren);
  T.consumeOpen();

  if (Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::err_expected) << tok::identifier;
    T.skipToEnd();
    return;
  }
  IdentifierLoc *ArgumentKind = ParseIdentifierLoc();

  if (RequireToken(tok::comma)) {
    T.skipToEnd();
    return;
  }

  SourceRange MatchingCTypeRange;
  TypeResult MatchingCType = ParseTypeName(&MatchingCTypeRange);
  if (MatchingCType.isInvalid()) {
    T.skipToEnd();
    return;
  }

  bool LayoutCompatible = false;
  bool MustBeNull = false;
  while (TryConsumeToken(tok::comma)) {
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected) << tok::identifier;
      T.skipToEnd();
      return;
    }
    IdentifierInfo *Flag = Tok.getIdentifierInfo();
    if (Flag->isStr("layout_compatible"))
      LayoutCompatible = true;
    else if (Flag->isStr("must_be_null"))
      MustBeNull = true;
    else {
      Diag(Tok, diag::err_type_safety_unknown_flag) << Flag;
      T.skipToEnd();
      return;
    }
    ConsumeToken(); // consume flag
  }

  if (!T.consumeClose()) {
    Attrs.addNewTypeTagForDatatype(&AttrName, AttrNameLoc, ScopeName, ScopeLoc,
                                   ArgumentKind, MatchingCType.get(),
                                   LayoutCompatible, MustBeNull, Form);
  }

  if (EndLoc)
    *EndLoc = T.getCloseLocation();
}

bool Parser::DiagnoseProhibitedBracketAttribute() {
  assert(Tok.is(tok::l_square) && NextToken().is(tok::l_square));

  switch (isBracketAttributeSpecifier(/*Disambiguate*/ true)) {
  case CAK_NotAttributeSpecifier:
    // No diagnostic: this is not actually an attribute in this context.
    return false;

  case CAK_InvalidAttributeSpecifier:
    Diag(Tok.getLocation(), diag::err_l_square_l_square_not_attribute);
    return false;

  case CAK_AttributeSpecifier:
    SourceLocation BeginLoc = ConsumeBracket();
    ConsumeBracket();
    SkipUntil(tok::r_square);
    assert(Tok.is(tok::r_square) && "isBracketAttributeSpecifier lied");
    SourceLocation EndLoc = ConsumeBracket();
    Diag(BeginLoc, diag::err_attributes_not_allowed)
        << SourceRange(BeginLoc, EndLoc);
    return true;
  }
}

void Parser::DiagnoseProhibitedAttributes(
    const ParsedAttributesView &Attrs, const SourceLocation CorrectLocation) {
  auto *FirstAttr = Attrs.empty() ? nullptr : &Attrs.front();
  if (CorrectLocation.isValid()) {
    CharSourceRange AttrRange(Attrs.Range, true);
    (FirstAttr && FirstAttr->isRegularKeywordAttribute()
         ? Diag(CorrectLocation, diag::err_keyword_misplaced) << FirstAttr
         : Diag(CorrectLocation, diag::err_attributes_misplaced))
        << FixItHint::CreateInsertionFromRange(CorrectLocation, AttrRange)
        << FixItHint::CreateRemoval(AttrRange);
  } else {
    const SourceRange &Range = Attrs.Range;
    (FirstAttr && FirstAttr->isRegularKeywordAttribute()
         ? Diag(Range.getBegin(), diag::err_keyword_not_allowed) << FirstAttr
         : Diag(Range.getBegin(), diag::err_attributes_not_allowed))
        << Range;
  }
}

void Parser::ProhibitBracketAttributes(ParsedAttributes &Attrs,
                                       unsigned AttrDiagID,
                                       unsigned KeywordDiagID,
                                       bool DiagnoseEmptyAttrs,
                                       bool WarnOnUnknownAttrs) {

  if (DiagnoseEmptyAttrs && Attrs.empty() && Attrs.Range.isValid()) {
    // An attribute list has been parsed, but it was empty.
    // This is the case for [[]].
    const auto &LangOpts = getLangOpts();
    auto &SM = PP.getSourceManager();
    Token FirstLSquare;
    SourceScanner::scanRawToken(Attrs.Range.getBegin(), FirstLSquare, SM,
                                LangOpts);

    if (FirstLSquare.is(tok::l_square)) {
      std::optional<Token> SecondLSquare = SourceScanner::peekNextToken(
          FirstLSquare.getLocation(), SM, LangOpts);

      if (SecondLSquare && SecondLSquare->is(tok::l_square)) {
        // The attribute range starts with [[, but is empty. So this must
        // be [[]], which we are supposed to diagnose because
        // DiagnoseEmptyAttrs is true.
        Diag(Attrs.Range.getBegin(), AttrDiagID) << Attrs.Range;
        return;
      }
    }
  }

  for (const ParsedAttr &AL : Attrs) {
    if (AL.isRegularKeywordAttribute()) {
      Diag(AL.getLoc(), KeywordDiagID) << AL;
      AL.setInvalid();
      continue;
    }
    if (!AL.isStandardAttributeSyntax())
      continue;
    if (AL.getKind() == ParsedAttr::UnknownAttribute) {
      if (WarnOnUnknownAttrs)
        Diag(AL.getLoc(), diag::warn_unknown_attribute_ignored)
            << AL << AL.getRange();
    } else {
      Diag(AL.getLoc(), AttrDiagID) << AL;
      AL.setInvalid();
    }
  }
}

// Usually, `__attribute__((attrib)) struct Foo { ... } var` means the
// attribute applies to var, not the struct type.
// As an exception, __declspec(align(...)) before the struct tag affects the
// type instead of the variable.
// Microsoft-style [attributes] also tend to bind to the type here.
// This function moves attributes that should apply to the type off DS to Attrs.
void Parser::stripTypeAttributesOffDeclSpec(ParsedAttributes &Attrs,
                                            DeclSpec &DS,
                                            Sema::TagUseKind TUK) {
  if (TUK == Sema::TUK_Reference)
    return;

  llvm::SmallVector<ParsedAttr *, 1> ToBeMoved;

  for (ParsedAttr &AL : DS.getAttributes()) {
    if ((AL.getKind() == ParsedAttr::AT_Aligned && AL.isDeclspecAttribute()) ||
        AL.isMicrosoftAttribute())
      ToBeMoved.push_back(&AL);
  }

  for (ParsedAttr *AL : ToBeMoved) {
    DS.getAttributes().remove(AL);
    Attrs.addAtEnd(AL);
  }
}
