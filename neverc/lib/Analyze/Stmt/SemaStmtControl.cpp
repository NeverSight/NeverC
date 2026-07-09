//===- SemaStmtControl.cpp - If / switch statement semantics --------------===//

#include "SemaStmtUtils.h"
#include "neverc/Analyze/Initialization.h"
#include "neverc/Analyze/ScopeInfo.h"
#include "neverc/Analyze/SemaInternal.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/TreeDiag.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"
#include "neverc/Tree/Expr/IgnoreExpr.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"

using namespace neverc;
using namespace sema;

// ===----------------------------------------------------------------------===
// If / switch
// ===----------------------------------------------------------------------===

StmtResult Sema::OnIfStmt(SourceLocation IfLoc, SourceLocation LParenLoc,
                          Stmt *InitStmt, ConditionResult Cond,
                          SourceLocation RParenLoc, Stmt *thenStmt,
                          SourceLocation ElseLoc, Stmt *elseStmt) {
  if (Cond.isInvalid())
    return StmtError();

  Expr *CondExpr = Cond.get().second;
  assert(CondExpr && "If statement: missing condition");
  // Only call the CommaVisitor when not C89 due to differences in scope flags.
  if (getLangOpts().C99 &&
      !Diags.isIgnored(diag::warn_comma_operator, CondExpr->getExprLoc()))
    CommaVisitor(*this).Visit(CondExpr);

  if (!elseStmt)
    DiagnoseEmptyStmtBody(RParenLoc, thenStmt, diag::warn_empty_if_body);

  std::tuple<bool, const Attr *, const Attr *> LHC =
      Stmt::determineLikelihoodConflict(thenStmt, elseStmt);
  if (std::get<0>(LHC)) {
    const Attr *ThenAttr = std::get<1>(LHC);
    const Attr *ElseAttr = std::get<2>(LHC);
    Diags.Report(ThenAttr->getLocation(),
                 diag::warn_attributes_likelihood_ifstmt_conflict)
        << ThenAttr << ThenAttr->getRange();
    Diags.Report(ElseAttr->getLocation(), diag::note_conflicting_attribute)
        << ElseAttr << ElseAttr->getRange();
  }

  return FormIfStmt(IfLoc, LParenLoc, InitStmt, Cond, RParenLoc, thenStmt,
                    ElseLoc, elseStmt);
}

StmtResult Sema::FormIfStmt(SourceLocation IfLoc, SourceLocation LParenLoc,
                            Stmt *InitStmt, ConditionResult Cond,
                            SourceLocation RParenLoc, Stmt *thenStmt,
                            SourceLocation ElseLoc, Stmt *elseStmt) {
  if (Cond.isInvalid())
    return StmtError();

  return IfStmt::Create(Context, IfLoc, InitStmt, Cond.get().first,
                        Cond.get().second, LParenLoc, RParenLoc, thenStmt,
                        ElseLoc, elseStmt);
}

namespace {
struct CaseCompareFunctor {
  bool operator()(const std::pair<llvm::APSInt, CaseStmt *> &LHS,
                  const llvm::APSInt &RHS) {
    return LHS.first < RHS;
  }
  bool operator()(const std::pair<llvm::APSInt, CaseStmt *> &LHS,
                  const std::pair<llvm::APSInt, CaseStmt *> &RHS) {
    return LHS.first < RHS.first;
  }
  bool operator()(const llvm::APSInt &LHS,
                  const std::pair<llvm::APSInt, CaseStmt *> &RHS) {
    return LHS < RHS.first;
  }
};

bool cmpCaseVals(const std::pair<llvm::APSInt, CaseStmt *> &lhs,
                 const std::pair<llvm::APSInt, CaseStmt *> &rhs) {
  if (lhs.first < rhs.first)
    return true;

  if (lhs.first == rhs.first &&
      lhs.second->getCaseLoc() < rhs.second->getCaseLoc())
    return true;
  return false;
}

bool cmpEnumVals(const std::pair<llvm::APSInt, EnumConstantDecl *> &lhs,
                 const std::pair<llvm::APSInt, EnumConstantDecl *> &rhs) {
  return lhs.first < rhs.first;
}

bool eqEnumVals(const std::pair<llvm::APSInt, EnumConstantDecl *> &lhs,
                const std::pair<llvm::APSInt, EnumConstantDecl *> &rhs) {
  return lhs.first == rhs.first;
}

QualType getTypeBeforeIntegralPromotion(const Expr *&E) {
  if (const auto *FE = dyn_cast<FullExpr>(E))
    E = FE->getSubExpr();
  while (const auto *ImpCast = dyn_cast<ImplicitCastExpr>(E)) {
    if (ImpCast->getCastKind() != CK_IntegralCast)
      break;
    E = ImpCast->getSubExpr();
  }
  return E->getType();
}
} // namespace

ExprResult Sema::CheckSwitchCondition(SourceLocation SwitchLoc, Expr *Cond) {
  if (Cond->hasPlaceholderType()) {
    ExprResult PH = CheckPlaceholderExpr(Cond);
    if (PH.isInvalid())
      return ExprError();
    Cond = PH.get();
  }

  ExprResult Converted = DefaultLvalueConversion(Cond);
  if (!Converted.isUsable())
    return ExprError();

  QualType T = Converted.get()->getType();
  if (!T->isIntegralOrEnumerationType()) {
    Diag(SwitchLoc, diag::err_typecheck_statement_requires_integer)
        << T << Cond->getSourceRange();
    return ExprError();
  }

  Cond = Converted.get();
  // Integer promotions on the switch controlling expression.
  return UsualUnaryConversions(Cond);
}

StmtResult Sema::OnStartOfSwitchStmt(SourceLocation SwitchLoc,
                                     SourceLocation LParenLoc, Stmt *InitStmt,
                                     ConditionResult Cond,
                                     SourceLocation RParenLoc) {
  Expr *CondExpr = Cond.get().second;
  assert((Cond.isInvalid() || CondExpr) && "switch with no condition");

  if (CondExpr) {
    // We have already converted the expression to an integral or enumeration
    // type, when we parsed the switch condition. There are cases where we don't
    // have an appropriate type, e.g. a typo-expr Cond was corrected to an
    // inappropriate-type expr, we just return an error.
    if (!CondExpr->getType()->isIntegralOrEnumerationType())
      return StmtError();
    if (CondExpr->isKnownToHaveBooleanValue()) {
      // switch(bool_expr) {...} is often a programmer error, e.g.
      //   switch(n && mask) { ... }  // Doh - should be "n & mask".
      // One can always use an if statement instead of switch(bool_expr).
      Diag(SwitchLoc, diag::warn_bool_switch_condition)
          << CondExpr->getSourceRange();
    }
  }

  setFunctionHasBranchIntoScope();

  auto *SS = SwitchStmt::Create(Context, InitStmt, Cond.get().first, CondExpr,
                                LParenLoc, RParenLoc);
  getCurFunction()->SwitchStack.push_back(
      FunctionScopeInfo::SwitchInfo(SS, false));
  return SS;
}

namespace {
void adjustAPSInt(llvm::APSInt &Val, unsigned BitWidth, bool IsSigned) {
  Val = Val.extOrTrunc(BitWidth);
  Val.setIsSigned(IsSigned);
}

void checkCaseValue(Sema &S, SourceLocation Loc, const llvm::APSInt &Val,
                    unsigned UnpromotedWidth, bool UnpromotedSign) {
  // If the case value was signed and negative and the switch expression is
  // unsigned, don't bother to warn: this is implementation-defined behavior.
  if (UnpromotedWidth < Val.getBitWidth()) {
    llvm::APSInt ConvVal(Val);
    adjustAPSInt(ConvVal, UnpromotedWidth, UnpromotedSign);
    adjustAPSInt(ConvVal, Val.getBitWidth(), Val.isSigned());
    // Use different diagnostics for overflow in conversion to promoted
    // type versus "switch expression cannot have this value". Use proper
    // IntRange checking rather than just looking at the unpromoted type here.
    if (ConvVal != Val)
      S.Diag(Loc, diag::warn_case_value_overflow)
          << toString(Val, 10) << toString(ConvVal, 10);
  }
}

typedef llvm::SmallVector<std::pair<llvm::APSInt, EnumConstantDecl *>, 64>
    EnumValsTy;

bool shouldDiagnoseSwitchCaseNotInEnum(const Sema &S, const EnumDecl *ED,
                                       const Expr *CaseExpr,
                                       EnumValsTy::iterator &EI,
                                       EnumValsTy::iterator &EIEnd,
                                       const llvm::APSInt &Val) {
  if (!ED->isClosed())
    return false;

  if (const DeclRefExpr *DRE =
          dyn_cast<DeclRefExpr>(CaseExpr->IgnoreParenImpCasts())) {
    if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      QualType VarType = VD->getType();
      QualType EnumType = S.Context.getTypeDeclType(ED);
      if (VD->hasGlobalStorage() && VarType.isConstQualified() &&
          S.Context.hasSameUnqualifiedType(EnumType, VarType))
        return false;
    }
  }

  if (ED->hasAttr<FlagEnumAttr>())
    return !S.IsValueInFlagEnum(ED, Val, false);

  while (EI != EIEnd && EI->first < Val)
    EI++;

  if (EI != EIEnd && EI->first == Val)
    return false;

  return true;
}

void checkEnumTypesInSwitchStmt(Sema &S, const Expr *Cond, const Expr *Case) {
  QualType CondType = Cond->getType();
  QualType CaseType = Case->getType();

  const EnumType *CondEnumType = CondType->getAs<EnumType>();
  const EnumType *CaseEnumType = CaseType->getAs<EnumType>();
  if (!CondEnumType || !CaseEnumType)
    return;

  // Ignore anonymous enums.
  if (!CondEnumType->getDecl()->getIdentifier() &&
      !CondEnumType->getDecl()->getTypedefNameForAnonDecl())
    return;
  if (!CaseEnumType->getDecl()->getIdentifier() &&
      !CaseEnumType->getDecl()->getTypedefNameForAnonDecl())
    return;

  if (S.Context.hasSameUnqualifiedType(CondType, CaseType))
    return;

  S.Diag(Case->getExprLoc(), diag::warn_comparison_of_mixed_enum_types_switch)
      << CondType << CaseType << Cond->getSourceRange()
      << Case->getSourceRange();
}
} // namespace

StmtResult Sema::OnFinishSwitchStmt(SourceLocation SwitchLoc, Stmt *Switch,
                                    Stmt *BodyStmt) {
  SwitchStmt *SS = cast<SwitchStmt>(Switch);
  bool CaseListIsIncomplete = getCurFunction()->SwitchStack.back().getInt();
  assert(SS == getCurFunction()->SwitchStack.back().getPointer() &&
         "switch stack missing push/pop!");

  getCurFunction()->SwitchStack.pop_back();

  if (!BodyStmt)
    return StmtError();
  SS->setBody(BodyStmt, SwitchLoc);

  Expr *CondExpr = SS->getCond();
  if (!CondExpr)
    return StmtError();

  QualType CondType = CondExpr->getType();

  // Case values are checked against the pre-promotion type of the
  // switch condition, not the promoted type.
  const Expr *CondExprBeforePromotion = CondExpr;
  QualType CondTypeBeforePromotion =
      getTypeBeforeIntegralPromotion(CondExprBeforePromotion);

  // Get the bitwidth of the switched-on value after promotions. We must
  // convert the integer case values to this width before comparison.
  unsigned CondWidth = Context.getIntWidth(CondType);
  bool CondIsSigned = CondType->isSignedIntegerOrEnumerationType();

  unsigned CondWidthBeforePromotion =
      Context.getIntWidth(CondTypeBeforePromotion);
  bool CondIsSignedBeforePromotion =
      CondTypeBeforePromotion->isSignedIntegerOrEnumerationType();

  // Accumulate all of the case values in a vector so that we can sort them
  // and detect duplicates.  This vector contains the APInt for the case after
  // it has been converted to the condition type.
  typedef llvm::SmallVector<std::pair<llvm::APSInt, CaseStmt *>, 64> CaseValsTy;
  CaseValsTy CaseVals;

  // Keep track of any GNU case ranges we see.  The APSInt is the low value.
  typedef std::vector<std::pair<llvm::APSInt, CaseStmt *>> CaseRangesTy;
  CaseRangesTy CaseRanges;

  DefaultStmt *TheDefaultStmt = nullptr;

  bool CaseListIsErroneous = false;

  for (SwitchCase *SC = SS->getSwitchCaseList(); SC;
       SC = SC->getNextSwitchCase()) {

    if (DefaultStmt *DS = dyn_cast<DefaultStmt>(SC)) {
      if (TheDefaultStmt) {
        Diag(DS->getDefaultLoc(), diag::err_multiple_default_labels_defined);
        Diag(TheDefaultStmt->getDefaultLoc(), diag::note_duplicate_case_prev);

        // Removing the default statement from the switch block to return a
        // valid AST would require recursing down the AST and
        // finding it, not something we are set up to do right now.  For now,
        // just lop the entire switch stmt out of the AST.
        CaseListIsErroneous = true;
      }
      TheDefaultStmt = DS;

    } else {
      CaseStmt *CS = cast<CaseStmt>(SC);

      Expr *Lo = CS->getLHS();

      // We already verified that the expression has a constant value;
      // get that value (prior to conversions).
      const Expr *LoBeforePromotion = Lo;
      getTypeBeforeIntegralPromotion(LoBeforePromotion);
      llvm::APSInt LoVal = LoBeforePromotion->EvaluateKnownConstInt(Context);

      checkCaseValue(*this, Lo->getBeginLoc(), LoVal, CondWidthBeforePromotion,
                     CondIsSignedBeforePromotion);
      checkEnumTypesInSwitchStmt(*this, CondExprBeforePromotion,
                                 LoBeforePromotion);

      adjustAPSInt(LoVal, CondWidth, CondIsSigned);

      // If this is a case range, remember it in CaseRanges, otherwise CaseVals.
      if (CS->getRHS()) {
        CaseRanges.push_back(std::make_pair(LoVal, CS));
      } else
        CaseVals.push_back(std::make_pair(LoVal, CS));
    }
  }

  {
    // If we don't have a default statement, check whether the
    // condition is constant.
    llvm::APSInt ConstantCondValue;
    bool HasConstantCond = false;
    if (!TheDefaultStmt) {
      Expr::EvalResult Result;
      HasConstantCond =
          CondExpr->EvaluateAsInt(Result, Context, Expr::SE_AllowSideEffects);
      if (Result.Val.isInt())
        ConstantCondValue = Result.Val.getInt();
      assert(!HasConstantCond ||
             (ConstantCondValue.getBitWidth() == CondWidth &&
              ConstantCondValue.isSigned() == CondIsSigned));
      Diag(SwitchLoc, diag::warn_switch_default);
    }
    bool ShouldCheckConstantCond = HasConstantCond;

    // Sort all the scalar case values so we can easily detect duplicates.
    llvm::stable_sort(CaseVals, cmpCaseVals);

    if (!CaseVals.empty()) {
      for (unsigned i = 0, e = CaseVals.size(); i != e; ++i) {
        if (ShouldCheckConstantCond && CaseVals[i].first == ConstantCondValue)
          ShouldCheckConstantCond = false;

        if (i != 0 && CaseVals[i].first == CaseVals[i - 1].first) {
          // If we have a duplicate, report it.
          // First, determine if either case value has a name
          llvm::StringRef PrevString, CurrString;
          Expr *PrevCase = CaseVals[i - 1].second->getLHS()->IgnoreParenCasts();
          Expr *CurrCase = CaseVals[i].second->getLHS()->IgnoreParenCasts();
          if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(PrevCase)) {
            PrevString = DeclRef->getDecl()->getName();
          }
          if (DeclRefExpr *DeclRef = dyn_cast<DeclRefExpr>(CurrCase)) {
            CurrString = DeclRef->getDecl()->getName();
          }
          llvm::SmallString<16> CaseValStr;
          CaseVals[i - 1].first.toString(CaseValStr);

          if (PrevString == CurrString)
            Diag(CaseVals[i].second->getLHS()->getBeginLoc(),
                 diag::err_duplicate_case)
                << (PrevString.empty() ? llvm::StringRef(CaseValStr)
                                       : PrevString);
          else
            Diag(CaseVals[i].second->getLHS()->getBeginLoc(),
                 diag::err_duplicate_case_differing_expr)
                << (PrevString.empty() ? llvm::StringRef(CaseValStr)
                                       : PrevString)
                << (CurrString.empty() ? llvm::StringRef(CaseValStr)
                                       : CurrString)
                << CaseValStr;

          Diag(CaseVals[i - 1].second->getLHS()->getBeginLoc(),
               diag::note_duplicate_case_prev);
          CaseListIsErroneous = true;
        }
      }
    }

    // Detect duplicate case ranges, which usually don't exist at all in
    // the first place.
    if (!CaseRanges.empty()) {
      // Sort all the case ranges by their low value so we can easily detect
      // overlaps between ranges.
      llvm::stable_sort(CaseRanges);

      // Scan the ranges, computing the high values and removing empty ranges.
      std::vector<llvm::APSInt> HiVals;
      for (unsigned i = 0, e = CaseRanges.size(); i != e; ++i) {
        llvm::APSInt &LoVal = CaseRanges[i].first;
        CaseStmt *CR = CaseRanges[i].second;
        Expr *Hi = CR->getRHS();

        const Expr *HiBeforePromotion = Hi;
        getTypeBeforeIntegralPromotion(HiBeforePromotion);
        llvm::APSInt HiVal = HiBeforePromotion->EvaluateKnownConstInt(Context);

        checkCaseValue(*this, Hi->getBeginLoc(), HiVal,
                       CondWidthBeforePromotion, CondIsSignedBeforePromotion);

        adjustAPSInt(HiVal, CondWidth, CondIsSigned);

        // If the low value is bigger than the high value, the case is empty.
        if (LoVal > HiVal) {
          Diag(CR->getLHS()->getBeginLoc(), diag::warn_case_empty_range)
              << SourceRange(CR->getLHS()->getBeginLoc(), Hi->getEndLoc());
          CaseRanges.erase(CaseRanges.begin() + i);
          --i;
          --e;
          continue;
        }

        if (ShouldCheckConstantCond && LoVal <= ConstantCondValue &&
            ConstantCondValue <= HiVal)
          ShouldCheckConstantCond = false;

        HiVals.push_back(HiVal);
      }

      // Rescan the ranges, looking for overlap with singleton values and other
      // ranges.  Since the range list is sorted, we only need to compare case
      // ranges with their neighbors.
      for (unsigned i = 0, e = CaseRanges.size(); i != e; ++i) {
        llvm::APSInt &CRLo = CaseRanges[i].first;
        llvm::APSInt &CRHi = HiVals[i];
        CaseStmt *CR = CaseRanges[i].second;

        CaseStmt *OverlapStmt = nullptr;
        llvm::APSInt OverlapVal(32);

        // Find the smallest value >= the lower bound.  If I is in the
        // case range, then we have overlap.
        CaseValsTy::iterator I =
            llvm::lower_bound(CaseVals, CRLo, CaseCompareFunctor());
        if (I != CaseVals.end() && I->first < CRHi) {
          OverlapVal = I->first; // Found overlap with scalar.
          OverlapStmt = I->second;
        }

        // Find the smallest value bigger than the upper bound.
        I = std::upper_bound(I, CaseVals.end(), CRHi, CaseCompareFunctor());
        if (I != CaseVals.begin() && (I - 1)->first >= CRLo) {
          OverlapVal = (I - 1)->first; // Found overlap with scalar.
          OverlapStmt = (I - 1)->second;
        }

        if (i && CRLo <= HiVals[i - 1]) {
          OverlapVal = HiVals[i - 1]; // Found overlap with range.
          OverlapStmt = CaseRanges[i - 1].second;
        }

        if (OverlapStmt) {
          // If we have a duplicate, report it.
          Diag(CR->getLHS()->getBeginLoc(), diag::err_duplicate_case)
              << toString(OverlapVal, 10);
          Diag(OverlapStmt->getLHS()->getBeginLoc(),
               diag::note_duplicate_case_prev);
          CaseListIsErroneous = true;
        }
      }
    }

    // Complain if we have a constant condition and we didn't find a match.
    if (!CaseListIsErroneous && !CaseListIsIncomplete &&
        ShouldCheckConstantCond) {
      Diag(CondExpr->getExprLoc(), diag::warn_missing_case_for_condition)
          << toString(ConstantCondValue, 10) << CondExpr->getSourceRange();
    }

    // Preserve enum coverage info in the AST even when 'default:' is present.
    const EnumType *ET = CondTypeBeforePromotion->getAs<EnumType>();
    if (!CaseListIsErroneous && !CaseListIsIncomplete && !HasConstantCond &&
        ET && ET->getDecl()->isCompleteDefinition() &&
        !ET->getDecl()->enumerators().empty()) {
      const EnumDecl *ED = ET->getDecl();
      EnumValsTy EnumVals;

      // Gather all enum values, set their type and sort them,
      // allowing easier comparison with CaseVals.
      for (auto *EDI : ED->enumerators()) {
        llvm::APSInt Val = EDI->getInitVal();
        adjustAPSInt(Val, CondWidth, CondIsSigned);
        EnumVals.push_back(std::make_pair(Val, EDI));
      }
      llvm::stable_sort(EnumVals, cmpEnumVals);
      auto EI = EnumVals.begin(),
           EIEnd = std::unique(EnumVals.begin(), EnumVals.end(), eqEnumVals);

      // See which case values aren't in enum.
      for (CaseValsTy::const_iterator CI = CaseVals.begin();
           CI != CaseVals.end(); CI++) {
        Expr *CaseExpr = CI->second->getLHS();
        if (shouldDiagnoseSwitchCaseNotInEnum(*this, ED, CaseExpr, EI, EIEnd,
                                              CI->first))
          Diag(CaseExpr->getExprLoc(), diag::warn_not_in_enum)
              << CondTypeBeforePromotion;
      }

      // See which of case ranges aren't in enum
      EI = EnumVals.begin();
      for (CaseRangesTy::const_iterator RI = CaseRanges.begin();
           RI != CaseRanges.end(); RI++) {
        Expr *CaseExpr = RI->second->getLHS();
        if (shouldDiagnoseSwitchCaseNotInEnum(*this, ED, CaseExpr, EI, EIEnd,
                                              RI->first))
          Diag(CaseExpr->getExprLoc(), diag::warn_not_in_enum)
              << CondTypeBeforePromotion;

        llvm::APSInt Hi = RI->second->getRHS()->EvaluateKnownConstInt(Context);
        adjustAPSInt(Hi, CondWidth, CondIsSigned);

        CaseExpr = RI->second->getRHS();
        if (shouldDiagnoseSwitchCaseNotInEnum(*this, ED, CaseExpr, EI, EIEnd,
                                              Hi))
          Diag(CaseExpr->getExprLoc(), diag::warn_not_in_enum)
              << CondTypeBeforePromotion;
      }

      auto CI = CaseVals.begin();
      auto RI = CaseRanges.begin();
      bool hasCasesNotInSwitch = false;

      llvm::SmallVector<DeclarationName, 8> UnhandledNames;

      for (EI = EnumVals.begin(); EI != EIEnd; EI++) {
        // Don't warn about omitted unavailable EnumConstantDecls.
        switch (EI->second->getAvailability()) {
        case AR_Deprecated:
          // Omitting a deprecated constant is ok; it should never materialize.
        case AR_Unavailable:
          continue;

        case AR_NotYetIntroduced:
          // Partially available enum constants should be present. Note that we
          // suppress -Wunguarded-availability diagnostics for such uses.
        case AR_Available:
          break;
        }

        if (EI->second->hasAttr<UnusedAttr>())
          continue;

        // Drop unneeded case values
        while (CI != CaseVals.end() && CI->first < EI->first)
          CI++;

        if (CI != CaseVals.end() && CI->first == EI->first)
          continue;

        // Drop unneeded case ranges
        for (; RI != CaseRanges.end(); RI++) {
          llvm::APSInt Hi =
              RI->second->getRHS()->EvaluateKnownConstInt(Context);
          adjustAPSInt(Hi, CondWidth, CondIsSigned);
          if (EI->first <= Hi)
            break;
        }

        if (RI == CaseRanges.end() || EI->first < RI->first) {
          hasCasesNotInSwitch = true;
          UnhandledNames.push_back(EI->second->getDeclName());
        }
      }

      if (TheDefaultStmt && UnhandledNames.empty() && ED->isClosedNonFlag())
        Diag(TheDefaultStmt->getDefaultLoc(), diag::warn_unreachable_default);

      // Produce a nice diagnostic if multiple values aren't handled.
      if (!UnhandledNames.empty()) {
        auto DB = Diag(CondExpr->getExprLoc(), TheDefaultStmt
                                                   ? diag::warn_def_missing_case
                                                   : diag::warn_missing_case)
                  << CondExpr->getSourceRange() << (int)UnhandledNames.size();

        for (size_t I = 0, E = std::min(UnhandledNames.size(), (size_t)3);
             I != E; ++I)
          DB << UnhandledNames[I];
      }

      if (!hasCasesNotInSwitch)
        SS->setAllEnumCasesCovered();
    }
  }

  if (BodyStmt)
    DiagnoseEmptyStmtBody(CondExpr->getEndLoc(), BodyStmt,
                          diag::warn_empty_switch_body);

  // If the case list was broken, just return the whole substmt as broken.
  if (CaseListIsErroneous)
    return StmtError();

  return SS;
}

void Sema::DiagnoseAssignmentEnum(QualType DstType, QualType SrcType,
                                  Expr *SrcExpr) {
  if (Diags.isIgnored(diag::warn_not_in_enum_assignment, SrcExpr->getExprLoc()))
    return;

  if (const EnumType *ET = DstType->getAs<EnumType>())
    if (!Context.hasSameUnqualifiedType(SrcType, DstType) &&
        SrcType->isIntegerType()) {
      if (SrcExpr->isIntegerConstantExpr(Context)) {
        // Get the bitwidth of the enum value before promotions.
        unsigned DstWidth = Context.getIntWidth(DstType);
        bool DstIsSigned = DstType->isSignedIntegerOrEnumerationType();

        llvm::APSInt RhsVal = SrcExpr->EvaluateKnownConstInt(Context);
        adjustAPSInt(RhsVal, DstWidth, DstIsSigned);
        const EnumDecl *ED = ET->getDecl();

        if (!ED->isClosed())
          return;

        if (ED->hasAttr<FlagEnumAttr>()) {
          if (!IsValueInFlagEnum(ED, RhsVal, true))
            Diag(SrcExpr->getExprLoc(), diag::warn_not_in_enum_assignment)
                << DstType.getUnqualifiedType();
        } else {
          typedef llvm::SmallVector<std::pair<llvm::APSInt, EnumConstantDecl *>,
                                    64>
              EnumValsTy;
          EnumValsTy EnumVals;

          // Gather all enum values, set their type and sort them,
          // allowing easier comparison with rhs constant.
          for (auto *EDI : ED->enumerators()) {
            llvm::APSInt Val = EDI->getInitVal();
            adjustAPSInt(Val, DstWidth, DstIsSigned);
            EnumVals.push_back(std::make_pair(Val, EDI));
          }
          if (EnumVals.empty())
            return;
          llvm::stable_sort(EnumVals, cmpEnumVals);
          EnumValsTy::iterator EIend =
              std::unique(EnumVals.begin(), EnumVals.end(), eqEnumVals);

          // See which values aren't in the enum.
          EnumValsTy::const_iterator EI = EnumVals.begin();
          while (EI != EIend && EI->first < RhsVal)
            EI++;
          if (EI == EIend || EI->first != RhsVal) {
            Diag(SrcExpr->getExprLoc(), diag::warn_not_in_enum_assignment)
                << DstType.getUnqualifiedType();
          }
        }
      }
    }
}
