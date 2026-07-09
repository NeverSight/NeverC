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
