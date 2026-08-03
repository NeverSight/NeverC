//===--- ExprParserCXX.cpp - C++ expression parsing -----------------------===//
#include "neverc/Syntax/SyntaxParser.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/Support/ErrorHandling.h"

using namespace neverc;

ExprResult Parser::ParseLambdaExpression() {
  assert(Tok.is(tok::l_square) && "expected lambda introducer");
  SourceLocation Begin = ConsumeBracket(); // [

  // capture-default: [=] or [&], plus simple capture-ids (x, &x, this).
  unsigned CaptureDefault = 0; // 0 none, 1 =, 2 &
  llvm::SmallVector<IdentifierInfo *, 4> CaptureIds;
  bool First = true;
  while (Tok.isNot(tok::r_square) && Tok.isNot(tok::eof)) {
    if (Tok.is(tok::comma)) {
      ConsumeToken();
      First = false;
      continue;
    }
    if (First && (Tok.is(tok::equal) || Tok.is(tok::amp))) {
      bool IsAmp = Tok.is(tok::amp);
      ConsumeToken();
      // capture-default only when not followed by identifier (that is &x).
      if (IsAmp && Tok.is(tok::identifier)) {
        CaptureIds.push_back(Tok.getIdentifierInfo());
        ConsumeToken();
      } else if (!IsAmp || Tok.is(tok::comma) || Tok.is(tok::r_square)) {
        CaptureDefault = IsAmp ? 2 : 1;
      } else {
        // &this / &*this etc. — consume progress
        if (Tok.is(tok::kw_this) || Tok.is(tok::star))
          ConsumeToken();
      }
      First = false;
      continue;
    }
    if (Tok.is(tok::identifier)) {
      CaptureIds.push_back(Tok.getIdentifierInfo());
      ConsumeToken();
      First = false;
      continue;
    }
    if (Tok.is(tok::kw_this) || Tok.is(tok::star) || Tok.is(tok::ellipsis) ||
        Tok.is(tok::amp)) {
      ConsumeToken();
      First = false;
      continue;
    }
    ConsumeAnyToken();
    First = false;
  }
  SourceLocation EndIntro = Tok.getLocation();
  if (Tok.is(tok::r_square)) ConsumeToken(); else Diag(Tok, diag::err_expected) << "]";

  // optional parameter list — parse simple types (builtin / identifier).
  llvm::SmallVector<QualType, 4> LambdaParamTys;
  if (Tok.is(tok::l_paren)) {
    BalancedDelimiterTracker T(*this, tok::l_paren);
    T.consumeOpen();
    auto builtinTy = [&]() -> QualType {
      TreeContext &Ctx = Actions.getTreeContext();
      switch (Tok.getKind()) {
      case tok::kw_void: return Ctx.VoidTy;
      case tok::kw_bool: return Ctx.BoolTy;
      case tok::kw_char: return Ctx.CharTy;
      case tok::kw_int: return Ctx.IntTy;
      case tok::kw_long: return Ctx.LongTy;
      case tok::kw_short: return Ctx.ShortTy;
      case tok::kw_float: return Ctx.FloatTy;
      case tok::kw_double: return Ctx.DoubleTy;
      case tok::kw_unsigned: return Ctx.UnsignedIntTy;
      case tok::kw_signed: return Ctx.IntTy;
      default: return QualType();
      }
    };
    while (Tok.isNot(tok::r_paren) && Tok.isNot(tok::eof)) {
      QualType PT;
      // cv-qual skip
      while (Tok.is(tok::kw_const) || Tok.is(tok::kw_volatile))
        ConsumeToken();
      if (Tok.is(tok::identifier)) {
        // Treat bare identifier as typedef/class name → Record lookup later;
        // use IntTy scaffold when unknown, or pointer if followed by *.
        IdentifierInfo *II = Tok.getIdentifierInfo();
        ConsumeToken();
        PT = Actions.getTreeContext().IntTy;
        // Optional namespace::name skip
        while (Tok.is(tok::coloncolon) || Tok.is(tok::identifier)) {
          if (Tok.is(tok::coloncolon)) ConsumeToken();
          else ConsumeToken();
        }
        (void)II;
      } else {
        PT = builtinTy();
        if (!PT.isNull())
          ConsumeToken();
        // multi-token long long / long double
        if (Tok.is(tok::kw_long) || Tok.is(tok::kw_int) || Tok.is(tok::kw_double)) {
          if (Tok.is(tok::kw_double))
            PT = Actions.getTreeContext().LongDoubleTy;
          else if (Tok.is(tok::kw_long))
            PT = Actions.getTreeContext().LongLongTy;
          ConsumeToken();
        }
      }
      // pointer / ref
      while (Tok.is(tok::star)) {
        ConsumeToken();
        if (!PT.isNull())
          PT = Actions.getTreeContext().getPointerType(PT);
      }
      if (Tok.is(tok::ampamp)) {
        ConsumeToken();
        if (!PT.isNull())
          PT = Actions.getTreeContext().getRValueReferenceType(PT);
      } else if (Tok.is(tok::amp)) {
        ConsumeToken();
        if (!PT.isNull())
          PT = Actions.getTreeContext().getLValueReferenceType(PT);
      }
      // skip param name and default arg
      if (Tok.is(tok::identifier))
        ConsumeToken();
      if (Tok.is(tok::equal)) {
        ConsumeToken();
        int Depth = 0;
        while (Tok.isNot(tok::eof)) {
          if (Tok.is(tok::l_paren) || Tok.is(tok::l_square) || Tok.is(tok::l_brace))
            ++Depth;
          else if (Tok.is(tok::r_paren) || Tok.is(tok::r_square) ||
                   Tok.is(tok::r_brace)) {
            if (Depth == 0) break;
            --Depth;
          } else if (Tok.is(tok::comma) && Depth == 0)
            break;
          ConsumeAnyToken();
        }
      }
      if (PT.isNull())
        PT = Actions.getTreeContext().IntTy;
      LambdaParamTys.push_back(PT);
      if (Tok.is(tok::comma)) {
        ConsumeToken();
        continue;
      }
      break;
    }
    T.consumeClose();
  }

  // optional mutable / constexpr / consteval / exception-spec / trailing-return
  while (Tok.is(tok::kw_mutable) || Tok.is(tok::kw_constexpr) ||
         Tok.is(tok::kw_consteval) || Tok.is(tok::kw_noexcept) ||
         Tok.is(tok::kw_throw)) {
    if (Tok.is(tok::kw_noexcept) || Tok.is(tok::kw_throw)) {
      ConsumeToken();
      if (Tok.is(tok::l_paren)) {
        BalancedDelimiterTracker T(*this, tok::l_paren);
        T.consumeOpen();
        while (Tok.isNot(tok::r_paren) && Tok.isNot(tok::eof))
          ConsumeAnyToken();
        T.consumeClose();
      }
    } else {
      ConsumeToken();
    }
  }
  if (Tok.is(tok::arrow)) {
    ConsumeToken();
    // type-id skip
    while (Tok.isNot(tok::l_brace) && Tok.isNot(tok::eof) && Tok.isNot(tok::semi))
      ConsumeAnyToken();
  }

  StmtResult Body = true;
  if (Tok.is(tok::l_brace))
    Body = ParseCompoundStatement();
  else {
    Diag(Tok, diag::err_expected) << "{";
    return ExprError();
  }

  return Actions.OnLambdaExpr(Begin, EndIntro, Body.get(), CaptureDefault,
                              CaptureIds, LambdaParamTys);
}

ExprResult Parser::ParseCXXNewExpression(bool UseGlobal, SourceLocation NewLoc) {
  (void)UseGlobal;
  // new (placement?) type-id (init?)
  if (Tok.is(tok::l_paren)) {
    // Could be placement or parenthesized type - simplified skip placement
    BalancedDelimiterTracker T(*this, tok::l_paren);
    T.consumeOpen();
    // If this looks like a type, still just parse expression list best-effort
    llvm::SmallVector<Expr *, 4> Placeholders;
    if (Tok.isNot(tok::r_paren)) {
      ExprResult E = ParseExpression();
      if (E.isUsable())
        Placeholders.push_back(E.get());
      while (Tok.is(tok::comma)) {
        ConsumeToken();
        E = ParseExpression();
        if (E.isUsable())
          Placeholders.push_back(E.get());
      }
    }
    T.consumeClose();
    (void)Placeholders;
  }

  // type
  DeclSpec DS(AttrFactory);
  ParseDeclarationSpecifiers(DS);
  Declarator D(DS, DeclaratorContext::TypeName);
  ParseDeclarator(D);

  Expr *Init = nullptr;
  if (Tok.is(tok::l_paren)) {
    BalancedDelimiterTracker T(*this, tok::l_paren);
    T.consumeOpen();
    if (Tok.isNot(tok::r_paren)) {
      ExprResult E = ParseExpression();
      if (E.isUsable())
        Init = E.get();
    }
    T.consumeClose();
  } else if (Tok.is(tok::l_brace)) {
    ExprResult E = ParseBraceInitializer();
    if (E.isUsable())
      Init = E.get();
  }

  return Actions.OnCXXNew(NewLoc, D, Init);
}

ExprResult Parser::ParseCXXDeleteExpression(bool UseGlobal,
                                            SourceLocation DeleteLoc) {
  (void)UseGlobal;
  bool ArrayForm = false;
  if (Tok.is(tok::l_square)) {
    ConsumeBracket();
    if (Tok.is(tok::r_square)) ConsumeToken(); else Diag(Tok, diag::err_expected) << "]";
    ArrayForm = true;
  }
  ExprResult Operand = ParseCastExpression(/*isUnaryExpression=*/false);
  return Actions.OnCXXDelete(DeleteLoc, ArrayForm, Operand.get());
}

ExprResult Parser::ParseCXXNamedCast(tok::TokenKind Kind, SourceLocation KWLoc) {
  if (Tok.isNot(tok::less)) {
    Diag(Tok, diag::err_expected) << "<";
    return ExprError();
  }
  ConsumeToken();
  // type-id
  TypeResult Ty = ParseTypeName();
  if (Tok.is(tok::greater))
    ConsumeToken();
  else if (Tok.is(tok::greatergreater))
    Tok.setKind(tok::greater);
  else
    Diag(Tok, diag::err_expected) << ">";

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen())
    return ExprError();
  ExprResult Op = ParseExpression();
  T.consumeClose();
  return Actions.OnCXXNamedCast(KWLoc, Kind, Ty, Op.get());
}

ExprResult Parser::ParseThrowExpression() {
  assert(Tok.is(tok::kw_throw));
  SourceLocation ThrowLoc = ConsumeToken();
  if (Tok.is(tok::semi) || Tok.is(tok::r_paren) || Tok.is(tok::colon) ||
      Tok.is(tok::comma) || Tok.is(tok::r_square) || Tok.is(tok::r_brace))
    return Actions.OnCXXThrow(ThrowLoc, nullptr);
  ExprResult Op = ParseAssignmentExpression();
  return Actions.OnCXXThrow(ThrowLoc, Op.get());
}

ExprResult Parser::ParseCoawaitExpression() {
  assert(Tok.is(tok::kw_co_await));
  SourceLocation Loc = ConsumeToken();
  ExprResult Op = ParseCastExpression(/*isUnaryExpression=*/false);
  return Actions.OnCoawaitExpr(Loc, Op.get());
}

ExprResult Parser::ParseCoyieldExpression() {
  assert(Tok.is(tok::kw_co_yield));
  SourceLocation Loc = ConsumeToken();
  ExprResult Op = ParseAssignmentExpression();
  return Actions.OnCoyieldExpr(Loc, Op.get());
}

StmtResult Parser::ParseCXXTryBlock() {
  assert(Tok.is(tok::kw_try));
  SourceLocation TryLoc = ConsumeToken();
  if (Tok.isNot(tok::l_brace)) {
    Diag(Tok, diag::err_expected) << "{";
    return StmtError();
  }
  StmtResult TryBlock = ParseCompoundStatement();
  llvm::SmallVector<Stmt *, 2> Handlers;
  while (Tok.is(tok::kw_catch)) {
    StmtResult H = ParseCXXCatchBlock();
    if (H.isUsable())
      Handlers.push_back(H.get());
  }
  return Actions.OnCXXTryBlock(TryLoc, TryBlock.get(), Handlers);
}

StmtResult Parser::ParseCXXCatchBlock() {
  assert(Tok.is(tok::kw_catch));
  SourceLocation CatchLoc = ConsumeToken();
  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen())
    return StmtError();
  Decl *ExceptionDecl = nullptr;
  if (Tok.is(tok::ellipsis)) {
    ConsumeToken();
  } else {
    ParsedAttributes attrs(AttrFactory);
    DeclSpec DS(AttrFactory);
    ParseDeclarationSpecifiers(DS, attrs);
    ParsingDeclarator D(*this, DS, attrs, DeclaratorContext::CXXCatch);
    ParseDeclarator(D);
    ExceptionDecl = Actions.OnExceptionDeclarator(getCurScope(), D);
  }
  T.consumeClose();
  StmtResult Handler = ParseCompoundStatement();
  return Actions.OnCXXCatchBlock(CatchLoc, ExceptionDecl, Handler.get());
}
