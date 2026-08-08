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
  Platform/BuiltinCommands.cpp
  Platform/Builtins/Common.cpp
  Platform/Builtins/FileCommands.cpp
  Platform/Builtins/TextCommands.cpp
  Platform/Builtins/PathCommands.cpp
  Platform/Builtins/LogicCommands.cpp
  Platform/Builtins/HashCommands.cpp
  Platform/Builtins/SystemCommands.cpp

  LINK_LIBS
  nevercFoundation
  )

target_include_directories(nevercBuild
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
