#include "neverc/Analyze/Designator.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Syntax/ParseDiag.h"
#include "neverc/Syntax/ParserGuards.h"
#include "neverc/Syntax/SyntaxParser.h"
#include "llvm/ADT/SmallString.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// Designator detection & parsing
// ===----------------------------------------------------------------------===

bool Parser::MayBeDesignationStart() {
  tok::TokenKind K = Tok.getKind();
  if (K == tok::period || K == tok::l_square)
    return true;
  return K == tok::identifier && PP.LookAhead(0).is(tok::colon);
}

ExprResult Parser::ParseInitializerWithPotentialDesignator() {
  // If this is the old-style GNU extension:
  //   designation ::= identifier ':'
  // Handle it as a field designator.  Otherwise, this must be the start of a
  // normal expression.
  if (Tok.is(tok::identifier)) {
    const IdentifierInfo *FieldName = Tok.getIdentifierInfo();

    llvm::SmallString<256> NewSyntax;
    llvm::raw_svector_ostream(NewSyntax)
        << '.' << FieldName->getName() << " = ";

    SourceLocation NameLoc = ConsumeToken(); // Eat the identifier.

    assert(Tok.is(tok::colon) && "MayBeDesignationStart not working properly!");
    SourceLocation ColonLoc = ConsumeToken();

    Diag(NameLoc, diag::ext_gnu_old_style_field_designator)
        << FixItHint::CreateReplacement(SourceRange(NameLoc, ColonLoc),
                                        NewSyntax);

    Designation D;
    D.AddDesignator(Designator::CreateFieldDesignator(
        FieldName, SourceLocation(), NameLoc));
    return Actions.OnDesignatedInitializer(D, ColonLoc, true,
                                           ParseInitializer());
  }

  Designation Desig;

  while (Tok.isOneOf(tok::period, tok::l_square)) {
    if (Tok.is(tok::period)) {
      // designator: '.' identifier
      SourceLocation DotLoc = ConsumeToken();

      if (Tok.isNot(tok::identifier)) {
        Diag(Tok.getLocation(), diag::err_expected_field_designator);
        return ExprError();
      }

      Desig.AddDesignator(Designator::CreateFieldDesignator(
          Tok.getIdentifierInfo(), DotLoc, Tok.getLocation()));
      ConsumeToken(); // Eat the identifier.
      continue;
    }

    assert(Tok.is(tok::l_square) && "Unexpected token!");

    // Handle the two forms of array designator:
    //   array-designator: '[' constant-expression ']'
    //   array-designator: '[' constant-expression '...' constant-expression ']'
    BalancedDelimiterTracker T(*this, tok::l_square);
    T.consumeOpen();
    SourceLocation StartLoc = T.getOpenLocation();

    ExprResult Idx;

    // Parse the index expression as an assignment expression.
    // Sema needs to validate that the expression is a constant.
    if (!Idx.get()) {
      Idx = ParseAssignmentExpression();
      if (Idx.isInvalid()) {
        SkipUntil(tok::r_square, StopAtSemi);
        return Idx;
      }
    }

    // Given an expression, we could either have a designator (if the next
    // tokens are '...' or ']') or an error.

    // If this is a normal array designator, remember it.
    if (Tok.isNot(tok::ellipsis)) {
      Desig.AddDesignator(
          Designator::CreateArrayDesignator(Idx.get(), StartLoc));
    } else {
      // Handle the gnu array range extension.
      Diag(Tok, diag::ext_gnu_array_range);
      SourceLocation EllipsisLoc = ConsumeToken();

      ExprResult RHS(ParseConstantExpression());
      if (RHS.isInvalid()) {
        SkipUntil(tok::r_square, StopAtSemi);
        return RHS;
      }
      Desig.AddDesignator(Designator::CreateArrayRangeDesignator(
          Idx.get(), RHS.get(), StartLoc, EllipsisLoc));
    }

    T.consumeClose();
    Desig.getDesignator(Desig.getNumDesignators() - 1)
        .setRBracketLoc(T.getCloseLocation());
  }

  // We're done with the designator sequence. There must be at least one
  // designator.
  assert(!Desig.empty() && "Designator is empty?");

  // Handle a normal designator sequence end, which is an equal.
  if (Tok.is(tok::equal)) {
    SourceLocation EqualLoc = ConsumeToken();
    return Actions.OnDesignatedInitializer(Desig, EqualLoc, false,
                                           ParseInitializer());
  }

  // We read some number of designators and found something that isn't an = or
  // an initializer.  If we have exactly one array designator, this
  // is the GNU 'designation: array-designator' extension.  Otherwise, it is a
  // parse error.
  if (Desig.getNumDesignators() == 1 &&
      (Desig.getDesignator(0).isArrayDesignator() ||
       Desig.getDesignator(0).isArrayRangeDesignator())) {
    Diag(Tok, diag::ext_gnu_missing_equal_designator)
        << FixItHint::CreateInsertion(Tok.getLocation(), "= ");
    return Actions.OnDesignatedInitializer(Desig, Tok.getLocation(), true,
                                           ParseInitializer());
  }

  Diag(Tok, diag::err_expected_equal_designator);
  return ExprError();
}

// ===----------------------------------------------------------------------===
// Brace-enclosed initializer lists
// ===----------------------------------------------------------------------===

ExprResult Parser::ParseBraceInitializer() {
  BalancedDelimiterTracker T(*this, tok::l_brace);
  T.consumeOpen();
  SourceLocation LBraceLoc = T.getOpenLocation();

  ExprVector InitExprs;

  if (Tok.is(tok::r_brace)) {
    // C23: empty braces are standard; before C23 this is a GNU extension.
    Diag(LBraceLoc, getLangOpts().C23 ? diag::warn_c23_compat_empty_initializer
                                      : diag::ext_c_empty_initializer);
    // Match the '}'.
    return Actions.OnInitList(LBraceLoc, std::nullopt, ConsumeBrace());
  }

  bool InitExprsOk = true;
  while (true) {
    if (getLangOpts().MicrosoftExt &&
        (Tok.is(tok::kw___if_exists) || Tok.is(tok::kw___if_not_exists))) {
      if (ParseMicrosoftIfExistsBraceInitializer(InitExprs, InitExprsOk)) {
        if (Tok.isNot(tok::comma))
          break;
        ConsumeToken();
      }
      if (Tok.is(tok::r_brace))
        break;
      continue;
    }

    // Parse: designation[opt] initializer

    // If we know that this cannot be a designation, just parse the nested
    // initializer directly.
    ExprResult SubElt;
    if (MayBeDesignationStart())
      SubElt = ParseInitializerWithPotentialDesignator();
    else
      SubElt = ParseInitializer();

    SubElt = Actions.CorrectDelayedTyposInExpr(SubElt.get());

    // If we couldn't parse the subelement, bail out.
    if (SubElt.isUsable()) {
      InitExprs.push_back(SubElt.get());
    } else {
      InitExprsOk = false;

      // We have two ways to try to recover from this error: if the code looks
      // grammatically ok (i.e. we have a comma coming up) try to continue
      // parsing the rest of the initializer.  This allows us to emit
      // diagnostics for later elements that we find.  If we don't see a comma,
      // assume there is a parse error, and just skip to recover.
      if (Tok.isNot(tok::comma)) {
        SkipUntil(tok::r_brace, StopBeforeMatch);
        break;
      }
    }

    // If we don't have a comma continued list, we're done.
    if (Tok.isNot(tok::comma))
      break;

    ConsumeToken();

    // Handle trailing comma.
    if (Tok.is(tok::r_brace))
      break;
  }

  bool closed = !T.consumeClose();

  if (InitExprsOk && closed)
    return Actions.OnInitList(LBraceLoc, InitExprs, T.getCloseLocation());

  return ExprError(); // an error occurred.
}

// Return true if a comma (or closing brace) is necessary after the
// __if_exists/if_not_exists statement.
// ===----------------------------------------------------------------------===
// MSVC __if_exists support inside aggregate initializers
// ===----------------------------------------------------------------------===

bool Parser::ParseMicrosoftIfExistsBraceInitializer(ExprVector &InitExprs,
                                                    bool &InitExprsOk) {
  bool trailingComma = false;
  IfExistsCondition Result;
  if (ParseMicrosoftIfExistsCondition(Result))
    return false;

  BalancedDelimiterTracker Braces(*this, tok::l_brace);
  if (Braces.consumeOpen()) {
    Diag(Tok, diag::err_expected) << tok::l_brace;
    return false;
  }

  switch (Result.Behavior) {
  case IEB_Parse:
    // Parse the declarations below.
    break;

  case IEB_Skip:
    Braces.skipToEnd();
    return false;
  }

  while (!isEofOrEom()) {
    trailingComma = false;
    ExprResult SubElt;
    if (MayBeDesignationStart())
      SubElt = ParseInitializerWithPotentialDesignator();
    else
      SubElt = ParseInitializer();

    // If we couldn't parse the subelement, bail out.
    if (!SubElt.isInvalid())
      InitExprs.push_back(SubElt.get());
    else
      InitExprsOk = false;

    if (Tok.is(tok::comma)) {
      ConsumeToken();
      trailingComma = true;
    }

    if (Tok.is(tok::r_brace))
      break;
  }

  Braces.consumeClose();

  return !trailingComma;
}
