set(LLVM_LINK_COMPONENTS
  Support
  TargetParser
  )

add_neverc_library(nevercScan
  Lexer/SourceScanner.cpp
  Lexer/SourceScannerSpelling.cpp
  Lexer/SourceScannerDecode.cpp
  Lexer/SourceScannerTokens.cpp
  Lexer/SourceScannerComment.cpp
  Lexer/LiteralParser.cpp
  Lexer/LiteralParserNumeric.cpp
  Lexer/TokenScratch.cpp
  Lexer/PasteAvoidance.cpp
  Lexer/ExpansionLexer.cpp

  PP/PrepEngine.cpp
  PP/LexerCore.cpp
  PP/TokenCache.cpp
  PP/EventDispatch.cpp
  PP/DirectiveHandler.cpp
  PP/DirectiveHandlerUtils.cpp
  PP/DirectiveHandlerInclude.cpp
  PP/DirectiveHandlerMacro.cpp
  PP/CondExprEval.cpp
  PP/LexerTransition.cpp
  PP/MacroExpander.cpp
  PP/MacroExpanderIntrinsic.cpp
  PP/PragmaProcessor.cpp
  PP/PragmaProcessorHandlers.cpp

  Macro/MacroArgStorage.cpp
  Macro/MacroRecord.cpp
  Macro/MacroGuardValidator.cpp

  Header/HeaderIndex.cpp
  Header/IncludeResolver.cpp
  Header/IncludeResolverSearch.cpp
  Header/IncludePathSetup.cpp

  LINK_LIBS
  nevercFoundation
  )
