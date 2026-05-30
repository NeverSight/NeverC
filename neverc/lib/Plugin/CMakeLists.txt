set(LLVM_LINK_COMPONENTS
  Analysis
  CodeGen
  Core
  Support
  TransformUtils
  )

add_neverc_library(nevercPlugin
  BridgeAnalysis.cpp
  BridgeDataStructures.cpp
  BridgeIR.cpp
  BridgeIRBuilder.cpp
  BridgeLinker.cpp
  BridgeMIR.cpp
  BridgeString.cpp
  HostAPIBridge.cpp
  PluginLoader.cpp
  PluginPassAdaptor.cpp

  LINK_LIBS
  nevercFoundation

  DEPENDS
  NeverCDriverOptions
  )

target_include_directories(nevercPlugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
