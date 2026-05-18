add_llvm_component_library(LLVMLTO
  LTO.cpp
  LTOBackend.cpp


  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/LTO
  DEPENDS
  intrinsics_gen
  llvm_vcsrevision_h

  LINK_COMPONENTS
  AggressiveInstCombine
  Analysis
  BinaryFormat
  BitReader
  BitWriter
  CodeGen
  CodeGenTypes
  Core
  IPO
  InstCombine
  Instrumentation
  Linker
  MC
  Object
  Passes
  Remarks
  Scalar
  Support
  Target
  TargetParser
  TransformUtils
  )

