set(LLVM_LINK_COMPONENTS
  BinaryFormat
  DebugInfoDWARF
  Object
  Support
  )

add_neverc_library(nevercMerge
  Core/MergerDispatch.cpp

  Common/DwarfRebase.cpp

  ELF/MergerELF.cpp
  MachO/MergerMachO.cpp
  COFF/MergerCOFF.cpp

  Verify/MergerVerify.cpp
  )

target_include_directories(nevercMerge
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
