set(LLVM_LINK_COMPONENTS
  BitReader
  BitstreamReader
  MC
  Option
  Support
  Target
  TargetParser
  )

set(NEVERC_COMPILER_SOURCES
  Core/CompilerInstance.cpp
  Core/CompilerInvocation.cpp
  Core/CompilerInvocationCodeGen.cpp
  Core/CompilerInvocationLang.cpp
  Core/ExecuteCompilerInvocation.cpp
  Core/FrontendOptions.cpp

  Action/FrontendAction.cpp
  Action/FrontendActions.cpp

  Diagnostic/DiagnosticRenderer.cpp
  Diagnostic/TextDiagnostic.cpp
  Diagnostic/TextDiagnosticSnippet.cpp
  Diagnostic/TextDiagnosticBuffer.cpp
  Diagnostic/TextDiagnosticPrinter.cpp

  Consumer/MultiplexConsumer.cpp

  Preprocessor/InitPreprocessor.cpp
  Preprocessor/InitPredefinedMacros.cpp
  Preprocessor/PrintPreprocessedOutput.cpp
  Preprocessor/PrintPPOutputPragmas.cpp
  Preprocessor/PrintPPOutputTokens.cpp
  Preprocessor/HeaderIncludeGen.cpp
  Preprocessor/DependencyFile.cpp
  )

add_neverc_library(nevercCompiler
  ${NEVERC_COMPILER_SOURCES}

  DEPENDS
  NeverCDriverOptions
  intrinsics_gen

  LINK_LIBS
  nevercTree
  nevercFoundation
  nevercInvoke
  nevercPluginCore
  nevercPluginIO
  nevercPluginTarget
  nevercScan
  nevercSyntax
  nevercAnalyze
  )

target_include_directories(nevercCompiler
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
