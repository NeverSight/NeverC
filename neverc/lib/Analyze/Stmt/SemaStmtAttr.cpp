#include "neverc/Analyze/DelayedDiagnostic.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include <optional>

using namespace neverc;
using namespace sema;

namespace {
Attr *handleFallThroughAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                            SourceRange Range) {
  FallThroughAttr Attr(S.Context, A);
  if (isa<SwitchCase>(St)) {
    S.Diag(A.getRange().getBegin(), diag::err_fallthrough_attr_wrong_target)
        << A << St->getBeginLoc();
    SourceLocation L = S.getLocForEndOfToken(Range.getEnd());
    S.Diag(L, diag::note_fallthrough_insert_semi_fixit)
        << FixItHint::CreateInsertion(L, ";");
    return nullptr;
  }
  auto *FnScope = S.getCurFunction();
  if (FnScope->SwitchStack.empty()) {
    S.Diag(A.getRange().getBegin(), diag::err_fallthrough_attr_outside_switch);
    return nullptr;
  }

  // `[[fallthrough]]` spelling: warn when used outside the standard attribute
  // rules for the current language (extension).
  if (A.isBracketAttribute() && !A.getScopeName())
    S.Diag(A.getLoc(), diag::ext_c23_attr) << A;

  FnScope->setHasFallthroughStmt();
  return ::new (S.Context) FallThroughAttr(S.Context, A);
}

Attr *handleSuppressAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                         SourceRange Range) {
  if (A.getAttributeSpellingListIndex() == SuppressAttr::Bracket_gsl_suppress &&
      A.getNumArgs() < 1) {
    // Suppression attribute with GSL spelling requires at least 1 argument.
    S.Diag(A.getLoc(), diag::err_attribute_too_few_arguments) << A << 1;
    return nullptr;
  }

  std::vector<llvm::StringRef> DiagnosticIdentifiers;
  for (unsigned I = 0, E = A.getNumArgs(); I != E; ++I) {
    llvm::StringRef RuleName;

    if (!S.checkStringLiteralArgumentAttr(A, I, RuleName, nullptr))
      return nullptr;

    DiagnosticIdentifiers.push_back(RuleName);
  }

  return ::new (S.Context) SuppressAttr(
      S.Context, A, DiagnosticIdentifiers.data(), DiagnosticIdentifiers.size());
}

namespace {
class CallExprFinder : public ConstEvaluatedExprVisitor<CallExprFinder> {
  bool FoundAsmStmt = false;
  std::vector<const CallExpr *> CallExprs;

public:
  typedef ConstEvaluatedExprVisitor<CallExprFinder> Inherited;

  CallExprFinder(Sema &S, const Stmt *St) : Inherited(S.Context) { Visit(St); }

  bool foundCallExpr() { return !CallExprs.empty(); }
  const std::vector<const CallExpr *> &getCallExprs() { return CallExprs; }

  bool foundAsmStmt() { return FoundAsmStmt; }

  void VisitCallExpr(const CallExpr *E) { CallExprs.push_back(E); }

  void VisitAsmStmt(const AsmStmt *S) { FoundAsmStmt = true; }

  void Visit(const Stmt *St) {
    if (!St)
      return;
    ConstEvaluatedExprVisitor<CallExprFinder>::Visit(St);
  }
};
} // namespace

Attr *handleNoMergeAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                        SourceRange Range) {
  NoMergeAttr NMA(S.Context, A);
  CallExprFinder CEF(S, St);

  if (!CEF.foundCallExpr() && !CEF.foundAsmStmt()) {
    S.Diag(St->getBeginLoc(), diag::warn_attribute_ignored_no_calls_in_stmt)
        << A;
    return nullptr;
  }

  return ::new (S.Context) NoMergeAttr(S.Context, A);
}

template <typename OtherAttr, int DiagIdx>
bool checkStmtInlineAttr(Sema &SemaRef, const Stmt *OrigSt, const Stmt *CurSt,
                         const AttributeCommonInfo &A) {
  CallExprFinder OrigCEF(SemaRef, OrigSt);
  CallExprFinder CEF(SemaRef, CurSt);

  bool CanSuppressDiag =
      OrigSt && CEF.getCallExprs().size() == OrigCEF.getCallExprs().size();

  if (!CEF.foundCallExpr()) {
    return SemaRef.Diag(CurSt->getBeginLoc(),
                        diag::warn_attribute_ignored_no_calls_in_stmt)
           << A;
  }

  for (const auto &Tup :
       llvm::zip_longest(OrigCEF.getCallExprs(), CEF.getCallExprs())) {
    // If the original call expression already had a callee, we already
    // diagnosed this, so skip it here. We can't skip if there isn't a 1:1
    // relationship between the two lists of call expressions.
    if (!CanSuppressDiag || !(*std::get<0>(Tup))->getCalleeDecl()) {
      const Decl *Callee = (*std::get<1>(Tup))->getCalleeDecl();
      if (Callee &&
          (Callee->hasAttr<OtherAttr>() || Callee->hasAttr<FlattenAttr>())) {
        SemaRef.Diag(CurSt->getBeginLoc(),
                     diag::warn_function_stmt_attribute_precedence)
            << A << (Callee->hasAttr<OtherAttr>() ? DiagIdx : 1);
        SemaRef.Diag(Callee->getBeginLoc(), diag::note_conflicting_attribute);
      }
    }
  }

  return false;
}
} // namespace

bool Sema::CheckNoInlineAttr(const Stmt *OrigSt, const Stmt *CurSt,
                             const AttributeCommonInfo &A) {
  return checkStmtInlineAttr<AlwaysInlineAttr, 0>(*this, OrigSt, CurSt, A);
}

bool Sema::CheckAlwaysInlineAttr(const Stmt *OrigSt, const Stmt *CurSt,
                                 const AttributeCommonInfo &A) {
  return checkStmtInlineAttr<NoInlineAttr, 2>(*this, OrigSt, CurSt, A);
}

namespace {
Attr *handleNoInlineAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                         SourceRange Range) {
  NoInlineAttr NIA(S.Context, A);
  if (!NIA.isNeverCNoInline()) {
    S.Diag(St->getBeginLoc(), diag::warn_function_attribute_ignored_in_stmt)
        << "[[neverc::noinline]]";
    return nullptr;
  }

  if (S.CheckNoInlineAttr(/*OrigSt=*/nullptr, St, A))
    return nullptr;

  return ::new (S.Context) NoInlineAttr(S.Context, A);
}

Attr *handleAlwaysInlineAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                             SourceRange Range) {
  AlwaysInlineAttr AIA(S.Context, A);
  if (!AIA.isNeverCAlwaysInline()) {
    S.Diag(St->getBeginLoc(), diag::warn_function_attribute_ignored_in_stmt)
        << "[[neverc::always_inline]]";
    return nullptr;
  }

  if (S.CheckAlwaysInlineAttr(/*OrigSt=*/nullptr, St, A))
    return nullptr;

  return ::new (S.Context) AlwaysInlineAttr(S.Context, A);
}

Attr *handleMustTailAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                         SourceRange Range) {
  // Validation is in Sema::OnAttributedStmt().
  return ::new (S.Context) MustTailAttr(S.Context, A);
}

Attr *handleLikely(Sema &S, Stmt *St, const ParsedAttr &A, SourceRange Range) {

  if (A.isBracketAttribute() && !A.getScopeName())
    S.Diag(A.getLoc(), diag::ext_c23_attr) << A << Range;

  return ::new (S.Context) LikelyAttr(S.Context, A);
}

Attr *handleUnlikely(Sema &S, Stmt *St, const ParsedAttr &A,
                     SourceRange Range) {

  if (A.isBracketAttribute() && !A.getScopeName())
    S.Diag(A.getLoc(), diag::ext_c23_attr) << A << Range;

  return ::new (S.Context) UnlikelyAttr(S.Context, A);
}
} // namespace

CodeAlignAttr *Sema::FormCodeAlignAttr(const AttributeCommonInfo &CI, Expr *E) {
  llvm::APSInt ArgVal;
  ExprResult Res = VerifyIntegerConstantExpression(E, &ArgVal);
  if (Res.isInvalid())
    return nullptr;
  E = Res.get();

  // This attribute requires an integer argument which is a constant power of
  // two between 1 and 4096 inclusive.
  if (ArgVal < CodeAlignAttr::MinimumAlignment ||
      ArgVal > CodeAlignAttr::MaximumAlignment || !ArgVal.isPowerOf2()) {
    if (std::optional<int64_t> Value = ArgVal.trySExtValue())
      Diag(CI.getLoc(), diag::err_attribute_power_of_two_in_range)
          << CI << CodeAlignAttr::MinimumAlignment
          << CodeAlignAttr::MaximumAlignment << Value.value();
    else
      Diag(CI.getLoc(), diag::err_attribute_power_of_two_in_range)
          << CI << CodeAlignAttr::MinimumAlignment
          << CodeAlignAttr::MaximumAlignment << E;
    return nullptr;
  }
  return new (Context) CodeAlignAttr(Context, CI, E);
}

namespace {
Attr *handleCodeAlignAttr(Sema &S, Stmt *St, const ParsedAttr &A) {

  Expr *E = A.getArgAsExpr(0);
  return S.FormCodeAlignAttr(A, E);
}

// Diagnose non-identical duplicates as a 'conflicting' loop attributes
// and suppress duplicate errors in cases where the two match.
template <typename LoopAttrT>
void checkForDuplicateLoopAttrs(Sema &S, llvm::ArrayRef<const Attr *> Attrs) {
  auto FindFunc = [](const Attr *A) { return isa<const LoopAttrT>(A); };
  const auto *FirstItr = std::find_if(Attrs.begin(), Attrs.end(), FindFunc);

  if (FirstItr == Attrs.end()) // no attributes found
    return;

  const auto *LastFoundItr = FirstItr;
  std::optional<llvm::APSInt> FirstValue;

  const auto *CAFA =
      dyn_cast<ConstantExpr>(cast<LoopAttrT>(*FirstItr)->getAlignment());
  // Return early if first alignment expression is dependent (since we don't
  // know what the effective size will be), and skip the loop entirely.
  if (!CAFA)
    return;

  while (Attrs.end() != (LastFoundItr = std::find_if(LastFoundItr + 1,
                                                     Attrs.end(), FindFunc))) {
    const auto *CASA =
        dyn_cast<ConstantExpr>(cast<LoopAttrT>(*LastFoundItr)->getAlignment());
    // If the value is dependent, we can not test anything.
    if (!CASA)
      return;
    // Test the attribute values.
    llvm::APSInt SecondValue = CASA->getResultAsAPSInt();
    if (!FirstValue)
      FirstValue = CAFA->getResultAsAPSInt();

    if (FirstValue != SecondValue) {
      S.Diag((*LastFoundItr)->getLocation(), diag::err_loop_attr_conflict)
          << *FirstItr;
      S.Diag((*FirstItr)->getLocation(), diag::note_previous_attribute);
    }
    return;
  }
}
} // namespace

#define WANT_STMT_MERGE_LOGIC
#include "neverc/Analyze/AttrParsedAttrImpl.td.h"
#undef WANT_STMT_MERGE_LOGIC

namespace {
Attr *processStmtAttribute(Sema &S, Stmt *St, const ParsedAttr &A,
                           SourceRange Range) {
  if (A.isInvalid() || A.getKind() == ParsedAttr::IgnoredAttribute)
    return nullptr;

  // Unknown attributes are automatically warned on. Target-specific attributes
  // which do not apply to the current target architecture are treated as
  // though they were unknown attributes.
  if (A.getKind() == ParsedAttr::UnknownAttribute ||
      !A.existsInTarget(S.Context.getTargetInfo())) {
    S.Diag(A.getLoc(), A.isRegularKeywordAttribute()
                           ? (unsigned)diag::err_keyword_not_supported_on_target
                       : A.isDeclspecAttribute()
                           ? (unsigned)diag::warn_unhandled_ms_attribute_ignored
                           : (unsigned)diag::warn_unknown_attribute_ignored)
        << A << A.getRange();
    return nullptr;
  }

  if (S.checkCommonAttributeFeatures(St, A))
    return nullptr;

  switch (A.getKind()) {
  case ParsedAttr::AT_AlwaysInline:
    return handleAlwaysInlineAttr(S, St, A, Range);
  case ParsedAttr::AT_FallThrough:
    return handleFallThroughAttr(S, St, A, Range);
  case ParsedAttr::AT_Suppress:
    return handleSuppressAttr(S, St, A, Range);
  case ParsedAttr::AT_NoMerge:
    return handleNoMergeAttr(S, St, A, Range);
  case ParsedAttr::AT_NoInline:
    return handleNoInlineAttr(S, St, A, Range);
  case ParsedAttr::AT_MustTail:
    return handleMustTailAttr(S, St, A, Range);
  case ParsedAttr::AT_Likely:
    return handleLikely(S, St, A, Range);
  case ParsedAttr::AT_Unlikely:
    return handleUnlikely(S, St, A, Range);
  case ParsedAttr::AT_CodeAlign:
    return handleCodeAlignAttr(S, St, A);
  default:
    // N.B., NeverCAttrEmitter.cpp emits a diagnostic helper that ensures a
    // declaration attribute is not written on a statement, but this code is
    // needed for attributes in Attr.td that do not list any subjects.
    S.Diag(A.getRange().getBegin(), diag::err_decl_attribute_invalid_on_stmt)
        << A << A.isRegularKeywordAttribute() << St->getBeginLoc();
    return nullptr;
  }
}
} // namespace

void Sema::ProcessStmtAttributes(
    Stmt *S, const ParsedAttributes &InAttrs,
    llvm::SmallVectorImpl<const Attr *> &OutAttrs) {
  for (const ParsedAttr &AL : InAttrs) {
    if (const Attr *A = processStmtAttribute(*this, S, AL, InAttrs.Range))
      OutAttrs.push_back(A);
  }

  if (OutAttrs.size() >= 2)
    (void)DiagnoseMutualExclusions(*this, OutAttrs);

  checkForDuplicateLoopAttrs<CodeAlignAttr>(*this, OutAttrs);
}

bool Sema::CheckRebuiltStmtAttributes(llvm::ArrayRef<const Attr *> Attrs) {
  checkForDuplicateLoopAttrs<CodeAlignAttr>(*this, Attrs);
  return false;
}
