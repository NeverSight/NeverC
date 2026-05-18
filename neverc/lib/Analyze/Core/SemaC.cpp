//===- SemaC.cpp -- C-only Sema entry points (StaticAssert / EmptyDecl) ---===//
//
// Out-of-line `Sema` actions that are intrinsic to C-language semantics
// rather than to any conversion / overload / standard-conversion path.
//
// Public entry points (declared in `Sema/Sema.h`):
//   * `Sema::OnEmptyDeclaration`             bare `;` declaration.
//   * `Sema::OnStaticAssertDeclaration`      C11 `_Static_assert`.
//   * `Sema::EvaluateStaticAssertMessageAsString`
//   * `Sema::FormStaticAssertDeclaration`
//
// Split out of `SemaConversions.cpp` (Section 1).  New C-only Sema entry
// points should land here.
//

#include "neverc/Analyze/Sema.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Diagnostic/DiagnosticSema.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace neverc;

Decl *Sema::OnEmptyDeclaration(Scope *S, const ParsedAttributesView &AttrList,
                               SourceLocation SemiLoc) {
  Decl *ED = EmptyDecl::Create(Context, CurContext, SemiLoc);
  // Attribute declarations appertain to empty declaration so we handle
  // them here.
  ProcessDeclAttributeList(S, ED, AttrList);

  CurContext->addDecl(ED);
  return ED;
}

Decl *Sema::OnStaticAssertDeclaration(SourceLocation StaticAssertLoc,
                                      Expr *AssertExpr, Expr *AssertMessageExpr,
                                      SourceLocation RParenLoc) {
  return FormStaticAssertDeclaration(StaticAssertLoc, AssertExpr,
                                     AssertMessageExpr, RParenLoc, false);
}

bool Sema::EvaluateStaticAssertMessageAsString(Expr *Message,
                                               std::string &Result,
                                               TreeContext &Ctx,
                                               bool ErrorOnInvalidMessage) {
  assert(Message);

  if (const auto *SL = dyn_cast<StringLiteral>(Message)) {
    assert(SL->isUnevaluated() && "expected an unevaluated string");
    Result.assign(SL->getString().begin(), SL->getString().end());
    return true;
  }

  Diag(Message->getBeginLoc(), diag::err_static_assert_invalid_message);
  return false;
}

Decl *Sema::FormStaticAssertDeclaration(SourceLocation StaticAssertLoc,
                                        Expr *AssertExpr, Expr *AssertMessage,
                                        SourceLocation RParenLoc, bool Failed) {
  assert(AssertExpr != nullptr && "Expected non-null condition");
  if (!Failed) {
    // In a static_assert-declaration, the constant-expression shall be a
    // constant expression that can be contextually converted to bool.
    ExprResult Converted = PerformContextuallyConvertToBool(AssertExpr);
    if (Converted.isInvalid())
      Failed = true;

    ExprResult FullAssertExpr =
        OnFinishFullExpr(Converted.get(), StaticAssertLoc,
                         /*DiscardedValue*/ false,
                         /*IsConstexpr*/ true);
    if (FullAssertExpr.isInvalid())
      Failed = true;
    else
      AssertExpr = FullAssertExpr.get();

    llvm::APSInt Cond;
    Expr *BaseExpr = AssertExpr;
    AllowFoldKind FoldKind = NoFold;

    // In C mode, allow folding as an extension for static_assert forms that
    // are not strictly integer constant expressions.
    FoldKind = AllowFold;

    if (!Failed &&
        VerifyIntegerConstantExpression(
            BaseExpr, &Cond, diag::err_static_assert_expression_is_not_constant,
            FoldKind)
            .isInvalid())
      Failed = true;

    // If the static_assert passes, only verify that
    // the message is grammatically valid without evaluating it.
    if (!Failed && AssertMessage && Cond.getBoolValue()) {
      std::string Str;
      EvaluateStaticAssertMessageAsString(AssertMessage, Str, Context,
                                          /*ErrorOnInvalidMessage=*/false);
    }

    // Static assertion failed (condition evaluates to false).
    if (!Failed && !Cond) {
      llvm::SmallString<256> MsgBuffer;
      llvm::raw_svector_ostream Msg(MsgBuffer);
      bool HasMessage = AssertMessage;
      if (AssertMessage) {
        std::string Str;
        HasMessage =
            EvaluateStaticAssertMessageAsString(
                AssertMessage, Str, Context, /*ErrorOnInvalidMessage=*/true) ||
            !Str.empty();
        Msg << Str;
      }
      Diag(AssertExpr->getBeginLoc(), diag::err_static_assert_failed)
          << !HasMessage << Msg.str() << AssertExpr->getSourceRange();
      PrintContextStack();
      Failed = true;
    }
  } else {
    ExprResult FullAssertExpr = OnFinishFullExpr(AssertExpr, StaticAssertLoc,
                                                 /*DiscardedValue*/ false,
                                                 /*IsConstexpr*/ true);
    if (FullAssertExpr.isInvalid())
      Failed = true;
    else
      AssertExpr = FullAssertExpr.get();
  }

  Decl *Decl =
      StaticAssertDecl::Create(Context, CurContext, StaticAssertLoc, AssertExpr,
                               AssertMessage, RParenLoc, Failed);

  CurContext->addDecl(Decl);
  return Decl;
}
