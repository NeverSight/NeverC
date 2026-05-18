add_llvm_component_library(LLVMBinaryFormat
  COFF.cpp
  Dwarf.cpp
  MachO.cpp
  Magic.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/BinaryFormat

  LINK_COMPONENTS
  Support
  TargetParser
  )
