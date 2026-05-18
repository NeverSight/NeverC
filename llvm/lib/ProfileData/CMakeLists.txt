add_llvm_component_library(LLVMProfileData
  InstrProf.cpp
  ProfileSummaryBuilder.cpp
  SampleProf.cpp
  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ProfileData

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Core
  Object
  Support
  DebugInfoDWARF
  TargetParser
  )
