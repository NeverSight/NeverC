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
  Decl/DeclParserDecl.cpp
  Decl/DeclParserSpec.cpp
  Decl/DeclParserDeclarator.cpp
  Decl/DeclParserType.cpp
  Decl/DeclParserAttr.cpp

  Expr/ExprParser.cpp
  Expr/ExprParserLiteral.cpp
  Expr/InitParser.cpp

  Stmt/StmtParser.cpp
  Stmt/StmtParserCompound.cpp
  Stmt/StmtParserControl.cpp
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

target_include_directories(nevercSyntax
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
