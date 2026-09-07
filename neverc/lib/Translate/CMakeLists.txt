set(LLVM_LINK_COMPONENTS Support TargetParser Core BitReader)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/modules/BuiltinCppFrontend.cmake")
neverc_setup_builtin_cpp_frontend()

set(_cpp_sdk_root "${CMAKE_CURRENT_SOURCE_DIR}/Cpp/SDK")
set(_cpp_sdk_catalog "${_cpp_sdk_root}/catalog.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_cpp_sdk_catalog}")
file(GLOB_RECURSE _cpp_sdk_files CONFIGURE_DEPENDS
  "${_cpp_sdk_root}/libcxx/*" "${_cpp_sdk_root}/resource/*"
  "${_cpp_sdk_root}/platform/*")
set(_cpp_sdk_data "${CMAKE_CURRENT_BINARY_DIR}/BuiltinCppSdkData.h")
add_custom_command(OUTPUT "${_cpp_sdk_data}"
  COMMAND "${Python3_EXECUTABLE}" "${_cpp_sdk_root}/embed.py"
    --catalog "${_cpp_sdk_catalog}" --root "${_cpp_sdk_root}"
    --output "${_cpp_sdk_data}"
  DEPENDS "${_cpp_sdk_root}/embed.py" "${_cpp_sdk_catalog}" ${_cpp_sdk_files}
  VERBATIM)
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

set(NEVERC_TRANSLATE_SOURCES
  ArtifactPlatform.cpp
  ArtifactWriter.cpp
  Diagnostics.cpp
  FrontendProcess.cpp
  Cpp/CompilationContext.cpp
  Cpp/CppSdk.cpp
  Cpp/LibraryMappings.cpp
  ProjectIR.cpp
  TranslateDriver.cpp
  TranslateIR.cpp
  NCEmitter.cpp
)

add_neverc_library(nevercTranslate
  ${NEVERC_TRANSLATE_SOURCES}
  "${_cpp_sdk_data}"
  LINK_LIBS nevercFoundation
)

# A version number alone cannot distinguish translator implementations. Track
# this component's source content without embedding developer checkout paths.
file(SHA256 "${_cpp_sdk_catalog}" _translate_fingerprints)
file(GLOB_RECURSE _translate_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
foreach(_source IN LISTS NEVERC_TRANSLATE_SOURCES _translate_headers)
  if(NOT IS_ABSOLUTE "${_source}")
    set(_source "${CMAKE_CURRENT_SOURCE_DIR}/${_source}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_source}")
  file(SHA256 "${_source}" _digest)
  string(APPEND _translate_fingerprints "${_digest}")
endforeach()
string(SHA256 _translate_build_id "${_translate_fingerprints}")
execute_process(COMMAND git rev-parse HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_VARIABLE _translate_revision
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT _translate_revision)
  set(_translate_revision "source-archive")
endif()
set_property(SOURCE TranslateDriver.cpp APPEND PROPERTY COMPILE_DEFINITIONS
  "NEVERC_TRANSLATE_BUILD_ID=\"${_translate_revision}:${_translate_build_id}\"")
