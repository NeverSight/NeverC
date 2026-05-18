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

// ===----------------------------------------------------------------------===
// Declaration specifiers
// ===----------------------------------------------------------------------===

__attribute__((hot)) void
Parser::ParseDeclarationSpecifiers(DeclSpec &DS, AccessSpecifier AS,
                                   DeclSpecContext DSContext,
                                   LateParsedAttrList *LateAttrs) {
  if (DS.getSourceRange().isInvalid()) {
    DS.SetRangeStart(Tok.getLocation());
    DS.SetRangeEnd(SourceLocation());
  }

  bool AttrsLastTime = false;
  ParsedAttributes attrs(AttrFactory);
  const PrintingPolicy &Policy = Actions.getCachedPrintingPolicy();
  while (true) {
    bool isInvalid = false;
    const char *PrevSpec = nullptr;
    unsigned DiagID = 0;

    SourceLocation ConsumedEnd;

    // MSVC compat: _Atomic<T> inside typedef.
    if (getLangOpts().MSVCCompat && Tok.is(tok::kw__Atomic) &&
        DS.getStorageClassSpec() == neverc::DeclSpec::SCS_typedef &&
        !DS.hasTypeSpecifier() && GetLookAheadToken(1).is(tok::less))
      Tok.setKind(tok::identifier);

    SourceLocation Loc = Tok.getLocation();

    switch (Tok.getKind()) {
    default:
      if (Tok.isRegularKeywordAttribute())
        goto Attribute;

    DoneWithDeclSpec:
      if (!AttrsLastTime)
        ProhibitAttributes(attrs);
      else {
        for (const ParsedAttr &PA : attrs) {
          if (!PA.isStandardAttributeSyntax() &&
              !PA.isRegularKeywordAttribute())
            continue;
          if (PA.getKind() == ParsedAttr::UnknownAttribute)
            continue;
          if (PA.getKind() == ParsedAttr::AT_VectorSize) {
            Diag(PA.getLoc(), diag::warn_attribute_ignored) << PA;
            PA.setInvalid();
            continue;
          }
          if (PA.isTypeAttr() && PA.getKind() != ParsedAttr::AT_AnyX86NoCfCheck)
            continue;
          Diag(PA.getLoc(), diag::err_attribute_not_type_attr)
              << PA << PA.isRegularKeywordAttribute();
          PA.setInvalid();
        }

        DS.takeAttributesFrom(attrs);
      }

      DS.Finish(Actions, Policy);
      return;

    case tok::l_square:
    case tok::kw_alignas:
      if (!isAllowedBracketAttributeSpecifier())
        goto DoneWithDeclSpec;

    Attribute:
      ProhibitAttributes(attrs);
      attrs.clear();
      attrs.Range = SourceRange();

      ParseBracketAttributes(attrs);
      AttrsLastTime = true;
      continue;

    case tok::coloncolon:
      goto DoneWithDeclSpec;

    case tok::annot_typename: {
      if (DS.hasTypeSpecifier() && DS.hasTagDefinition())
        goto DoneWithDeclSpec;

      TypeResult T = getTypeAnnotation(Tok);
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_typename, Loc, PrevSpec,
                                     DiagID, T, Policy);
      if (isInvalid)
        break;

      DS.SetRangeEnd(Tok.getAnnotationEndLoc());
      ConsumeAnnotationToken();
      // MSVC calling-convention in parens after typename.
      if (Tok.is(tok::l_paren) &&
          NextToken().isOneOf(tok::kw___stdcall, tok::kw___cdecl, tok::kw_cdecl,
                              tok::kw___fastcall, tok::kw___regcall,
                              tok::kw___vectorcall)) {
        const Token &NextNextToken = PP.LookAhead(1);
        if (NextNextToken.is(tok::r_paren)) {
          const Token &NextToken2 = PP.LookAhead(2);
          if (NextToken2.is(tok::l_paren)) {
            ConsumeParen();
          }
        }
      }
      continue;
    }

      goto DoneWithDeclSpec;

    case tok::identifier: {
      if (DS.hasTypeSpecifier())
        goto DoneWithDeclSpec;

      if (!getLangOpts().DeclSpecKeyword && Tok.is(tok::identifier) &&
          Tok.getIdentifierInfo()->getName().equals(
              tok::getKeywordSpelling(tok::kw___declspec))) {
        Diag(Loc, diag::err_ms_attributes_not_enabled);

        // The next token should be an open paren. If it is, eat the entire
        // attribute declaration and continue.
        if (NextToken().is(tok::l_paren)) {
          // Consume the __declspec identifier.
          ConsumeToken();

          // Eat the parens and everything between them.
          BalancedDelimiterTracker T(*this, tok::l_paren);
          if (T.consumeOpen()) {
            assert(false && "Not a left paren?");
            return;
          }
          T.skipToEnd();
          continue;
        }
      }

      ParsedType TypeRep =
          Actions.getTypeName(*Tok.getIdentifierInfo(), Tok.getLocation(),
                              getCurScope(), false, false);

      // If this is not a typedef name, don't parse it as part of the declspec,
      // it must be an implicit int or an error.
      if (!TypeRep) {
        if (Tok.isNot(tok::identifier))
          continue;
        ParsedAttributes Attrs(AttrFactory);
        if (ParseImpliedInt(DS, AS, DSContext, Attrs)) {
          if (!Attrs.empty()) {
            AttrsLastTime = true;
            attrs.takeAllFrom(Attrs);
          }
          continue;
        }
        goto DoneWithDeclSpec;
      }

      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_typename, Loc, PrevSpec,
                                     DiagID, TypeRep, Policy);
      if (isInvalid)
        break;

      DS.SetRangeEnd(Tok.getLocation());
      ConsumeToken(); // The identifier

      // Trailing qualifiers (e.g. typedef-name + const) are handled on later
      // iterations; conflicting type specifiers are diagnosed elsewhere.
      continue;
    }

    // Attributes support.
    case tok::kw___attribute:
    case tok::kw___declspec:
      ParseAttributes(PAKM_GNU | PAKM_Declspec, DS.getAttributes(), LateAttrs);
      continue;

    // Microsoft single token adornments.
    case tok::kw___forceinline: {
      isInvalid = DS.setFunctionSpecForceInline(Loc, PrevSpec, DiagID);
      IdentifierInfo *AttrName = Tok.getIdentifierInfo();
      SourceLocation AttrNameLoc = Tok.getLocation();
      DS.getAttributes().addNew(AttrName, AttrNameLoc, nullptr, AttrNameLoc,
                                nullptr, 0, tok::kw___forceinline);
      break;
    }

    case tok::kw___unaligned:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_unaligned, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;

    case tok::kw___sptr:
    case tok::kw___uptr:
    case tok::kw___ptr64:
    case tok::kw___ptr32:
    case tok::kw___w64:
    case tok::kw___cdecl:
    case tok::kw_cdecl:
    case tok::kw___stdcall:
    case tok::kw___fastcall:
    case tok::kw___regcall:
    case tok::kw___vectorcall:
      ParseMicrosoftTypeAttributes(DS.getAttributes());
      continue;

    // Nullability type specifiers.
    case tok::kw__Nonnull:
    case tok::kw__Nullable:
    case tok::kw__Null_unspecified:
      ParseNullabilityTypeSpecifiers(DS.getAttributes());
      continue;

    // storage-class-specifier
    case tok::kw_typedef:
      isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_typedef, Loc,
                                         PrevSpec, DiagID, Policy);

      break;
    case tok::kw_extern:
      if (DS.getThreadStorageClassSpec() == DeclSpec::TSCS___thread)
        Diag(Tok, diag::ext_thread_before)
            << tok::getKeywordSpelling(tok::kw_extern);
      isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_extern, Loc,
                                         PrevSpec, DiagID, Policy);

      break;
    case tok::kw___private_extern__:
      isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_private_extern,
                                         Loc, PrevSpec, DiagID, Policy);

      break;
    case tok::kw_static:
      if (DS.getThreadStorageClassSpec() == DeclSpec::TSCS___thread)
        Diag(Tok, diag::ext_thread_before)
            << tok::getKeywordSpelling(tok::kw_static);
      isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_static, Loc,
                                         PrevSpec, DiagID, Policy);

      break;
    case tok::kw_auto:
      if (getLangOpts().C23) {
        if (isKnownToBeTypeSpecifier(GetLookAheadToken(1))) {
          isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_auto, Loc,
                                             PrevSpec, DiagID, Policy);
          Diag(Tok, diag::err_c_auto_storage_class);
        } else
          isInvalid = DS.SetTypeSpecType(DeclSpec::TST_auto, Loc, PrevSpec,
                                         DiagID, Policy);
      } else {
        isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_auto, Loc,
                                           PrevSpec, DiagID, Policy);
        Diag(Tok, diag::err_c_auto_storage_class);
      }

      break;
    case tok::kw___auto_type:
      Diag(Tok, diag::ext_auto_type);
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_auto_type, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw_register:
      isInvalid = DS.SetStorageClassSpec(Actions, DeclSpec::SCS_register, Loc,
                                         PrevSpec, DiagID, Policy);

      break;
    case tok::kw___thread:
      isInvalid = DS.SetStorageClassSpecThread(DeclSpec::TSCS___thread, Loc,
                                               PrevSpec, DiagID);

      break;
    case tok::kw_thread_local:
      if (getLangOpts().C23)
        Diag(Tok, diag::warn_c23_compat_keyword) << Tok.getName();
      // Map \c thread_local to \c _Thread_local in C23 so storage class matches
      // C11 thread-local rules.
      // Diagnostics will show _Thread_local when the user wrote
      // thread_local in source in C23 mode; we need some general way to
      // identify which way the user spelled the keyword in source.
      isInvalid = DS.SetStorageClassSpecThread(
          getLangOpts().C23 ? DeclSpec::TSCS__Thread_local
                            : DeclSpec::TSCS_thread_local,
          Loc, PrevSpec, DiagID);

      break;
    case tok::kw__Thread_local:
      if (!getLangOpts().C11)
        Diag(Tok, diag::ext_c11_feature) << Tok.getName();
      isInvalid = DS.SetStorageClassSpecThread(DeclSpec::TSCS__Thread_local,
                                               Loc, PrevSpec, DiagID);

      break;

    // function-specifier
    case tok::kw_inline:
      isInvalid = DS.setFunctionSpecInline(Loc, PrevSpec, DiagID);
      break;
    case tok::kw__Noreturn:
      if (!getLangOpts().C11)
        Diag(Tok, diag::ext_c11_feature) << Tok.getName();
      isInvalid = DS.setFunctionSpecNoreturn(Loc, PrevSpec, DiagID);
      break;

    // alignment-specifier
    case tok::kw__Alignas:
      if (!getLangOpts().C11)
        Diag(Tok, diag::ext_c11_feature) << Tok.getName();
      ParseAlignmentSpecifier(DS.getAttributes());
      continue;

    // C23 \c constexpr
    case tok::kw_constexpr:
      isInvalid = DS.SetConstexprSpec(ConstexprSpecKind::Constexpr, Loc,
                                      PrevSpec, DiagID);
      break;

    // type-specifier
    case tok::kw_short:
      isInvalid = DS.SetTypeSpecWidth(TypeSpecifierWidth::Short, Loc, PrevSpec,
                                      DiagID, Policy);
      break;
    case tok::kw_long:
      if (DS.getTypeSpecWidth() != TypeSpecifierWidth::Long)
        isInvalid = DS.SetTypeSpecWidth(TypeSpecifierWidth::Long, Loc, PrevSpec,
                                        DiagID, Policy);
      else
        isInvalid = DS.SetTypeSpecWidth(TypeSpecifierWidth::LongLong, Loc,
                                        PrevSpec, DiagID, Policy);
      break;
    case tok::kw___int64:
      isInvalid = DS.SetTypeSpecWidth(TypeSpecifierWidth::LongLong, Loc,
                                      PrevSpec, DiagID, Policy);
      break;
    case tok::kw_signed:
      isInvalid =
          DS.SetTypeSpecSign(TypeSpecifierSign::Signed, Loc, PrevSpec, DiagID);
      break;
    case tok::kw_unsigned:
      isInvalid = DS.SetTypeSpecSign(TypeSpecifierSign::Unsigned, Loc, PrevSpec,
                                     DiagID);
      break;
    case tok::kw__Complex:
      if (!getLangOpts().C99)
        Diag(Tok, diag::ext_c99_feature) << Tok.getName();
      isInvalid =
          DS.SetTypeSpecComplex(DeclSpec::TSC_complex, Loc, PrevSpec, DiagID);
      break;
    case tok::kw__Imaginary:
      if (!getLangOpts().C99)
        Diag(Tok, diag::ext_c99_feature) << Tok.getName();
      isInvalid =
          DS.SetTypeSpecComplex(DeclSpec::TSC_imaginary, Loc, PrevSpec, DiagID);
      break;
    case tok::kw_void:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_void, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_char:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_char, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_int:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_int, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw__ExtInt:
    case tok::kw__BitInt: {
      DiagnoseBitIntUse(Tok);
      ExprResult ER = ParseExtIntegerArgument();
      if (ER.isInvalid())
        continue;
      isInvalid = DS.SetBitIntType(Loc, ER.get(), PrevSpec, DiagID, Policy);
      ConsumedEnd = PrevTokLocation;
      break;
    }
    case tok::kw___int128:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_int128, Loc, PrevSpec,
                                     DiagID, Policy);
      break;

    // NeverC Rust-style fixed-width integer types
    case tok::kw_i8:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_i8, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_i16:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_i16, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_i32:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_i32, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_i64:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_i64, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_i128:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_i128, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_u8:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_u8, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_u16:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_u16, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_u32:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_u32, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_u64:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_u64, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_u128:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_u128, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_isize:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_isize, Loc, PrevSpec, DiagID,
                                     Policy);
      break;
    case tok::kw_usize:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_usize, Loc, PrevSpec, DiagID,
                                     Policy);
      break;

    case tok::kw___bf16:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_BFloat16, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw_float:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_float, Loc, PrevSpec, DiagID,
                                     Policy);
      break;
    case tok::kw_double:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_double, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw__Float16:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_float16, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw__Accum:
      assert(getLangOpts().FixedPoint &&
             "This keyword is only used when fixed point types are enabled "
             "with `-ffixed-point`");
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_accum, Loc, PrevSpec, DiagID,
                                     Policy);
      break;
    case tok::kw__Fract:
      assert(getLangOpts().FixedPoint &&
             "This keyword is only used when fixed point types are enabled "
             "with `-ffixed-point`");
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_fract, Loc, PrevSpec, DiagID,
                                     Policy);
      break;
    case tok::kw__Sat:
      assert(getLangOpts().FixedPoint &&
             "This keyword is only used when fixed point types are enabled "
             "with `-ffixed-point`");
      isInvalid = DS.SetTypeSpecSat(Loc, PrevSpec, DiagID);
      break;
    case tok::kw___float128:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_float128, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw___fp16:
      isInvalid =
          DS.SetTypeSpecType(DeclSpec::TST_half, Loc, PrevSpec, DiagID, Policy);
      break;
    case tok::kw_bool:
      if (getLangOpts().C23)
        Diag(Tok, diag::warn_c23_compat_keyword) << Tok.getName();
      [[fallthrough]];
    case tok::kw__Bool:
      if (Tok.is(tok::kw__Bool) && !getLangOpts().C99)
        Diag(Tok, diag::ext_c99_feature) << Tok.getName();

      if (Tok.is(tok::kw_bool) &&
          DS.getTypeSpecType() != DeclSpec::TST_unspecified &&
          DS.getStorageClassSpec() == DeclSpec::SCS_typedef) {
        PrevSpec = ""; // Not used by the diagnostic.
        DiagID = diag::err_bool_redeclaration;
        // For better error recovery.
        Tok.setKind(tok::identifier);
        isInvalid = true;
      } else {
        isInvalid = DS.SetTypeSpecType(DeclSpec::TST_bool, Loc, PrevSpec,
                                       DiagID, Policy);
      }
      break;
    case tok::kw__Decimal32:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_decimal32, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw__Decimal64:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_decimal64, Loc, PrevSpec,
                                     DiagID, Policy);
      break;
    case tok::kw__Decimal128:
      isInvalid = DS.SetTypeSpecType(DeclSpec::TST_decimal128, Loc, PrevSpec,
                                     DiagID, Policy);
      break;

    // record-specifier:
    case tok::kw_struct:
    case tok::kw_union: {
      tok::TokenKind Kind = Tok.getKind();
      ConsumeToken();

      // These are attributes following record specifiers.
      // To produce better diagnostic, we parse them when
      // parsing the record specifier.
      ParsedAttributes Attributes(AttrFactory);
      ParseStructOrUnionSpecifier(Kind, Loc, DS, AS, DSContext, Attributes);

      // If there are attributes following the record specifier,
      // take them over and handle them here.
      if (!Attributes.empty()) {
        AttrsLastTime = true;
        attrs.takeAllFrom(Attributes);
      }
      continue;
    }

    // enum-specifier:
    case tok::kw_enum:
      ConsumeToken();
      ParseEnumSpecifier(Loc, DS, AS, DSContext);
      continue;

    // cv-qualifier:
    case tok::kw_const:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_const, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw_volatile:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_volatile, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw_restrict:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_restrict, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;

    // C23/GNU typeof support.
    case tok::kw_typeof:
    case tok::kw_typeof_unqual:
      ParseTypeofSpecifier(DS);
      continue;

    case tok::annot_pragma_pack:
      ProcessPragmaPack();
      continue;

    case tok::annot_pragma_ms_pragma:
      ProcessPragmaMSPragma();
      continue;

    case tok::kw__Atomic:
      // C11 6.7.2.4/4:
      //   If the _Atomic keyword is immediately followed by a left parenthesis,
      //   it is interpreted as a type specifier (with a type name), not as a
      //   type qualifier.
      if (!getLangOpts().C11)
        Diag(Tok, diag::ext_c11_feature) << Tok.getName();

      if (NextToken().is(tok::l_paren)) {
        ParseAtomicSpecifier(DS);
        continue;
      }
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_atomic, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;

    case tok::less:
      goto DoneWithDeclSpec;
    }

    DS.SetRangeEnd(ConsumedEnd.isValid() ? ConsumedEnd : Tok.getLocation());

    // If the specifier wasn't legal, issue a diagnostic.
    if (isInvalid) {
      assert(PrevSpec && "Method did not return previous specifier!");
      assert(DiagID);

      if (DiagID == diag::ext_duplicate_declspec ||
          DiagID == diag::ext_warn_duplicate_declspec ||
          DiagID == diag::err_duplicate_declspec)
        Diag(Loc, DiagID) << PrevSpec
                          << FixItHint::CreateRemoval(
                                 SourceRange(Loc, DS.getEndLoc()));
      else
        Diag(Loc, DiagID) << PrevSpec;
    }

    if (DiagID != diag::err_bool_redeclaration && ConsumedEnd.isInvalid())
      // After an error the next token can be an annotation token.
      ConsumeAnyToken();

    AttrsLastTime = false;
  }
}

void Parser::ParseStructDeclaration(
    ParsingDeclSpec &DS,
    llvm::function_ref<void(ParsingFieldDeclarator &)> FieldsCallback) {

  if (Tok.is(tok::kw___extension__)) {
    // __extension__ silences extension warnings in the subexpression.
    ExtensionRAIIObject O(Diags); // Use RAII to do this.
    ConsumeToken();
    return ParseStructDeclaration(DS, FieldsCallback);
  }

  ParsedAttributes Attrs(AttrFactory);
  MaybeParseBracketAttributes(Attrs);

  ParseSpecifierQualifierList(DS);

  // If there are no declarators, this is a free-standing declaration
  // specifier. Let the actions module cope with it.
  if (Tok.is(tok::semi)) {
    // C23 6.7.2.1p9 : "The optional attribute specifier sequence in a
    // member declaration appertains to each of the members declared by the
    // member declarator list; it shall not appear if the optional member
    // declarator list is omitted."
    ProhibitAttributes(Attrs);
    RecordDecl *AnonRecord = nullptr;
    Decl *TheDecl = Actions.ParsedFreeStandingDeclSpec(
        getCurScope(), AS_none, DS, ParsedAttributesView::none(), AnonRecord);
    assert(!AnonRecord && "Did not expect anonymous struct or union here");
    DS.complete(TheDecl);
    return;
  }

  // Read struct-declarators until we find the semicolon.
  bool FirstDeclarator = true;
  SourceLocation CommaLoc;
  while (true) {
    ParsingFieldDeclarator DeclaratorInfo(*this, DS, Attrs);

    // Attributes are only allowed here on successive declarators.
    if (!FirstDeclarator) {
      // However, this does not apply for [[]] attributes (which could show up
      // before or after the __attribute__ attributes).
      DiagnoseAndSkipBracketAttributes();
      MaybeParseGNUAttributes(DeclaratorInfo.D);
      DiagnoseAndSkipBracketAttributes();
    }

    /// struct-declarator: declarator
    /// struct-declarator: declarator[opt] ':' constant-expression
    if (Tok.isNot(tok::colon)) {
      // Don't parse FOO:BAR as if it were a typo for FOO::BAR.
      ColonProtectionRAIIObject X(*this);
      ParseDeclarator(DeclaratorInfo.D);
    } else
      DeclaratorInfo.D.SetIdentifier(nullptr, Tok.getLocation());

    if (TryConsumeToken(tok::colon)) {
      ExprResult Res(ParseConstantExpression());
      if (Res.isInvalid())
        SkipUntil(tok::semi, StopBeforeMatch);
      else
        DeclaratorInfo.BitfieldSize = Res.get();
    }

    // If attributes exist after the declarator, parse them.
    MaybeParseGNUAttributes(DeclaratorInfo.D);

    // We're done with this declarator;  invoke the callback.
    FieldsCallback(DeclaratorInfo);

    // If we don't have a comma, it is either the end of the list (a ';')
    // or an error, bail out.
    if (!TryConsumeToken(tok::comma, CommaLoc))
      return;

    FirstDeclarator = false;
  }
}

void Parser::ParseStructUnionBody(SourceLocation RecordLoc,
                                  DeclSpec::TST TagType, RecordDecl *TagDecl) {
  PrettyDeclStackTraceEntry CrashInfo(Actions.Context, TagDecl, RecordLoc,
                                      "parsing struct/union body");

  BalancedDelimiterTracker T(*this, tok::l_brace);
  if (T.consumeOpen())
    return;

  ParseScope StructScope(this, Scope::RecordScope | Scope::DeclScope);
  Actions.OnTagStartDefinition(getCurScope(), TagDecl);

  // While we still have something to read, read the declarations in the struct.
  while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
    // Each iteration of this loop reads one struct-declaration.
    if (Tok.is(tok::semi)) {
      ConsumeRedundantSemicolons(InsideStruct, TagType);
      continue;
    }

    if (Tok.isOneOf(tok::kw__Static_assert, tok::kw_static_assert)) {
      SourceLocation DeclEnd;
      ParseStaticAssertDeclaration(DeclEnd);
      continue;
    }

    if (Tok.is(tok::annot_pragma_pack)) {
      ProcessPragmaPack();
      continue;
    }

    if (Tok.is(tok::annot_pragma_align)) {
      ProcessPragmaAlign();
      continue;
    }

    if (tok::isPragmaAnnotation(Tok.getKind())) {
      Diag(Tok.getLocation(), diag::err_pragma_misplaced_in_decl)
          << DeclSpec::getSpecifierName(
                 TagType, Actions.getTreeContext().getPrintingPolicy());
      ConsumeAnnotationToken();
      continue;
    }

    auto CFieldCallback = [&](ParsingFieldDeclarator &FD) {
      Decl *Field =
          Actions.OnField(getCurScope(), TagDecl,
                          FD.D.getDeclSpec().getSourceRange().getBegin(), FD.D,
                          FD.BitfieldSize);
      FD.complete(Field);
    };

    ParsingDeclSpec DS(*this);
    ParseStructDeclaration(DS, CFieldCallback);

    if (TryConsumeToken(tok::semi))
      continue;

    if (Tok.is(tok::r_brace)) {
      RequireToken(tok::semi, diag::ext_expected_semi_decl_list);
      break;
    }

    RequireToken(tok::semi, diag::err_expected_semi_decl_list);
    // Skip to end of block or statement to avoid ext-warning on extra ';'.
    SkipUntil(tok::r_brace, StopAtSemi | StopBeforeMatch);
    // If we stopped at a ';', eat it.
    TryConsumeToken(tok::semi);
  }

  T.consumeClose();

  ParsedAttributes attrs(AttrFactory);
  // If attributes exist after struct contents, parse them.
  MaybeParseGNUAttributes(attrs);

  llvm::SmallVector<Decl *, 32> FieldDecls(TagDecl->fields());

  Actions.OnFields(getCurScope(), RecordLoc, TagDecl, FieldDecls,
                   T.getOpenLocation(), T.getCloseLocation(), attrs);
  StructScope.Exit();
  Actions.OnTagFinishDefinition(getCurScope(), TagDecl, T.getRange());
}

void Parser::ParseEnumSpecifier(SourceLocation StartLoc, DeclSpec &DS,
                                AccessSpecifier AS, DeclSpecContext DSC) {
  // If attributes exist after tag, parse them.
  ParsedAttributes attrs(AttrFactory);
  MaybeParseAttributes(PAKM_GNU | PAKM_Declspec | PAKM_Bracket, attrs);

  // Determine whether this declaration is permitted to have an enum-base.
  AllowDefiningTypeSpec AllowEnumSpecifier =
      isDefiningTypeSpecifierContext(DSC);
  bool CanBeOpaqueEnumDeclaration =
      DS.isEmpty() && isOpaqueEnumDeclarationContext(DSC);
  bool CanHaveEnumBase = getLangOpts().MicrosoftExt &&
                         (AllowEnumSpecifier == AllowDefiningTypeSpec::Yes ||
                          CanBeOpaqueEnumDeclaration);

  // Must have either 'enum name' or 'enum {...}' or (rarely) 'enum : T { ...
  // }'.
  if (Tok.isNot(tok::identifier) && Tok.isNot(tok::l_brace) &&
      Tok.isNot(tok::colon)) {
    Diag(Tok, diag::err_expected_either) << tok::identifier << tok::l_brace;

    DS.SetTypeSpecError();
    // Skip the rest of this declarator, up until the comma or semicolon.
    SkipUntil(tok::comma, StopAtSemi);
    return;
  }

  // If an identifier is present, consume and remember it.
  IdentifierInfo *Name = nullptr;
  SourceLocation NameLoc;
  if (Tok.is(tok::identifier)) {
    Name = Tok.getIdentifierInfo();
    NameLoc = ConsumeToken();
  }

  TypeResult BaseType;
  SourceRange BaseRange;

  bool CanBeBitfield = getCurScope()->isRecordScope() && Name;

  if (Tok.is(tok::colon)) {
    // This might be an enum-base or part of some unrelated enclosing context.
    //
    // 'enum E : base' is permitted in two circumstances:
    //
    // 1) As a defining-type-specifier, when followed by '{'.
    // 2) As the sole constituent of a complete declaration -- when DS is empty
    //    and the next token is ';'.
    //
    // The restriction to defining-type-specifiers matters so _Generic(a, enum E
    // : int{}) can disambiguate ':' after the tag.
    //
    // Disambiguate ':' after the tag: MSVC-style fixed underlying type vs.
    // struct bit-field width.

    if (!CanBeBitfield && (CanHaveEnumBase || !ColonIsSacred)) {
      SourceLocation ColonLoc = ConsumeToken();

      // Parse a type-specifier-seq as a type. We can't just ParseTypeName here,
      // because under -fms-extensions,
      //   enum E : int *p;
      // declares 'enum E : int; E *p;' not 'enum E : int*; E p;'.
      DeclSpec DS(AttrFactory);
      // Parse enum-base as a specifier-qualifier-list (fixed underlying type).
      ParseSpecifierQualifierList(DS, AS, DeclSpecContext::DSC_type_specifier);
      Declarator DeclaratorInfo(DS, ParsedAttributesView::none(),
                                DeclaratorContext::TypeName);
      BaseType = Actions.OnTypeName(getCurScope(), DeclaratorInfo);

      BaseRange =
          SourceRange(ColonLoc, DeclaratorInfo.getSourceRange().getEnd());

      if (!getLangOpts().C23) {
        if (getLangOpts().MicrosoftExt)
          Diag(ColonLoc, diag::ext_ms_c_enum_fixed_underlying_type)
              << BaseRange;
        else
          Diag(ColonLoc, diag::ext_c_enum_fixed_underlying_type) << BaseRange;
      }
    }
  }

  // C99 6.7.2.3p11:
  // enum foo {..};  void bar() { enum foo; }    <- new foo in bar.
  // enum foo {..};  void bar() { enum foo x; }  <- use of old foo.
  Sema::TagUseKind TUK;
  if (AllowEnumSpecifier == AllowDefiningTypeSpec::No)
    TUK = Sema::TUK_Reference;
  else if (Tok.is(tok::l_brace)) {
    TUK = Sema::TUK_Definition;
  } else if (!isTypeSpecifier(DSC) &&
             (Tok.is(tok::semi) ||
              (Tok.isAtStartOfLine() &&
               !isValidAfterTypeSpecifier(CanBeBitfield)))) {
    TUK = Sema::TUK_Declaration;
    if (Tok.isNot(tok::semi)) {
      RequireToken(tok::semi, diag::err_expected_after,
                   tok::getKeywordSpelling(tok::kw_enum));
      PP.InjectToken(Tok, /*IsReinject=*/true);
      Tok.setKind(tok::semi);
    }
  } else {
    TUK = Sema::TUK_Reference;
  }

  bool IsElaboratedTypeSpecifier = TUK == Sema::TUK_Reference;

  if (!Name && TUK != Sema::TUK_Definition) {
    Diag(Tok, diag::err_enumerator_unnamed_no_def);

    DS.SetTypeSpecError();
    // Skip the rest of this declarator, up until the comma or semicolon.
    SkipUntil(tok::comma, StopAtSemi);
    return;
  }

  // An elaborated-type-specifier has a much more constrained grammar:
  //
  //   'enum' nested-name-specifier[opt] identifier
  //
  // If we parsed any other bits, reject them now.
  //
  // MSVC permits a full enum-specifier or opaque-enum-declaration anywhere.
  if (IsElaboratedTypeSpecifier && !getLangOpts().MicrosoftExt) {
    ProhibitBracketAttributes(attrs, diag::err_attributes_not_allowed,
                              diag::err_keyword_not_allowed,
                              /*DiagnoseEmptyAttrs=*/true);
    if (BaseType.isUsable())
      Diag(BaseRange.getBegin(), diag::ext_enum_base_in_type_specifier)
          << (AllowEnumSpecifier == AllowDefiningTypeSpec::Yes) << BaseRange;
  }

  stripTypeAttributesOffDeclSpec(attrs, DS, TUK);

  Sema::SkipBodyInfo SkipBody;

  bool Owned = false;
  const char *PrevSpec = nullptr;
  unsigned DiagID;
  Decl *TagDecl =
      Actions
          .OnTag(getCurScope(), DeclSpec::TST_enum, TUK, StartLoc, Name,
                 NameLoc, attrs, AS, Owned, BaseType, OffsetOfState, &SkipBody)
          .get();

  if (SkipBody.ShouldSkip) {
    assert(TUK == Sema::TUK_Definition && "can only skip a definition");

    BalancedDelimiterTracker T(*this, tok::l_brace);
    T.consumeOpen();
    T.skipToEnd();

    if (DS.SetTypeSpecType(DeclSpec::TST_enum, StartLoc,
                           NameLoc.isValid() ? NameLoc : StartLoc, PrevSpec,
                           DiagID, TagDecl, Owned,
                           Actions.getTreeContext().getPrintingPolicy()))
      Diag(StartLoc, DiagID) << PrevSpec;
    return;
  }

  if (!TagDecl) {
    // The action failed to produce an enumeration tag. If this is a
    // definition, consume the entire definition.
    if (Tok.is(tok::l_brace) && TUK != Sema::TUK_Reference) {
      ConsumeBrace();
      SkipUntil(tok::r_brace, StopAtSemi);
    }

    DS.SetTypeSpecError();
    return;
  }

  if (Tok.is(tok::l_brace) && TUK == Sema::TUK_Definition) {
    Decl *D = SkipBody.CheckSameAsPrevious ? SkipBody.New : TagDecl;
    ParseEnumBody(StartLoc, D);
    if (SkipBody.CheckSameAsPrevious) {
      DS.SetTypeSpecError();
      return;
    }
  }

  if (DS.SetTypeSpecType(DeclSpec::TST_enum, StartLoc,
                         NameLoc.isValid() ? NameLoc : StartLoc, PrevSpec,
                         DiagID, TagDecl, Owned,
                         Actions.getTreeContext().getPrintingPolicy()))
    Diag(StartLoc, DiagID) << PrevSpec;
}

void Parser::ParseEnumBody(SourceLocation StartLoc, Decl *EnumDecl) {
  // Enter the scope of the enum body and start the definition.
  ParseScope EnumScope(this, Scope::DeclScope | Scope::EnumScope);
  Actions.OnTagStartDefinition(getCurScope(), EnumDecl);

  BalancedDelimiterTracker T(*this, tok::l_brace);
  T.consumeOpen();

  if (Tok.is(tok::r_brace))
    Diag(Tok, diag::err_empty_enum);

  llvm::SmallVector<Decl *, 32> EnumConstantDecls;
  llvm::SmallVector<ParsingDeclDiagnosticScope, 32> EnumConstParsingDeclScopes;

  Decl *LastEnumConstDecl = nullptr;

  while (Tok.isNot(tok::r_brace)) {
    // Parse enumerator. If failed, try skipping till the start of the next
    // enumerator definition.
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
      if (SkipUntil(tok::comma, tok::r_brace, StopBeforeMatch) &&
          TryConsumeToken(tok::comma))
        continue;
      break;
    }
    IdentifierInfo *Ident = Tok.getIdentifierInfo();
    SourceLocation IdentLoc = ConsumeToken();

    // If attributes exist after the enumerator, parse them.
    ParsedAttributes attrs(AttrFactory);
    MaybeParseGNUAttributes(attrs);
    if (isAllowedBracketAttributeSpecifier()) {
      ParseBracketAttributes(attrs);
    }

    SourceLocation EqualLoc;
    ExprResult AssignedVal;
    EnumConstParsingDeclScopes.emplace_back(*this);

    EnterExpressionEvaluationContext ConstantEvaluated(
        Actions, Sema::ExpressionEvaluationContext::ConstantEvaluated);
    if (TryConsumeToken(tok::equal, EqualLoc)) {
      AssignedVal = ParseConstantExpressionInExprEvalContext();
      if (AssignedVal.isInvalid())
        SkipUntil(tok::comma, tok::r_brace, StopBeforeMatch);
    }

    // Install the enumerator constant into EnumDecl.
    Decl *EnumConstDecl = Actions.OnEnumConstant(
        getCurScope(), EnumDecl, LastEnumConstDecl, IdentLoc, Ident, attrs,
        EqualLoc, AssignedVal.get());
    EnumConstParsingDeclScopes.back().done();

    EnumConstantDecls.push_back(EnumConstDecl);
    LastEnumConstDecl = EnumConstDecl;

    if (Tok.is(tok::identifier)) {
      // We're missing a comma between enumerators.
      SourceLocation Loc = getEndOfPreviousToken();
      Diag(Loc, diag::err_enumerator_list_missing_comma)
          << FixItHint::CreateInsertion(Loc, ", ");
      continue;
    }

    // Emumerator definition must be finished, only comma or r_brace are
    // allowed here.
    SourceLocation CommaLoc;
    if (Tok.isNot(tok::r_brace) && !TryConsumeToken(tok::comma, CommaLoc)) {
      if (EqualLoc.isValid())
        Diag(Tok.getLocation(), diag::err_expected_either)
            << tok::r_brace << tok::comma;
      else
        Diag(Tok.getLocation(), diag::err_expected_end_of_enumerator);
      if (SkipUntil(tok::comma, tok::r_brace, StopBeforeMatch)) {
        if (TryConsumeToken(tok::comma, CommaLoc))
          continue;
      } else {
        break;
      }
    }

    if (Tok.is(tok::r_brace) && CommaLoc.isValid()) {
      if (!getLangOpts().C99)
        Diag(CommaLoc, diag::ext_enumerator_list_comma_c)
            << FixItHint::CreateRemoval(CommaLoc);
      break;
    }
  }

  // Eat the }.
  T.consumeClose();

  // If attributes exist after the identifier list, parse them.
  ParsedAttributes attrs(AttrFactory);
  MaybeParseGNUAttributes(attrs);

  Actions.OnEnumBody(StartLoc, T.getRange(), EnumDecl, EnumConstantDecls,
                     getCurScope(), attrs);

  // Now attach delayed diagnostics to each enumerator declaration.
  assert(EnumConstantDecls.size() == EnumConstParsingDeclScopes.size());
  for (size_t i = 0, e = EnumConstantDecls.size(); i != e; ++i) {
    ParsingDeclRAIIObject PD(*this, ParsingDeclRAIIObject::NoParent);
    EnumConstParsingDeclScopes[i].redelay();
    PD.complete(EnumConstantDecls[i]);
  }

  EnumScope.Exit();
  Actions.OnTagFinishDefinition(getCurScope(), EnumDecl, T.getRange());

  // The next token must be valid after an enum definition. If not, a ';'
  // was probably forgotten.
  bool CanBeBitfield = getCurScope()->isRecordScope();
  if (!isValidAfterTypeSpecifier(CanBeBitfield)) {
    RequireToken(tok::semi, diag::err_expected_after,
                 tok::getKeywordSpelling(tok::kw_enum));
    // Push this token back into the preprocessor and change our current token
    // to ';' so that the rest of the code recovers as though there were an
    // ';' after the definition.
    PP.InjectToken(Tok, /*IsReinject=*/true);
    Tok.setKind(tok::semi);
  }
}

bool Parser::isKnownToBeTypeSpecifier(const Token &Tok) const {
  switch (Tok.getKind()) {
  default:
    return false;
    // type-specifiers
  case tok::kw_short:
  case tok::kw_long:
  case tok::kw___int64:
  case tok::kw___int128:
  case tok::kw_signed:
  case tok::kw_unsigned:
  case tok::kw__Complex:
  case tok::kw__Imaginary:
  case tok::kw_void:
  case tok::kw_char:
  case tok::kw_int:
  case tok::kw__ExtInt:
  case tok::kw__BitInt:
  case tok::kw___bf16:
  case tok::kw_float:
  case tok::kw_double:
  case tok::kw__Accum:
  case tok::kw__Fract:
  case tok::kw__Float16:
  case tok::kw___fp16:
  case tok::kw___float128:
  case tok::kw_bool:
  case tok::kw__Bool:
  case tok::kw__Decimal32:
  case tok::kw__Decimal64:
  case tok::kw__Decimal128:
    // NeverC Rust-style integer types
  case tok::kw_i8:
  case tok::kw_i16:
  case tok::kw_i32:
  case tok::kw_i64:
  case tok::kw_i128:
  case tok::kw_u8:
  case tok::kw_u16:
  case tok::kw_u32:
  case tok::kw_u64:
  case tok::kw_u128:
  case tok::kw_isize:
  case tok::kw_usize:

    // struct-or-union-specifier (C99)
  case tok::kw_struct:
  case tok::kw_union:
    // enum-specifier
  case tok::kw_enum:

    // typedef-name
  case tok::annot_typename:
    return true;
  }
}

bool Parser::isTypeSpecifierQualifier() {
  switch (Tok.getKind()) {
  default:
    return false;

  case tok::identifier: // foo::bar
    // Annotate typenames / scope; if that changes the token stream, recurse.
    TryResolveTypeAnnotation();
    if (Tok.is(tok::identifier))
      return false;
    return isTypeSpecifierQualifier();

    // GNU attributes support.
  case tok::kw___attribute:
    // C23/GNU typeof support.
  case tok::kw_typeof:
  case tok::kw_typeof_unqual:

    // type-specifiers
  case tok::kw_short:
  case tok::kw_long:
  case tok::kw___int64:
  case tok::kw___int128:
  case tok::kw_signed:
  case tok::kw_unsigned:
  case tok::kw__Complex:
  case tok::kw__Imaginary:
  case tok::kw_void:
  case tok::kw_char:
  case tok::kw_int:
  case tok::kw__ExtInt:
  case tok::kw__BitInt:
  case tok::kw___bf16:
  case tok::kw_float:
  case tok::kw_double:
  case tok::kw__Accum:
  case tok::kw__Fract:
  case tok::kw__Float16:
  case tok::kw___fp16:
  case tok::kw___float128:
  case tok::kw_bool:
  case tok::kw__Bool:
  case tok::kw__Decimal32:
  case tok::kw__Decimal64:
  case tok::kw__Decimal128:
    // NeverC Rust-style integer types
  case tok::kw_i8:
  case tok::kw_i16:
  case tok::kw_i32:
  case tok::kw_i64:
  case tok::kw_i128:
  case tok::kw_u8:
  case tok::kw_u16:
  case tok::kw_u32:
  case tok::kw_u64:
  case tok::kw_u128:
  case tok::kw_isize:
  case tok::kw_usize:

    // struct-or-union-specifier (C99)
  case tok::kw_struct:
  case tok::kw_union:
    // enum-specifier
  case tok::kw_enum:

    // type-qualifier
  case tok::kw_const:
  case tok::kw_volatile:
  case tok::kw_restrict:
  case tok::kw__Sat:

    // typedef-name
  case tok::annot_typename:
    return true;

  case tok::less:
    return false;

  case tok::kw___cdecl:
  case tok::kw_cdecl:
  case tok::kw___stdcall:
  case tok::kw___fastcall:
  case tok::kw___regcall:
  case tok::kw___vectorcall:
  case tok::kw___w64:
  case tok::kw___ptr64:
  case tok::kw___ptr32:
  case tok::kw___unaligned:

  case tok::kw__Nonnull:
  case tok::kw__Nullable:
  case tok::kw__Null_unspecified:

  // C11 _Atomic
  case tok::kw__Atomic:
    return true;
  }
}

bool Parser::isDeclarationSpecifier() {
  switch (Tok.getKind()) {
  default:
    return false;

  case tok::identifier: // foo::bar
    // Annotate typenames / scope; if that changes the token stream, recurse.
    TryResolveTypeAnnotation();
    if (Tok.is(tok::identifier))
      return false;

    return isDeclarationSpecifier();

    // storage-class-specifier
  case tok::kw_typedef:
  case tok::kw_extern:
  case tok::kw___private_extern__:
  case tok::kw_static:
  case tok::kw_auto:
  case tok::kw___auto_type:
  case tok::kw_register:
  case tok::kw___thread:
  case tok::kw_thread_local:
  case tok::kw__Thread_local:

    // type-specifiers
  case tok::kw_short:
  case tok::kw_long:
  case tok::kw___int64:
  case tok::kw___int128:
  case tok::kw_signed:
  case tok::kw_unsigned:
  case tok::kw__Complex:
  case tok::kw__Imaginary:
  case tok::kw_void:
  case tok::kw_char:

  case tok::kw_int:
  case tok::kw__ExtInt:
  case tok::kw__BitInt:
  case tok::kw___bf16:
  case tok::kw_float:
  case tok::kw_double:
  case tok::kw__Accum:
  case tok::kw__Fract:
  case tok::kw__Float16:
  case tok::kw___fp16:
  case tok::kw___float128:
  case tok::kw_bool:
  case tok::kw__Bool:
  case tok::kw__Decimal32:
  case tok::kw__Decimal64:
  case tok::kw__Decimal128:
    // NeverC Rust-style integer types
  case tok::kw_i8:
  case tok::kw_i16:
  case tok::kw_i32:
  case tok::kw_i64:
  case tok::kw_i128:
  case tok::kw_u8:
  case tok::kw_u16:
  case tok::kw_u32:
  case tok::kw_u64:
  case tok::kw_u128:
  case tok::kw_isize:
  case tok::kw_usize:

    // struct-or-union-specifier (C99)
  case tok::kw_struct:
  case tok::kw_union:
    // enum-specifier
  case tok::kw_enum:

    // type-qualifier
  case tok::kw_const:
  case tok::kw_volatile:
  case tok::kw_restrict:
  case tok::kw__Sat:

    // function-specifier
  case tok::kw_inline:
  case tok::kw__Noreturn:

    // alignment-specifier
  case tok::kw__Alignas:

    // static_assert-declaration
  case tok::kw_static_assert:
  case tok::kw__Static_assert:

    // C23/GNU typeof support.
  case tok::kw_typeof:
  case tok::kw_typeof_unqual:

    // GNU attributes.
  case tok::kw___attribute:

    // C23 \c constexpr
  case tok::kw_constexpr:

    // C11 _Atomic
  case tok::kw__Atomic:
    return true;

  case tok::less:
    return false;

    // typedef-name
  case tok::annot_typename:
    return true;

  case tok::kw___declspec:
  case tok::kw___cdecl:
  case tok::kw_cdecl:
  case tok::kw___stdcall:
  case tok::kw___fastcall:
  case tok::kw___regcall:
  case tok::kw___vectorcall:
  case tok::kw___w64:
  case tok::kw___sptr:
  case tok::kw___uptr:
  case tok::kw___ptr64:
  case tok::kw___ptr32:
  case tok::kw___forceinline:
  case tok::kw___unaligned:

  case tok::kw__Nonnull:
  case tok::kw__Nullable:
  case tok::kw__Null_unspecified:
    return true;
  }
  return false;
}

void Parser::ParseTypeQualifierListOpt(DeclSpec &DS, unsigned AttrReqs,
                                       bool AtomicAllowed,
                                       bool IdentifierRequired) {
  if ((AttrReqs & AR_BracketAttributesParsed) &&
      isAllowedBracketAttributeSpecifier()) {
    ParsedAttributes Attrs(AttrFactory);
    ParseBracketAttributes(Attrs);
    DS.takeAttributesFrom(Attrs);
  }

  SourceLocation EndLoc;

  while (true) {
    bool isInvalid = false;
    const char *PrevSpec = nullptr;
    unsigned DiagID = 0;
    SourceLocation Loc = Tok.getLocation();

    switch (Tok.getKind()) {
    case tok::kw_const:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_const, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw_volatile:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_volatile, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw_restrict:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_restrict, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw__Atomic:
      if (!AtomicAllowed)
        goto DoneWithTypeQuals;
      if (!getLangOpts().C11)
        Diag(Tok, diag::ext_c11_feature) << Tok.getName();
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_atomic, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;

    case tok::kw___unaligned:
      isInvalid = DS.SetTypeQual(DeclSpec::TQ_unaligned, Loc, PrevSpec, DiagID,
                                 getLangOpts());
      break;
    case tok::kw___uptr:
      if ((AttrReqs & AR_DeclspecAttributesParsed) && IdentifierRequired &&
          DS.isEmpty() && NextToken().is(tok::semi)) {
        if (TryKeywordIdentFallback(false))
          continue;
      }
      [[fallthrough]];
    case tok::kw___sptr:
    case tok::kw___w64:
    case tok::kw___ptr64:
    case tok::kw___ptr32:
    case tok::kw___cdecl:
    case tok::kw_cdecl:
    case tok::kw___stdcall:
    case tok::kw___fastcall:
    case tok::kw___regcall:
    case tok::kw___vectorcall:
      if (AttrReqs & AR_DeclspecAttributesParsed) {
        ParseMicrosoftTypeAttributes(DS.getAttributes());
        continue;
      }
      goto DoneWithTypeQuals;

    // Nullability type specifiers.
    case tok::kw__Nonnull:
    case tok::kw__Nullable:
    case tok::kw__Null_unspecified:
      ParseNullabilityTypeSpecifiers(DS.getAttributes());
      continue;

    case tok::kw___attribute:
      if (AttrReqs & AR_GNUAttributesParsedAndRejected)
        // When GNU attributes are expressly forbidden, diagnose their usage.
        Diag(Tok, diag::err_attributes_not_allowed);

      // Parse the attributes even if they are rejected to ensure that error
      // recovery is graceful.
      if (AttrReqs & AR_GNUAttributesParsed ||
          AttrReqs & AR_GNUAttributesParsedAndRejected) {
        ParseGNUAttributes(DS.getAttributes());
        continue; // do *not* consume the next token!
      }
      // otherwise, FALL THROUGH!
      [[fallthrough]];
    default:
    DoneWithTypeQuals:
      // If this is not a type-qualifier token, we're done reading type
      // qualifiers.  First verify that DeclSpec's are consistent.
      DS.Finish(Actions, Actions.getTreeContext().getPrintingPolicy());
      if (EndLoc.isValid())
        DS.SetRangeEnd(EndLoc);
      return;
    }

    // If the specifier combination wasn't legal, issue a diagnostic.
    if (isInvalid) {
      assert(PrevSpec && "Method did not return previous specifier!");
      Diag(Tok, DiagID) << PrevSpec;
    }
    EndLoc = ConsumeToken();
  }
}

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

__attribute__((hot)) void Parser::ParseDirectDeclarator(Declarator &D) {
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

// ===----------------------------------------------------------------------===
// Bracket & Microsoft attributes
// ===----------------------------------------------------------------------===

namespace {
bool isRecognizedBracketAttr(IdentifierInfo *AttrName,
                             IdentifierInfo *ScopeName) {
  switch (
      ParsedAttr::getParsedKind(AttrName, ScopeName, ParsedAttr::AS_Bracket)) {
  case ParsedAttr::AT_CarriesDependency:
  case ParsedAttr::AT_Deprecated:
  case ParsedAttr::AT_FallThrough:
  case ParsedAttr::AT_StandardNoReturn:
  case ParsedAttr::AT_Likely:
  case ParsedAttr::AT_Unlikely:
    return true;
  case ParsedAttr::AT_WarnUnusedResult:
    return !ScopeName && AttrName->getName().equals("nodiscard");
  case ParsedAttr::AT_Unused:
    return !ScopeName && AttrName->getName().equals("maybe_unused");
  default:
    return false;
  }
}
} // namespace

bool Parser::ParseBracketAttributeArgs(IdentifierInfo *AttrName,
                                       SourceLocation AttrNameLoc,
                                       ParsedAttributes &Attrs,
                                       SourceLocation *EndLoc,
                                       IdentifierInfo *ScopeName,
                                       SourceLocation ScopeLoc) {
  assert(Tok.is(tok::l_paren) && "Not a [[...]] attribute argument list");
  SourceLocation LParenLoc = Tok.getLocation();
  ParsedAttr::Form Form = ParsedAttr::Form::C23();

  if (getLangOpts().MicrosoftExt) {
    if (hasAttribute(AttributeCommonInfo::Syntax::AS_Microsoft, ScopeName,
                     AttrName, getTargetInfo(), getLangOpts()))
      Form = ParsedAttr::Form::Microsoft();
  }

  if (Form.getSyntax() != ParsedAttr::AS_Microsoft &&
      !hasAttribute(AttributeCommonInfo::Syntax::AS_C23, ScopeName, AttrName,
                    getTargetInfo(), getLangOpts())) {
    ConsumeParen();
    SkipUntil(tok::r_paren);
    return false;
  }

  if (ScopeName && (ScopeName->isStr("gnu") || ScopeName->isStr("__gnu__"))) {
    ParseGNUAttributeArgs(AttrName, AttrNameLoc, Attrs, EndLoc, ScopeName,
                          ScopeLoc, Form, nullptr);
    return true;
  }

  unsigned NumArgs;
  if (ScopeName && (ScopeName->isStr("neverc") || ScopeName->isStr("_NeverC")))
    NumArgs = ParseCustomAttributeArgs(AttrName, AttrNameLoc, Attrs, EndLoc,
                                       ScopeName, ScopeLoc, Form);
  else
    NumArgs = ParseAttributeArgsCommon(AttrName, AttrNameLoc, Attrs, EndLoc,
                                       ScopeName, ScopeLoc, Form);

  if (!Attrs.empty() && isRecognizedBracketAttr(AttrName, ScopeName)) {
    ParsedAttr &Attr = Attrs.back();
    if (!Attr.existsInTarget(getTargetInfo())) {
      Diag(LParenLoc, diag::warn_unknown_attribute_ignored) << AttrName;
      Attr.setInvalid(true);
      return true;
    }
    if (Attr.getMaxArgs() && !NumArgs) {
      Diag(LParenLoc, diag::err_attribute_requires_arguments) << AttrName;
      Attr.setInvalid(true);
    } else if (!Attr.getMaxArgs()) {
      Diag(LParenLoc, diag::err_attribute_forbids_arguments)
          << AttrName
          << FixItHint::CreateRemoval(SourceRange(LParenLoc, *EndLoc));
      Attr.setInvalid(true);
    }
  }
  return true;
}

void Parser::ParseBracketAttributeSpecifierInternal(ParsedAttributes &Attrs,
                                                    SourceLocation *EndLoc) {
  if (Tok.is(tok::kw_alignas)) {
    if (getLangOpts().C23)
      Diag(Tok, diag::warn_c23_compat_keyword) << Tok.getName();
    ParseAlignmentSpecifier(Attrs, EndLoc);
    return;
  }

  if (Tok.isRegularKeywordAttribute()) {
    SourceLocation Loc = Tok.getLocation();
    IdentifierInfo *AttrName = Tok.getIdentifierInfo();
    Attrs.addNew(AttrName, Loc, nullptr, Loc, nullptr, 0, Tok.getKind());
    ConsumeToken();
    return;
  }

  assert(Tok.is(tok::l_square) && NextToken().is(tok::l_square) &&
         "Not a double square bracket attribute list");

  SourceLocation OpenLoc = Tok.getLocation();
  Diag(OpenLoc, getLangOpts().C23 ? diag::warn_pre_c23_compat_attributes
                                  : diag::warn_ext_c23_attributes);

  ConsumeBracket();
  checkCompoundToken(OpenLoc, tok::l_square, CompoundToken::AttrBegin);
  ConsumeBracket();

  SourceLocation CommonScopeLoc;
  IdentifierInfo *CommonScopeName = nullptr;
  if (Tok.is(tok::identifier) && Tok.getIdentifierInfo()->isStr("using")) {
    Diag(Tok.getLocation(), diag::ext_using_attribute_ns);
    ConsumeToken();
    CommonScopeName = TryParseBracketAttributeIdentifier(CommonScopeLoc);
    if (!CommonScopeName) {
      Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
      SkipUntil(tok::r_square, tok::colon, StopBeforeMatch);
    }
    if (!TryConsumeToken(tok::colon) && CommonScopeName)
      Diag(Tok.getLocation(), diag::err_expected) << tok::colon;
  }

  bool AttrParsed = false;
  while (!Tok.isOneOf(tok::r_square, tok::semi, tok::eof)) {
    if (AttrParsed) {
      if (RequireToken(tok::comma)) {
        SkipUntil(tok::r_square, StopAtSemi | StopBeforeMatch);
        continue;
      }
      AttrParsed = false;
    }

    while (TryConsumeToken(tok::comma))
      ;

    SourceLocation ScopeLoc, AttrLoc;
    IdentifierInfo *ScopeName = nullptr, *AttrName = nullptr;

    AttrName = TryParseBracketAttributeIdentifier(AttrLoc);
    if (!AttrName)
      break;

    if (TryConsumeToken(tok::coloncolon)) {
      ScopeName = AttrName;
      ScopeLoc = AttrLoc;
      AttrName = TryParseBracketAttributeIdentifier(AttrLoc);
      if (!AttrName) {
        Diag(Tok.getLocation(), diag::err_expected) << tok::identifier;
        SkipUntil(tok::r_square, tok::comma, StopAtSemi | StopBeforeMatch);
        continue;
      }
    }

    if (CommonScopeName) {
      if (ScopeName)
        Diag(ScopeLoc, diag::err_using_attribute_ns_conflict)
            << SourceRange(CommonScopeLoc);
      else {
        ScopeName = CommonScopeName;
        ScopeLoc = CommonScopeLoc;
      }
    }

    if (Tok.is(tok::l_paren))
      AttrParsed = ParseBracketAttributeArgs(AttrName, AttrLoc, Attrs, EndLoc,
                                             ScopeName, ScopeLoc);

    if (!AttrParsed) {
      Attrs.addNew(
          AttrName,
          SourceRange(ScopeLoc.isValid() ? ScopeLoc : AttrLoc, AttrLoc),
          ScopeName, ScopeLoc, nullptr, 0, ParsedAttr::Form::C23());
      AttrParsed = true;
    }

    if (TryConsumeToken(tok::ellipsis))
      Diag(Tok, diag::err_attribute_forbids_ellipsis) << AttrName;
  }

  if (Tok.is(tok::semi)) {
    ConsumeToken();
    return;
  }

  SourceLocation CloseLoc = Tok.getLocation();
  if (RequireToken(tok::r_square))
    SkipUntil(tok::r_square);
  else if (Tok.is(tok::r_square))
    checkCompoundToken(CloseLoc, tok::r_square, CompoundToken::AttrEnd);
  if (EndLoc)
    *EndLoc = Tok.getLocation();
  if (RequireToken(tok::r_square))
    SkipUntil(tok::r_square);
}

void Parser::ParseBracketAttributes(ParsedAttributes &Attrs) {
  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc = StartLoc;
  do {
    ParseBracketAttributeSpecifier(Attrs, &EndLoc);
  } while (isAllowedBracketAttributeSpecifier());
  Attrs.Range = SourceRange(StartLoc, EndLoc);
}

void Parser::DiagnoseAndSkipBracketAttributes() {
  auto Keyword =
      Tok.isRegularKeywordAttribute() ? Tok.getIdentifierInfo() : nullptr;
  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc = SkipBracketAttributes();
  if (EndLoc.isValid()) {
    SourceRange Range(StartLoc, EndLoc);
    (Keyword ? Diag(StartLoc, diag::err_keyword_not_allowed) << Keyword
             : Diag(StartLoc, diag::err_attributes_not_allowed))
        << Range;
  }
}

SourceLocation Parser::SkipBracketAttributes() {
  SourceLocation EndLoc;
  if (!isBracketAttributeSpecifier())
    return EndLoc;
  do {
    if (Tok.is(tok::l_square)) {
      BalancedDelimiterTracker T(*this, tok::l_square);
      T.consumeOpen();
      T.skipToEnd();
      EndLoc = T.getCloseLocation();
    } else if (Tok.isRegularKeywordAttribute()) {
      EndLoc = Tok.getLocation();
      ConsumeToken();
    } else {
      assert(Tok.is(tok::kw_alignas) && "not an attribute specifier");
      ConsumeToken();
      BalancedDelimiterTracker T(*this, tok::l_paren);
      if (!T.consumeOpen())
        T.skipToEnd();
      EndLoc = T.getCloseLocation();
    }
  } while (isBracketAttributeSpecifier());
  return EndLoc;
}

void Parser::ParseMicrosoftAttributes(ParsedAttributes &Attrs) {
  assert(Tok.is(tok::l_square) && "Not a Microsoft attribute list");

  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc = StartLoc;
  do {
    BalancedDelimiterTracker T(*this, tok::l_square);
    T.consumeOpen();
    while (true) {
      SkipUntil(tok::r_square, tok::identifier, StopAtSemi | StopBeforeMatch);
      if (Tok.isNot(tok::identifier))
        break;
      {
        IdentifierInfo *II = Tok.getIdentifierInfo();
        SourceLocation NameLoc = Tok.getLocation();
        ConsumeToken();
        ParsedAttr::Kind AttrKind =
            ParsedAttr::getParsedKind(II, nullptr, ParsedAttr::AS_Microsoft);
        if (AttrKind != ParsedAttr::UnknownAttribute) {
          bool AttrParsed = false;
          if (Tok.is(tok::l_paren))
            AttrParsed = ParseBracketAttributeArgs(II, NameLoc, Attrs, &EndLoc,
                                                   nullptr, SourceLocation());
          if (!AttrParsed)
            Attrs.addNew(II, NameLoc, nullptr, SourceLocation(), nullptr, 0,
                         ParsedAttr::Form::Microsoft());
        }
      }
    }
    T.consumeClose();
    EndLoc = T.getCloseLocation();
  } while (Tok.is(tok::l_square));

  Attrs.Range = SourceRange(StartLoc, EndLoc);
}
