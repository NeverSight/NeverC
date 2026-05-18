add_llvm_component_library(LLVMInstrumentation
  CGProfile.cpp
  IndirectCallPromotion.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis
  Core
  MC
  Support
  TargetParser
  TransformUtils
  ProfileData
  )
