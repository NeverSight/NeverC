add_llvm_component_library(LLVMRemarks
  BitstreamRemarkSerializer.cpp
  RemarkFormat.cpp
  RemarkSerializer.cpp
  RemarkStreamer.cpp
  RemarkStringTable.cpp
  YAMLRemarkSerializer.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Remarks

  LINK_COMPONENTS
  BitstreamReader
  Support

  DEPENDS
  intrinsics_gen
)
