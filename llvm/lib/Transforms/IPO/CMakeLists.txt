set(LLVMIPO_SOURCES
  AlwaysInliner.cpp
  Annotation2Metadata.cpp
  ArgumentPromotion.cpp
  CalledValuePropagation.cpp
  ConstantMerge.cpp
  DeadArgumentElimination.cpp
  ElimAvailExtern.cpp
  EmbedBitcodePass.cpp
  ForceFunctionAttrs.cpp
  FunctionAttrs.cpp

  GlobalDCE.cpp
  GlobalOpt.cpp
  InferFunctionAttrs.cpp
  Inliner.cpp
  Internalize.cpp
  LowerTypeTests.cpp
  SCCP.cpp

  MSVCMacroRebuilding.cpp
)

set(LLVMIPO_DEPENDS intrinsics_gen)

set(LLVMIPO_LINK_COMPONENTS
  AggressiveInstCombine
  Analysis
  BitReader
  BitWriter
  Core
  InstCombine
  IRReader
  Linker
  Object
  ProfileData
  Scalar
  Support
  TargetParser
  TransformUtils
  Vectorize
  Instrumentation
)

add_llvm_component_library(LLVMipo
  ${LLVMIPO_SOURCES}

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/IPO

  DEPENDS
  ${LLVMIPO_DEPENDS}

  COMPONENT_NAME
  IPO

  LINK_COMPONENTS
  ${LLVMIPO_LINK_COMPONENTS}
  )
