set(LLVM_LINK_COMPONENTS
  CodeGen
  Core
  IRReader
  IPO
  MC
  Object
  Option
  Passes
  Support
  TargetParser
  TransformUtils
  )

# User-extensible .def tables: each bundled table gets a companion
# UserExtra_<Stem>.def generated into the build tree.  Point
# -DNEVERC_EXTRA_<Stem>=<path> at a .def file to append rows.
set(_neverc_dyncode_tables
  Syscalls_Darwin
  Syscalls_LinuxArm64
  Syscalls_LinuxX86_64
  SyscallLikelyNames
  Win32Apis
  PosixCanonicalTypes
  PosixToWin32Alt
  ReservedMemStdlibNames
  ComplexLibcallNames
  LibmTranscendentalNames
  StdioCallNames
  ScalarSoftFloatNames
  LongIntegerHelperPrefixes
  CompilerGeneratedExternHints
  StackProbeNames
  Binary128HelperNames
  HeapAllocatorNames
  HeapArenaRewriteTargets
  StringRuntimeAllocatorNames
  LibcInlineHelpers
  SetjmpNames
  KernelHelperNames
  KernelImportReservedPrefixes
  MIRRewritePatterns
  MIRRewriteOpcodes
  MIRStripPseudoOpcodes
  Win32PosixCompat
  DefaultEntryNames
  DynCodeInternalRuntimePrefixes
  DynCodeIncompats
  DynCodeInjectFlags
  TargetInjectFlags_Unix_X86_64
  TargetInjectFlags_Unix_AArch64
  TargetInjectFlags_Windows_X86_64
  TargetInjectFlags_Windows_AArch64
  TargetKernelFlags_Unix_X86_64
  TargetKernelFlags_Unix_AArch64
  TargetKernelFlags_Windows_X86_64
  TargetKernelFlags_Windows_AArch64
  )

set(_neverc_tables_out_dir
  "${NEVERC_BINARY_DIR}/include/neverc/DynCode/Tables")
file(MAKE_DIRECTORY "${_neverc_tables_out_dir}")

foreach(_t IN LISTS _neverc_dyncode_tables)
  set(_var "NEVERC_EXTRA_${_t}")
  set(${_var} "" CACHE FILEPATH
    "Optional .def file appended to the bundled ${_t}.def table.")
  set(_out "${_neverc_tables_out_dir}/UserExtra_${_t}.def")
  if(NOT "${${_var}}" STREQUAL "" AND EXISTS "${${_var}}")
    configure_file("${${_var}}" "${_out}" COPYONLY)
  else()
    file(WRITE "${_out}" "")
  endif()
endforeach()

unset(_neverc_tables_out_dir)
unset(_neverc_dyncode_tables)

add_neverc_library(nevercDynCode
  Pipeline/BuiltinMIRTransformProvider.cpp
  Pipeline/DriverIntegration.cpp
  Pipeline/DynCodeMIRVerifier.cpp
  Pipeline/ExternalReferenceLedger.cpp
  Pipeline/Pipeline.cpp
  Pipeline/TargetDesc.cpp

  Extractor/COFFExtractor.cpp
  Extractor/DynCodeExtractionPlan.cpp
  Extractor/DynCodeImage.cpp
  Extractor/DynCodeReport.cpp
  Extractor/ELFExtractor.cpp
  Extractor/ExtractorCommon.cpp
  Extractor/MachOExtractor.cpp
  Extractor/DynCodeExtractor.cpp

  IR/AllBlrPass.cpp
  IR/CompilerRtPass.cpp
  IR/Data2TextPass.cpp
  IR/DynCodeIRStageSupport.cpp
  IR/DynCodeIRVerifier.cpp
  IR/DynCodePreparePass.cpp
  IR/ExternRewriter.cpp
  IR/HeapArenaPass.cpp
  IR/IndirectBrPass.cpp
  IR/MemIntrinPass.cpp
  IR/StackifyPass.cpp
  IR/StringRuntimePass.cpp
  IR/ZeroRelocPass.cpp

  Import/KernelImportPass.cpp
  Import/SyscallStub.cpp
  Import/SyscallTables.cpp
  Import/WinImportTables.cpp
  Import/WinPEBImport.cpp

  Plugin/DynCodeRequest.cpp
  Plugin/DynCodeRegistry.cpp
  Plugin/DynCodeImportRegistry.cpp
  Plugin/DynCodeTargetRegistry.cpp
  Plugin/DynCodePhaseRegistry.cpp
  Plugin/DynCodePhaseExecutor.cpp
  Plugin/DynCodePhaseCAPI.cpp

  LINK_LIBS
  nevercFoundation
  nevercPluginCore
  nevercPluginTarget

  DEPENDS
  NeverCDriverOptions
  )

target_include_directories(nevercDynCode
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )

