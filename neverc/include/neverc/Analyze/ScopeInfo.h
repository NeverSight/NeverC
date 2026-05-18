#ifndef NEVERC_SEMA_SCOPEINFO_H
#define NEVERC_SEMA_SCOPEINFO_H

#include "neverc/Analyze/CleanupInfo.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Diagnostic/PartialDiagnostic.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

namespace neverc {

using llvm::isa;

class ParmVarDecl;
class ReturnStmt;
class Stmt;
class SwitchStmt;

namespace sema {

class CompoundScopeInfo {
public:
  bool HasEmptyLoopBodies = false;

  bool IsStmtExpr;

  FPOptions InitialFPFeatures;

  CompoundScopeInfo(bool IsStmtExpr, FPOptions FPO)
      : IsStmtExpr(IsStmtExpr), InitialFPFeatures(FPO) {}

  void setHasEmptyLoopBodies() { HasEmptyLoopBodies = true; }
};

class PossiblyUnreachableDiag {
public:
  PartialDiagnostic PD;
  SourceLocation Loc;
  llvm::TinyPtrVector<const Stmt *> Stmts;

  PossiblyUnreachableDiag(const PartialDiagnostic &PD, SourceLocation Loc,
                          llvm::ArrayRef<const Stmt *> Stmts)
      : PD(PD), Loc(Loc), Stmts(Stmts) {}
};

class FunctionScopeInfo {
public:
  bool HasBranchProtectedScope : 1;

  bool HasBranchIntoScope : 1;

  bool HasIndirectGoto : 1;

  bool HasMustTail : 1;

  bool HasDroppedStmt : 1;

  bool HasFallthroughStmt : 1;

  bool UsesFPIntrin : 1;

  bool HasPotentialAvailabilityViolations : 1;

  SourceLocation FirstReturnLoc;

  SourceLocation FirstSEHTryLoc;

private:
  DiagnosticErrorTrap ErrorTrap;

public:
  using SwitchInfo = llvm::PointerIntPair<SwitchStmt *, 1, bool>;

  llvm::SmallVector<SwitchInfo, 8> SwitchStack;

  llvm::SmallVector<ReturnStmt *, 4> Returns;

  llvm::SmallVector<CompoundScopeInfo, 4> CompoundScopes;

  llvm::SmallVector<PossiblyUnreachableDiag, 4> PossiblyUnreachableDiags;

  llvm::SmallPtrSet<const ParmVarDecl *, 8> ModifiedNonNullParams;

  llvm::SmallVector<AddrLabelExpr *, 4> AddrLabels;

public:
protected:
  FunctionScopeInfo(const FunctionScopeInfo &) = default;

public:
  FunctionScopeInfo(DiagnosticsEngine &Diag)
      : HasBranchProtectedScope(false), HasBranchIntoScope(false),
        HasIndirectGoto(false), HasMustTail(false), HasDroppedStmt(false),
        HasFallthroughStmt(false), UsesFPIntrin(false),
        HasPotentialAvailabilityViolations(false), ErrorTrap(Diag) {}

  virtual ~FunctionScopeInfo();

  bool hasUnrecoverableErrorOccurred() const {
    return ErrorTrap.hasUnrecoverableErrorOccurred();
  }

  void setHasBranchIntoScope() { HasBranchIntoScope = true; }

  void setHasBranchProtectedScope() { HasBranchProtectedScope = true; }

  void setHasIndirectGoto() { HasIndirectGoto = true; }

  void setHasMustTail() { HasMustTail = true; }

  void setHasDroppedStmt() { HasDroppedStmt = true; }

  void setHasFallthroughStmt() { HasFallthroughStmt = true; }

  void setUsesFPIntrin() { UsesFPIntrin = true; }

  void setHasSEHTry(SourceLocation TryLoc) {
    setHasBranchProtectedScope();
    FirstSEHTryLoc = TryLoc;
  }

  void setHasVLA(SourceLocation VLALoc) {
    (void)VLALoc;
    setHasBranchProtectedScope();
  }

  bool NeedsScopeChecking() const {
    return !HasDroppedStmt && (HasIndirectGoto || HasMustTail ||
                               (HasBranchProtectedScope && HasBranchIntoScope));
  }

  void Clear();
};

} // namespace sema

} // namespace neverc

#endif // NEVERC_SEMA_SCOPEINFO_H
