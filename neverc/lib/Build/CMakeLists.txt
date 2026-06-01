set(LLVM_LINK_COMPONENTS
  Support
  )

add_neverc_library(nevercBuild
  BuildDriver.cpp
  Lexer.cpp
  Parser.cpp
  VariableEnv.cpp
  Function.cpp
  RuleDB.cpp
  DepGraph.cpp
  JobScheduler.cpp
  Platform.cpp

  LINK_LIBS
  nevercFoundation
  )
