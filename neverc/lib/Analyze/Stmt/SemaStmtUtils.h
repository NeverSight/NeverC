//===- SemaStmtUtils.h - Shared helpers for split SemaStmt TUs ------------===//
//
// Internal header that exposes helpers originally file-local inside
// SemaStmt.cpp. Splitting statement checking into per-topic TUs
// (Control / Loop / Jump) requires these symbols to be visible across
// translation units.
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_LIB_ANALYZE_STMT_SEMASTMTUTILS_H
#define NEVERC_LIB_ANALYZE_STMT_SEMASTMTUTILS_H

#include "neverc/Analyze/Sema.h"
#include "neverc/Tree/Expr/EvaluatedExprVisitor.h"

namespace neverc {

class CommaVisitor : public EvaluatedExprVisitor<CommaVisitor> {
  typedef EvaluatedExprVisitor<CommaVisitor> Inherited;
  Sema &SemaRef;

public:
  CommaVisitor(Sema &SemaRef) : Inherited(SemaRef.Context), SemaRef(SemaRef) {}
  void VisitBinaryOperator(BinaryOperator *E) {
    if (E->getOpcode() == BO_Comma)
      SemaRef.DiagnoseCommaOperator(E->getLHS(), E->getExprLoc());
    EvaluatedExprVisitor<CommaVisitor>::VisitBinaryOperator(E);
  }
};

} // namespace neverc

#endif // NEVERC_LIB_ANALYZE_STMT_SEMASTMTUTILS_H
