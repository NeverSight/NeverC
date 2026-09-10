set(LLVM_LINK_COMPONENTS
  Option
  Support
  )

add_neverc_library(nevercRun
  Core/RunDriver.cpp

  DEPENDS
  NeverCDriverOptions

  LINK_LIBS
  nevercDriverOptions
  )

target_include_directories(nevercRun
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
