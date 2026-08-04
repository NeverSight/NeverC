set(LLVM_LINK_COMPONENTS
  Support
  )

add_neverc_library(nevercUpdate
  Core/UpdateManager.cpp
  Core/UpdateTransaction.cpp

  LINK_LIBS
  nevercRelease
  nevercRuntime
  )

target_include_directories(nevercUpdate
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
