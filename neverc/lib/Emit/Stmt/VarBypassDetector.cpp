#include "Stmt/VarBypassDetector.h"

#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "llvm/Support/Compiler.h"

using namespace neverc;
using namespace Emit;

void VarBypassDetector::Init(const Stmt *Body) {
  FromScopes.clear();
  ToScopes.clear();
  Bypasses.clear();
  Scopes = {{~0U, nullptr}};
  Scopes.reserve(32);
  unsigned ParentScope = 0;
  AlwaysBypassed = !FormScopeInformation(Body, ParentScope);
  if (LLVM_LIKELY(!AlwaysBypassed))
    Detect();
}

bool VarBypassDetector::FormScopeInformation(const Decl *D,
                                             unsigned &ParentScope) {
  const VarDecl *VD = dyn_cast<VarDecl>(D);
  if (VD && VD->hasLocalStorage()) {
    Scopes.push_back({ParentScope, VD});
    ParentScope = Scopes.size() - 1;
  }

  if (const VarDecl *VD = dyn_cast<VarDecl>(D))
    if (const Expr *Init = VD->getInit())
      return FormScopeInformation(Init, ParentScope);

  return true;
}

bool VarBypassDetector::FormScopeInformation(const Stmt *S,
                                             unsigned &origParentScope) {
  // If this is a statement, rather than an expression, scopes within it don't
  // propagate out into the enclosing scope.
  unsigned independentParentScope = origParentScope;
  unsigned &ParentScope =
      ((isa<Expr>(S) && !isa<StmtExpr>(S)) ? origParentScope
                                           : independentParentScope);

  unsigned StmtsToSkip = 0u;

  switch (S->getStmtClass()) {
  case Stmt::IndirectGotoStmtClass:
    return false;

  case Stmt::SwitchStmtClass:
    if (const Stmt *Init = cast<SwitchStmt>(S)->getInit()) {
      if (!FormScopeInformation(Init, ParentScope))
        return false;
      ++StmtsToSkip;
    }
    if (const VarDecl *Var = cast<SwitchStmt>(S)->getConditionVariable()) {
      if (!FormScopeInformation(Var, ParentScope))
        return false;
      ++StmtsToSkip;
    }
    [[fallthrough]];

  case Stmt::GotoStmtClass:
    FromScopes.push_back({S, ParentScope});
    break;

  case Stmt::DeclStmtClass: {
    const DeclStmt *DS = cast<DeclStmt>(S);
    for (auto *I : DS->decls())
      if (!FormScopeInformation(I, origParentScope))
        return false;
    return true;
  }

  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
  case Stmt::LabelStmtClass:
    llvm_unreachable("the loop below handles labels and cases");
    break;

  default:
    break;
  }

  for (const Stmt *SubStmt : S->children()) {
    if (!SubStmt)
      continue;
    if (StmtsToSkip) {
      --StmtsToSkip;
      continue;
    }

    // Cases, labels, and defaults aren't "scope parents".  It's also
    // important to handle these iteratively instead of recursively in
    // order to avoid blowing out the stack.
    while (true) {
      const Stmt *Next;
      if (const SwitchCase *SC = dyn_cast<SwitchCase>(SubStmt))
        Next = SC->getSubStmt();
      else if (const LabelStmt *LS = dyn_cast<LabelStmt>(SubStmt))
        Next = LS->getSubStmt();
      else
        break;

      ToScopes[SubStmt] = ParentScope;
      SubStmt = Next;
    }

    // Recursively walk the AST.
    if (!FormScopeInformation(SubStmt, ParentScope))
      return false;
  }
  return true;
}

void VarBypassDetector::Detect() {
  const unsigned N = FromScopes.size();
  for (unsigned Idx = 0; Idx < N; ++Idx) {
    if (LLVM_LIKELY(Idx + 1 < N))
      __builtin_prefetch(&FromScopes[Idx + 1], 0, 2);
    const Stmt *St = FromScopes[Idx].first;
    const unsigned FromScope = FromScopes[Idx].second;
    if (LLVM_LIKELY(isa<GotoStmt>(St))) {
      const auto *GS = cast<GotoStmt>(St);
      if (const LabelStmt *LS = GS->getLabel()->getStmt()) {
        auto It = ToScopes.find(LS);
        if (LLVM_LIKELY(It != ToScopes.end()))
          Detect(FromScope, It->second);
      }
    } else if (isa<SwitchStmt>(St)) {
      const auto *SS = cast<SwitchStmt>(St);
      for (const SwitchCase *SC = SS->getSwitchCaseList(); SC;
           SC = SC->getNextSwitchCase()) {
        auto It = ToScopes.find(SC);
        if (LLVM_LIKELY(It != ToScopes.end()))
          Detect(FromScope, It->second);
      }
    } else {
      llvm_unreachable("goto or switch was expected");
    }
  }
}

void VarBypassDetector::Detect(unsigned From, unsigned To) {
  if (LLVM_LIKELY(From == To))
    return;
  const auto *ScopeData = Scopes.data();
  while (From != To) {
    if (From < To) {
      assert(ScopeData[To].first < To);
      const VarDecl *VD = ScopeData[To].second;
      unsigned NextTo = ScopeData[To].first;
      if (LLVM_LIKELY(VD != nullptr))
        Bypasses.insert(VD);
      To = NextTo;
    } else {
      assert(ScopeData[From].first < From);
      From = ScopeData[From].first;
    }
  }
}
