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
set(_neverc_shellcode_tables
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
  ShellcodeInternalRuntimePrefixes
  ShellcodeIncompats
  ShellcodeInjectFlags
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
  "${NEVERC_BINARY_DIR}/include/neverc/Shellcode/Tables")
file(MAKE_DIRECTORY "${_neverc_tables_out_dir}")

foreach(_t IN LISTS _neverc_shellcode_tables)
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
unset(_neverc_shellcode_tables)

# E2E test-only plugin (OFF by default).  Compile-time gated; at runtime
# the plugin also checks NEVERC_ENABLE_SHELLCODE_TEST_PLUGIN=1 in env.
option(NEVERC_ENABLE_SHELLCODE_TEST_PLUGIN
  "Compile the test plugin into nevercShellcode for e2e plugin SDK tests."
  OFF)

set(_neverc_shellcode_test_plugin_sources)
if(NEVERC_ENABLE_SHELLCODE_TEST_PLUGIN)
  list(APPEND _neverc_shellcode_test_plugin_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/neverc/shellcode/test_plugin/TestShellcodePlugin.cpp)
  # Directory-scoped so the macro reaches every TU in this CMakeLists.
  add_definitions(-DNEVERC_HAVE_SHELLCODE_TEST_PLUGIN=1)
endif()

add_neverc_library(nevercShellcode
  Pipeline/DriverIntegration.cpp
  Pipeline/Pipeline.cpp
  Pipeline/Plugins.cpp
  Pipeline/TargetDesc.cpp

  ${_neverc_shellcode_test_plugin_sources}

  Extractor/COFFExtractor.cpp
  Extractor/ELFExtractor.cpp
  Extractor/ExtractorCommon.cpp
  Extractor/MachOExtractor.cpp
  Extractor/ShellcodeExtractor.cpp

  IR/AllBlrPass.cpp
  IR/CompilerRtPass.cpp
  IR/Data2TextPass.cpp
  IR/ExternRewriter.cpp
  IR/HeapArenaPass.cpp
  IR/IndirectBrPass.cpp
  IR/MemIntrinPass.cpp
  IR/StringRuntimePass.cpp
  IR/ZeroRelocPass.cpp

  MIR/MIRPrepPass.cpp

  Import/KernelImportPass.cpp
  Import/SyscallStub.cpp
  Import/SyscallTables.cpp
  Import/WinImportTables.cpp
  Import/WinPEBImport.cpp

  LINK_LIBS
  nevercFoundation
  nevercPlugin

  DEPENDS
  NeverCDriverOptions
  )

target_include_directories(nevercShellcode
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Extractor
  )

