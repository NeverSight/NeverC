set(LLVM_LINK_COMPONENTS
  MC
  MCParser
  Support
  TargetParser
  )

add_neverc_library(nevercSyntax
  Core/SyntaxParser.cpp
  Core/RunParser.cpp

  Decl/DeclParser.cpp
  Decl/DeclParserSpec.cpp

  Expr/ExprParser.cpp
  Expr/InitParser.cpp

  Stmt/StmtParser.cpp
  Stmt/AsmParser.cpp

  Pragma/PragmaParser.cpp

  LINK_LIBS
  nevercTree
  nevercFoundation
  nevercScan
  nevercAnalyze

  DEPENDS
  NeverCDriverOptions
  )
