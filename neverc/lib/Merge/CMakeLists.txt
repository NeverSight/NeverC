set(LLVM_LINK_COMPONENTS
  BinaryFormat
  Object
  Support
  )

add_neverc_library(nevercMerge
  MergerDispatch.cpp
  MergerELF.cpp
  MergerMachO.cpp
  MergerCOFF.cpp

  LINK_LIBS
  )

target_include_directories(nevercMerge
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
