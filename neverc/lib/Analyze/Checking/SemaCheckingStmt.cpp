#include "SemaCheckingUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
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
#include "neverc/Analyze/SemaInternal.h"

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

LLVM_ATTRIBUTE_ALWAYS_INLINE
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

LLVM_ATTRIBUTE_ALWAYS_INLINE
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

__attribute__((hot)) void
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

// ===----------------------------------------------------------------------===
// Bit-field & parameter validation
// ===----------------------------------------------------------------------===

void Sema::CheckBitFieldInitialization(SourceLocation InitLoc,
                                       FieldDecl *BitField, Expr *Init) {
  (void)AnalyzeBitFieldAssignment(*this, BitField, Init, InitLoc);
}

namespace {
void diagnoseArrayStarInParamType(Sema &S, QualType PType, SourceLocation Loc) {
  if (!PType->isVariablyModifiedType())
    return;
  if (const auto *PointerTy = dyn_cast<PointerType>(PType)) {
    diagnoseArrayStarInParamType(S, PointerTy->getPointeeType(), Loc);
    return;
  }
  if (const auto *ParenTy = dyn_cast<ParenType>(PType)) {
    diagnoseArrayStarInParamType(S, ParenTy->getInnerType(), Loc);
    return;
  }

  const ArrayType *AT = S.Context.getAsArrayType(PType);
  if (!AT)
    return;

  if (AT->getSizeModifier() != ArraySizeModifier::Star) {
    diagnoseArrayStarInParamType(S, AT->getElementType(), Loc);
    return;
  }

  S.Diag(Loc, diag::err_array_star_in_function_definition);
}
} // namespace

bool Sema::CheckParmsForFunctionDef(llvm::ArrayRef<ParmVarDecl *> Parameters,
                                    bool CheckParameterNames) {
  bool HasInvalidParm = false;
  for (ParmVarDecl *Param : Parameters) {
    assert(Param && "null in a parameter list");
    // C99 6.7.5.3p4: parameters in a function *definition* shall have complete
    // type.
    if (!Param->isInvalidDecl() &&
        RequireCompleteType(Param->getLocation(), Param->getType(),
                            diag::err_typecheck_decl_incomplete_type)) {
      Param->setInvalidDecl();
      HasInvalidParm = true;
    }

    // C99 6.9.1p5: If the declarator includes a parameter type list, the
    // declaration of each parameter shall include an identifier.
    if (CheckParameterNames && Param->getIdentifier() == nullptr &&
        !Param->isImplicit()) {
      // Diagnose this as an extension in C17 and earlier.
      if (!getLangOpts().C23)
        Diag(Param->getLocation(), diag::ext_parameter_name_omitted_c23);
    }

    // C99 6.7.5.3p12:
    //   If the function declarator is not part of a definition of that
    //   function, parameters may have incomplete type and may use the [*]
    //   notation in their sequences of declarator specifiers to specify
    //   variable length array types.
    QualType PType = Param->getOriginalType();
    diagnoseArrayStarInParamType(*this, PType, Param->getLocation());

    // Parameters with the pass_object_size attribute only need to be marked
    // constant at function definitions. Because we lack information about
    // whether we're on a declaration or definition when we're instantiating the
    // attribute, we need to check for constness here.
    if (const auto *Attr = Param->getAttr<PassObjectSizeAttr>())
      if (!Param->getType().isConstQualified())
        Diag(Param->getLocation(), diag::err_attribute_pointers_only)
            << Attr->getSpelling() << 1;
  }

  return HasInvalidParm;
}

// ===----------------------------------------------------------------------===
// Cast alignment
// ===----------------------------------------------------------------------===

namespace {

std::optional<std::pair<CharUnits, CharUnits>>
getBaseAlignmentAndOffsetFromPtr(const Expr *E, TreeContext &Ctx);
std::optional<std::pair<CharUnits, CharUnits>>
getAlignmentAndOffsetFromBinAddOrSub(const Expr *PtrE, const Expr *IntE,
                                     bool IsSub, TreeContext &Ctx) {
  QualType PointeeType = PtrE->getType()->getPointeeType();

  if (!PointeeType->isConstantSizeType())
    return std::nullopt;

  auto P = getBaseAlignmentAndOffsetFromPtr(PtrE, Ctx);

  if (!P)
    return std::nullopt;

  CharUnits EltSize = Ctx.getTypeSizeInChars(PointeeType);
  if (std::optional<llvm::APSInt> IdxRes = IntE->getIntegerConstantExpr(Ctx)) {
    CharUnits Offset = EltSize * IdxRes->getExtValue();
    if (IsSub)
      Offset = -Offset;
    return std::make_pair(P->first, P->second + Offset);
  }

  // If the integer expression isn't a constant expression, compute the lower
  // bound of the alignment using the alignment and offset of the pointer
  // expression and the element size.
  return std::make_pair(
      P->first.alignmentAtOffset(P->second).alignmentAtOffset(EltSize),
      CharUnits::Zero());
}

std::optional<std::pair<
    CharUnits,
    CharUnits>> static getBaseAlignmentAndOffsetFromLValue(const Expr *E,
                                                           TreeContext &Ctx) {
  E = E->IgnoreParens();
  switch (E->getStmtClass()) {
  default:
    break;
  case Stmt::CStyleCastExprClass:
  case Stmt::ImplicitCastExprClass: {
    auto *CE = cast<CastExpr>(E);
    const Expr *From = CE->getSubExpr();
    switch (CE->getCastKind()) {
    default:
      break;
    case CK_NoOp:
      return getBaseAlignmentAndOffsetFromLValue(From, Ctx);
    }
    break;
  }
  case Stmt::ArraySubscriptExprClass: {
    auto *ASE = cast<ArraySubscriptExpr>(E);
    return getAlignmentAndOffsetFromBinAddOrSub(ASE->getBase(), ASE->getIdx(),
                                                false, Ctx);
  }
  case Stmt::DeclRefExprClass: {
    if (auto *VD = dyn_cast<VarDecl>(cast<DeclRefExpr>(E)->getDecl())) {
      // Dependent alignment cannot be resolved -> bail out.
      if (VD->hasDependentAlignment())
        break;
      return std::make_pair(Ctx.getDeclAlign(VD), CharUnits::Zero());
    }
    break;
  }
  case Stmt::MemberExprClass: {
    auto *ME = cast<MemberExpr>(E);
    auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
    if (!FD || FD->getParent()->isInvalidDecl())
      break;
    std::optional<std::pair<CharUnits, CharUnits>> P;
    if (ME->isArrow())
      P = getBaseAlignmentAndOffsetFromPtr(ME->getBase(), Ctx);
    else
      P = getBaseAlignmentAndOffsetFromLValue(ME->getBase(), Ctx);
    if (!P)
      break;
    const StructRecordLayout &Layout =
        Ctx.getStructRecordLayout(FD->getParent());
    uint64_t Offset = Layout.getFieldOffset(FD->getFieldIndex());
    return std::make_pair(P->first,
                          P->second + CharUnits::fromQuantity(Offset));
  }
  case Stmt::UnaryOperatorClass: {
    auto *UO = cast<UnaryOperator>(E);
    switch (UO->getOpcode()) {
    default:
      break;
    case UO_Deref:
      return getBaseAlignmentAndOffsetFromPtr(UO->getSubExpr(), Ctx);
    }
    break;
  }
  case Stmt::BinaryOperatorClass: {
    auto *BO = cast<BinaryOperator>(E);
    auto Opcode = BO->getOpcode();
    switch (Opcode) {
    default:
      break;
    case BO_Comma:
      return getBaseAlignmentAndOffsetFromLValue(BO->getRHS(), Ctx);
    }
    break;
  }
  }
  return std::nullopt;
}

std::optional<std::pair<
    CharUnits, CharUnits>> static getBaseAlignmentAndOffsetFromPtr(const Expr
                                                                       *E,
                                                                   TreeContext
                                                                       &Ctx) {
  E = E->IgnoreParens();
  switch (E->getStmtClass()) {
  default:
    break;
  case Stmt::CStyleCastExprClass:
  case Stmt::ImplicitCastExprClass: {
    auto *CE = cast<CastExpr>(E);
    const Expr *From = CE->getSubExpr();
    switch (CE->getCastKind()) {
    default:
      break;
    case CK_NoOp:
      return getBaseAlignmentAndOffsetFromPtr(From, Ctx);
    case CK_ArrayToPointerDecay:
      return getBaseAlignmentAndOffsetFromLValue(From, Ctx);
    }
    break;
  }
  case Stmt::UnaryOperatorClass: {
    auto *UO = cast<UnaryOperator>(E);
    if (UO->getOpcode() == UO_AddrOf)
      return getBaseAlignmentAndOffsetFromLValue(UO->getSubExpr(), Ctx);
    break;
  }
  case Stmt::BinaryOperatorClass: {
    auto *BO = cast<BinaryOperator>(E);
    auto Opcode = BO->getOpcode();
    switch (Opcode) {
    default:
      break;
    case BO_Add:
    case BO_Sub: {
      const Expr *LHS = BO->getLHS(), *RHS = BO->getRHS();
      if (Opcode == BO_Add && !RHS->getType()->isIntegralOrEnumerationType())
        std::swap(LHS, RHS);
      return getAlignmentAndOffsetFromBinAddOrSub(LHS, RHS, Opcode == BO_Sub,
                                                  Ctx);
    }
    case BO_Comma:
      return getBaseAlignmentAndOffsetFromPtr(BO->getRHS(), Ctx);
    }
    break;
  }
  }
  return std::nullopt;
}

CharUnits getPresumedAlignmentOfPointer(const Expr *E, Sema &S) {
  // See if we can compute the alignment of a VarDecl and an offset from it.
  std::optional<std::pair<CharUnits, CharUnits>> P =
      getBaseAlignmentAndOffsetFromPtr(E, S.Context);

  if (P)
    return P->first.alignmentAtOffset(P->second);

  // If that failed, return the type's alignment.
  return S.Context.getTypeAlignInChars(E->getType()->getPointeeType());
}
} // namespace

void Sema::CheckCastAlign(Expr *Op, QualType T, SourceRange TRange) {
  // This is actually a lot of work to potentially be doing on every
  // cast; don't do it if we're ignoring -Wcast_align (as is the default).
  if (getDiagnostics().isIgnored(diag::warn_cast_align, TRange.getBegin()))
    return;

  // Require that the destination be a pointer type.
  const PointerType *DestPtr = T->getAs<PointerType>();
  if (!DestPtr)
    return;

  // If the destination has alignment 1, we're done.
  QualType DestPointee = DestPtr->getPointeeType();
  if (DestPointee->isIncompleteType())
    return;
  CharUnits DestAlign = Context.getTypeAlignInChars(DestPointee);
  if (DestAlign.isOne())
    return;

  // Require that the source be a pointer type.
  const PointerType *SrcPtr = Op->getType()->getAs<PointerType>();
  if (!SrcPtr)
    return;
  QualType SrcPointee = SrcPtr->getPointeeType();

  // Explicitly allow casts from cv void*.  We already implicitly
  // allowed casts to cv void*, since they have alignment 1.
  // Also allow casts involving incomplete types, which implicitly
  // includes 'void'.
  if (SrcPointee->isIncompleteType())
    return;

  CharUnits SrcAlign = getPresumedAlignmentOfPointer(Op, *this);

  if (SrcAlign >= DestAlign)
    return;

  Diag(TRange.getBegin(), diag::warn_cast_align)
      << Op->getType() << T << static_cast<unsigned>(SrcAlign.getQuantity())
      << static_cast<unsigned>(DestAlign.getQuantity()) << TRange
      << Op->getSourceRange();
}

// ===----------------------------------------------------------------------===
// Array bounds checking
// ===----------------------------------------------------------------------===

void Sema::CheckArrayAccess(const Expr *BaseExpr, const Expr *IndexExpr,
                            const ArraySubscriptExpr *ASE, bool AllowOnePastEnd,
                            bool IndexNegated) {
  if (LLVM_UNLIKELY(Diags.getIgnoreAllWarnings()))
    return;

  if (isConstantEvaluatedContext())
    return;

  IndexExpr = IndexExpr->IgnoreParenImpCasts();

  const Type *EffectiveType =
      BaseExpr->getType()->getPointeeOrArrayElementType();
  BaseExpr = BaseExpr->IgnoreParenCasts();
  const ConstantArrayType *ArrayTy =
      Context.getAsConstantArrayType(BaseExpr->getType());

  LangOptions::StrictFlexArraysLevelKind StrictFlexArraysLevel =
      getLangOpts().getStrictFlexArraysLevel();

  const Type *BaseType =
      ArrayTy == nullptr ? nullptr : ArrayTy->getElementType().getTypePtr();
  bool IsUnboundedArray =
      BaseType == nullptr ||
      BaseExpr->isFlexibleArrayMemberLike(Context, StrictFlexArraysLevel,
                                          /*IgnoreMacroSubstitution=*/true);

  Expr::EvalResult Result;
  if (!IndexExpr->EvaluateAsInt(Result, Context, Expr::SE_AllowSideEffects))
    return;

  llvm::APSInt index = Result.Val.getInt();
  if (IndexNegated) {
    index.setIsUnsigned(false);
    index = -index;
  }

  if (IsUnboundedArray) {
    if (EffectiveType->isFunctionType())
      return;
    if (index.isUnsigned() || !index.isNegative()) {
      const auto &ASTC = getTreeContext();
      unsigned AddrBits = ASTC.getTargetInfo().getPointerWidth(
          EffectiveType->getCanonicalTypeInternal().getAddressSpace());
      if (index.getBitWidth() < AddrBits)
        index = index.zext(AddrBits);
      std::optional<CharUnits> ElemCharUnits =
          ASTC.getTypeSizeInCharsIfKnown(EffectiveType);
      // PR50741 - If EffectiveType has unknown size (e.g., if it's a void
      // pointer) bounds-checking isn't meaningful.
      if (!ElemCharUnits || ElemCharUnits->isZero())
        return;
      llvm::APInt ElemBytes(index.getBitWidth(), ElemCharUnits->getQuantity());
      // If index has more active bits than address space, we already know
      // we have a bounds violation to warn about.  Otherwise, compute
      // address of (index + 1)th element, and warn about bounds violation
      // only if that address exceeds address space.
      if (index.getActiveBits() <= AddrBits) {
        bool Overflow;
        llvm::APInt Product(index);
        Product += 1;
        Product = Product.umul_ov(ElemBytes, Overflow);
        if (!Overflow && Product.getActiveBits() <= AddrBits)
          return;
      }

      // Need to compute max possible elements in address space, since that
      // is included in diag message.
      llvm::APInt MaxElems = llvm::APInt::getMaxValue(AddrBits);
      MaxElems = MaxElems.zext(std::max(AddrBits + 1, ElemBytes.getBitWidth()));
      MaxElems += 1;
      ElemBytes = ElemBytes.zextOrTrunc(MaxElems.getBitWidth());
      MaxElems = MaxElems.udiv(ElemBytes);

      unsigned DiagID =
          ASE ? diag::warn_array_index_exceeds_max_addressable_bounds
              : diag::warn_ptr_arith_exceeds_max_addressable_bounds;

      // Diag message shows element size in bits and in "bytes" (platform-
      // dependent CharUnits)
      DiagRuntimeBehavior(BaseExpr->getBeginLoc(), BaseExpr,
                          PDiag(DiagID)
                              << toString(index, 10, true) << AddrBits
                              << (unsigned)ASTC.toBits(*ElemCharUnits)
                              << toString(ElemBytes, 10, false)
                              << toString(MaxElems, 10, false)
                              << (unsigned)MaxElems.getLimitedValue(~0U)
                              << IndexExpr->getSourceRange());

      const NamedDecl *ND = nullptr;
      // Try harder to find a NamedDecl to point at in the note.
      while (const auto *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr))
        BaseExpr = ASE->getBase()->IgnoreParenCasts();
      if (const auto *DRE = dyn_cast<DeclRefExpr>(BaseExpr))
        ND = DRE->getDecl();
      if (const auto *ME = dyn_cast<MemberExpr>(BaseExpr))
        ND = ME->getMemberDecl();

      if (ND)
        DiagRuntimeBehavior(ND->getBeginLoc(), BaseExpr,
                            PDiag(diag::note_array_declared_here) << ND);
    }
    return;
  }

  if (index.isUnsigned() || !index.isNegative()) {
    // It is possible that the type of the base expression after
    // IgnoreParenCasts is incomplete, even though the type of the base
    // expression before IgnoreParenCasts is complete (see PR39746 for an
    // example). In this case we have no information about whether the array
    // access exceeds the array bounds. However we can still diagnose an array
    // access which precedes the array bounds.
    if (BaseType->isIncompleteType())
      return;

    llvm::APInt size = ArrayTy->getSize();

    if (BaseType != EffectiveType) {
      // Make sure we're comparing apples to apples when comparing index to
      // size.
      uint64_t ptrarith_typesize = Context.getTypeSize(EffectiveType);
      uint64_t array_typesize = Context.getTypeSize(BaseType);

      // Handle ptrarith_typesize being zero, such as when casting to void*.
      // Use the size in bits (what "getTypeSize()" returns) rather than bytes.
      if (!ptrarith_typesize)
        ptrarith_typesize = Context.getCharWidth();

      if (ptrarith_typesize != array_typesize) {
        // There's a cast to a different size type involved.
        uint64_t ratio = array_typesize / ptrarith_typesize;

        if (ptrarith_typesize * ratio == array_typesize)
          size *= llvm::APInt(size.getBitWidth(), ratio);
      }
    }

    if (size.getBitWidth() > index.getBitWidth())
      index = index.zext(size.getBitWidth());
    else if (size.getBitWidth() < index.getBitWidth())
      size = size.zext(index.getBitWidth());

    // For array subscripting the index must be less than size, but for pointer
    // arithmetic also allow the index (offset) to be equal to size since
    // computing the next address after the end of the array is legal and
    // commonly used for one-past-the-end pointers.
    if (AllowOnePastEnd ? index.ule(size) : index.ult(size))
      return;

    // Suppress the warning if the subscript expression (as identified by the
    // ']' location) and the index expression are both from macro expansions
    // within a system header.
    if (ASE) {
      SourceLocation RBracketLoc =
          SourceMgr.getSpellingLoc(ASE->getRBracketLoc());
      if (SourceMgr.isInSystemHeader(RBracketLoc)) {
        SourceLocation IndexLoc =
            SourceMgr.getSpellingLoc(IndexExpr->getBeginLoc());
        if (SourceMgr.isWrittenInSameFile(RBracketLoc, IndexLoc))
          return;
      }
    }

    unsigned DiagID = ASE ? diag::warn_array_index_exceeds_bounds
                          : diag::warn_ptr_arith_exceeds_bounds;
    unsigned CastMsg = (!ASE || BaseType == EffectiveType) ? 0 : 1;
    QualType CastMsgTy = ASE ? ASE->getLHS()->getType() : QualType();

    DiagRuntimeBehavior(
        BaseExpr->getBeginLoc(), BaseExpr,
        PDiag(DiagID) << toString(index, 10, true) << ArrayTy->desugar()
                      << CastMsg << CastMsgTy << IndexExpr->getSourceRange());
  } else {
    unsigned DiagID = diag::warn_array_index_precedes_bounds;
    if (!ASE) {
      DiagID = diag::warn_ptr_arith_precedes_bounds;
      if (index.isNegative())
        index = -index;
    }

    DiagRuntimeBehavior(BaseExpr->getBeginLoc(), BaseExpr,
                        PDiag(DiagID) << toString(index, 10, true)
                                      << IndexExpr->getSourceRange());
  }

  const NamedDecl *ND = nullptr;
  // Try harder to find a NamedDecl to point at in the note.
  while (const auto *ASE = dyn_cast<ArraySubscriptExpr>(BaseExpr))
    BaseExpr = ASE->getBase()->IgnoreParenCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(BaseExpr))
    ND = DRE->getDecl();
  if (const auto *ME = dyn_cast<MemberExpr>(BaseExpr))
    ND = ME->getMemberDecl();

  if (ND)
    DiagRuntimeBehavior(ND->getBeginLoc(), BaseExpr,
                        PDiag(diag::note_array_declared_here) << ND);
}

void Sema::CheckArrayAccess(const Expr *expr) {
  if (LLVM_UNLIKELY(Diags.getIgnoreAllWarnings()))
    return;

  switch (expr->getStmtClass()) {
  case Stmt::BinaryOperatorClass:
  case Stmt::CompoundAssignOperatorClass:
  case Stmt::IntegerLiteralClass:
  case Stmt::FloatingLiteralClass:
  case Stmt::CharacterLiteralClass:
  case Stmt::DeclRefExprClass:
  case Stmt::CallExprClass:
    return;
  default:
    break;
  }
  int AllowOnePastEnd = 0;
  while (expr) {
    expr = expr->IgnoreParenImpCasts();
    switch (expr->getStmtClass()) {
    case Stmt::ArraySubscriptExprClass: {
      const ArraySubscriptExpr *ASE = cast<ArraySubscriptExpr>(expr);
      CheckArrayAccess(ASE->getBase(), ASE->getIdx(), ASE, AllowOnePastEnd > 0);
      expr = ASE->getBase();
      break;
    }
    case Stmt::MemberExprClass: {
      expr = cast<MemberExpr>(expr)->getBase();
      break;
    }
    case Stmt::UnaryOperatorClass: {
      // Only unwrap the * and & unary operators
      const UnaryOperator *UO = cast<UnaryOperator>(expr);
      expr = UO->getSubExpr();
      switch (UO->getOpcode()) {
      case UO_AddrOf:
        AllowOnePastEnd++;
        break;
      case UO_Deref:
        AllowOnePastEnd--;
        break;
      default:
        return;
      }
      break;
    }
    case Stmt::ConditionalOperatorClass: {
      const ConditionalOperator *cond = cast<ConditionalOperator>(expr);
      if (const Expr *lhs = cond->getLHS())
        CheckArrayAccess(lhs);
      if (const Expr *rhs = cond->getRHS())
        CheckArrayAccess(rhs);
      return;
    }
    default:
      return;
    }
  }
}

// ===----------------------------------------------------------------------===
// Empty body diagnostics
// ===----------------------------------------------------------------------===

namespace {
bool shouldDiagnoseEmptyStmtBody(const SourceManager &SourceMgr,
                                 SourceLocation StmtLoc, const NullStmt *Body) {
  // Do not warn if the body is a macro that expands to nothing, e.g:
  //
  // #define CALL(x)
  // if (condition)
  //   CALL(0);
  if (Body->hasLeadingEmptyMacro())
    return false;

  // Get line numbers of statement and body.
  bool StmtLineInvalid;
  unsigned StmtLine =
      SourceMgr.getPresumedLineNumber(StmtLoc, &StmtLineInvalid);
  if (StmtLineInvalid)
    return false;

  bool BodyLineInvalid;
  unsigned BodyLine =
      SourceMgr.getSpellingLineNumber(Body->getSemiLoc(), &BodyLineInvalid);
  if (BodyLineInvalid)
    return false;

  // Warn if null statement and body are on the same line.
  if (StmtLine != BodyLine)
    return false;

  return true;
}
} // namespace

void Sema::DiagnoseEmptyStmtBody(SourceLocation StmtLoc, const Stmt *Body,
                                 unsigned DiagID) {
  const NullStmt *NBody = dyn_cast<NullStmt>(Body);
  if (!NBody)
    return;

  if (Diags.isIgnored(DiagID, NBody->getSemiLoc()))
    return;

  if (!shouldDiagnoseEmptyStmtBody(SourceMgr, StmtLoc, NBody))
    return;

  Diag(NBody->getSemiLoc(), DiagID);
  Diag(NBody->getSemiLoc(), diag::note_empty_body_on_separate_line);
}

void Sema::DiagnoseEmptyLoopBody(const Stmt *S, const Stmt *PossibleBody) {
  SourceLocation StmtLoc;
  const Stmt *Body;
  unsigned DiagID;
  if (const ForStmt *FS = dyn_cast<ForStmt>(S)) {
    StmtLoc = FS->getRParenLoc();
    Body = FS->getBody();
    DiagID = diag::warn_empty_for_body;
  } else if (const WhileStmt *WS = dyn_cast<WhileStmt>(S)) {
    StmtLoc = WS->getRParenLoc();
    Body = WS->getBody();
    DiagID = diag::warn_empty_while_body;
  } else
    return; // Neither `for' nor `while'.

  // The body should be a null statement.
  const NullStmt *NBody = dyn_cast<NullStmt>(Body);
  if (!NBody)
    return;

  // Skip expensive checks if diagnostic is disabled.
  if (Diags.isIgnored(DiagID, NBody->getSemiLoc()))
    return;

  // Do the usual checks.
  if (!shouldDiagnoseEmptyStmtBody(SourceMgr, StmtLoc, NBody))
    return;

  // `for(...);' and `while(...);' are popular idioms, so in order to keep
  // noise level low, emit diagnostics only if for/while is followed by a
  // CompoundStmt, e.g.:
  //    for (int i = 0; i < n; i++);
  //    {
  //      a(i);
  //    }
  // or if for/while is followed by a statement with more indentation
  // than for/while itself:
  //    for (int i = 0; i < n; i++);
  //      a(i);
  bool ProbableTypo = isa<CompoundStmt>(PossibleBody);
  if (!ProbableTypo) {
    bool BodyColInvalid;
    unsigned BodyCol = SourceMgr.getPresumedColumnNumber(
        PossibleBody->getBeginLoc(), &BodyColInvalid);
    if (BodyColInvalid)
      return;

    bool StmtColInvalid;
    unsigned StmtCol =
        SourceMgr.getPresumedColumnNumber(S->getBeginLoc(), &StmtColInvalid);
    if (StmtColInvalid)
      return;

    if (BodyCol > StmtCol)
      ProbableTypo = true;
  }

  if (ProbableTypo) {
    Diag(NBody->getSemiLoc(), DiagID);
    Diag(NBody->getSemiLoc(), diag::note_empty_body_on_separate_line);
  }
}
