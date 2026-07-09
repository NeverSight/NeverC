set(LLVM_LINK_COMPONENTS
  Support
  )

add_neverc_library(nevercBuild
  Core/BuildDriver.cpp
  Core/VariableEnv.cpp
  Core/Function.cpp
  Core/StringUtils.cpp

  Parse/Lexer.cpp
  Parse/Parser.cpp

  Graph/RuleDB.cpp
  Graph/DepGraph.cpp
  Graph/JobScheduler.cpp

  Platform/Platform.cpp

  LINK_LIBS
  nevercFoundation
  )

target_include_directories(nevercBuild
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
