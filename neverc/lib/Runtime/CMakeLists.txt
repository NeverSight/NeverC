set(LLVM_LINK_COMPONENTS
  Support
  )

add_neverc_library(nevercRuntime
  Core/RuntimeManager.cpp
  )

target_include_directories(nevercRuntime
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
