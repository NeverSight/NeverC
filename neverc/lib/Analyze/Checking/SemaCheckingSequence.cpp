//===- SemaCheckingSequence.cpp - Overflow / unsequenced / completed ------===//
//
// Split from SemaCheckingStmt.cpp: CheckForIntOverflow, SequenceChecker /
// CheckUnsequencedOperations, and CheckCompletedExpr (+ local helpers).
//
#include "Checking/SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Core/SyncScope.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Tree/Core/APValue.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Format/FormatString.h"
#include "neverc/Tree/Type/StructLayout.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/SaveAndRestore.h"
#include <cassert>
#include <optional>

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// Integer overflow
// ===----------------------------------------------------------------------===

static bool hasNonLiteralLeaf(const Expr *E, unsigned Depth = 5) {
  for (;;) {
    if (Depth == 0)
      return true;
    E = E->IgnoreParenImpCasts();
    if (isa<IntegerLiteral, FloatingLiteral, CharacterLiteral>(E))
      return false;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
      return !isa<EnumConstantDecl>(DRE->getDecl());
    if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
      if (hasNonLiteralLeaf(BO->getRHS(), Depth - 1))
        return true;
      E = BO->getLHS();
      --Depth;
      continue;
    }
    if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
      E = UO->getSubExpr();
      --Depth;
      continue;
    }
    return true;
  }
}

void Sema::CheckForIntOverflow(const Expr *E) {
  // Use a work list to deal with nested struct initializers.
  llvm::SmallVector<const Expr *, 2> Exprs(1, E);

  do {
    const Expr *OriginalE = Exprs.pop_back_val();
    const Expr *E = OriginalE->IgnoreParenCasts();

    if (isa<BinaryOperator, UnaryOperator>(E)) {
      if (!hasNonLiteralLeaf(E))
        E->EvaluateForOverflow(Context);
      continue;
    }

    if (const auto *InitList = dyn_cast<InitListExpr>(OriginalE))
      Exprs.append(InitList->inits().begin(), InitList->inits().end());
    else if (const auto *Call = dyn_cast<CallExpr>(E))
      Exprs.append(Call->arg_begin(), Call->arg_end());
    else if (const auto *Array = dyn_cast<ArraySubscriptExpr>(E))
      Exprs.push_back(Array->getIdx());
    else if (const auto *Compound = dyn_cast<CompoundLiteralExpr>(E))
      Exprs.push_back(Compound->getInitializer());
  } while (!Exprs.empty());
}

namespace {

class SequenceChecker : public ConstEvaluatedExprVisitor<SequenceChecker> {
  using Base = ConstEvaluatedExprVisitor<SequenceChecker>;

  class SequenceTree {
    struct Value {
      explicit Value(unsigned Parent) : Parent(Parent), Merged(false) {}
      unsigned Parent : 31;
      unsigned Merged : 1;
    };
    llvm::SmallVector<Value, 8> Values;

  public:
    /// A region within an expression which may be sequenced with respect
    /// to some other region.
    class Seq {
      friend class SequenceTree;

      unsigned Index;

      explicit Seq(unsigned N) : Index(N) {}

    public:
      Seq() : Index(0) {}
    };

    SequenceTree() { Values.push_back(Value(0)); }
    Seq root() const { return Seq(0); }

    /// Create a new sequence of operations, which is an unsequenced
    /// subset of \p Parent. This sequence of operations is sequenced with
    /// respect to other children of \p Parent.
    Seq allocate(Seq Parent) {
      Values.push_back(Value(Parent.Index));
      return Seq(Values.size() - 1);
    }

    /// Merge a sequence of operations into its parent.
    void merge(Seq S) { Values[S.Index].Merged = true; }

    /// Determine whether two operations are unsequenced. This operation
    /// is asymmetric: \p Cur should be the more recent sequence, and \p Old
    /// should have been merged into its parent as appropriate.
    bool isUnsequenced(Seq Cur, Seq Old) {
      unsigned C = representative(Cur.Index);
      unsigned Target = representative(Old.Index);
      while (C >= Target) {
        if (C == Target)
          return true;
        C = Values[C].Parent;
      }
      return false;
    }

  private:
    /// Pick a representative for a sequence.
    unsigned representative(unsigned K) {
      if (Values[K].Merged)
        // Perform path compression as we go.
        return Values[K].Parent = representative(Values[K].Parent);
      return K;
    }
  };

  using Object = const NamedDecl *;

  enum UsageKind {
    /// A read of an object. Multiple unsequenced reads are OK.
    UK_Use,

    /// A modification sequenced before the expression's value (e.g. `++n`).
    UK_ModAsValue,

    /// A modification of an object which is not sequenced before the value
    /// computation of the expression, such as n++.
    UK_ModAsSideEffect,

    UK_Count = UK_ModAsSideEffect + 1
  };

  struct Usage {
    const Expr *UsageExpr = nullptr;
    SequenceTree::Seq Seq;

    Usage() = default;
  };

  struct UsageInfo {
    Usage Uses[UK_Count];

    /// Have we issued a diagnostic for this object already?
    bool Diagnosed = false;

    UsageInfo() = default;
  };
  using UsageInfoMap = llvm::SmallDenseMap<Object, UsageInfo, 16>;

  Sema &SemaRef;

  SequenceTree Tree;

  UsageInfoMap UsageMap;

  SequenceTree::Seq Region;

  llvm::SmallVectorImpl<std::pair<Object, Usage>> *ModAsSideEffect = nullptr;

  struct SequencedSubexpression {
    SequencedSubexpression(SequenceChecker &Self)
        : Self(Self), OldModAsSideEffect(Self.ModAsSideEffect) {
      Self.ModAsSideEffect = &ModAsSideEffect;
    }

    ~SequencedSubexpression() {
      for (const std::pair<Object, Usage> &M : llvm::reverse(ModAsSideEffect)) {
        // Add a new usage with usage kind UK_ModAsValue, and then restore
        // the previous usage with UK_ModAsSideEffect (thus clearing it if
        // the previous one was empty).
        UsageInfo &UI = Self.UsageMap[M.first];
        auto &SideEffectUsage = UI.Uses[UK_ModAsSideEffect];
        Self.addUsage(M.first, UI, SideEffectUsage.UsageExpr, UK_ModAsValue);
        SideEffectUsage = M.second;
      }
      Self.ModAsSideEffect = OldModAsSideEffect;
    }

    SequenceChecker &Self;
    llvm::SmallVector<std::pair<Object, Usage>, 4> ModAsSideEffect;
    llvm::SmallVectorImpl<std::pair<Object, Usage>> *OldModAsSideEffect;
  };

  class EvaluationTracker {
  public:
    EvaluationTracker(SequenceChecker &Self)
        : Self(Self), Prev(Self.EvalTracker) {
      Self.EvalTracker = this;
    }

    ~EvaluationTracker() {
      Self.EvalTracker = Prev;
      if (Prev)
        Prev->EvalOK &= EvalOK;
    }

    bool evaluate(const Expr *E, bool &Result) {
      if (!EvalOK)
        return false;
      EvalOK = E->EvaluateAsBooleanCondition(
          Result, Self.SemaRef.Context,
          Self.SemaRef.isConstantEvaluatedContext());
      return EvalOK;
    }

  private:
    SequenceChecker &Self;
    EvaluationTracker *Prev;
    bool EvalOK = true;
  } *EvalTracker = nullptr;

  Object getObject(const Expr *E, bool Mod) const {
    E = E->IgnoreParenCasts();
    if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
      if (Mod && (UO->getOpcode() == UO_PreInc || UO->getOpcode() == UO_PreDec))
        return getObject(UO->getSubExpr(), Mod);
    } else if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_Comma)
        return getObject(BO->getRHS(), Mod);
      if (Mod && BO->isAssignmentOp())
        return getObject(BO->getLHS(), Mod);
    } else if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
      return DRE->getDecl();
    return nullptr;
  }

  void addUsage(Object O, UsageInfo &UI, const Expr *UsageExpr, UsageKind UK) {
    Usage &U = UI.Uses[UK];
    if (!U.UsageExpr || !Tree.isUnsequenced(Region, U.Seq)) {
      // If we have a modification as side effect and are in a sequenced
      // subexpression, save the old Usage so that we can restore it later
      // in SequencedSubexpression::~SequencedSubexpression.
      if (UK == UK_ModAsSideEffect && ModAsSideEffect)
        ModAsSideEffect->push_back(std::make_pair(O, U));
      // Then record the new usage with the current sequencing region.
      U.UsageExpr = UsageExpr;
      U.Seq = Region;
    }
  }

  void checkUsage(Object O, UsageInfo &UI, const Expr *UsageExpr,
                  UsageKind OtherKind, bool IsModMod) {
    if (UI.Diagnosed)
      return;

    const Usage &U = UI.Uses[OtherKind];
    if (!U.UsageExpr || !Tree.isUnsequenced(Region, U.Seq))
      return;

    const Expr *Mod = U.UsageExpr;
    const Expr *ModOrUse = UsageExpr;
    if (OtherKind == UK_Use)
      std::swap(Mod, ModOrUse);

    SemaRef.DiagRuntimeBehavior(
        Mod->getExprLoc(), {Mod, ModOrUse},
        SemaRef.PDiag(IsModMod ? diag::warn_unsequenced_mod_mod
                               : diag::warn_unsequenced_mod_use)
            << O << SourceRange(ModOrUse->getExprLoc()));
    UI.Diagnosed = true;
  }

  // notePre/Post{Use,Mod}: record uses/modifications around child visitation.
  // `SequencedSubexpression` wraps sequenced subexpressions (`||`, `&&`, `,`,
  // ...): it tracks side-effect modifications and restores the previous usage
  // when leaving the LHS region.

  void notePreUse(Object O, const Expr *UseExpr) {
    UsageInfo &UI = UsageMap[O];
    // Uses conflict with other modifications.
    checkUsage(O, UI, UseExpr, /*OtherKind=*/UK_ModAsValue, /*IsModMod=*/false);
  }

  void notePostUse(Object O, const Expr *UseExpr) {
    UsageInfo &UI = UsageMap[O];
    checkUsage(O, UI, UseExpr, /*OtherKind=*/UK_ModAsSideEffect,
               /*IsModMod=*/false);
    addUsage(O, UI, UseExpr, /*UsageKind=*/UK_Use);
  }

  void notePreMod(Object O, const Expr *ModExpr) {
    UsageInfo &UI = UsageMap[O];
    // Modifications conflict with other modifications and with uses.
    checkUsage(O, UI, ModExpr, /*OtherKind=*/UK_ModAsValue, /*IsModMod=*/true);
    checkUsage(O, UI, ModExpr, /*OtherKind=*/UK_Use, /*IsModMod=*/false);
  }

  void notePostMod(Object O, const Expr *ModExpr, UsageKind UK) {
    UsageInfo &UI = UsageMap[O];
    checkUsage(O, UI, ModExpr, /*OtherKind=*/UK_ModAsSideEffect,
               /*IsModMod=*/true);
    addUsage(O, UI, ModExpr, /*UsageKind=*/UK);
  }

public:
  SequenceChecker(Sema &S, const Expr *E)
      : Base(S.Context), SemaRef(S), Region(Tree.root()) {
    Visit(E);
  }

  void VisitStmt(const Stmt *S) {
    // Skip all statements which aren't expressions for now.
  }

  void VisitExpr(const Expr *E) {
    // By default, just recurse to evaluated subexpressions.
    Base::VisitStmt(E);
  }

  void VisitCastExpr(const CastExpr *E) {
    Object O = Object();
    if (E->getCastKind() == CK_LValueToRValue)
      O = getObject(E->getSubExpr(), false);

    if (O)
      notePreUse(O, E);
    VisitExpr(E);
    if (O)
      notePostUse(O, E);
  }

  void VisitSequencedExpressions(const Expr *SequencedBefore,
                                 const Expr *SequencedAfter) {
    SequenceTree::Seq BeforeRegion = Tree.allocate(Region);
    SequenceTree::Seq AfterRegion = Tree.allocate(Region);
    SequenceTree::Seq OldRegion = Region;

    {
      SequencedSubexpression SeqBefore(*this);
      Region = BeforeRegion;
      Visit(SequencedBefore);
    }

    Region = AfterRegion;
    Visit(SequencedAfter);

    Region = OldRegion;

    Tree.merge(BeforeRegion);
    Tree.merge(AfterRegion);
  }

  void VisitArraySubscriptExpr(const ArraySubscriptExpr *ASE) {
    // Subscript: evaluate base (and index) left-to-right.
    {
      Visit(ASE->getLHS());
      Visit(ASE->getRHS());
    }
  }

  void VisitBinShl(const BinaryOperator *BO) { VisitBinShlShr(BO); }
  void VisitBinShr(const BinaryOperator *BO) { VisitBinShlShr(BO); }
  void VisitBinShlShr(const BinaryOperator *BO) {
    Visit(BO->getLHS());
    Visit(BO->getRHS());
  }

  void VisitBinComma(const BinaryOperator *BO) {
    // Comma: left fully sequenced before right.
    VisitSequencedExpressions(BO->getLHS(), BO->getRHS());
  }

  void VisitBinAssign(const BinaryOperator *BO) {
    SequenceTree::Seq RHSRegion;
    SequenceTree::Seq LHSRegion;
    RHSRegion = Region;
    LHSRegion = Region;
    SequenceTree::Seq OldRegion = Region;

    // Assignment: record modified object, visit LHS then RHS (no LR sequencing
    // between operands in the C model this analysis uses).
    Object O = getObject(BO->getLHS(), /*Mod=*/true);
    if (O)
      notePreMod(O, BO);

    Region = LHSRegion;
    Visit(BO->getLHS());

    if (O && isa<CompoundAssignOperator>(BO))
      notePostUse(O, BO);

    Region = RHSRegion;
    Visit(BO->getRHS());

    Region = OldRegion;
    if (O)
      notePostMod(O, BO, UK_ModAsSideEffect);
  }

  void VisitCompoundAssignOperator(const CompoundAssignOperator *CAO) {
    VisitBinAssign(CAO);
  }

  void VisitUnaryPreInc(const UnaryOperator *UO) { VisitUnaryPreIncDec(UO); }
  void VisitUnaryPreDec(const UnaryOperator *UO) { VisitUnaryPreIncDec(UO); }
  void VisitUnaryPreIncDec(const UnaryOperator *UO) {
    Object O = getObject(UO->getSubExpr(), true);
    if (!O)
      return VisitExpr(UO);

    notePreMod(O, UO);
    Visit(UO->getSubExpr());
    notePostMod(O, UO, UK_ModAsSideEffect);
  }

  void VisitUnaryPostInc(const UnaryOperator *UO) { VisitUnaryPostIncDec(UO); }
  void VisitUnaryPostDec(const UnaryOperator *UO) { VisitUnaryPostIncDec(UO); }
  void VisitUnaryPostIncDec(const UnaryOperator *UO) {
    Object O = getObject(UO->getSubExpr(), true);
    if (!O)
      return VisitExpr(UO);

    notePreMod(O, UO);
    Visit(UO->getSubExpr());
    notePostMod(O, UO, UK_ModAsSideEffect);
  }

  void VisitBinLOr(const BinaryOperator *BO) {
    // `||`: LHS sequenced before RHS when RHS is evaluated.
    SequenceTree::Seq LHSRegion = Tree.allocate(Region);
    SequenceTree::Seq RHSRegion = Tree.allocate(Region);
    SequenceTree::Seq OldRegion = Region;

    EvaluationTracker Eval(*this);
    {
      SequencedSubexpression Sequenced(*this);
      Region = LHSRegion;
      Visit(BO->getLHS());
    }

    bool EvalResult = false;
    bool EvalOK = Eval.evaluate(BO->getLHS(), EvalResult);
    bool ShouldVisitRHS = !EvalOK || (EvalOK && !EvalResult);
    if (ShouldVisitRHS) {
      Region = RHSRegion;
      Visit(BO->getRHS());
    }

    Region = OldRegion;
    Tree.merge(LHSRegion);
    Tree.merge(RHSRegion);
  }

  void VisitBinLAnd(const BinaryOperator *BO) {
    // `&&`: LHS sequenced before RHS when RHS is evaluated.
    SequenceTree::Seq LHSRegion = Tree.allocate(Region);
    SequenceTree::Seq RHSRegion = Tree.allocate(Region);
    SequenceTree::Seq OldRegion = Region;

    EvaluationTracker Eval(*this);
    {
      SequencedSubexpression Sequenced(*this);
      Region = LHSRegion;
      Visit(BO->getLHS());
    }

    bool EvalResult = false;
    bool EvalOK = Eval.evaluate(BO->getLHS(), EvalResult);
    bool ShouldVisitRHS = !EvalOK || (EvalOK && EvalResult);
    if (ShouldVisitRHS) {
      Region = RHSRegion;
      Visit(BO->getRHS());
    }

    Region = OldRegion;
    Tree.merge(LHSRegion);
    Tree.merge(RHSRegion);
  }

  void VisitAbstractConditionalOperator(const AbstractConditionalOperator *CO) {
    // `?:`: condition sequenced before the chosen arm; when the condition is
    // unknown, both arms may be visited for analysis (see VisitBinLOr).
    SequenceTree::Seq ConditionRegion = Tree.allocate(Region);

    SequenceTree::Seq TrueRegion = Tree.allocate(Region);
    SequenceTree::Seq FalseRegion = Tree.allocate(Region);
    SequenceTree::Seq OldRegion = Region;

    EvaluationTracker Eval(*this);
    {
      SequencedSubexpression Sequenced(*this);
      Region = ConditionRegion;
      Visit(CO->getCond());
    }

    bool EvalResult = false;
    bool EvalOK = Eval.evaluate(CO->getCond(), EvalResult);
    bool ShouldVisitTrueExpr = !EvalOK || (EvalOK && EvalResult);
    bool ShouldVisitFalseExpr = !EvalOK || (EvalOK && !EvalResult);
    if (ShouldVisitTrueExpr) {
      Region = TrueRegion;
      Visit(CO->getTrueExpr());
    }
    if (ShouldVisitFalseExpr) {
      Region = FalseRegion;
      Visit(CO->getFalseExpr());
    }

    Region = OldRegion;
    Tree.merge(ConditionRegion);
    Tree.merge(TrueRegion);
    Tree.merge(FalseRegion);
  }

  void VisitCallExpr(const CallExpr *CE) {
    if (CE->isUnevaluatedBuiltinCall(Context))
      return;

    // Call: callee and arguments evaluated before entering the callee.
    SequencedSubexpression Sequenced(*this);
    SemaRef.runWithSufficientStackSpace(CE->getExprLoc(), [&] {
      SequenceTree::Seq CalleeRegion;
      SequenceTree::Seq OtherRegion;
      CalleeRegion = Region;
      OtherRegion = Region;
      SequenceTree::Seq OldRegion = Region;

      // Visit the callee expression first.
      Region = CalleeRegion;
      Visit(CE->getCallee());

      // Then visit the argument expressions.
      Region = OtherRegion;
      for (const Expr *Argument : CE->arguments())
        Visit(Argument);

      Region = OldRegion;
    });
  }

  void VisitInitListExpr(const InitListExpr *ILE) { return VisitExpr(ILE); }
};

} // namespace

// ===----------------------------------------------------------------------===
// Unsequenced operations & expression completion
// ===----------------------------------------------------------------------===

static bool hasSideEffectOp(const Expr *E, unsigned Depth) {
  for (;;) {
    if (LLVM_UNLIKELY(Depth == 0))
      return true;
    E = E->IgnoreParenImpCasts();
    switch (E->getStmtClass()) {
    case Stmt::BinaryOperatorClass:
    case Stmt::CompoundAssignOperatorClass: {
      const auto *BO = cast<BinaryOperator>(E);
      if (BO->isAssignmentOp())
        return true;
      if (hasSideEffectOp(BO->getRHS(), Depth - 1))
        return true;
      E = BO->getLHS();
      --Depth;
      continue;
    }
    case Stmt::UnaryOperatorClass: {
      const auto *UO = cast<UnaryOperator>(E);
      auto Op = UO->getOpcode();
      if (Op == UO_PreInc || Op == UO_PreDec || Op == UO_PostInc ||
          Op == UO_PostDec)
        return true;
      E = UO->getSubExpr();
      --Depth;
      continue;
    }
    case Stmt::ConditionalOperatorClass:
    case Stmt::BinaryConditionalOperatorClass: {
      const auto *CO = cast<AbstractConditionalOperator>(E);
      if (hasSideEffectOp(CO->getCond(), Depth - 1) ||
          hasSideEffectOp(CO->getTrueExpr(), Depth - 1))
        return true;
      E = CO->getFalseExpr();
      --Depth;
      continue;
    }
    case Stmt::CallExprClass:
    case Stmt::StmtExprClass:
      return true;
    case Stmt::ArraySubscriptExprClass: {
      const auto *ASE = cast<ArraySubscriptExpr>(E);
      if (hasSideEffectOp(ASE->getIdx(), Depth - 1))
        return true;
      E = ASE->getBase();
      --Depth;
      continue;
    }
    case Stmt::MemberExprClass:
      E = cast<MemberExpr>(E)->getBase();
      --Depth;
      continue;
    case Stmt::CompoundLiteralExprClass:
      E = cast<CompoundLiteralExpr>(E)->getInitializer();
      --Depth;
      continue;
    case Stmt::ImplicitCastExprClass:
    case Stmt::CStyleCastExprClass:
      E = cast<CastExpr>(E)->getSubExpr();
      continue;
    case Stmt::IntegerLiteralClass:
    case Stmt::FloatingLiteralClass:
    case Stmt::CharacterLiteralClass:
    case Stmt::StringLiteralClass:
    case Stmt::DeclRefExprClass:
    case Stmt::UnaryExprOrTypeTraitExprClass:
      return false;
    default:
      return false;
    }
  }
}

void Sema::CheckUnsequencedOperations(const Expr *E) {
  if (!hasSideEffectOp(E, 14))
    return;
  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->isAssignmentOp() && !hasSideEffectOp(BO->getRHS(), 12)) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      if (isa<DeclRefExpr, MemberExpr>(LHS))
        return;
      if (isa<ArraySubscriptExpr>(LHS) && !hasSideEffectOp(LHS, 4))
        return;
    }
  }
  SequenceChecker(*this, E);
}

static bool isPureArithExpr(const Expr *E, unsigned Depth,
                            bool &HasNonLiteral) {
  for (;;) {
    if (LLVM_UNLIKELY(Depth == 0))
      return false;
    switch (E->getStmtClass()) {
    case Stmt::ParenExprClass:
      E = cast<ParenExpr>(E)->getSubExpr();
      continue;
    case Stmt::ImplicitCastExprClass: {
      auto CK = cast<CastExpr>(E)->getCastKind();
      if (CK == CK_LValueToRValue || CK == CK_IntegralCast ||
          CK == CK_FloatingCast || CK == CK_NoOp) {
        E = cast<CastExpr>(E)->getSubExpr();
        continue;
      }
      return false;
    }
    case Stmt::CStyleCastExprClass: {
      auto CK = cast<CastExpr>(E)->getCastKind();
      if (CK == CK_LValueToRValue || CK == CK_IntegralCast ||
          CK == CK_FloatingCast || CK == CK_IntegralToFloating ||
          CK == CK_FloatingToIntegral || CK == CK_NoOp) {
        E = cast<CastExpr>(E)->getSubExpr();
        continue;
      }
      return false;
    }
    case Stmt::IntegerLiteralClass:
    case Stmt::CharacterLiteralClass:
    case Stmt::FloatingLiteralClass:
    case Stmt::UnaryExprOrTypeTraitExprClass:
      return E->getType()->isArithmeticType();
    case Stmt::DeclRefExprClass:
      if (!E->getType()->isArithmeticType())
        return false;
      HasNonLiteral = true;
      return true;
    case Stmt::MemberExprClass:
      if (!E->getType()->isArithmeticType())
        return false;
      HasNonLiteral = true;
      return true;
    case Stmt::ArraySubscriptExprClass:
      if (!E->getType()->isArithmeticType())
        return false;
      HasNonLiteral = true;
      return true;
    case Stmt::BinaryOperatorClass:
    case Stmt::CompoundAssignOperatorClass: {
      if (!E->getType()->isArithmeticType())
        return false;
      const auto *BO = cast<BinaryOperator>(E);
      if (!isPureArithExpr(BO->getRHS(), Depth - 1, HasNonLiteral))
        return false;
      E = BO->getLHS();
      --Depth;
      continue;
    }
    case Stmt::ConditionalOperatorClass: {
      if (!E->getType()->isArithmeticType())
        return false;
      const auto *CO = cast<ConditionalOperator>(E);
      if (!isPureArithExpr(CO->getCond(), Depth - 1, HasNonLiteral))
        return false;
      if (!isPureArithExpr(CO->getTrueExpr(), Depth - 1, HasNonLiteral))
        return false;
      E = CO->getFalseExpr();
      --Depth;
      continue;
    }
    case Stmt::UnaryOperatorClass: {
      if (!E->getType()->isArithmeticType())
        return false;
      const auto *UO = cast<UnaryOperator>(E);
      auto Op = UO->getOpcode();
      if (Op == UO_PreInc || Op == UO_PreDec || Op == UO_PostInc ||
          Op == UO_PostDec)
        return false;
      E = UO->getSubExpr();
      --Depth;
      continue;
    }
    default:
      return false;
    }
  }
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
static bool isSameTypeArithComparison(const Expr *E) {
  const auto *BO = dyn_cast<BinaryOperator>(E);
  if (!BO || !BO->isComparisonOp())
    return false;
  const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
  QualType LT = LHS->getType();
  QualType RT = RHS->getType();
  if (!LT->isArithmeticType() || LT != RT)
    return false;
  bool Dummy = false;
  return isPureArithExpr(BO->getLHS(), 12, Dummy) &&
         isPureArithExpr(BO->getRHS(), 12, Dummy);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
static bool isSimpleIncDecExpr(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  const auto *UO = dyn_cast<UnaryOperator>(E);
  if (!UO)
    return false;
  auto Op = UO->getOpcode();
  if (Op != UO_PreInc && Op != UO_PreDec && Op != UO_PostInc &&
      Op != UO_PostDec)
    return false;
  const Expr *Sub = UO->getSubExpr()->IgnoreParenImpCasts();
  return isa<DeclRefExpr, MemberExpr, ArraySubscriptExpr>(Sub);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
static bool isSimpleLValue(const Expr *E) {
  if (isa<DeclRefExpr, MemberExpr, ArraySubscriptExpr>(E))
    return true;
  if (const auto *UO = dyn_cast<UnaryOperator>(E))
    return UO->getOpcode() == UO_Deref;
  return false;
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
static bool isSimpleCompoundAssign(const Expr *E) {
  const auto *BO = dyn_cast<CompoundAssignOperator>(E);
  if (!BO)
    return false;
  if (!isSimpleLValue(BO->getLHS()->IgnoreParenImpCasts()))
    return false;
  bool HasNonLiteral = false;
  return isPureArithExpr(BO->getRHS(), 12, HasNonLiteral);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
static bool isSimplePureAssignment(const Expr *E) {
  const auto *BO = dyn_cast<BinaryOperator>(E);
  if (!BO || BO->getOpcode() != BO_Assign)
    return false;
  if (!isSimpleLValue(BO->getLHS()->IgnoreParenImpCasts()))
    return false;
  bool Dummy = false;
  return isPureArithExpr(BO->getRHS(), 12, Dummy);
}

LLVM_ATTRIBUTE_ALWAYS_INLINE
static bool isSimpleCallExpr(const Expr *E) {
  const auto *CE = dyn_cast<CallExpr>(E);
  if (!CE)
    return false;
  for (const Expr *Arg : CE->arguments()) {
    if (!hasSideEffectOp(Arg, 6))
      continue;
    return false;
  }
  return true;
}

NEVERC_HOT void
Sema::CheckCompletedExpr(Expr *E, SourceLocation CheckLoc, bool IsConstexpr) {
  if (LLVM_UNLIKELY(Diags.getIgnoreAllWarnings())) {
    if (LLVM_UNLIKELY(!MisalignedMembers.empty()))
      DiagnoseMisalignedMembers();
    return;
  }
  if (LLVM_LIKELY(!IsConstexpr)) {
    auto SC = E->getStmtClass();
    if (LLVM_UNLIKELY(SC == Stmt::IntegerLiteralClass ||
                      SC == Stmt::FloatingLiteralClass ||
                      SC == Stmt::CharacterLiteralClass ||
                      SC == Stmt::DeclRefExprClass ||
                      SC == Stmt::StringLiteralClass)) {
      if (LLVM_UNLIKELY(!MisalignedMembers.empty()))
        DiagnoseMisalignedMembers();
      return;
    }
    bool HasNonLiteral = false;
    if (isPureArithExpr(E, 14, HasNonLiteral) && HasNonLiteral) {
      if (LLVM_UNLIKELY(!MisalignedMembers.empty()))
        DiagnoseMisalignedMembers();
      return;
    }
    if (isSimpleIncDecExpr(E) || isSimpleCompoundAssign(E) ||
        isSameTypeArithComparison(E) || isSimplePureAssignment(E)) {
      if (LLVM_UNLIKELY(!MisalignedMembers.empty()))
        DiagnoseMisalignedMembers();
      return;
    }
    if (isSimpleCallExpr(E)) {
      CheckImplicitConversions(E, CheckLoc);
      if (LLVM_UNLIKELY(!MisalignedMembers.empty()))
        DiagnoseMisalignedMembers();
      return;
    }
  }
  llvm::SaveAndRestore ConstantContext(isConstantEvaluatedOverride,
                                       IsConstexpr || isa<ConstantExpr>(E));
  CheckImplicitConversions(E, CheckLoc);
  CheckUnsequencedOperations(E);
  if (!IsConstexpr)
    CheckForIntOverflow(E);
  if (LLVM_UNLIKELY(!MisalignedMembers.empty()))
    DiagnoseMisalignedMembers();
}

