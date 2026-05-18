//===- SemaCheckingUtils.h - Shared helpers for split SemaChecking TUs ----===//
//
// Internal header that exposes a small set of helpers originally defined as
// file-scope ``static`` functions inside SemaChecking.cpp. Splitting the
// translation unit into per-topic files (SemaCheckingFormat / Memory / Numeric
// / Stmt) requires these helpers to be visible across the resulting TUs.
//
// Functions live in the global namespace to match their original definitions
// (only the ``static`` storage class was removed).

#ifndef NEVERC_LIB_SEMA_SEMACHECKINGUTILS_H
#define NEVERC_LIB_SEMA_SEMACHECKINGUTILS_H

#include "neverc/Analyze/Sema.h"
#include "neverc/Tree/Expr/Expr.h"

bool CheckNonNullExpr(::neverc::Sema &S, const ::neverc::Expr *Expr);
bool isNonNullType(::neverc::QualType type);

bool AnalyzeBitFieldAssignment(::neverc::Sema &S, ::neverc::FieldDecl *Bitfield,
                               ::neverc::Expr *Init,
                               ::neverc::SourceLocation InitLoc);

#endif // NEVERC_LIB_SEMA_SEMACHECKINGUTILS_H
