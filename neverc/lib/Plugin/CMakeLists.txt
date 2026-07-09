set(LLVM_LINK_COMPONENTS
  Analysis
  CodeGen
  Core
  Support
  TransformUtils
  )

add_neverc_library(nevercPlugin
  Core/PluginLoader.cpp
  Core/PluginPassAdaptor.cpp

  Host/HostAPIBridge.cpp

  Bridge/BridgeAnalysis.cpp
  Bridge/BridgeDataStructures.cpp
  Bridge/BridgeIR.cpp
  Bridge/BridgeIRBuilder.cpp
  Bridge/BridgeLinker.cpp
  Bridge/BridgeMIR.cpp
  Bridge/BridgeString.cpp

  LINK_LIBS
  nevercFoundation

  DEPENDS
  NeverCDriverOptions
  )

target_include_directories(nevercPlugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
