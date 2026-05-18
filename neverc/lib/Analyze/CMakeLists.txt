set(LLVM_LINK_COMPONENTS
  Core
  MC
  Support
  TargetParser
  )

add_neverc_library(nevercAnalyze
  Core/Sema.cpp
  Core/SemaC.cpp
  Core/Scope.cpp
  Core/ScopeInfo.cpp
  Core/SemaLookup.cpp
  Core/IdentifierResolver.cpp
  Core/DelayedDiagnostic.cpp
  Core/SemaFixItUtils.cpp

  Decl/SemaDecl.cpp
  Decl/SemaDeclAttr.cpp
  Decl/DeclSpec.cpp
  Decl/ParsedAttr.cpp
  Decl/SemaAttr.cpp
  Decl/SemaAvailability.cpp

  Expr/SemaExpr.cpp
  Expr/SemaExprMember.cpp
  Expr/SemaNeverCString.cpp
  Expr/SemaCast.cpp
  Expr/SemaConversions.cpp
  Expr/SemaInit.cpp

  Stmt/SemaStmt.cpp
  Stmt/SemaStmtAsm.cpp
  Stmt/SemaStmtAttr.cpp
  Stmt/JumpDiagnostics.cpp

  Checking/SemaChecking.cpp
  Checking/SemaCheckingFormat.cpp
  Checking/SemaCheckingMemory.cpp
  Checking/SemaCheckingNumeric.cpp
  Checking/SemaCheckingStmt.cpp

  Type/SemaType.cpp
  Type/TypeLocBuilder.cpp

  DEPENDS
  NeverCDriverOptions

  LINK_LIBS
  nevercTree
  nevercFoundation
  nevercScan
  )

target_include_directories(nevercAnalyze
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
