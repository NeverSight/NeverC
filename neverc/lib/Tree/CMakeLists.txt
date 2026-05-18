set(LLVM_LINK_COMPONENTS
  BinaryFormat
  Core
  Support
  TargetParser
  )

set(AST_INTERP_SOURCES
  Interp/State.cpp
)

add_neverc_library(nevercTree
  Core/TreeConsumer.cpp
  Core/TreeContext.cpp
  Core/TreeDiag.cpp
  Core/APValue.cpp
  Core/AttrImpl.cpp
  Core/DependCalc.cpp
  Core/Mangle.cpp

  Decl/Decl.cpp
  Decl/DeclBase.cpp
  Decl/DeclC.cpp
  Decl/DeclPrinter.cpp
  Decl/DeclarationName.cpp

  Expr/Expr.cpp
  Expr/ExprClassification.cpp
  Expr/ConstEval.cpp

  Stmt/Stmt.cpp
  Stmt/StmtPrinter.cpp
  Stmt/StmtProfile.cpp

  Type/Type.cpp
  Type/TypeLoc.cpp
  Type/TypePrinter.cpp
  Type/StructLayoutBuilder.cpp

  Format/FormatString.cpp
  Format/PrintfFormatString.cpp
  Format/ScanfFormatString.cpp

  ${AST_INTERP_SOURCES}

  LINK_LIBS
  nevercFoundation
  nevercScan

  DEPENDS
  NeverCDriverOptions
  intrinsics_gen
  )

target_include_directories(nevercTree
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
