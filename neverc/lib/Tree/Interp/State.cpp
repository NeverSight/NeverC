#include "State.h"
#include "Frame.h"
#include "neverc/Tree/Core/OptionalDiagnostic.h"
#include "neverc/Tree/Core/TreeContext.h"

using namespace neverc;
using namespace neverc::interp;

State::~State() {}

OptionalDiagnostic State::FFDiag(SourceLocation Loc, diag::kind DiagId,
                                 unsigned ExtraNotes) {
  return diag(Loc, DiagId, ExtraNotes, false);
}

OptionalDiagnostic State::FFDiag(const Expr *E, diag::kind DiagId,
                                 unsigned ExtraNotes) {
  if (getEvalStatus().Diag)
    return diag(E->getExprLoc(), DiagId, ExtraNotes, false);
  setActiveDiagnostic(false);
  return OptionalDiagnostic();
}

OptionalDiagnostic State::CCEDiag(SourceLocation Loc, diag::kind DiagId,
                                  unsigned ExtraNotes) {
  // Don't override a previous diagnostic. Don't bother collecting
  // diagnostics if we're evaluating for overflow.
  if (!getEvalStatus().Diag || !getEvalStatus().Diag->empty()) {
    setActiveDiagnostic(false);
    return OptionalDiagnostic();
  }
  return diag(Loc, DiagId, ExtraNotes, true);
}

OptionalDiagnostic State::CCEDiag(const Expr *E, diag::kind DiagId,
                                  unsigned ExtraNotes) {
  return CCEDiag(E->getExprLoc(), DiagId, ExtraNotes);
}

OptionalDiagnostic State::Note(SourceLocation Loc, diag::kind DiagId) {
  if (!hasActiveDiagnostic())
    return OptionalDiagnostic();
  return OptionalDiagnostic(&addDiag(Loc, DiagId));
}

void State::addNotes(llvm::ArrayRef<PartialDiagnosticAt> Diags) {
  if (hasActiveDiagnostic()) {
    getEvalStatus().Diag->insert(getEvalStatus().Diag->end(), Diags.begin(),
                                 Diags.end());
  }
}

DiagnosticBuilder State::report(SourceLocation Loc, diag::kind DiagId) {
  return getCtx().getDiagnostics().Report(Loc, DiagId);
}

PartialDiagnostic &State::addDiag(SourceLocation Loc, diag::kind DiagId) {
  PartialDiagnostic PD(DiagId, getCtx().getDiagAllocator());
  getEvalStatus().Diag->push_back(std::make_pair(Loc, PD));
  return getEvalStatus().Diag->back().second;
}

OptionalDiagnostic State::diag(SourceLocation Loc, diag::kind DiagId,
                               unsigned ExtraNotes, bool IsCCEDiag) {
  Expr::EvalStatus &EvalStatus = getEvalStatus();
  if (EvalStatus.Diag) {
    if (hasPriorDiagnostic()) {
      return OptionalDiagnostic();
    }

    unsigned CallStackNotes = getCallStackDepth() - 1;
    unsigned Limit = getCtx().getDiagnostics().getConstexprBacktraceLimit();
    if (Limit)
      CallStackNotes = std::min(CallStackNotes, Limit + 1);
    if (checkingPotentialConstantExpression())
      CallStackNotes = 0;

    setActiveDiagnostic(true);
    setFoldFailureDiagnostic(!IsCCEDiag);
    EvalStatus.Diag->clear();
    EvalStatus.Diag->reserve(1 + ExtraNotes + CallStackNotes);
    addDiag(Loc, DiagId);
    if (!checkingPotentialConstantExpression()) {
      addCallStack(Limit);
    }
    return OptionalDiagnostic(&(*EvalStatus.Diag)[0].second);
  }
  setActiveDiagnostic(false);
  return OptionalDiagnostic();
}

const LangOptions &State::getLangOpts() const { return getCtx().getLangOpts(); }

void State::addCallStack(unsigned Limit) {
  // Determine which calls to skip, if any.
  unsigned ActiveCalls = getCallStackDepth() - 1;
  unsigned SkipStart = ActiveCalls, SkipEnd = SkipStart;
  if (Limit && Limit < ActiveCalls) {
    SkipStart = Limit / 2 + Limit % 2;
    SkipEnd = ActiveCalls - Limit / 2;
  }

  // Walk the call stack and add the diagnostics.
  unsigned CallIdx = 0;
  const Frame *Top = getCurrentFrame();
  const Frame *Bottom = getBottomFrame();
  for (const Frame *F = Top; F != Bottom; F = F->getCaller(), ++CallIdx) {
    SourceRange CallRange = F->getCallRange();

    // Skip this call?
    if (CallIdx >= SkipStart && CallIdx < SkipEnd) {
      if (CallIdx == SkipStart) {
        // Note that we're skipping calls.
        addDiag(CallRange.getBegin(), diag::note_constexpr_calls_suppressed)
            << unsigned(ActiveCalls - Limit);
      }
      continue;
    }

    llvm::SmallString<128> Buffer;
    llvm::raw_svector_ostream Out(Buffer);
    F->describe(Out);
    addDiag(CallRange.getBegin(), diag::note_constexpr_call_here)
        << Out.str() << CallRange;
  }
}

Frame::~Frame() = default;
