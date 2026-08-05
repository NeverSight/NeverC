set(LLVM_LINK_COMPONENTS
  Support
  )

add_neverc_library(nevercRun
  Core/RunDriver.cpp
  )

target_include_directories(nevercRun
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
