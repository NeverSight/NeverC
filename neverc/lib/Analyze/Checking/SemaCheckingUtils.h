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
#include "neverc/Foundation/Builtin/TargetBuiltins.h"
#include "neverc/Tree/Expr/Expr.h"

bool CheckNonNullExpr(::neverc::Sema &S, const ::neverc::Expr *Expr);
bool isNonNullType(::neverc::QualType type);

bool AnalyzeBitFieldAssignment(::neverc::Sema &S, ::neverc::FieldDecl *Bitfield,
                               ::neverc::Expr *Init,
                               ::neverc::SourceLocation InitLoc);

bool checkArgCountAtLeast(::neverc::Sema &S, ::neverc::CallExpr *Call,
                          unsigned MinArgCount);
bool checkArgCountAtMost(::neverc::Sema &S, ::neverc::CallExpr *Call,
                         unsigned MaxArgCount);
bool checkArgCountRange(::neverc::Sema &S, ::neverc::CallExpr *Call,
                        unsigned MinArgCount, unsigned MaxArgCount);
bool checkArgCount(::neverc::Sema &S, ::neverc::CallExpr *Call,
                   unsigned DesiredArgCount);
bool convertArgumentToType(::neverc::Sema &S, ::neverc::Expr *&Value,
                           ::neverc::QualType Ty);
bool checkBuiltinArgument(::neverc::Sema &S, ::neverc::CallExpr *E,
                          unsigned ArgIndex);

// Individual builtin semantic checks (SemaCheckingBuiltin.cpp)
bool semaBuiltinAnnotation(::neverc::Sema &S, ::neverc::CallExpr *TheCall);
::neverc::ExprResult semaBuiltinNeverCXorstr(::neverc::Sema &S,
                                             ::neverc::CallExpr *TheCall);
bool semaBuiltinMSVCAnnotation(::neverc::Sema &S, ::neverc::CallExpr *TheCall);
bool semaBuiltinFunctionStart(::neverc::Sema &S, ::neverc::CallExpr *TheCall);
bool semaBuiltinPreserveAI(::neverc::Sema &S, ::neverc::CallExpr *TheCall);
bool semaBuiltinAlignment(::neverc::Sema &S, ::neverc::CallExpr *TheCall,
                           unsigned ID);
bool semaBuiltinOverflow(::neverc::Sema &S, ::neverc::CallExpr *TheCall,
                          unsigned BuiltinID);
::neverc::ExprResult semaBuiltinDumpStruct(::neverc::Sema &S,
                                           ::neverc::CallExpr *TheCall);
bool semaBuiltinCallWithStaticChain(::neverc::Sema &S,
                                     ::neverc::CallExpr *BuiltinCall);

// Used by SemaCheckingAtomic.cpp
void checkNonNullArgument(::neverc::Sema &S, const ::neverc::Expr *ArgExpr,
                          ::neverc::SourceLocation CallSiteLoc);

// Used by SemaCheckingDiag.cpp
bool checkMathBuiltinElementType(::neverc::Sema &S,
                                  ::neverc::SourceLocation Loc,
                                  ::neverc::QualType Ty);
bool checkFPMathBuiltinElementType(::neverc::Sema &S,
                                    ::neverc::SourceLocation Loc,
                                    ::neverc::QualType Ty, int ArgIndex);

// Used by SemaCheckingTarget.cpp
::neverc::QualType getNeonEltType(::neverc::NeonTypeFlags Flags,
                                  ::neverc::TreeContext &Context,
                                  bool IsPolyUnsigned, bool IsInt64Long);

#endif // NEVERC_LIB_SEMA_SEMACHECKINGUTILS_H
