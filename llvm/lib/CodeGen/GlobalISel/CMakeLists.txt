add_llvm_component_library(LLVMGlobalISel
  Utils.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/CodeGen/GlobalISel

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  CodeGen
  CodeGenTypes
  Core
  MC
  Support
  Target
  )
