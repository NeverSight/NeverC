set(LLVM_LINK_COMPONENTS Support TargetParser Core BitReader)

set(_cpp_sdk_catalog "${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/translate-frontends/cpp/sdk/approved-sdk.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_cpp_sdk_catalog}")
# Numeric byte initializers avoid MSVC's string-literal limits and preserve
# the catalog bytes without source-encoding or line-ending interpretation.
file(READ "${_cpp_sdk_catalog}" _cpp_sdk_catalog_hex HEX)
string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "char(0x\\1),\n"
  _cpp_sdk_catalog_bytes "${_cpp_sdk_catalog_hex}")
configure_file(Cpp/CppSdkCatalog.inc.in CppSdkCatalog.inc @ONLY)
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
