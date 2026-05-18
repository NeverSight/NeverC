#include "neverc/Analyze/ScopeInfo.h"

using namespace neverc;
using namespace sema;

void FunctionScopeInfo::Clear() {
  HasBranchProtectedScope = false;
  HasBranchIntoScope = false;
  HasIndirectGoto = false;
  HasDroppedStmt = false;
  HasFallthroughStmt = false;
  UsesFPIntrin = false;
  HasPotentialAvailabilityViolations = false;
  FirstReturnLoc = SourceLocation();
  FirstSEHTryLoc = SourceLocation();

  SwitchStack.clear();
  Returns.clear();
  ErrorTrap.reset();
  PossiblyUnreachableDiags.clear();
  ModifiedNonNullParams.clear();
  AddrLabels.clear();
}

FunctionScopeInfo::~FunctionScopeInfo() {}
