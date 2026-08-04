set(LLVM_LINK_COMPONENTS
  Support
  TargetParser
  )

add_neverc_library(nevercRelease
  Core/ReleaseClient.cpp
  )

target_include_directories(nevercRelease
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
