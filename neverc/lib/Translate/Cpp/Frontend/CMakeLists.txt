# This directory is added only by the private upstream LLVM CMake build.
if(NOT PACKAGE_VERSION VERSION_EQUAL "20.1.8" OR
   NOT DEFINED NEVERC_CPP_REPOSITORY_ROOT)
  message(FATAL_ERROR "The builtin C++ frontend requires its isolated LLVM 20.1.8 build")
endif()
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
  "${_builtin_sdk}/catalog.json" "${_builtin_sdk}/embed.py")
set(_identity "builtin:llvm20.1.8:6898f963c8e938981e6c4a302e83ec5beb4630147c7311183cf61069af16333d")
foreach(_input Frontend.cpp Lowering.cpp Project.cpp MathSDK.cpp Frontend.h
    CMakeLists.txt IsolateSymbols.py NoCrashReporter.h BuildID.h.in)
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
set(_output "${LLVM_BINARY_DIR}/lib/$<CONFIG>/${CMAKE_STATIC_LIBRARY_PREFIX}nevercCppFrontend${CMAKE_STATIC_LIBRARY_SUFFIX}")
if(APPLE)
  find_program(_bundle_tool NAMES libtool REQUIRED)
  set(_bundle_kind darwin)
elseif(MSVC AND CMAKE_AR MATCHES "(^|[/\\\\])(llvm-)?lib(\\.exe)?$")
  set(_bundle_tool "${CMAKE_AR}")
  set(_bundle_kind msvc)
else()
  set(_bundle_tool "${CMAKE_AR}")
  set(_bundle_kind mri)
endif()
add_custom_command(OUTPUT "${_output}"
  COMMAND "${CMAKE_COMMAND}" "-DINPUTS=${_inputs}" "-DOUTPUT=${_output}"
    "-DTOOL=${_bundle_tool}" "-DKIND=${_bundle_kind}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/BundleArchive.cmake"
  DEPENDS ${_archives}
    "${CMAKE_CURRENT_SOURCE_DIR}/BundleArchive.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/AuditArchive.py"
  VERBATIM)
add_custom_target(nevercCppFrontendBundle
  COMMAND "${Python3_EXECUTABLE}" -E -B "${CMAKE_CURRENT_SOURCE_DIR}/AuditArchiveTests.py"
  COMMAND "${Python3_EXECUTABLE}" -E -B
    "${NEVERC_CPP_REPOSITORY_ROOT}/tests/neverc/HostCoffSymbolsTests.py"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/AuditArchive.py"
    --nm "$<TARGET_FILE:llvm-nm>" --archive "${_output}"
    --prefix-header "${LLVM_BINARY_DIR}/NeverCCppPrivatePrefix.h"
  DEPENDS "${_output}" llvm-nm
  VERBATIM)
