//===--- SemaOverload.cpp - C++ overload resolution -----------------------===//
#include "neverc/Analyze/Overload.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclCXX.h"
#include "neverc/Tree/Expr/Expr.h"

using namespace neverc;

static unsigned rankICS(const ImplicitConversionSequence &ICS) {
  if (!ICS.isInitialized())
    return 100;
  switch (ICS.getKind()) {
  case ImplicitConversionSequence::StandardConversion: {
    unsigned Rank = 0;
    auto bump = [&](ImplicitConversionKind K) {
      switch (K) {
      case ICK_Identity:
        break;
      case ICK_Lvalue_To_Rvalue:
      case ICK_Array_To_Pointer:
      case ICK_Function_To_Pointer:
      case ICK_Qualification:
        Rank += 1;
        break;
      case ICK_Integral_Promotion:
      case ICK_Floating_Promotion:
      case ICK_Complex_Promotion:
        Rank += 2;
        break;
      default:
        Rank += 4;
        break;
      }
    };
    bump(ICS.Standard.First);
    bump(ICS.Standard.Second);
    bump(ICS.Standard.Third);
    return Rank;
  }
  case ImplicitConversionSequence::UserDefinedConversion:
    return 20;
  case ImplicitConversionSequence::EllipsisConversion:
    return 30;
  case ImplicitConversionSequence::BadConversion:
    return 100;
  }
  return 100;
}

OverloadCandidate *
OverloadCandidateSet::BestViableFunction(Sema &S, SourceLocation /*Loc*/) {
  (void)S;
  OverloadCandidate *Best = nullptr;
  bool Ambiguous = false;
  for (auto &Cand : Candidates) {
    if (!Cand.Viable)
      continue;
    if (!Best) {
      Best = &Cand;
      Ambiguous = false;
      continue;
    }
    unsigned BestRank = 0, CandRank = 0;
    for (const auto &C : Best->Conversions)
      BestRank += rankICS(C);
    for (const auto &C : Cand.Conversions)
      CandRank += rankICS(C);
    if (CandRank < BestRank) {
      Best = &Cand;
      Ambiguous = false;
    } else if (CandRank == BestRank) {
      Ambiguous = true;
    }
  }
  if (Ambiguous)
    return nullptr;
  return Best;
}

void Sema::AddOverloadCandidate(FunctionDecl *FD, ArrayRef<Expr *> Args,
                                OverloadCandidateSet &CandidateSet) {
  if (!FD)
    return;
  OverloadCandidate &Cand =
      CandidateSet.addCandidate(static_cast<unsigned>(Args.size()));
  Cand.Function = FD;
  Cand.FoundDecl = DeclAccessPair::make(FD, FD->getAccess());
  Cand.ExplicitCallArguments = static_cast<unsigned>(Args.size());
  Cand.Viable = true;

  unsigned NumParams = FD->getNumParams();
  bool Variadic = FD->isVariadic();
  if (Args.size() > NumParams && !Variadic) {
    Cand.Viable = false;
    Cand.FailureKind.setBad(Args.empty() ? QualType() : Args[0]->getType(),
                            QualType());
    return;
  }

  for (unsigned I = 0, E = static_cast<unsigned>(Args.size()); I != E; ++I) {
    if (I < NumParams) {
      QualType ParamTy = FD->getParamDecl(I)->getType().getUnqualifiedType();
      QualType ArgTy =
          Args[I] ? Args[I]->getType().getUnqualifiedType() : ParamTy;
      ImplicitConversionSequence ICS;
      if (Args[I]) {
        // Prefer full standard-conversion analysis when an expression is
        // available; fall back to type-kind ranking for null args.
        ICS = TryImplicitConversion(Args[I], ParamTy, /*CStyle=*/false);
      } else {
        ICS.setStandard();
        ICS.Standard.setAsIdentityConversion();
        ICS.Standard.setFromType(ArgTy);
        ICS.Standard.setAllToTypes(ParamTy);
      }
      if (ICS.isBad()) {
        Cand.Viable = false;
        Cand.FailureKind.setBad(ArgTy, ParamTy);
      }
      Cand.Conversions[I] = ICS;
    } else {
      // Ellipsis conversion for trailing args.
      Cand.Conversions[I].setStandard();
      Cand.Conversions[I].Standard.setAsIdentityConversion();
    }
  }
}

void Sema::AddMethodCandidate(CXXMethodDecl *Method, Expr *Object,
                              ArrayRef<Expr *> Args,
                              OverloadCandidateSet &CandidateSet) {
  (void)Object;
  AddOverloadCandidate(Method, Args, CandidateSet);
}

FunctionDecl *Sema::ResolveAddressOfOverloadedFunction(Expr *Fn,
                                                       QualType TargetType,
                                                       bool Complain,
                                                       DeclAccessPair &Found) {
  (void)Fn;
  (void)TargetType;
  (void)Complain;
  Found = DeclAccessPair();
  return nullptr;
}
