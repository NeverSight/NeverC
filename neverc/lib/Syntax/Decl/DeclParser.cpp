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
// Type name parsing
// ===----------------------------------------------------------------------===

TypeResult Parser::ParseTypeName(SourceRange *Range, DeclaratorContext Context,
                                 AccessSpecifier AS, Decl **OwnedType,
                                 ParsedAttributes *Attrs) {
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
    return false;

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

__attribute__((hot)) Parser::DeclGroupPtrTy
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

