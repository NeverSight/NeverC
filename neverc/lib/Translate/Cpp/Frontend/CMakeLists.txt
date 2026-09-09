# This directory is added only by the private upstream LLVM CMake build.
if(NOT PACKAGE_VERSION VERSION_EQUAL "20.1.8" OR
   NOT DEFINED NEVERC_CPP_REPOSITORY_ROOT)
  message(FATAL_ERROR "The builtin C++ frontend requires its isolated LLVM 20.1.8 build")
endif()
# The builtin frontend uses ClangTool directly. It does not use Tooling's
# command-line executor registry or Support's balanced-partitioning algorithm.
# Keep this change in the private build, and recheck the source/target layout
# when updating the upstream pin instead of filtering arbitrary file names.
get_filename_component(_neverc_cpp_upstream "${LLVM_MAIN_SRC_DIR}" DIRECTORY)
foreach(_source_pin IN ITEMS
    "clang/lib/Tooling/CMakeLists.txt|7f0160cd9c0c03b89e9ac57ce9921b364e92912afcd7d549ed26e14406a95b25"
    "llvm/lib/Support/CMakeLists.txt|433969af272d535f2616bb7513677b9c99b1a9d61572d8e933e624250f2770ca"
    "clang/cmake/modules/AddClang.cmake|34e3b3cf152fe9a6692d05e82b9d14b4b3d377624cf4ab79b33824c23075006d"
    "llvm/cmake/modules/AddLLVM.cmake|e8bcd0649e978d2c26ad0484e081fbac3fd30c6addfd0ec60f807f2b6b8d322b")
  string(REPLACE "|" ";" _source_pin_parts "${_source_pin}")
  list(GET _source_pin_parts 0 _source_pin_path)
  list(GET _source_pin_parts 1 _source_pin_hash)
  file(SHA256 "${_neverc_cpp_upstream}/${_source_pin_path}" _source_hash)
  if(NOT _source_hash STREQUAL _source_pin_hash)
    message(FATAL_ERROR
      "Changed LLVM 20.1.8 source layout for builtin C++: ${_source_pin_path}")
  endif()
endforeach()

function(_neverc_cpp_remove_sources target expected_type source_directory)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "Missing private C++ source owner: ${target}")
  endif()
  get_target_property(_type "${target}" TYPE)
  get_target_property(_directory "${target}" SOURCE_DIR)
  get_target_property(_sources "${target}" SOURCES)
  file(REAL_PATH "${source_directory}" _expected_directory)
  file(REAL_PATH "${_directory}" _actual_directory)
  if(NOT _type STREQUAL expected_type OR
     NOT _actual_directory STREQUAL _expected_directory OR NOT _sources)
    message(FATAL_ERROR "Changed private C++ source owner: ${target}")
  endif()
  set(_object_source "")
  set(_object_matches 0)
  if(target STREQUAL "LLVMSupport")
    if(NOT TARGET LLVMSupportBlake3)
      message(FATAL_ERROR "Missing private LLVMSupportBlake3 object library")
    endif()
    get_target_property(_blake_type LLVMSupportBlake3 TYPE)
    get_target_property(_blake_directory LLVMSupportBlake3 SOURCE_DIR)
    file(REAL_PATH "${_blake_directory}" _blake_directory)
    file(REAL_PATH "${source_directory}/BLAKE3" _expected_blake_directory)
    if(NOT _blake_type STREQUAL "OBJECT_LIBRARY" OR
       NOT _blake_directory STREQUAL _expected_blake_directory)
      message(FATAL_ERROR "Changed private LLVMSupportBlake3 object-library layout")
    endif()
    set(_object_source "$<TARGET_OBJECTS:LLVMSupportBlake3>")
  endif()
  foreach(_source IN LISTS _sources)
    if(_source MATCHES "\\$<")
      if(_object_source AND _source STREQUAL _object_source)
        math(EXPR _object_matches "${_object_matches} + 1")
      else()
        message(FATAL_ERROR
          "Unexpected generator expression in private C++ source owner ${target}: ${_source}")
      endif()
    endif()
  endforeach()
  if(_object_source AND NOT _object_matches EQUAL 1)
    message(FATAL_ERROR "Expected exactly one private LLVMSupportBlake3 object reference")
  endif()
  foreach(_name IN LISTS ARGN)
    if(NOT EXISTS "${source_directory}/${_name}")
      message(FATAL_ERROR "Missing pinned private C++ source: ${_name}")
    endif()
    file(REAL_PATH "${source_directory}/${_name}" _expected_source)
    set(_matches 0)
    set(_remaining)
    foreach(_source IN LISTS _sources)
      if(_object_source AND _source STREQUAL _object_source)
        list(APPEND _remaining "${_source}")
        continue()
      endif()
      file(REAL_PATH "${_source}" _actual_source BASE_DIRECTORY "${_directory}")
      if(_actual_source STREQUAL _expected_source)
        math(EXPR _matches "${_matches} + 1")
      else()
        list(APPEND _remaining "${_source}")
      endif()
    endforeach()
    if(NOT _matches EQUAL 1)
      message(FATAL_ERROR
        "Expected exactly one ${_name} in private C++ owner ${target}; found ${_matches}")
    endif()
    set(_sources "${_remaining}")
  endforeach()
  set_property(TARGET "${target}" PROPERTY SOURCES "${_sources}")
endfunction()

if(TARGET obj.clangTooling)
  get_target_property(_tooling_type clangTooling TYPE)
  get_target_property(_tooling_sources clangTooling SOURCES)
  if(NOT _tooling_type STREQUAL "STATIC_LIBRARY" OR
     NOT _tooling_sources STREQUAL "$<TARGET_OBJECTS:obj.clangTooling>")
    message(FATAL_ERROR "Changed private clangTooling object-library layout")
  endif()
  set(_tooling_source_owner obj.clangTooling)
  set(_tooling_source_type OBJECT_LIBRARY)
elseif(CMAKE_GENERATOR STREQUAL "Xcode" OR
       CMAKE_GENERATOR MATCHES "^Visual Studio ")
  set(_tooling_source_owner clangTooling)
  set(_tooling_source_type STATIC_LIBRARY)
else()
  message(FATAL_ERROR "Missing private clangTooling object-library owner")
endif()
# Execution.cpp's registry anchors require both executor implementations. Drop
# all three together, preserving the unrelated LLVMInterpreter/Execution.cpp.
_neverc_cpp_remove_sources("${_tooling_source_owner}" "${_tooling_source_type}"
  "${_neverc_cpp_upstream}/clang/lib/Tooling"
  AllTUsExecution.cpp Execution.cpp StandaloneExecution.cpp)
# BPFunctionNode's constructor remains inline in its header. The bundled
# InstrProf code only constructs/sorts nodes; llvm-profdata and LLD, which call
# the algorithm, are not built or linked into this private frontend bundle.
_neverc_cpp_remove_sources(LLVMSupport STATIC_LIBRARY
  "${LLVM_MAIN_SRC_DIR}/lib/Support" BalancedPartitioning.cpp)

# These upstream command-line options have external linkage outside namespace
# llvm. Keep their definitions and references private without changing upstream
# source files. The archive audit also rejects the original global names.
set_property(SOURCE "${LLVM_MAIN_SRC_DIR}/lib/Analysis/BranchProbabilityInfo.cpp"
  DIRECTORY "${LLVM_MAIN_SRC_DIR}/lib/Analysis" APPEND PROPERTY COMPILE_DEFINITIONS
  PrintBranchProbFuncName=neverc_cpp_PrintBranchProbFuncName)
set_property(SOURCE "${LLVM_MAIN_SRC_DIR}/lib/Analysis/MemoryBuiltins.cpp"
  DIRECTORY "${LLVM_MAIN_SRC_DIR}/lib/Analysis" APPEND PROPERTY COMPILE_DEFINITIONS
  mangledNameForMallocFamily=neverc_cpp_mangledNameForMallocFamily)
set_property(SOURCE "${LLVM_MAIN_SRC_DIR}/lib/Bitcode/Reader/BitcodeReader.cpp"
  DIRECTORY "${LLVM_MAIN_SRC_DIR}/lib/Bitcode/Reader" APPEND PROPERTY COMPILE_DEFINITIONS
  deserializeSanitizerMetadata=neverc_cpp_deserializeSanitizerMetadata)
foreach(_file ModuleSummaryAnalysis.cpp ProfileSummaryInfo.cpp)
  set_property(SOURCE "${LLVM_MAIN_SRC_DIR}/lib/Analysis/${_file}"
    DIRECTORY "${LLVM_MAIN_SRC_DIR}/lib/Analysis" APPEND PROPERTY COMPILE_DEFINITIONS
    ScalePartialSampleProfileWorkingSetSize=neverc_cpp_ScalePartialSampleProfileWorkingSetSize)
endforeach()
if(APPLE)
  # The Darwin crash-reporter annotation uses an assembler spelling that a
  # namespace/C identifier macro cannot rename. The host owns process metadata;
  # keep the private LLVM's stack formatting without a second OS annotation.
  set_property(SOURCE "${LLVM_MAIN_SRC_DIR}/lib/Support/PrettyStackTrace.cpp"
    DIRECTORY "${LLVM_MAIN_SRC_DIR}/lib/Support" APPEND PROPERTY COMPILE_OPTIONS
    -include "${CMAKE_CURRENT_SOURCE_DIR}/NoCrashReporter.h")
endif()
set(_builtin_sdk "${NEVERC_CPP_REPOSITORY_ROOT}/neverc/lib/Translate/Cpp/SDK")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  Frontend.cpp Lowering.cpp Project.cpp MathSDK.cpp Frontend.h
  SetupGuidSymbols.py RewriteSetupCoffSymbols.py BundleArchive.cmake
  MsvcRuntimeSymbols.py
  AuditArchive.py CoffWeakAliases.py HostCoffSymbols.py VerifyArchiveMembers.py
  "${_builtin_sdk}/catalog.json" "${_builtin_sdk}/embed.py")
set(_identity "builtin:llvm20.1.8:6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d")
foreach(_input Frontend.cpp Lowering.cpp Project.cpp MathSDK.cpp Frontend.h
    CMakeLists.txt IsolateSymbols.py NoCrashReporter.h BuildID.h.in
    SetupGuidSymbols.py RewriteSetupCoffSymbols.py BundleArchive.cmake
    MsvcRuntimeSymbols.py
    AuditArchive.py CoffWeakAliases.py HostCoffSymbols.py VerifyArchiveMembers.py)
  file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${_input}" _hash)
  string(APPEND _identity ":${_hash}")
endforeach()
file(SHA256 "${_builtin_sdk}/catalog.json" _builtin_catalog_hash)
file(SHA256 "${_builtin_sdk}/embed.py" _builtin_generator_hash)
string(SHA256 NEVERC_CPP_BUILD_ID
  "${_identity}:${_builtin_catalog_hash}:${_builtin_generator_hash}")
configure_file(BuildID.h.in BuildID.h @ONLY)
file(GLOB_RECURSE _builtin_headers CONFIGURE_DEPENDS
  "${_builtin_sdk}/libcxx/*" "${_builtin_sdk}/resource/*"
  "${_builtin_sdk}/platform/*")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/BuiltinCppSdkData.h"
  COMMAND "${Python3_EXECUTABLE}" "${_builtin_sdk}/embed.py"
    --catalog "${_builtin_sdk}/catalog.json" --root "${_builtin_sdk}"
    --output "${CMAKE_CURRENT_BINARY_DIR}/BuiltinCppSdkData.h"
  DEPENDS ${_builtin_headers} "${_builtin_sdk}/catalog.json"
    "${_builtin_sdk}/embed.py"
  VERBATIM)
add_library(nevercCppFrontendBridge STATIC
  Frontend.cpp Lowering.cpp Project.cpp MathSDK.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/BuiltinCppSdkData.h")
set_target_properties(nevercCppFrontendBridge PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(nevercCppFrontendBridge PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" "${LLVM_MAIN_SRC_DIR}/../clang/include"
  "${LLVM_BINARY_DIR}/tools/clang/include"
  "${LLVM_MAIN_SRC_DIR}/include" "${LLVM_BINARY_DIR}/include")
target_link_libraries(nevercCppFrontendBridge PRIVATE clangTooling clangIndex)
# Lowering's local Failure exception is caught inside our ASTConsumer callback.
# LLVM/Clang themselves remain built without EH/RTTI. In particular, enabling
# RTTI only here would require typeinfo for upstream's non-RTTI base classes.
if(MSVC)
  target_compile_options(nevercCppFrontendBridge PRIVATE /EHsc /GR-)
else()
  target_compile_options(nevercCppFrontendBridge PRIVATE -fexceptions -fno-rtti)
endif()

# Resolve the real upstream static closure. No LLVM/Clang CMake targets or
# headers escape this sub-build into the host compiler's incompatible LLVM.
function(_neverc_cpp_collect target)
  get_property(_visited GLOBAL PROPERTY NEVERC_CPP_ARCHIVE_TARGETS)
  if("${target}" IN_LIST _visited)
    return()
  endif()
  get_target_property(_type "${target}" TYPE)
  if(_type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "MODULE_LIBRARY")
    message(FATAL_ERROR "Unexpected dynamic dependency in builtin C++ frontend: ${target}")
  endif()
  if(_type STREQUAL "STATIC_LIBRARY")
    set_property(GLOBAL APPEND PROPERTY NEVERC_CPP_ARCHIVE_TARGETS "${target}")
  endif()
  get_target_property(_deps "${target}" LINK_LIBRARIES)
  get_target_property(_interface "${target}" INTERFACE_LINK_LIBRARIES)
  foreach(_dep IN LISTS _deps _interface)
    if(_dep MATCHES "^\\$<LINK_ONLY:([^>]+)>$")
      set(_dep "${CMAKE_MATCH_1}")
    endif()
    if(TARGET "${_dep}")
      _neverc_cpp_collect("${_dep}")
    endif()
  endforeach()
endfunction()
_neverc_cpp_collect(nevercCppFrontendBridge)
get_property(_archives GLOBAL PROPERTY NEVERC_CPP_ARCHIVE_TARGETS)
set(_manifest "")
foreach(_target IN LISTS _archives)
  string(APPEND _manifest "$<TARGET_FILE:${_target}>\n")
endforeach()
set(_inputs "${CMAKE_CURRENT_BINARY_DIR}/archives-$<CONFIG>.txt")
file(GENERATE OUTPUT "${_inputs}" CONTENT "${_manifest}")
file(GENERATE OUTPUT "${LLVM_BINARY_DIR}/neverc-cpp-nm-$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE:llvm-nm>\n")
set(_audit_tools llvm-nm)
set(_coff_audit_arguments)
set(_bundle_coff_arguments)
set(_setup_contract_dependencies)
if(WIN32)
  # COFF weak externals carry a fallback symbol in auxiliary records that nm's
  # one-character type cannot represent. This reader is a build tool only.
  list(APPEND _audit_tools llvm-readobj)
  list(APPEND _coff_audit_arguments --coff-readobj "$<TARGET_FILE:llvm-readobj>")
  file(GENERATE OUTPUT "${LLVM_BINARY_DIR}/neverc-cpp-readobj-$<CONFIG>.txt"
    CONTENT "$<TARGET_FILE:llvm-readobj>\n")
  list(APPEND _bundle_coff_arguments
    "-DCOFF=ON" "-DPYTHON=${Python3_EXECUTABLE}"
    "-DNM=$<TARGET_FILE:llvm-nm>" "-DREADOBJ=$<TARGET_FILE:llvm-readobj>"
    "-DWRITER=${CMAKE_CURRENT_SOURCE_DIR}/RewriteSetupCoffSymbols.py"
    "-DWRITER_TESTS=${CMAKE_CURRENT_SOURCE_DIR}/RewriteSetupCoffSymbolsTests.py"
    "-DAUDITOR=${CMAKE_CURRENT_SOURCE_DIR}/AuditArchive.py"
    "-DPREFIX_HEADER=${LLVM_BINARY_DIR}/NeverCCppPrivatePrefix.h")
  set(_setup_contract_report "$ENV{NEVERC_CPP_SETUP_CONTRACT_REPORT_DIR}")
  if(_setup_contract_report)
    # This environment variable is passed by the existing Windows Clang CI
    # jobs and inherited through ExternalProject. It has no product role.
    if(NOT "$ENV{GITHUB_ACTIONS}" STREQUAL "true" OR
       NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
       NOT IS_ABSOLUTE "${_setup_contract_report}" OR "$ENV{LLVM_ROOT}" STREQUAL "")
      message(FATAL_ERROR "Invalid Windows Setup contract CI context")
    endif()
    if(CMAKE_CXX_COMPILER_TARGET)
      set(_setup_target "${CMAKE_CXX_COMPILER_TARGET}")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|X86_64)$")
      set(_setup_target x86_64-pc-windows-msvc)
    else()
      message(FATAL_ERROR "Missing exact native target for Setup contract")
    endif()
    if(NOT _setup_target MATCHES "^(x86_64|aarch64)-pc-windows-msvc$")
      message(FATAL_ERROR "Unsupported Setup contract target: ${_setup_target}")
    endif()
    set(_setup_test "${NEVERC_CPP_REPOSITORY_ROOT}/tests/neverc/CppFrontendToolchainTests.py")
    set(_setup_header "${LLVM_MAIN_SRC_DIR}/include/llvm/WindowsDriver/MSVCSetupApi.h")
    list(APPEND _bundle_coff_arguments
      "-DSETUP_CONTRACT_REPORT_DIR=${_setup_contract_report}"
      "-DHOST_LLVM_ROOT=$ENV{LLVM_ROOT}" "-DSETUP_TARGET=${_setup_target}"
      "-DTOOLCHAIN_TESTS=${_setup_test}" "-DSETUP_HEADER=${_setup_header}")
    list(APPEND _setup_contract_dependencies "${_setup_test}" "${_setup_header}")
  endif()
endif()
set(_output "${LLVM_BINARY_DIR}/lib/$<CONFIG>/${CMAKE_STATIC_LIBRARY_PREFIX}nevercCppFrontend${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(APPLE)
  find_program(_bundle_tool NAMES libtool REQUIRED)
  set(_bundle_kind darwin)
  set(_member_reader_kind darwin)
elseif(MSVC AND CMAKE_AR MATCHES "(^|[/\\\\])(llvm-)?lib(\\.exe)?$")
  set(_bundle_tool "${CMAKE_AR}")
  set(_bundle_kind msvc)
  set(_member_reader_kind msvc)
else()
  set(_bundle_tool "${CMAKE_AR}")
  set(_bundle_kind mri)
  set(_member_reader_kind ar)
endif()
if(WIN32)
  list(APPEND _bundle_coff_arguments
    "-DMEMBER_READER=${CMAKE_CURRENT_SOURCE_DIR}/VerifyArchiveMembers.py"
    "-DMEMBER_TESTS=${CMAKE_CURRENT_SOURCE_DIR}/VerifyArchiveMembersTests.py"
    "-DMEMBER_ARCHIVER=${CMAKE_AR}" "-DMEMBER_KIND=${_member_reader_kind}")
endif()
add_custom_command(OUTPUT "${_output}"
  COMMAND "${CMAKE_COMMAND}" "-DINPUTS=${_inputs}" "-DOUTPUT=${_output}"
    "-DTOOL=${_bundle_tool}" "-DKIND=${_bundle_kind}"
    ${_bundle_coff_arguments}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/BundleArchive.cmake"
  DEPENDS ${_archives} ${_audit_tools} ${_setup_contract_dependencies}
    "${CMAKE_CURRENT_SOURCE_DIR}/BundleArchive.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/AuditArchive.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/MsvcRuntimeSymbols.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/CoffWeakAliases.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/SetupGuidSymbols.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/RewriteSetupCoffSymbols.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/RewriteSetupCoffSymbolsTests.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/HostCoffSymbols.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/VerifyArchiveMembers.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/VerifyArchiveMembersTests.py"
  VERBATIM)
add_custom_target(nevercCppFrontendBundle
  COMMAND "${Python3_EXECUTABLE}" -I -B
    "${CMAKE_CURRENT_SOURCE_DIR}/VerifyArchiveMembersTests.py"
  # Always inspect the actual archive, including when an earlier invocation
  # left an existing output behind. A missing nm symbol is not member evidence.
  COMMAND "${Python3_EXECUTABLE}" -I -B
    "${CMAKE_CURRENT_SOURCE_DIR}/VerifyArchiveMembers.py"
    --archive "${_output}" --archiver "${CMAKE_AR}" --kind "${_member_reader_kind}"
  COMMAND "${Python3_EXECUTABLE}" -E -B "${CMAKE_CURRENT_SOURCE_DIR}/AuditArchiveTests.py"
  COMMAND "${Python3_EXECUTABLE}" -E -B "${CMAKE_CURRENT_SOURCE_DIR}/MsvcRuntimeSymbolsTests.py"
  COMMAND "${Python3_EXECUTABLE}" -E -B "${CMAKE_CURRENT_SOURCE_DIR}/CoffWeakAliasesTests.py"
  COMMAND "${Python3_EXECUTABLE}" -E -B
    "${NEVERC_CPP_REPOSITORY_ROOT}/tests/neverc/HostCoffSymbolsTests.py"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/AuditArchive.py"
    --nm "$<TARGET_FILE:llvm-nm>" --archive "${_output}"
    --prefix-header "${LLVM_BINARY_DIR}/NeverCCppPrivatePrefix.h"
    ${_coff_audit_arguments}
  DEPENDS "${_output}" ${_audit_tools}
  VERBATIM)
