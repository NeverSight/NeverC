set(LLVM_LINK_COMPONENTS
  BitReader
  BitstreamReader
  MC
  Option
  Support
  Target
  TargetParser
  )

set(NEVERC_FRONTEND_SOURCES
  Compiler/CompilerInstance.cpp
  Compiler/CompilerInvocation.cpp
  Compiler/FrontendOptions.cpp

  Action/FrontendAction.cpp
  Action/FrontendActions.cpp

  Diagnostic/DiagnosticRenderer.cpp
  Diagnostic/TextDiagnostic.cpp
  Diagnostic/TextDiagnosticBuffer.cpp
  Diagnostic/TextDiagnosticPrinter.cpp
  Consumer/MultiplexConsumer.cpp

  Preprocessor/InitPreprocessor.cpp
  Preprocessor/PrintPreprocessedOutput.cpp
  Preprocessor/HeaderIncludeGen.cpp
  Preprocessor/DependencyFile.cpp

  Compiler/ExecuteCompilerInvocation.cpp
  )


add_neverc_library(nevercCompiler
  ${NEVERC_FRONTEND_SOURCES}

  DEPENDS
  NeverCDriverOptions
  intrinsics_gen

  LINK_LIBS
  nevercTree
  nevercFoundation
  nevercInvoke
  nevercScan
  nevercSyntax
  nevercAnalyze
  )
