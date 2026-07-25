# Standalone NeverC plugin conformance suite.
#
# This target links ONLY GoogleTest (plus the C++ standard library); it never
# links or includes the host's private libraries or headers. At run time it
# builds the fixtures under fixtures/ with the *system* C compiler against the
# staged public SDK and drives the compiler-under-test through its public CLI.
# A green run therefore proves the shipped ABI and SDK are self-sufficient for
# an independent toolchain.
#
# The compiler-under-test and SDK can be overridden at run time with the
# NEVERC_UNDER_TEST and NEVERC_PLUGIN_SDK_ROOT environment variables, so the
# same binary can validate a released package.

if(DEFINED NEVERC_EXTERNAL_BINARY AND NOT NEVERC_EXTERNAL_BINARY STREQUAL "")
  set(_NEVERC_CONF_BINARY "${NEVERC_EXTERNAL_BINARY}")
  set(_NEVERC_CONF_DEPENDS FALSE)
else()
  set(_NEVERC_CONF_BINARY "$<TARGET_FILE:neverc>")
  set(_NEVERC_CONF_DEPENDS TRUE)
endif()

# The system compiler that builds the fixtures may default to a different
# architecture than this build -- an x64 clang running on an arm64 Windows host,
# for instance -- which makes the fixture object disagree with the CRT it links
# against. Pin it to this build's triple. Only clang drivers accept --target=,
# and the harness' MSVC-driver path ignores this.
set(_NEVERC_CONF_CC_TARGET "")
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  if(CMAKE_C_COMPILER_TARGET)
    set(_NEVERC_CONF_CC_TARGET "${CMAKE_C_COMPILER_TARGET}")
  elseif(DEFINED LLVM_HOST_TRIPLE)
    set(_NEVERC_CONF_CC_TARGET "${LLVM_HOST_TRIPLE}")
  endif()
endif()

add_executable(neverc-plugin-conformance
  PluginConformanceMain.cpp
  ConformanceEnvironment.cpp
  ConformanceSummary.cpp
  SDKABIConformanceTests.cpp
  LoaderLifecycleConformanceTests.cpp
  DriverFrontendConformanceTests.cpp
  IRMirConformanceTests.cpp
  TargetObjectConformanceTests.cpp
  LinkLTOConformanceTests.cpp
  DynCodeConformanceTests.cpp
  HandleFailureConformanceTests.cpp
)
target_include_directories(neverc-plugin-conformance PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(neverc-plugin-conformance PRIVATE GTest::gtest)
target_compile_features(neverc-plugin-conformance PRIVATE cxx_std_17)
target_compile_definitions(neverc-plugin-conformance PRIVATE
  NEVERC_CONF_NEVERC="${_NEVERC_CONF_BINARY}"
  NEVERC_CONF_CC="${CMAKE_C_COMPILER}"
  NEVERC_CONF_CC_TARGET="${_NEVERC_CONF_CC_TARGET}"
  NEVERC_CONF_SDK_INCLUDE="${CMAKE_CURRENT_SOURCE_DIR}/../../pluginsdk/include"
  NEVERC_CONF_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
if(_NEVERC_CONF_DEPENDS AND TARGET neverc)
  add_dependencies(neverc-plugin-conformance neverc)
endif()

include(GoogleTest)
gtest_discover_tests(neverc-plugin-conformance
  TEST_PREFIX "PluginConformance."
  PROPERTIES LABELS plugin-conformance
)

# Ship the conformance harness sources so downstreams can build and run it
# against their own installed host and SDK.
install(FILES
  PluginConformanceMain.cpp
  ConformanceEnvironment.cpp
  ConformanceEnvironment.h
  ConformanceSummary.cpp
  ConformanceSummary.h
  ConformanceTest.h
  SDKABIConformanceTests.cpp
  LoaderLifecycleConformanceTests.cpp
  DriverFrontendConformanceTests.cpp
  IRMirConformanceTests.cpp
  TargetObjectConformanceTests.cpp
  LinkLTOConformanceTests.cpp
  DynCodeConformanceTests.cpp
  HandleFailureConformanceTests.cpp
  DESTINATION pluginsdk/conformance
  COMPONENT neverc-pluginsdk
)

add_subdirectory(fixtures)
