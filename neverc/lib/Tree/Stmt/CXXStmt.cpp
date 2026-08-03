//===- CXXStmt.cpp - C++ statement AST helpers ----------------------------===//
#include "neverc/Tree/Stmt/Stmt.h"
#include "neverc/Tree/Core/TreeContext.h"

using namespace neverc;

CXXTryStmt *CXXTryStmt::Create(const TreeContext &C, SourceLocation TryLoc,
                               Stmt *TryBlock, ArrayRef<Stmt *> Handlers) {
  unsigned N = static_cast<unsigned>(Handlers.size());
  // Always allocate try-block slot + handlers so getTryBlock() works.
  void *Mem = C.Allocate(sizeof(Stmt *) * (N + 1), alignof(Stmt *));
  Stmt **Stmts = static_cast<Stmt **>(Mem);
  Stmts[0] = TryBlock;
  for (unsigned I = 0; I < N; ++I)
    Stmts[I + 1] = Handlers[I];
  return new (C) CXXTryStmt(TryLoc, Stmts, N);
}
