set(NEVERC_TEST_SOURCES
  NeverCTestFixture.cpp
  BasicTests.cpp
  HostTests.cpp
  DriverTests.cpp
  PluginActionGraphTests.cpp
  PluginJobGraphTests.cpp
  PluginJobExecutionTests.cpp
  PluginTaskLifecycleTests.cpp
  PluginShutdownTests.cpp
  PluginDriverDiagnosticTests.cpp
  PluginSDKExampleTests.cpp
  PluginSDKInstallTests.cpp
  PluginCapabilityInventoryTests.cpp
  PluginSDKGenerationTests.cpp
  PluginSDKGenerationSingleHeader.cpp
  PluginArgumentPhaseTests.cpp
  PluginToolChainTests.cpp
  PluginSourcePhaseTests.cpp
  PluginTokenInterceptorTests.cpp
  PluginPrepHookInterceptorTests.cpp
  PluginParserInterceptorTests.cpp
  PluginParserProviderTests.cpp
  PluginSemaInterceptorTests.cpp
  PluginSemaProviderTests.cpp
  PluginIRGenProviderTests.cpp
  PluginIRPipelineTests.cpp
  PluginIRPassTests.cpp
  PluginLTOIRPassTests.cpp
  PluginMIRPassTests.cpp
  PluginABILoweringDriverTests.cpp
  PluginDynCodeDriverTests.cpp
  PluginBuiltinTargetProviderDriverTests.cpp
  PluginCOFFLinkAdapterTests.cpp
  PluginELFLinkAdapterTests.cpp
  PluginMachOLinkAdapterTests.cpp
  PluginNewBackendTests.cpp
  PluginFrontendLifecycleTests.cpp
  PluginPreprocessorProviderTests.cpp
  PluginAssemblerTests.cpp
  PluginVFSIntegrationTests.cpp
  AndroidKernelRuntimeTests.cpp
  LTOTests.cpp
  LinkerTests.cpp
  OverrideTests.cpp
  ConformanceTests.cpp
  DynCodeTests.cpp
  DynCodeStressTests.cpp
  DynCodeCrossTargetTests.cpp
  DebugTests.cpp
  BenchmarkTests.cpp
  MimallocTests.cpp
  PluginABIContractTests.cpp
  CustomCallConvTests.cpp
  SEHTests.cpp
  X86PrivilegedIntrinTests.cpp
  StrHashTests.cpp
  XorStrTests.cpp
)

# NEVERC_EXTERNAL_BINARY: spawn a prebuilt neverc (e.g. downloaded from CI) instead
# of $<TARGET_FILE:neverc>. Debug workflows use this to build only the gtest
# harness and skip the multi-hour LTO compiler rebuild.
if(DEFINED NEVERC_EXTERNAL_BINARY AND NOT NEVERC_EXTERNAL_BINARY STREQUAL "")
  set(_NEVERC_BINARY "${NEVERC_EXTERNAL_BINARY}")
  set(_NEVERC_TESTS_DEPENDS_NEVERC FALSE)
else()
  set(_NEVERC_BINARY "$<TARGET_FILE:neverc>")
  set(_NEVERC_TESTS_DEPENDS_NEVERC TRUE)
endif()

macro(neverc_tests_link_neverc target)
  if(_NEVERC_TESTS_DEPENDS_NEVERC AND TARGET neverc)
    add_dependencies(${target} neverc)
  endif()
endmacro()

add_executable(neverc-tests ${NEVERC_TEST_SOURCES})

target_link_libraries(neverc-tests PRIVATE GTest::gtest GTest::gtest_main)

add_library(neverc-plugin-abi-compile-c11 OBJECT
  Inputs/Plugin/ABICompileC.c
)
target_include_directories(neverc-plugin-abi-compile-c11 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-abi-compile-c11 PRIVATE c_std_11)

add_library(neverc-plugin-abi-compile-c23 OBJECT
  Inputs/Plugin/ABICompileC.c
)
target_include_directories(neverc-plugin-abi-compile-c23 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-abi-compile-c23 PRIVATE c_std_23)

add_library(neverc-plugin-abi-compile-cxx17 OBJECT
  Inputs/Plugin/ABICompileCXX.cpp
)
target_include_directories(neverc-plugin-abi-compile-cxx17 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-abi-compile-cxx17 PRIVATE cxx_std_17)

add_dependencies(neverc-tests
  neverc-plugin-abi-compile-c11
  neverc-plugin-abi-compile-c23
  neverc-plugin-abi-compile-cxx17
)

add_library(neverc-plugin-frontend-abi-compile-c11 OBJECT
  Inputs/Plugin/FrontendABICompile.c
)
target_include_directories(neverc-plugin-frontend-abi-compile-c11 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-frontend-abi-compile-c11 PRIVATE c_std_11)

add_library(neverc-plugin-frontend-abi-compile-c23 OBJECT
  Inputs/Plugin/FrontendABICompile.c
)
target_include_directories(neverc-plugin-frontend-abi-compile-c23 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-frontend-abi-compile-c23 PRIVATE c_std_23)

add_library(neverc-plugin-ir-abi-compile-c11 OBJECT
  Inputs/Plugin/IRABICompile.c
)
target_include_directories(neverc-plugin-ir-abi-compile-c11 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-ir-abi-compile-c11 PRIVATE c_std_11)

add_library(neverc-plugin-ir-abi-compile-c23 OBJECT
  Inputs/Plugin/IRABICompile.c
)
target_include_directories(neverc-plugin-ir-abi-compile-c23 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-ir-abi-compile-c23 PRIVATE c_std_23)

add_library(neverc-plugin-mir-abi-compile-c11 OBJECT
  Inputs/Plugin/MIRABICompile.c
)
target_include_directories(neverc-plugin-mir-abi-compile-c11 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-mir-abi-compile-c11 PRIVATE c_std_11)

add_library(neverc-plugin-mir-abi-compile-c23 OBJECT
  Inputs/Plugin/MIRABICompile.c
)
target_include_directories(neverc-plugin-mir-abi-compile-c23 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-mir-abi-compile-c23 PRIVATE c_std_23)

add_library(neverc-plugin-target-abi-compile-c11 OBJECT
  Inputs/Plugin/TargetABICompile.c
)
target_include_directories(neverc-plugin-target-abi-compile-c11 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-target-abi-compile-c11 PRIVATE c_std_11)

add_library(neverc-plugin-target-abi-compile-c23 OBJECT
  Inputs/Plugin/TargetABICompile.c
)
target_include_directories(neverc-plugin-target-abi-compile-c23 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-target-abi-compile-c23 PRIVATE c_std_23)

add_library(neverc-plugin-mc-abi-compile-c11 OBJECT
  Inputs/Plugin/MCABICompile.c
)
target_include_directories(neverc-plugin-mc-abi-compile-c11 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-mc-abi-compile-c11 PRIVATE c_std_11)

add_library(neverc-plugin-mc-abi-compile-c23 OBJECT
  Inputs/Plugin/MCABICompile.c
)
target_include_directories(neverc-plugin-mc-abi-compile-c23 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-mc-abi-compile-c23 PRIVATE c_std_23)

add_library(neverc-plugin-object-abi-compile-c11 OBJECT
  Inputs/Plugin/ObjectABICompile.c
)
target_include_directories(neverc-plugin-object-abi-compile-c11 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-object-abi-compile-c11 PRIVATE c_std_11)

add_library(neverc-plugin-object-abi-compile-c23 OBJECT
  Inputs/Plugin/ObjectABICompile.c
)
target_include_directories(neverc-plugin-object-abi-compile-c23 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-object-abi-compile-c23 PRIVATE c_std_23)

add_library(neverc-plugin-link-abi-compile-c11 OBJECT
  Inputs/Plugin/LinkABICompile.c
)
target_include_directories(neverc-plugin-link-abi-compile-c11 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-link-abi-compile-c11 PRIVATE c_std_11)

add_library(neverc-plugin-link-abi-compile-c23 OBJECT
  Inputs/Plugin/LinkABICompile.c
)
target_include_directories(neverc-plugin-link-abi-compile-c23 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-link-abi-compile-c23 PRIVATE c_std_23)

add_library(neverc-plugin-lto-abi-compile-c11 OBJECT
  Inputs/Plugin/LTOABICompile.c
)
target_include_directories(neverc-plugin-lto-abi-compile-c11 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-lto-abi-compile-c11 PRIVATE c_std_11)

add_library(neverc-plugin-lto-abi-compile-c23 OBJECT
  Inputs/Plugin/LTOABICompile.c
)
target_include_directories(neverc-plugin-lto-abi-compile-c23 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-lto-abi-compile-c23 PRIVATE c_std_23)

add_library(neverc-plugin-dyncode-abi-compile-c11 OBJECT
  Inputs/Plugin/DynCodeABICompile.c
)
target_include_directories(neverc-plugin-dyncode-abi-compile-c11 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-dyncode-abi-compile-c11 PRIVATE c_std_11)

add_library(neverc-plugin-dyncode-abi-compile-c23 OBJECT
  Inputs/Plugin/DynCodeABICompile.c
)
target_include_directories(neverc-plugin-dyncode-abi-compile-c23 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-dyncode-abi-compile-c23 PRIVATE c_std_23)

add_library(neverc-test-plugin-link-route MODULE
  Inputs/Plugin/LinkRoutePlugin.c
)
target_include_directories(neverc-test-plugin-link-route PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-link-route PRIVATE c_std_11)
set_target_properties(neverc-test-plugin-link-route PROPERTIES
  PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
)

add_library(neverc-test-plugin-dyncode-phase MODULE
  Inputs/Plugin/DynCodePhaseTracePlugin.c
)
target_include_directories(neverc-test-plugin-dyncode-phase PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-dyncode-phase PRIVATE c_std_11)
set_target_properties(neverc-test-plugin-dyncode-phase PROPERTIES
  PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
)

add_library(neverc-test-plugin-object-merge MODULE
  Inputs/Plugin/ObjectMergePlugin.c
)
target_include_directories(neverc-test-plugin-object-merge PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-object-merge PRIVATE c_std_11
)
set_target_properties(neverc-test-plugin-object-merge PROPERTIES
  PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib"
)

function(add_neverc_registry_fixture target)
  add_library(${target} MODULE Inputs/Plugin/RegistryFixturePlugin.c)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(${target} PRIVATE c_std_11)
  set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()

add_neverc_registry_fixture(neverc-test-plugin-minimal)
add_neverc_registry_fixture(neverc-test-plugin-target-valid)
target_compile_definitions(neverc-test-plugin-target-valid PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.target-valid"
  NEVERC_TEST_REGISTER_TARGET=1
)
add_neverc_registry_fixture(PluginTestTarget)
target_compile_definitions(PluginTestTarget PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.new-backend"
  NEVERC_TEST_PLUGIN_DISPLAY_NAME="NeverC Test Target Backend"
  NEVERC_TEST_REGISTER_TARGET=1
  NEVERC_TEST_REGISTER_NOBJ_BACKEND=1
  NEVERC_TEST_TARGET_ID_LOW=42
  NEVERC_TEST_TARGET_NAME="neverc-test"
  NEVERC_TEST_TARGET_RAW_TRIPLE="neverc-test-none"
  NEVERC_TEST_TARGET_ARCHITECTURE="neverc"
  NEVERC_TEST_CONCURRENCY=NEVERC_CONCURRENCY_THREAD_SAFE
)

add_library(neverc-test-plugin-target-language MODULE
  Inputs/Plugin/TargetLanguagePlugin.c
)
target_include_directories(neverc-test-plugin-target-language PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-target-language PRIVATE c_std_11)
set_target_properties(neverc-test-plugin-target-language PROPERTIES PREFIX "")

add_library(neverc-test-plugin-assembly-provider MODULE
  Inputs/Plugin/AssemblyProviderPlugin.c
)
target_include_directories(neverc-test-plugin-assembly-provider PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-assembly-provider PRIVATE c_std_11)
set_target_properties(neverc-test-plugin-assembly-provider PROPERTIES PREFIX "")

add_library(neverc-test-plugin-mc-emission-hook MODULE
  Inputs/Plugin/MCEmissionHookPlugin.c
)
target_include_directories(neverc-test-plugin-mc-emission-hook PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-mc-emission-hook PRIVATE c_std_11)
set_target_properties(
  neverc-test-plugin-mc-emission-hook PROPERTIES PREFIX ""
)

add_library(neverc-test-plugin-object-post-write MODULE
  Inputs/Plugin/ObjectPostWritePlugin.c
)
target_include_directories(neverc-test-plugin-object-post-write PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-object-post-write PRIVATE c_std_11
)
set_target_properties(
  neverc-test-plugin-object-post-write PROPERTIES PREFIX ""
)

add_neverc_registry_fixture(neverc-test-plugin-target-indirect)
target_compile_definitions(neverc-test-plugin-target-indirect PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.target-indirect"
  NEVERC_TEST_REGISTER_TARGET=1
  NEVERC_TEST_TARGET_ID_LOW=2
  NEVERC_TEST_TARGET_NAME="test.fixture-target-indirect"
  NEVERC_TEST_ABI_FORCE_INDIRECT_ARGUMENTS=1
)
add_neverc_registry_fixture(neverc-test-plugin-target-coerce-expand)
target_compile_definitions(neverc-test-plugin-target-coerce-expand PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.target-coerce-expand"
  NEVERC_TEST_REGISTER_TARGET=1
  NEVERC_TEST_TARGET_ID_LOW=3
  NEVERC_TEST_TARGET_NAME="test.fixture-target-coerce-expand"
  NEVERC_TEST_ABI_COERCE_AND_EXPAND_ARGUMENTS=1
)
add_neverc_registry_fixture(neverc-test-plugin-target-ms-va)
target_compile_definitions(neverc-test-plugin-target-ms-va PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.target-ms-va"
  NEVERC_TEST_REGISTER_TARGET=1
  NEVERC_TEST_TARGET_ID_LOW=4
  NEVERC_TEST_TARGET_NAME="test.fixture-target-ms-va"
  NEVERC_TEST_HAS_MS_VA_LIST=1
)
add_neverc_registry_fixture(neverc-test-plugin-target-indirect-aliased)
target_compile_definitions(neverc-test-plugin-target-indirect-aliased PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.target-indirect-aliased"
  NEVERC_TEST_REGISTER_TARGET=1
  NEVERC_TEST_TARGET_ID_LOW=5
  NEVERC_TEST_TARGET_NAME="test.fixture-target-indirect-aliased"
  NEVERC_TEST_ABI_INDIRECT_ALIASED_ARGUMENTS=1
)
add_neverc_registry_fixture(neverc-test-plugin-target-cc-plan)
target_compile_definitions(neverc-test-plugin-target-cc-plan PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.target-cc-plan"
  NEVERC_TEST_REGISTER_TARGET=1
  NEVERC_TEST_TARGET_ID_LOW=6
  NEVERC_TEST_TARGET_NAME="test.fixture-target-cc-plan"
  NEVERC_TEST_TARGET_RAW_TRIPLE="x86_64-unknown-linux-gnu"
  NEVERC_TEST_TARGET_ARCHITECTURE="x86_64"
  NEVERC_TEST_CC_PLAN_X86=1
)
add_neverc_registry_fixture(neverc-test-plugin-target-unknown-format)
target_compile_definitions(neverc-test-plugin-target-unknown-format PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.target-unknown-format"
  NEVERC_TEST_REGISTER_TARGET=1
  NEVERC_TEST_TARGET_UNKNOWN_FORMAT=1
)
add_neverc_registry_fixture(neverc-test-plugin-target-registration-failure)
target_compile_definitions(neverc-test-plugin-target-registration-failure PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.target-registration-failure"
  NEVERC_TEST_REGISTER_TARGET=1
  NEVERC_TEST_TARGET_REGISTRATION_FAILURE=1
)
add_library(neverc-test-plugin-removed-prototype MODULE
  Inputs/Plugin/RemovedPrototypePlugin.c
)
target_compile_features(neverc-test-plugin-removed-prototype PRIVATE c_std_11)
set_target_properties(neverc-test-plugin-removed-prototype PROPERTIES PREFIX "")
add_neverc_registry_fixture(neverc-test-plugin-duplicate-id)
add_neverc_registry_fixture(neverc-test-plugin-other)
target_compile_definitions(neverc-test-plugin-other PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.other"
)
add_neverc_registry_fixture(neverc-test-plugin-wrong-abi)
target_compile_definitions(neverc-test-plugin-wrong-abi PRIVATE
  NEVERC_TEST_PLUGIN_ABI_MAJOR=2
)
add_neverc_registry_fixture(neverc-test-plugin-short-descriptor)
target_compile_definitions(neverc-test-plugin-short-descriptor PRIVATE
  NEVERC_TEST_SHORT_DESCRIPTOR=1
)
add_neverc_registry_fixture(neverc-test-plugin-prefix-descriptor)
target_compile_definitions(neverc-test-plugin-prefix-descriptor PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.prefix"
  NEVERC_TEST_PREFIX_ONLY_DESCRIPTOR=1
)
add_neverc_registry_fixture(neverc-test-plugin-long-descriptor)
target_compile_definitions(neverc-test-plugin-long-descriptor PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.long"
  NEVERC_TEST_LONG_DESCRIPTOR=1
)
add_neverc_registry_fixture(neverc-test-plugin-invalid-id)
target_compile_definitions(neverc-test-plugin-invalid-id PRIVATE
  NEVERC_TEST_PLUGIN_ID="Org.neverc.test.invalid"
)
add_neverc_registry_fixture(neverc-test-plugin-required-interface)
target_compile_definitions(neverc-test-plugin-required-interface PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.required-interface"
  NEVERC_TEST_REQUIRED_INTERFACE_HIGH=0x1111111111111111ULL
  NEVERC_TEST_REQUIRED_INTERFACE_LOW=0x2222222222222222ULL
  NEVERC_TEST_REQUIRED_INTERFACE_MAJOR=1
  NEVERC_TEST_REQUIRED_INTERFACE_MINOR=1
)
add_neverc_registry_fixture(neverc-test-plugin-missing-interface)
target_compile_definitions(neverc-test-plugin-missing-interface PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.missing-interface"
  NEVERC_TEST_REQUIRED_INTERFACE_HIGH=0xAAAAAAAAAAAAAAAAULL
  NEVERC_TEST_REQUIRED_INTERFACE_LOW=0xBBBBBBBBBBBBBBBBULL
  NEVERC_TEST_REQUIRED_INTERFACE_MAJOR=1
  NEVERC_TEST_REQUIRED_INTERFACE_MINOR=0
)
add_neverc_registry_fixture(neverc-test-plugin-dependency-a)
target_compile_definitions(neverc-test-plugin-dependency-a PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.dependency.a"
  NEVERC_TEST_PLUGIN_VERSION_MAJOR=1
  NEVERC_TEST_PLUGIN_VERSION_MINOR=0
  NEVERC_TEST_PLUGIN_VERSION_PATCH=0
)
add_neverc_registry_fixture(neverc-test-plugin-dependency-b)
target_compile_definitions(neverc-test-plugin-dependency-b PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.dependency.b"
  NEVERC_TEST_DEPENDENCY_ID="org.neverc.test.dependency.a"
  NEVERC_TEST_DEPENDENCY_KIND=NEVERC_DEPENDENCY_REQUIRED
  NEVERC_TEST_DEPENDENCY_MIN_MAJOR=1
  NEVERC_TEST_DEPENDENCY_MIN_MINOR=0
  NEVERC_TEST_DEPENDENCY_MIN_PATCH=0
  NEVERC_TEST_DEPENDENCY_MAX_MAJOR=2
  NEVERC_TEST_DEPENDENCY_MAX_MINOR=0
  NEVERC_TEST_DEPENDENCY_MAX_PATCH=0
)
add_neverc_registry_fixture(neverc-test-plugin-dependency-missing)
target_compile_definitions(neverc-test-plugin-dependency-missing PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.dependency.missing"
  NEVERC_TEST_DEPENDENCY_ID="org.neverc.test.dependency.a"
  NEVERC_TEST_DEPENDENCY_KIND=NEVERC_DEPENDENCY_REQUIRED
  NEVERC_TEST_DEPENDENCY_MIN_MAJOR=1
  NEVERC_TEST_DEPENDENCY_MIN_MINOR=0
  NEVERC_TEST_DEPENDENCY_MIN_PATCH=0
)
add_neverc_registry_fixture(neverc-test-plugin-dependency-mismatch)
target_compile_definitions(neverc-test-plugin-dependency-mismatch PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.dependency.mismatch"
  NEVERC_TEST_DEPENDENCY_ID="org.neverc.test.dependency.a"
  NEVERC_TEST_DEPENDENCY_KIND=NEVERC_DEPENDENCY_REQUIRED
  NEVERC_TEST_DEPENDENCY_MIN_MAJOR=2
  NEVERC_TEST_DEPENDENCY_MIN_MINOR=0
  NEVERC_TEST_DEPENDENCY_MIN_PATCH=0
)
add_neverc_registry_fixture(neverc-test-plugin-cycle-a)
target_compile_definitions(neverc-test-plugin-cycle-a PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.cycle.a"
  NEVERC_TEST_DEPENDENCY_ID="org.neverc.test.cycle.b"
  NEVERC_TEST_DEPENDENCY_KIND=NEVERC_DEPENDENCY_REQUIRED
  NEVERC_TEST_DEPENDENCY_MIN_MAJOR=1
  NEVERC_TEST_DEPENDENCY_MIN_MINOR=0
  NEVERC_TEST_DEPENDENCY_MIN_PATCH=0
)
add_neverc_registry_fixture(neverc-test-plugin-cycle-b)
target_compile_definitions(neverc-test-plugin-cycle-b PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.cycle.b"
  NEVERC_TEST_DEPENDENCY_ID="org.neverc.test.cycle.a"
  NEVERC_TEST_DEPENDENCY_KIND=NEVERC_DEPENDENCY_REQUIRED
  NEVERC_TEST_DEPENDENCY_MIN_MAJOR=1
  NEVERC_TEST_DEPENDENCY_MIN_MINOR=0
  NEVERC_TEST_DEPENDENCY_MIN_PATCH=0
)
add_neverc_registry_fixture(neverc-test-plugin-lifecycle-a)
target_compile_definitions(neverc-test-plugin-lifecycle-a PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.lifecycle.a"
  NEVERC_TEST_PLUGIN_VERSION_MAJOR=1
  NEVERC_TEST_PLUGIN_VERSION_MINOR=0
  NEVERC_TEST_PLUGIN_VERSION_PATCH=0
)
add_neverc_registry_fixture(neverc-test-plugin-lifecycle-b)
target_compile_definitions(neverc-test-plugin-lifecycle-b PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.lifecycle.b"
  NEVERC_TEST_DEPENDENCY_ID="org.neverc.test.lifecycle.a"
  NEVERC_TEST_DEPENDENCY_KIND=NEVERC_DEPENDENCY_REQUIRED
  NEVERC_TEST_DEPENDENCY_MIN_MAJOR=1
  NEVERC_TEST_DEPENDENCY_MIN_MINOR=0
  NEVERC_TEST_DEPENDENCY_MIN_PATCH=0
)
add_neverc_registry_fixture(neverc-test-plugin-process-failure)
target_compile_definitions(neverc-test-plugin-process-failure PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.lifecycle.process-failure"
  NEVERC_TEST_DEPENDENCY_ID="org.neverc.test.lifecycle.a"
  NEVERC_TEST_DEPENDENCY_KIND=NEVERC_DEPENDENCY_REQUIRED
  NEVERC_TEST_DEPENDENCY_MIN_MAJOR=1
  NEVERC_TEST_DEPENDENCY_MIN_MINOR=0
  NEVERC_TEST_DEPENDENCY_MIN_PATCH=0
  NEVERC_TEST_PROCESS_BEGIN_FAILURE=1
)
add_neverc_registry_fixture(neverc-test-plugin-registration-failure)
target_compile_definitions(neverc-test-plugin-registration-failure PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.lifecycle.registration-failure"
  NEVERC_TEST_DEPENDENCY_ID="org.neverc.test.lifecycle.a"
  NEVERC_TEST_DEPENDENCY_KIND=NEVERC_DEPENDENCY_REQUIRED
  NEVERC_TEST_DEPENDENCY_MIN_MAJOR=1
  NEVERC_TEST_DEPENDENCY_MIN_MINOR=0
  NEVERC_TEST_DEPENDENCY_MIN_PATCH=0
  NEVERC_TEST_REGISTRATION_FAILURE=1
)
add_neverc_registry_fixture(neverc-test-plugin-registration-userdata)
target_compile_definitions(neverc-test-plugin-registration-userdata PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.registration-userdata"
  NEVERC_TEST_REGISTER_USERDATA=1
)
add_neverc_registry_fixture(neverc-test-plugin-registration-userdata-failure)
target_compile_definitions(
  neverc-test-plugin-registration-userdata-failure PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.registration-userdata-failure"
  NEVERC_TEST_REGISTER_USERDATA=1
  NEVERC_TEST_REGISTRATION_FAILURE=1
)
add_neverc_registry_fixture(neverc-test-plugin-option)
target_compile_definitions(neverc-test-plugin-option PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.option"
  NEVERC_TEST_REGISTER_OPTION=1
)

function(add_neverc_vfs_fixture target plugin_id)
  add_library(${target} MODULE Inputs/Plugin/VFSPlugin.c)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(${target} PRIVATE c_std_11)
  target_compile_definitions(${target} PRIVATE
    NEVERC_TEST_VFS_PLUGIN_ID="${plugin_id}"
  )
  set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()
add_neverc_vfs_fixture(
  neverc-test-plugin-vfs
  org.neverc.test.vfs
)
add_library(neverc-test-plugin-virtual-archive MODULE
  Inputs/Plugin/VirtualArchivePlugin.c
)
target_include_directories(neverc-test-plugin-virtual-archive PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-virtual-archive PRIVATE c_std_11)
set_target_properties(neverc-test-plugin-virtual-archive PROPERTIES PREFIX "")
add_library(neverc-test-plugin-source-phase MODULE
  Inputs/Plugin/SourcePhasePlugin.c
)
target_include_directories(neverc-test-plugin-source-phase PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-source-phase PRIVATE c_std_11
)
set_target_properties(
  neverc-test-plugin-source-phase PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-token-rewrite MODULE
  Inputs/Plugin/TokenRewritePlugin.c
)
target_include_directories(neverc-test-plugin-token-rewrite PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-token-rewrite PRIVATE c_std_11
)
set_target_properties(
  neverc-test-plugin-token-rewrite PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-prep-hooks MODULE
  Inputs/Plugin/PrepHookPlugin.c
)
target_include_directories(neverc-test-plugin-prep-hooks PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-prep-hooks PRIVATE c_std_11
)
set_target_properties(
  neverc-test-plugin-prep-hooks PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-parser-interceptor MODULE
  Inputs/Plugin/ParserInterceptorPlugin.c
)
target_include_directories(neverc-test-plugin-parser-interceptor PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-parser-interceptor PRIVATE c_std_11
)
set_target_properties(
  neverc-test-plugin-parser-interceptor PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-sema-interceptor MODULE
  Inputs/Plugin/SemaInterceptorPlugin.c
)
target_include_directories(neverc-test-plugin-sema-interceptor PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-sema-interceptor PRIVATE c_std_11
)
set_target_properties(
  neverc-test-plugin-sema-interceptor PROPERTIES PREFIX ""
)
function(add_neverc_parser_provider_fixture target)
  add_library(${target} MODULE
    Inputs/Plugin/ParserProviderPlugin.c
  )
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(${target} PRIVATE c_std_11)
  set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()
add_neverc_parser_provider_fixture(neverc-test-plugin-parser-provider)
add_neverc_parser_provider_fixture(neverc-test-plugin-parser-provider-binary)
target_compile_definitions(
  neverc-test-plugin-parser-provider-binary PRIVATE
  NEVERC_TEST_PARSER_PROVIDER_BINARY_REPLAY=1
)
add_neverc_parser_provider_fixture(neverc-test-plugin-parser-phase-interceptor)
target_compile_definitions(
  neverc-test-plugin-parser-phase-interceptor PRIVATE
  NEVERC_TEST_PARSER_PHASE_INTERCEPTOR=1
)
add_neverc_parser_provider_fixture(
  neverc-test-plugin-parser-provider-missing-root
)
target_compile_definitions(
  neverc-test-plugin-parser-provider-missing-root PRIVATE
  NEVERC_TEST_PARSER_PROVIDER_MISSING_ROOT=1
)
function(add_neverc_sema_provider_fixture target)
  add_library(${target} MODULE
    Inputs/Plugin/SemaProviderPlugin.c
  )
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(${target} PRIVATE c_std_11)
  set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()
add_neverc_sema_provider_fixture(neverc-test-plugin-sema-provider)
add_neverc_sema_provider_fixture(neverc-test-plugin-sema-provider-custom)
target_compile_definitions(
  neverc-test-plugin-sema-provider-custom PRIVATE
  NEVERC_TEST_SEMA_PROVIDER_CUSTOM_PRODUCT=1
)
add_library(neverc-test-plugin-irgen-provider MODULE
  Inputs/Plugin/IRGenProvider.c
)
target_include_directories(neverc-test-plugin-irgen-provider PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-irgen-provider PRIVATE c_std_11)
set_target_properties(
  neverc-test-plugin-irgen-provider PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-ir-optimization-provider MODULE
  Inputs/Plugin/IROptimizationProvider.c
)
target_include_directories(
  neverc-test-plugin-ir-optimization-provider PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-ir-optimization-provider PRIVATE c_std_11
)
set_target_properties(
  neverc-test-plugin-ir-optimization-provider PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-ir-optimization-invalid MODULE
  Inputs/Plugin/IROptimizationProvider.c
)
target_include_directories(
  neverc-test-plugin-ir-optimization-invalid PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-ir-optimization-invalid PRIVATE c_std_11
)
target_compile_definitions(
  neverc-test-plugin-ir-optimization-invalid PRIVATE
  NEVERC_TEST_IR_OPTIMIZATION_INVALID=1
)
set_target_properties(
  neverc-test-plugin-ir-optimization-invalid PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-ir-pass MODULE
  Inputs/Plugin/IRPassPlugin.c
)
target_include_directories(neverc-test-plugin-ir-pass PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-ir-pass PRIVATE c_std_11)
set_target_properties(
  neverc-test-plugin-ir-pass PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-ir-pass-invalid MODULE
  Inputs/Plugin/IRPassPlugin.c
)
target_include_directories(neverc-test-plugin-ir-pass-invalid PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-ir-pass-invalid PRIVATE c_std_11)
target_compile_definitions(neverc-test-plugin-ir-pass-invalid PRIVATE
  NEVERC_TEST_IR_PASS_INVALID_PRESERVE=1
)
set_target_properties(
  neverc-test-plugin-ir-pass-invalid PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-ir-analysis-cycle MODULE
  Inputs/Plugin/IRPassPlugin.c
)
target_include_directories(neverc-test-plugin-ir-analysis-cycle PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-ir-analysis-cycle PRIVATE c_std_11)
target_compile_definitions(neverc-test-plugin-ir-analysis-cycle PRIVATE
  NEVERC_TEST_IR_ANALYSIS_CYCLE=1
)
set_target_properties(
  neverc-test-plugin-ir-analysis-cycle PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-ir-analysis-recursive MODULE
  Inputs/Plugin/IRPassPlugin.c
)
target_include_directories(neverc-test-plugin-ir-analysis-recursive PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-ir-analysis-recursive PRIVATE c_std_11
)
target_compile_definitions(neverc-test-plugin-ir-analysis-recursive PRIVATE
  NEVERC_TEST_IR_ANALYSIS_RECURSIVE=1
)
set_target_properties(
  neverc-test-plugin-ir-analysis-recursive PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-ir-analysis-error MODULE
  Inputs/Plugin/IRPassPlugin.c
)
target_include_directories(neverc-test-plugin-ir-analysis-error PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-ir-analysis-error PRIVATE c_std_11)
target_compile_definitions(neverc-test-plugin-ir-analysis-error PRIVATE
  NEVERC_TEST_IR_ANALYSIS_ERROR=1
)
set_target_properties(
  neverc-test-plugin-ir-analysis-error PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-ir-analysis-destroy-order MODULE
  Inputs/Plugin/IRPassPlugin.c
)
target_include_directories(
  neverc-test-plugin-ir-analysis-destroy-order PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-ir-analysis-destroy-order PRIVATE c_std_11
)
target_compile_definitions(
  neverc-test-plugin-ir-analysis-destroy-order PRIVATE
  NEVERC_TEST_IR_ANALYSIS_DESTROY_ORDER=1
)
set_target_properties(
  neverc-test-plugin-ir-analysis-destroy-order PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-ir-analysis-mutation MODULE
  Inputs/Plugin/IRPassPlugin.c
)
target_include_directories(
  neverc-test-plugin-ir-analysis-mutation PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-ir-analysis-mutation PRIVATE c_std_11
)
target_compile_definitions(
  neverc-test-plugin-ir-analysis-mutation PRIVATE
  NEVERC_TEST_IR_ANALYSIS_MUTATION=1
)
set_target_properties(
  neverc-test-plugin-ir-analysis-mutation PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-ir-analysis-concurrent MODULE
  Inputs/Plugin/IRConcurrentAnalysisPlugin.cpp
)
target_include_directories(neverc-test-plugin-ir-analysis-concurrent PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-ir-analysis-concurrent PRIVATE cxx_std_17
)
set_target_properties(
  neverc-test-plugin-ir-analysis-concurrent PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-lto-ir-pass MODULE
  Inputs/Plugin/IRLTOPlugin.c
)
target_include_directories(neverc-test-plugin-lto-ir-pass PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-lto-ir-pass PRIVATE c_std_11)
target_compile_definitions(neverc-test-plugin-lto-ir-pass PRIVATE
  NEVERC_TEST_LTO_REQUIRE_PARTITIONS=1
)
set_target_properties(
  neverc-test-plugin-lto-ir-pass PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-lto-ir-pass-error MODULE
  Inputs/Plugin/IRLTOPlugin.c
)
target_include_directories(neverc-test-plugin-lto-ir-pass-error PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-lto-ir-pass-error PRIVATE c_std_11)
target_compile_definitions(neverc-test-plugin-lto-ir-pass-error PRIVATE
  NEVERC_TEST_LTO_PLUGIN_ERROR=1
)
set_target_properties(
  neverc-test-plugin-lto-ir-pass-error PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-mir-pass MODULE
  Inputs/Plugin/MIRPassPlugin.c
)
target_include_directories(neverc-test-plugin-mir-pass PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-mir-pass PRIVATE c_std_11)
set_target_properties(
  neverc-test-plugin-mir-pass PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-mir-pass-invalid MODULE
  Inputs/Plugin/MIRPassPlugin.c
)
target_include_directories(neverc-test-plugin-mir-pass-invalid PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-mir-pass-invalid PRIVATE c_std_11)
target_compile_definitions(neverc-test-plugin-mir-pass-invalid PRIVATE
  NEVERC_TEST_MIR_INVALID_PRESERVE=1
)
set_target_properties(
  neverc-test-plugin-mir-pass-invalid PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-mir-pass-module MODULE
  Inputs/Plugin/MIRPassPlugin.c
)
target_include_directories(neverc-test-plugin-mir-pass-module PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-mir-pass-module PRIVATE c_std_11)
target_compile_definitions(neverc-test-plugin-mir-pass-module PRIVATE
  NEVERC_TEST_MIR_MODULE_LEVEL=1
)
set_target_properties(
  neverc-test-plugin-mir-pass-module PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-mir-pass-basic-block MODULE
  Inputs/Plugin/MIRPassPlugin.c
)
target_include_directories(neverc-test-plugin-mir-pass-basic-block PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-mir-pass-basic-block PRIVATE c_std_11)
target_compile_definitions(neverc-test-plugin-mir-pass-basic-block PRIVATE
  NEVERC_TEST_MIR_BASIC_BLOCK_LEVEL=1
)
set_target_properties(
  neverc-test-plugin-mir-pass-basic-block PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-frontend-lifecycle MODULE
  Inputs/Plugin/FrontendLifecyclePlugin.c
)
target_include_directories(neverc-test-plugin-frontend-lifecycle PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-frontend-lifecycle PRIVATE c_std_11)
set_target_properties(
  neverc-test-plugin-frontend-lifecycle PROPERTIES PREFIX ""
)
function(add_neverc_prep_provider_fixture target)
  add_library(${target} MODULE
    Inputs/Plugin/PreprocessorProviderPlugin.c
  )
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(${target} PRIVATE c_std_11)
  set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()
add_neverc_prep_provider_fixture(neverc-test-plugin-prep-provider)
add_neverc_prep_provider_fixture(neverc-test-plugin-prep-provider-missing-eof)
target_compile_definitions(
  neverc-test-plugin-prep-provider-missing-eof PRIVATE
  NEVERC_TEST_PREP_PROVIDER_OMIT_EOF=1
)
add_neverc_prep_provider_fixture(neverc-test-plugin-prep-provider-unmapped)
target_compile_definitions(
  neverc-test-plugin-prep-provider-unmapped PRIVATE
  NEVERC_TEST_PREP_PROVIDER_INVALID_MAPPING=1
)
add_neverc_prep_provider_fixture(neverc-test-plugin-prep-provider-over-limit)
target_compile_definitions(
  neverc-test-plugin-prep-provider-over-limit PRIVATE
  NEVERC_TEST_PREP_PROVIDER_OVER_LIMIT=1
)
add_neverc_prep_provider_fixture(neverc-test-plugin-prep-provider-error)
target_compile_definitions(
  neverc-test-plugin-prep-provider-error PRIVATE
  NEVERC_TEST_PREP_PROVIDER_ERROR=1
)
add_neverc_vfs_fixture(
  neverc-test-plugin-vfs-bad-size
  org.neverc.test.vfs.bad-size
)
target_compile_definitions(neverc-test-plugin-vfs-bad-size PRIVATE
  NEVERC_TEST_VFS_BAD_SIZE=1
)
add_neverc_vfs_fixture(
  neverc-test-plugin-vfs-error-fallback
  org.neverc.test.vfs.error-fallback
)
target_compile_definitions(neverc-test-plugin-vfs-error-fallback PRIVATE
  NEVERC_TEST_VFS_ERROR_FALLBACK=1
)
add_neverc_vfs_fixture(
  neverc-test-plugin-vfs-recursive
  org.neverc.test.vfs.recursive
)
target_compile_definitions(neverc-test-plugin-vfs-recursive PRIVATE
  NEVERC_TEST_VFS_RECURSIVE=1
)
add_neverc_vfs_fixture(
  neverc-test-plugin-vfs-half-result
  org.neverc.test.vfs.half-result
)
target_compile_definitions(neverc-test-plugin-vfs-half-result PRIVATE
  NEVERC_TEST_VFS_HALF_RESULT=1
)
add_neverc_vfs_fixture(
  neverc-test-plugin-vfs-invalid-disposition
  org.neverc.test.vfs.invalid-disposition
)
target_compile_definitions(
  neverc-test-plugin-vfs-invalid-disposition PRIVATE
  NEVERC_TEST_VFS_INVALID_DISPOSITION=1
)

add_library(neverc-test-plugin-argument-rewrite MODULE
  Inputs/Plugin/ArgumentRewritePlugin.c
)
target_include_directories(neverc-test-plugin-argument-rewrite PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-argument-rewrite PRIVATE c_std_11)
set_target_properties(neverc-test-plugin-argument-rewrite PROPERTIES PREFIX "")
add_library(neverc-test-plugin-argument-observer MODULE
  Inputs/Plugin/ArgumentRewritePlugin.c
)
target_include_directories(neverc-test-plugin-argument-observer PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-argument-observer PRIVATE c_std_11)
target_compile_definitions(neverc-test-plugin-argument-observer PRIVATE
  NEVERC_TEST_ARGUMENT_OBSERVER_ONLY=1
)
set_target_properties(neverc-test-plugin-argument-observer PROPERTIES PREFIX "")
add_library(neverc-test-plugin-parsed-argument-rewrite MODULE
  Inputs/Plugin/ParsedArgumentRewritePlugin.c
)
target_include_directories(
  neverc-test-plugin-parsed-argument-rewrite PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-parsed-argument-rewrite PRIVATE c_std_11
)
set_target_properties(
  neverc-test-plugin-parsed-argument-rewrite PROPERTIES PREFIX ""
)
function(add_neverc_toolchain_fixture target plugin_id)
  add_library(${target} MODULE Inputs/Plugin/ToolChainRewritePlugin.c)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(${target} PRIVATE c_std_11)
  target_compile_definitions(${target} PRIVATE
    NEVERC_TEST_TOOLCHAIN_PLUGIN_ID="${plugin_id}"
  )
  set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()
add_neverc_toolchain_fixture(
  neverc-test-plugin-toolchain-rewrite
  org.neverc.test.toolchain-rewrite
)
add_neverc_toolchain_fixture(
  neverc-test-plugin-toolchain-replace
  org.neverc.test.toolchain-replace
)
target_compile_definitions(neverc-test-plugin-toolchain-replace PRIVATE
  NEVERC_TEST_TOOLCHAIN_REPLACE=1
)
add_neverc_toolchain_fixture(
  neverc-test-plugin-toolchain-invalid
  org.neverc.test.toolchain-invalid
)
target_compile_definitions(neverc-test-plugin-toolchain-invalid PRIVATE
  NEVERC_TEST_TOOLCHAIN_INVALID=1
)
add_neverc_toolchain_fixture(
  neverc-test-plugin-toolchain-custom
  org.neverc.test.toolchain-custom
)
target_compile_definitions(neverc-test-plugin-toolchain-custom PRIVATE
  NEVERC_TEST_TOOLCHAIN_CUSTOM=1
)
function(add_neverc_action_graph_fixture target plugin_id)
  add_library(${target} MODULE Inputs/Plugin/ActionGraphPlugin.c)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(${target} PRIVATE c_std_11)
  target_compile_definitions(${target} PRIVATE
    NEVERC_TEST_ACTION_GRAPH_PLUGIN_ID="${plugin_id}"
  )
  set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()
add_neverc_action_graph_fixture(
  neverc-test-plugin-action-graph-intercept
  org.neverc.test.action-graph-intercept
)
add_neverc_action_graph_fixture(
  neverc-test-plugin-action-graph-observe
  org.neverc.test.action-graph-observe
)
target_compile_definitions(neverc-test-plugin-action-graph-observe PRIVATE
  NEVERC_TEST_ACTION_GRAPH_OBSERVE=1
)
add_neverc_action_graph_fixture(
  neverc-test-plugin-action-graph-replace
  org.neverc.test.action-graph-replace
)
target_compile_definitions(neverc-test-plugin-action-graph-replace PRIVATE
  NEVERC_TEST_ACTION_GRAPH_REPLACE=1
)
add_neverc_action_graph_fixture(
  neverc-test-plugin-action-graph-cycle
  org.neverc.test.action-graph-cycle
)
target_compile_definitions(neverc-test-plugin-action-graph-cycle PRIVATE
  NEVERC_TEST_ACTION_GRAPH_CYCLE=1
)
add_neverc_action_graph_fixture(
  neverc-test-plugin-action-graph-type
  org.neverc.test.action-graph-type
)
target_compile_definitions(neverc-test-plugin-action-graph-type PRIVATE
  NEVERC_TEST_ACTION_GRAPH_TYPE=1
)
function(add_neverc_job_graph_fixture target mode)
  add_library(${target} MODULE Inputs/plugin_job_graph_fixture.cpp)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_definitions(${target} PRIVATE
    NEVERC_JOB_GRAPH_MODE=${mode}
  )
  set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()
add_neverc_job_graph_fixture(neverc-test-plugin-job-graph-remove 1)
add_neverc_job_graph_fixture(neverc-test-plugin-job-graph-rewrite 2)
add_neverc_job_graph_fixture(neverc-test-plugin-job-graph-insert 3)
add_neverc_job_graph_fixture(neverc-test-plugin-job-graph-replace 4)
add_neverc_job_graph_fixture(neverc-test-plugin-job-graph-cycle 5)
add_neverc_job_graph_fixture(neverc-test-plugin-job-graph-duplicate-output 6)
add_neverc_job_graph_fixture(neverc-test-plugin-job-graph-dangling-input 7)
add_library(neverc-test-plugin-replace-frontend-job MODULE
  Inputs/Plugin/ReplaceFrontendJobPlugin.c
)
target_include_directories(neverc-test-plugin-replace-frontend-job PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-replace-frontend-job PRIVATE c_std_11
)
set_target_properties(
  neverc-test-plugin-replace-frontend-job PROPERTIES PREFIX ""
)
function(add_neverc_output_job_fixture target mode)
  add_library(${target} MODULE Inputs/Plugin/OutputJobPlugin.c)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(${target} PRIVATE c_std_11)
  target_compile_definitions(${target} PRIVATE
    NEVERC_TEST_OUTPUT_JOB_MODE=${mode}
  )
  set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()
add_neverc_output_job_fixture(neverc-test-plugin-output-job 1)
add_neverc_output_job_fixture(neverc-test-plugin-output-job-missing 2)
add_neverc_output_job_fixture(neverc-test-plugin-output-job-extra 3)
add_library(neverc-test-plugin-fail-job MODULE
  Inputs/Plugin/FailJobPlugin.c
)
target_include_directories(neverc-test-plugin-fail-job PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-fail-job PRIVATE c_std_11)
set_target_properties(neverc-test-plugin-fail-job PROPERTIES PREFIX "")
add_library(neverc-test-plugin-task-lifecycle MODULE
  Inputs/Plugin/TaskLifecyclePlugin.c
)
target_include_directories(neverc-test-plugin-task-lifecycle PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(
  neverc-test-plugin-task-lifecycle PRIVATE c_std_11
)
set_target_properties(
  neverc-test-plugin-task-lifecycle PROPERTIES PREFIX ""
)
add_library(neverc-test-plugin-shutdown MODULE
  Inputs/Plugin/ShutdownPlugin.c
)
target_include_directories(neverc-test-plugin-shutdown PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-shutdown PRIVATE c_std_11)
set_target_properties(neverc-test-plugin-shutdown PROPERTIES PREFIX "")
add_library(neverc-test-plugin-diagnostic MODULE
  Inputs/Plugin/DiagnosticPlugin.c
)
target_include_directories(neverc-test-plugin-diagnostic PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-test-plugin-diagnostic PRIVATE c_std_11)
set_target_properties(neverc-test-plugin-diagnostic PROPERTIES PREFIX "")
add_neverc_registry_fixture(neverc-test-plugin-scope-session)
target_compile_definitions(neverc-test-plugin-scope-session PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.scope.session"
  NEVERC_TEST_SCOPE_CALLBACKS=1
)
add_neverc_registry_fixture(neverc-test-plugin-scope-thread-safe)
target_compile_definitions(neverc-test-plugin-scope-thread-safe PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.scope.thread-safe"
  NEVERC_TEST_SCOPE_CALLBACKS=1
  NEVERC_TEST_CONCURRENCY=NEVERC_CONCURRENCY_THREAD_SAFE
)
add_neverc_registry_fixture(neverc-test-plugin-scope-process-serial)
target_compile_definitions(neverc-test-plugin-scope-process-serial PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.scope.process-serial"
  NEVERC_TEST_SCOPE_CALLBACKS=1
  NEVERC_TEST_CONCURRENCY=NEVERC_CONCURRENCY_PROCESS_SERIAL
)
add_neverc_registry_fixture(neverc-test-plugin-scope-reentrant)
target_compile_definitions(neverc-test-plugin-scope-reentrant PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.scope.reentrant"
  NEVERC_TEST_SCOPE_CALLBACKS=1
  NEVERC_TEST_REENTRANCY=NEVERC_REENTRANCY_ALLOWED
)
add_neverc_registry_fixture(neverc-test-plugin-session-failure)
target_compile_definitions(neverc-test-plugin-session-failure PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.scope.session-failure"
  NEVERC_TEST_SCOPE_CALLBACKS=1
  NEVERC_TEST_SESSION_BEGIN_FAILURE=1
  NEVERC_TEST_DEPENDENCY_ID="org.neverc.test.scope.session"
  NEVERC_TEST_DEPENDENCY_KIND=NEVERC_DEPENDENCY_REQUIRED
  NEVERC_TEST_DEPENDENCY_MIN_MAJOR=1
  NEVERC_TEST_DEPENDENCY_MIN_MINOR=0
  NEVERC_TEST_DEPENDENCY_MIN_PATCH=0
)
add_neverc_registry_fixture(neverc-test-plugin-task-failure)
target_compile_definitions(neverc-test-plugin-task-failure PRIVATE
  NEVERC_TEST_PLUGIN_ID="org.neverc.test.scope.task-failure"
  NEVERC_TEST_SCOPE_CALLBACKS=1
  NEVERC_TEST_TASK_BEGIN_FAILURE=1
  NEVERC_TEST_DEPENDENCY_ID="org.neverc.test.scope.session"
  NEVERC_TEST_DEPENDENCY_KIND=NEVERC_DEPENDENCY_REQUIRED
  NEVERC_TEST_DEPENDENCY_MIN_MAJOR=1
  NEVERC_TEST_DEPENDENCY_MIN_MINOR=0
  NEVERC_TEST_DEPENDENCY_MIN_PATCH=0
)

add_library(neverc-test-plugin-missing-entry MODULE
  Inputs/Plugin/RegistryMissingEntry.c
)
target_compile_features(neverc-test-plugin-missing-entry PRIVATE c_std_11)
set_target_properties(neverc-test-plugin-missing-entry PROPERTIES PREFIX "")

add_executable(neverc-plugin-core-tests
  PluginArtifactTests.cpp
  PluginBootstrapTests.cpp
  PluginCancellationTests.cpp
  PluginConcurrencyTests.cpp
  PluginDependencyTests.cpp
  PluginDiagnosticTests.cpp
  PluginHandleTests.cpp
  PluginInterfaceTests.cpp
  PluginLLVMOptionSnapshotTests.cpp
  PluginLifecycleTests.cpp
  PluginOptionTests.cpp
  PluginPerformanceTests.cpp
  PluginPhaseExecutionTests.cpp
  PluginPhaseGraphTests.cpp
  PluginPhaseSchemaTests.cpp
  PluginDynCodeABICompileTests.cpp
  PluginProcessServicesTests.cpp
  PluginRegistryTests.cpp
  PluginSessionTests.cpp
)
target_link_libraries(neverc-plugin-core-tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  nevercInvoke
  nevercPluginCore
)
add_dependencies(neverc-plugin-core-tests
  neverc-plugin-dyncode-abi-compile-c11
  neverc-plugin-dyncode-abi-compile-c23
)
target_include_directories(neverc-plugin-core-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/lib/Invoke
)
target_compile_features(neverc-plugin-core-tests PRIVATE cxx_std_17)
target_compile_definitions(neverc-plugin-core-tests PRIVATE
  NEVERC_TEST_MINIMAL_PLUGIN="$<TARGET_FILE:neverc-test-plugin-minimal>"
  NEVERC_TEST_REMOVED_PROTOTYPE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-removed-prototype>"
  NEVERC_TEST_DUPLICATE_ID_PLUGIN="$<TARGET_FILE:neverc-test-plugin-duplicate-id>"
  NEVERC_TEST_OTHER_PLUGIN="$<TARGET_FILE:neverc-test-plugin-other>"
  NEVERC_TEST_WRONG_ABI_PLUGIN="$<TARGET_FILE:neverc-test-plugin-wrong-abi>"
  NEVERC_TEST_SHORT_DESCRIPTOR_PLUGIN="$<TARGET_FILE:neverc-test-plugin-short-descriptor>"
  NEVERC_TEST_PREFIX_DESCRIPTOR_PLUGIN="$<TARGET_FILE:neverc-test-plugin-prefix-descriptor>"
  NEVERC_TEST_LONG_DESCRIPTOR_PLUGIN="$<TARGET_FILE:neverc-test-plugin-long-descriptor>"
  NEVERC_TEST_INVALID_ID_PLUGIN="$<TARGET_FILE:neverc-test-plugin-invalid-id>"
  NEVERC_TEST_REQUIRED_INTERFACE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-required-interface>"
  NEVERC_TEST_MISSING_INTERFACE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-missing-interface>"
  NEVERC_TEST_DEPENDENCY_A_PLUGIN="$<TARGET_FILE:neverc-test-plugin-dependency-a>"
  NEVERC_TEST_DEPENDENCY_B_PLUGIN="$<TARGET_FILE:neverc-test-plugin-dependency-b>"
  NEVERC_TEST_DEPENDENCY_MISSING_PLUGIN="$<TARGET_FILE:neverc-test-plugin-dependency-missing>"
  NEVERC_TEST_DEPENDENCY_MISMATCH_PLUGIN="$<TARGET_FILE:neverc-test-plugin-dependency-mismatch>"
  NEVERC_TEST_CYCLE_A_PLUGIN="$<TARGET_FILE:neverc-test-plugin-cycle-a>"
  NEVERC_TEST_CYCLE_B_PLUGIN="$<TARGET_FILE:neverc-test-plugin-cycle-b>"
  NEVERC_TEST_LIFECYCLE_A_PLUGIN="$<TARGET_FILE:neverc-test-plugin-lifecycle-a>"
  NEVERC_TEST_LIFECYCLE_B_PLUGIN="$<TARGET_FILE:neverc-test-plugin-lifecycle-b>"
  NEVERC_TEST_PROCESS_FAILURE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-process-failure>"
  NEVERC_TEST_REGISTRATION_FAILURE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-registration-failure>"
  NEVERC_TEST_REGISTRATION_USERDATA_PLUGIN="$<TARGET_FILE:neverc-test-plugin-registration-userdata>"
  NEVERC_TEST_REGISTRATION_USERDATA_FAILURE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-registration-userdata-failure>"
  NEVERC_TEST_OPTION_PLUGIN="$<TARGET_FILE:neverc-test-plugin-option>"
  NEVERC_TEST_SCOPE_SESSION_PLUGIN="$<TARGET_FILE:neverc-test-plugin-scope-session>"
  NEVERC_TEST_SCOPE_THREAD_SAFE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-scope-thread-safe>"
  NEVERC_TEST_SCOPE_PROCESS_SERIAL_PLUGIN="$<TARGET_FILE:neverc-test-plugin-scope-process-serial>"
  NEVERC_TEST_SCOPE_REENTRANT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-scope-reentrant>"
  NEVERC_TEST_SESSION_FAILURE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-session-failure>"
  NEVERC_TEST_TASK_FAILURE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-task-failure>"
  NEVERC_TEST_MISSING_ENTRY_PLUGIN="$<TARGET_FILE:neverc-test-plugin-missing-entry>"
)
add_dependencies(neverc-plugin-core-tests
  neverc-test-plugin-minimal
  neverc-test-plugin-removed-prototype
  neverc-test-plugin-duplicate-id
  neverc-test-plugin-other
  neverc-test-plugin-wrong-abi
  neverc-test-plugin-short-descriptor
  neverc-test-plugin-prefix-descriptor
  neverc-test-plugin-long-descriptor
  neverc-test-plugin-invalid-id
  neverc-test-plugin-required-interface
  neverc-test-plugin-missing-interface
  neverc-test-plugin-dependency-a
  neverc-test-plugin-dependency-b
  neverc-test-plugin-dependency-missing
  neverc-test-plugin-dependency-mismatch
  neverc-test-plugin-cycle-a
  neverc-test-plugin-cycle-b
  neverc-test-plugin-lifecycle-a
  neverc-test-plugin-lifecycle-b
  neverc-test-plugin-process-failure
  neverc-test-plugin-registration-failure
  neverc-test-plugin-registration-userdata
  neverc-test-plugin-registration-userdata-failure
  neverc-test-plugin-option
  neverc-test-plugin-scope-session
  neverc-test-plugin-scope-thread-safe
  neverc-test-plugin-scope-process-serial
  neverc-test-plugin-scope-reentrant
  neverc-test-plugin-session-failure
  neverc-test-plugin-task-failure
  neverc-test-plugin-missing-entry
)

gtest_discover_tests(neverc-plugin-core-tests
  PROPERTIES
    TIMEOUT 60
  DISCOVERY_TIMEOUT 30
)

add_executable(neverc-plugin-frontend-tests
  PluginASTMutationTests.cpp
  PluginASTReadTests.cpp
  PluginASTSchemaTests.cpp
  PluginCompilerInstanceOutputTests.cpp
  PluginFrontendABICompileTests.cpp
  PluginFrontendTestSupport.cpp
  PluginMacroTests.cpp
  PluginPrepObserverTests.cpp
  PluginPrepSchemaTests.cpp
  PluginSemaAPITests.cpp
  PluginSourceTests.cpp
  PluginTokenInterceptorUnitTests.cpp
  PluginTokenTests.cpp
)
target_link_libraries(neverc-plugin-frontend-tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  nevercCompiler
  nevercPluginCore
  nevercPluginFrontend
)
target_include_directories(neverc-plugin-frontend-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-frontend-tests PRIVATE cxx_std_17)
if(LLVM_ENABLE_RTTI)
  target_compile_options(neverc-plugin-frontend-tests PRIVATE -frtti)
else()
  target_compile_options(neverc-plugin-frontend-tests PRIVATE -fno-rtti)
endif()
target_compile_definitions(neverc-plugin-frontend-tests PRIVATE
  NEVERC_TEST_MINIMAL_PLUGIN="$<TARGET_FILE:neverc-test-plugin-minimal>"
)
add_dependencies(neverc-plugin-frontend-tests
  neverc-plugin-frontend-abi-compile-c11
  neverc-plugin-frontend-abi-compile-c23
  neverc-test-plugin-minimal
)
gtest_discover_tests(neverc-plugin-frontend-tests
  PROPERTIES
    TIMEOUT 60
    DISCOVERY_TIMEOUT 30
)

add_executable(neverc-plugin-ir-tests
  PluginIRABICompileTests.cpp
  PluginIRBuilderTests.cpp
  PluginIRCoreTests.cpp
  PluginIRDataTests.cpp
  PluginIRHandleTests.cpp
  PluginIRSchemaTests.cpp
  PluginIRSerializationTests.cpp
  PluginMIRBuilderTests.cpp
  PluginMIRHandleTests.cpp
  PluginMIRCoreTests.cpp
  PluginMIRSchemaTests.cpp
  PluginMIRStateTests.cpp
)
target_link_libraries(neverc-plugin-ir-tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  LLVMBitReader
  LLVMBitWriter
  LLVM${LLVM_NATIVE_ARCH}CodeGen
  LLVM${LLVM_NATIVE_ARCH}Desc
  LLVM${LLVM_NATIVE_ARCH}Info
  nevercPluginCore
  nevercPluginIR
)
target_include_directories(neverc-plugin-ir-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/lib/Plugin/IR
)
target_compile_features(neverc-plugin-ir-tests PRIVATE cxx_std_17)
add_dependencies(neverc-plugin-ir-tests
  neverc-plugin-ir-abi-compile-c11
  neverc-plugin-ir-abi-compile-c23
  neverc-plugin-mir-abi-compile-c11
  neverc-plugin-mir-abi-compile-c23
)
gtest_discover_tests(neverc-plugin-ir-tests
  PROPERTIES
    TIMEOUT 60
    DISCOVERY_TIMEOUT 30
)

add_executable(neverc-plugin-target-mc-tests
  PluginABILoweringTests.cpp
  PluginAssemblyProviderTests.cpp
  PluginBuiltinTargetProviderTests.cpp
  PluginCallingConventionTests.cpp
  PluginCodeGenRouteTests.cpp
  PluginIRToMIRProviderTests.cpp
  PluginMIRToMCProviderTests.cpp
  PluginBuiltinObjectFormatTests.cpp
  PluginMCBridgeTests.cpp
  PluginMCBuilderTests.cpp
  PluginMCComponentProviderTests.cpp
  PluginMCEmissionHookTests.cpp
  PluginMCEncodeDecodeTests.cpp
  PluginMCHandleTests.cpp
  PluginMCLayoutTests.cpp
  PluginMCSchemaTests.cpp
  PluginObjectBuilderTests.cpp
  PluginObjectReaderTests.cpp
  PluginObjectSchemaTests.cpp
  PluginObjectWriterTests.cpp
  PluginTargetInfoTests.cpp
  PluginTargetLanguageTests.cpp
  PluginTargetMCABICompileTests.cpp
  PluginTargetDescriptorTests.cpp
  PluginTargetRegistryTests.cpp
  PluginTargetSchemaTests.cpp
)
target_link_libraries(neverc-plugin-target-mc-tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  LLVMTarget
  nevercCompiler
  nevercEmit
  nevercPluginMCObject
  nevercPluginTarget
)
if("AArch64" IN_LIST LLVM_TARGETS_TO_BUILD)
  target_link_libraries(neverc-plugin-target-mc-tests PRIVATE
    LLVMAArch64AsmParser
    LLVMAArch64CodeGen
    LLVMAArch64Desc
    LLVMAArch64Info
    LLVMAArch64Utils
  )
endif()
if("X86" IN_LIST LLVM_TARGETS_TO_BUILD)
  target_link_libraries(neverc-plugin-target-mc-tests PRIVATE
    LLVMX86AsmParser
    LLVMX86CodeGen
    LLVMX86Desc
    LLVMX86Info
  )
endif()
target_include_directories(neverc-plugin-target-mc-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-target-mc-tests PRIVATE cxx_std_17)
if(MSVC)
  set_source_files_properties(
    PluginMCComponentProviderTests.cpp
    PluginMCEmissionHookTests.cpp
    PluginMCEncodeDecodeTests.cpp
    PROPERTIES COMPILE_OPTIONS "/GR-"
    )
else()
  set_source_files_properties(
    PluginMCComponentProviderTests.cpp
    PluginMCEmissionHookTests.cpp
    PluginMCEncodeDecodeTests.cpp
    PROPERTIES COMPILE_OPTIONS "-fno-rtti"
    )
endif()
target_compile_definitions(neverc-plugin-target-mc-tests PRIVATE
  NEVERC_SOURCE_DIR="${CMAKE_SOURCE_DIR}/.."
  NEVERC_PLUGIN_TARGET_SCHEMA_ROOT="${CMAKE_SOURCE_DIR}/../pluginsdk/schemas/targets"
  NEVERC_PLUGIN_TARGET_SCHEMA_GENERATOR="$<TARGET_FILE:neverc-plugin-target-schema-gen>"
  NEVERC_TEST_MINIMAL_PLUGIN="$<TARGET_FILE:neverc-test-plugin-minimal>"
  NEVERC_TEST_TARGET_VALID_PLUGIN="$<TARGET_FILE:neverc-test-plugin-target-valid>"
  NEVERC_TEST_TARGET_LANGUAGE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-target-language>"
  NEVERC_TEST_ASSEMBLY_PROVIDER_PLUGIN="$<TARGET_FILE:neverc-test-plugin-assembly-provider>"
  NEVERC_TEST_MC_EMISSION_HOOK_PLUGIN="$<TARGET_FILE:neverc-test-plugin-mc-emission-hook>"
  NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-object-post-write>"
  NEVERC_TEST_NEW_BACKEND_PLUGIN="$<TARGET_FILE:PluginTestTarget>"
  NEVERC_TEST_TARGET_UNKNOWN_FORMAT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-target-unknown-format>"
  NEVERC_TEST_TARGET_REGISTRATION_FAILURE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-target-registration-failure>"
)
add_dependencies(neverc-plugin-target-mc-tests
  neverc-plugin-target-abi-compile-c11
  neverc-plugin-target-abi-compile-c23
  neverc-plugin-mc-abi-compile-c11
  neverc-plugin-mc-abi-compile-c23
  neverc-plugin-object-abi-compile-c11
  neverc-plugin-object-abi-compile-c23
  neverc-test-plugin-minimal
  neverc-test-plugin-target-valid
  neverc-test-plugin-target-language
  neverc-test-plugin-assembly-provider
  neverc-test-plugin-mc-emission-hook
  neverc-test-plugin-object-post-write
  PluginTestTarget
  neverc-test-plugin-target-unknown-format
  neverc-test-plugin-target-registration-failure
  neverc-plugin-target-schema-gen
)
gtest_discover_tests(neverc-plugin-target-mc-tests
  PROPERTIES
    TIMEOUT 60
    DISCOVERY_TIMEOUT 30
)

add_executable(neverc-plugin-dyncode-tests
  PluginDynCodeRequestTests.cpp
  PluginDynCodePhaseTests.cpp
  PluginDynCodeIRStageTests.cpp
  PluginDynCodeIRProviderTests.cpp
  PluginDynCodeImportTests.cpp
)
target_link_libraries(neverc-plugin-dyncode-tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  LLVMCore
  nevercDynCode
  nevercPluginCore
  nevercPluginIO
  nevercPluginTarget
)
target_include_directories(neverc-plugin-dyncode-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/lib/DynCode/Plugin
)
target_compile_features(neverc-plugin-dyncode-tests PRIVATE cxx_std_17)
# PluginDynCodeIRStageTests subclasses llvm::DiagnosticHandler; LLVM is built
# without RTTI, so the file that derives from an LLVM polymorphic type must also
# be compiled without RTTI to avoid an undefined base-class typeinfo at link.
if(MSVC)
  set_source_files_properties(PluginDynCodeIRStageTests.cpp
    PROPERTIES COMPILE_OPTIONS "/GR-")
else()
  set_source_files_properties(PluginDynCodeIRStageTests.cpp
    PROPERTIES COMPILE_OPTIONS "-fno-rtti")
endif()
target_compile_definitions(neverc-plugin-dyncode-tests PRIVATE
  NEVERC_TEST_DYNCODE_PHASE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-dyncode-phase>"
)
add_dependencies(neverc-plugin-dyncode-tests
  neverc-test-plugin-dyncode-phase
)
gtest_discover_tests(neverc-plugin-dyncode-tests
  PROPERTIES
    TIMEOUT 60
    DISCOVERY_TIMEOUT 30
)

add_executable(neverc-plugin-link-tests
  Inputs/Plugin/GCICFPlugin.c
  Inputs/Plugin/InvalidLinkMutationPlugin.c
  Inputs/Plugin/LinkLayoutPlugin.c
  Inputs/Plugin/LinkPhaseTracePlugin.c
  Inputs/Plugin/PostEmitPlugin.c
  Inputs/Plugin/SymbolResolutionPlugin.c
  Inputs/Plugin/SyntheticLinkPlugin.c
  PluginCOFFContextIsolationTests.cpp
  PluginBinaryImageTests.cpp
  PluginELFContextIsolationTests.cpp
  PluginLinkABICompileTests.cpp
  PluginLinkContextTests.cpp
  PluginLinkGraphTests.cpp
  PluginLinkGCICFTests.cpp
  PluginLinkInputTests.cpp
  PluginLinkLayoutTests.cpp
  PluginLinkMutationTests.cpp
  PluginMachOContextIsolationTests.cpp
  PluginParallelLinkTests.cpp
  PluginLinkPhaseTests.cpp
  PluginLinkRouteTests.cpp
  PluginLinkSynthesisTests.cpp
  PluginLinkVerifierTests.cpp
  PluginSymbolResolutionTests.cpp
)
target_link_libraries(neverc-plugin-link-tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  linkerCOFF
  linkerCore
  linkerELF
  linkerMachO
  nevercPluginIO
  nevercPluginLink
)
target_include_directories(neverc-plugin-link-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include/neverc
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/lib/Plugin
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/lib/Linker/Backends/COFF
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/lib/Linker/Backends/ELF
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/lib/Linker/Backends/MachO
)
target_compile_features(neverc-plugin-link-tests PRIVATE cxx_std_17)
target_compile_definitions(neverc-plugin-link-tests PRIVATE
  NEVERC_TEST_LINK_ROUTE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-link-route>"
  NEVERC_TEST_VIRTUAL_ARCHIVE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-virtual-archive>"
)
add_dependencies(neverc-plugin-link-tests
  neverc-plugin-link-abi-compile-c11
  neverc-plugin-link-abi-compile-c23
  neverc-plugin-lto-abi-compile-c11
  neverc-plugin-lto-abi-compile-c23
  neverc-test-plugin-link-route
  neverc-test-plugin-virtual-archive
)
gtest_discover_tests(neverc-plugin-link-tests
  PROPERTIES
    TIMEOUT 60
    DISCOVERY_TIMEOUT 30
)

add_executable(neverc-plugin-object-merge-tests
  PluginObjectMergeTests.cpp
)
target_link_libraries(neverc-plugin-object-merge-tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  nevercPluginCore
  nevercPluginLink
  nevercPluginMCObject
)
if("AArch64" IN_LIST LLVM_TARGETS_TO_BUILD)
  target_link_libraries(neverc-plugin-object-merge-tests PRIVATE
    LLVMAArch64AsmParser
    LLVMAArch64CodeGen
    LLVMAArch64Desc
    LLVMAArch64Info
    LLVMAArch64Utils
  )
endif()
if("X86" IN_LIST LLVM_TARGETS_TO_BUILD)
  target_link_libraries(neverc-plugin-object-merge-tests PRIVATE
    LLVMX86AsmParser
    LLVMX86CodeGen
    LLVMX86Desc
    LLVMX86Info
  )
endif()
target_include_directories(neverc-plugin-object-merge-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/lib/Plugin
)
target_compile_features(
  neverc-plugin-object-merge-tests PRIVATE cxx_std_17
)
target_compile_definitions(neverc-plugin-object-merge-tests PRIVATE
  NEVERC_TEST_OBJECT_MERGE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-object-merge>"
)
add_dependencies(neverc-plugin-object-merge-tests
  neverc-test-plugin-object-merge
)
gtest_discover_tests(neverc-plugin-object-merge-tests
  PROPERTIES
    TIMEOUT 60
    DISCOVERY_TIMEOUT 30
)

add_executable(neverc-plugin-lto-input-tests
  PluginLTOInputTests.cpp
)
target_link_libraries(neverc-plugin-lto-input-tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  LLVMBitWriter
  LLVMCore
  nevercPluginCore
  nevercPluginLink
  nevercPluginMCObject
)
target_include_directories(neverc-plugin-lto-input-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/lib/Plugin
)
target_compile_features(
  neverc-plugin-lto-input-tests PRIVATE cxx_std_17
)
gtest_discover_tests(neverc-plugin-lto-input-tests
  PROPERTIES
    TIMEOUT 60
    DISCOVERY_TIMEOUT 30
)

add_executable(neverc-plugin-io-tests
  PluginIOIsolationTests.cpp
  PluginOutputSinkTests.cpp
  PluginProviderVFSTests.cpp
  PluginVFSTests.cpp
)
target_link_libraries(neverc-plugin-io-tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  nevercPluginCore
  nevercPluginIO
)
target_include_directories(neverc-plugin-io-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)
target_compile_features(neverc-plugin-io-tests PRIVATE cxx_std_17)
target_compile_definitions(neverc-plugin-io-tests PRIVATE
  NEVERC_TEST_VFS_PLUGIN="$<TARGET_FILE:neverc-test-plugin-vfs>"
  NEVERC_TEST_VFS_BAD_SIZE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-vfs-bad-size>"
  NEVERC_TEST_VFS_ERROR_FALLBACK_PLUGIN="$<TARGET_FILE:neverc-test-plugin-vfs-error-fallback>"
  NEVERC_TEST_VFS_RECURSIVE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-vfs-recursive>"
  NEVERC_TEST_VFS_HALF_RESULT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-vfs-half-result>"
  NEVERC_TEST_VFS_INVALID_DISPOSITION_PLUGIN="$<TARGET_FILE:neverc-test-plugin-vfs-invalid-disposition>"
)
add_dependencies(neverc-plugin-io-tests
  neverc-test-plugin-vfs
  neverc-test-plugin-vfs-bad-size
  neverc-test-plugin-vfs-error-fallback
  neverc-test-plugin-vfs-recursive
  neverc-test-plugin-vfs-half-result
  neverc-test-plugin-vfs-invalid-disposition
)
gtest_discover_tests(neverc-plugin-io-tests
  PROPERTIES
    TIMEOUT 60
    DISCOVERY_TIMEOUT 30
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
  NAME PluginPhaseSchemaGeneratorTests
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/PluginPhaseSchemaGeneratorTests.py
)
add_test(
  NAME PluginPrepSchemaGeneratorTests
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/plugin-api/gen-prep-schema.py
          --check
)
add_test(
  NAME PluginASTSchemaGeneratorTests
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/plugin-api/gen-ast-schema.py
          --check
)
add_test(
  NAME PluginPrepObserverCoverageTests
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/PluginPrepObserverCoverageTests.py
)
add_test(
  NAME PluginCoverageManifestUnitTests
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/PluginCoverageManifestTests.py
)
add_test(
  NAME PluginCoverageManifest
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/plugin-api/check-coverage.py
          --schema
          ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
          --manifest
          ${CMAKE_CURRENT_SOURCE_DIR}/../../docs/plugin-api/coverage.json
          --ctest-binary-dir
          ${CMAKE_CURRENT_BINARY_DIR}
          --ctest
          ${CMAKE_CTEST_COMMAND}
)
add_test(
  NAME PluginSDKGenerationSingleHeaderCheck
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/plugin-api/gen-single-header.py
          --check
)
add_test(
  NAME PluginSDKGenerationManifestCheck
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/plugin-api/gen-sdk-manifest.py
          --check
)
add_test(
  NAME PluginSDKGenerationConsistency
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/plugin-api/check-single-header.py
)
set_tests_properties(
  PluginPhaseSchemaGeneratorTests
  PluginPrepSchemaGeneratorTests
  PluginASTSchemaGeneratorTests
  PluginPrepObserverCoverageTests
  PluginCoverageManifestUnitTests
  PluginCoverageManifest
  PluginSDKGenerationSingleHeaderCheck
  PluginSDKGenerationManifestCheck
  PluginSDKGenerationConsistency
  PROPERTIES TIMEOUT 60
)

# Dependency-free contract headers (e.g. LTOCacheContract.h) shared with
# the product code so tests never restate env var names or file naming.
target_include_directories(neverc-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)

target_compile_features(neverc-tests PRIVATE cxx_std_17)

target_compile_definitions(neverc-tests PRIVATE
  NEVERC_BINARY="${_NEVERC_BINARY}"
  TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  TEST_C_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../C"
  NEVERC_PLUGINSDK_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../pluginsdk"
  NEVERC_NEVERC_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include"
  NEVERC_PLUGINSDK_SINGLE_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/../../pluginsdk/include/neverc/Plugin/NevercPluginAPI.h"
  NEVERC_CMAKE_COMMAND="${CMAKE_COMMAND}"
  NEVERC_BUILD_DIR="${CMAKE_BINARY_DIR}"
  NEVERC_PYTHON="${Python3_EXECUTABLE}"
  NEVERC_TEST_INSTALLED_SDK_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/../../utils/plugin-api/test-installed-sdk.py"
  NEVERC_TEST_ARGUMENT_REWRITE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-argument-rewrite>"
  NEVERC_TEST_PARSED_ARGUMENT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-parsed-argument-rewrite>"
  NEVERC_TEST_TOOLCHAIN_REWRITE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-toolchain-rewrite>"
  NEVERC_TEST_TOOLCHAIN_REPLACE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-toolchain-replace>"
  NEVERC_TEST_TOOLCHAIN_INVALID_PLUGIN="$<TARGET_FILE:neverc-test-plugin-toolchain-invalid>"
  NEVERC_TEST_TOOLCHAIN_CUSTOM_PLUGIN="$<TARGET_FILE:neverc-test-plugin-toolchain-custom>"
  NEVERC_TEST_ACTION_GRAPH_INTERCEPT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-action-graph-intercept>"
  NEVERC_TEST_ACTION_GRAPH_OBSERVE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-action-graph-observe>"
  NEVERC_TEST_ACTION_GRAPH_REPLACE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-action-graph-replace>"
  NEVERC_TEST_ACTION_GRAPH_CYCLE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-action-graph-cycle>"
  NEVERC_TEST_ACTION_GRAPH_TYPE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-action-graph-type>"
  NEVERC_TEST_JOB_GRAPH_REMOVE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-job-graph-remove>"
  NEVERC_TEST_JOB_GRAPH_REWRITE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-job-graph-rewrite>"
  NEVERC_TEST_JOB_GRAPH_INSERT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-job-graph-insert>"
  NEVERC_TEST_JOB_GRAPH_REPLACE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-job-graph-replace>"
  NEVERC_TEST_JOB_GRAPH_CYCLE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-job-graph-cycle>"
  NEVERC_TEST_JOB_GRAPH_DUPLICATE_OUTPUT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-job-graph-duplicate-output>"
  NEVERC_TEST_JOB_GRAPH_DANGLING_INPUT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-job-graph-dangling-input>"
  NEVERC_TEST_REPLACE_FRONTEND_JOB_PLUGIN="$<TARGET_FILE:neverc-test-plugin-replace-frontend-job>"
  NEVERC_TEST_OUTPUT_JOB_PLUGIN="$<TARGET_FILE:neverc-test-plugin-output-job>"
  NEVERC_TEST_OUTPUT_JOB_MISSING_PLUGIN="$<TARGET_FILE:neverc-test-plugin-output-job-missing>"
  NEVERC_TEST_OUTPUT_JOB_EXTRA_PLUGIN="$<TARGET_FILE:neverc-test-plugin-output-job-extra>"
  NEVERC_TEST_FAIL_JOB_PLUGIN="$<TARGET_FILE:neverc-test-plugin-fail-job>"
  NEVERC_TEST_TASK_LIFECYCLE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-task-lifecycle>"
  NEVERC_TEST_SHUTDOWN_PLUGIN="$<TARGET_FILE:neverc-test-plugin-shutdown>"
  NEVERC_TEST_DIAGNOSTIC_PLUGIN="$<TARGET_FILE:neverc-test-plugin-diagnostic>"
  NEVERC_TEST_DRIVER_WRONG_ABI_PLUGIN="$<TARGET_FILE:neverc-test-plugin-wrong-abi>"
  NEVERC_TEST_DRIVER_TRACE_EXAMPLE_PLUGIN="$<TARGET_FILE:neverc-plugin-example-driver-trace>"
  NEVERC_TEST_VFS_PLUGIN="$<TARGET_FILE:neverc-test-plugin-vfs>"
  NEVERC_TEST_SOURCE_PHASE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-source-phase>"
  NEVERC_TEST_TOKEN_REWRITE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-token-rewrite>"
  NEVERC_TEST_PREP_HOOK_PLUGIN="$<TARGET_FILE:neverc-test-plugin-prep-hooks>"
  NEVERC_TEST_PARSER_INTERCEPTOR_PLUGIN="$<TARGET_FILE:neverc-test-plugin-parser-interceptor>"
  NEVERC_TEST_SEMA_INTERCEPTOR_PLUGIN="$<TARGET_FILE:neverc-test-plugin-sema-interceptor>"
  NEVERC_TEST_PARSER_PROVIDER_PLUGIN="$<TARGET_FILE:neverc-test-plugin-parser-provider>"
  NEVERC_TEST_PARSER_PROVIDER_BINARY_PLUGIN="$<TARGET_FILE:neverc-test-plugin-parser-provider-binary>"
  NEVERC_TEST_PARSER_PROVIDER_MISSING_ROOT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-parser-provider-missing-root>"
  NEVERC_TEST_PARSER_PHASE_INTERCEPTOR_PLUGIN="$<TARGET_FILE:neverc-test-plugin-parser-phase-interceptor>"
  NEVERC_TEST_SEMA_PROVIDER_PLUGIN="$<TARGET_FILE:neverc-test-plugin-sema-provider>"
  NEVERC_TEST_SEMA_PROVIDER_CUSTOM_PLUGIN="$<TARGET_FILE:neverc-test-plugin-sema-provider-custom>"
  NEVERC_TEST_IRGEN_PROVIDER_PLUGIN="$<TARGET_FILE:neverc-test-plugin-irgen-provider>"
  NEVERC_TEST_IR_OPTIMIZATION_PROVIDER_PLUGIN="$<TARGET_FILE:neverc-test-plugin-ir-optimization-provider>"
  NEVERC_TEST_IR_OPTIMIZATION_INVALID_PLUGIN="$<TARGET_FILE:neverc-test-plugin-ir-optimization-invalid>"
  NEVERC_TEST_IR_PASS_PLUGIN="$<TARGET_FILE:neverc-test-plugin-ir-pass>"
  NEVERC_TEST_IR_PASS_INVALID_PLUGIN="$<TARGET_FILE:neverc-test-plugin-ir-pass-invalid>"
  NEVERC_TEST_IR_ANALYSIS_CYCLE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-ir-analysis-cycle>"
  NEVERC_TEST_IR_ANALYSIS_RECURSIVE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-ir-analysis-recursive>"
  NEVERC_TEST_IR_ANALYSIS_ERROR_PLUGIN="$<TARGET_FILE:neverc-test-plugin-ir-analysis-error>"
  NEVERC_TEST_IR_ANALYSIS_DESTROY_ORDER_PLUGIN="$<TARGET_FILE:neverc-test-plugin-ir-analysis-destroy-order>"
  NEVERC_TEST_IR_ANALYSIS_MUTATION_PLUGIN="$<TARGET_FILE:neverc-test-plugin-ir-analysis-mutation>"
  NEVERC_TEST_IR_ANALYSIS_CONCURRENT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-ir-analysis-concurrent>"
  NEVERC_TEST_LTO_IR_PASS_PLUGIN="$<TARGET_FILE:neverc-test-plugin-lto-ir-pass>"
  NEVERC_TEST_LTO_IR_PASS_ERROR_PLUGIN="$<TARGET_FILE:neverc-test-plugin-lto-ir-pass-error>"
  NEVERC_TEST_MIR_PASS_PLUGIN="$<TARGET_FILE:neverc-test-plugin-mir-pass>"
  NEVERC_TEST_MIR_PASS_INVALID_PLUGIN="$<TARGET_FILE:neverc-test-plugin-mir-pass-invalid>"
  NEVERC_TEST_MIR_PASS_MODULE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-mir-pass-module>"
  NEVERC_TEST_MIR_PASS_BASIC_BLOCK_PLUGIN="$<TARGET_FILE:neverc-test-plugin-mir-pass-basic-block>"
  NEVERC_TEST_TARGET_VALID_PLUGIN="$<TARGET_FILE:neverc-test-plugin-target-valid>"
  NEVERC_TEST_TARGET_LANGUAGE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-target-language>"
  NEVERC_TEST_ASSEMBLY_PROVIDER_PLUGIN="$<TARGET_FILE:neverc-test-plugin-assembly-provider>"
  NEVERC_TEST_OBJECT_POST_WRITE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-object-post-write>"
  NEVERC_TEST_NEW_BACKEND_PLUGIN="$<TARGET_FILE:PluginTestTarget>"
  NEVERC_TEST_TARGET_INDIRECT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-target-indirect>"
  NEVERC_TEST_TARGET_COERCE_EXPAND_PLUGIN="$<TARGET_FILE:neverc-test-plugin-target-coerce-expand>"
  NEVERC_TEST_TARGET_MS_VA_PLUGIN="$<TARGET_FILE:neverc-test-plugin-target-ms-va>"
  NEVERC_TEST_TARGET_INDIRECT_ALIASED_PLUGIN="$<TARGET_FILE:neverc-test-plugin-target-indirect-aliased>"
  NEVERC_TEST_TARGET_CC_PLAN_PLUGIN="$<TARGET_FILE:neverc-test-plugin-target-cc-plan>"
  NEVERC_TEST_FRONTEND_LIFECYCLE_PLUGIN="$<TARGET_FILE:neverc-test-plugin-frontend-lifecycle>"
  NEVERC_TEST_PREP_PROVIDER_PLUGIN="$<TARGET_FILE:neverc-test-plugin-prep-provider>"
  NEVERC_TEST_PREP_PROVIDER_MISSING_EOF_PLUGIN="$<TARGET_FILE:neverc-test-plugin-prep-provider-missing-eof>"
  NEVERC_TEST_PREP_PROVIDER_UNMAPPED_PLUGIN="$<TARGET_FILE:neverc-test-plugin-prep-provider-unmapped>"
  NEVERC_TEST_PREP_PROVIDER_OVER_LIMIT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-prep-provider-over-limit>"
  NEVERC_TEST_PREP_PROVIDER_ERROR_PLUGIN="$<TARGET_FILE:neverc-test-plugin-prep-provider-error>"
  NEVERC_TEST_EMPTY_PLUGIN="$<TARGET_FILE:neverc-test-plugin-minimal>"
  NEVERC_TEST_ARGUMENT_OBSERVER_PLUGIN="$<TARGET_FILE:neverc-test-plugin-argument-observer>"
)

neverc_tests_link_neverc(neverc-tests)
add_dependencies(neverc-tests
  neverc-test-plugin-argument-rewrite
  neverc-test-plugin-parsed-argument-rewrite
  neverc-test-plugin-toolchain-rewrite
  neverc-test-plugin-toolchain-replace
  neverc-test-plugin-toolchain-invalid
  neverc-test-plugin-toolchain-custom
  neverc-test-plugin-action-graph-intercept
  neverc-test-plugin-action-graph-observe
  neverc-test-plugin-action-graph-replace
  neverc-test-plugin-action-graph-cycle
  neverc-test-plugin-action-graph-type
  neverc-test-plugin-job-graph-remove
  neverc-test-plugin-job-graph-rewrite
  neverc-test-plugin-job-graph-insert
  neverc-test-plugin-job-graph-replace
  neverc-test-plugin-job-graph-cycle
  neverc-test-plugin-job-graph-duplicate-output
  neverc-test-plugin-job-graph-dangling-input
  neverc-test-plugin-replace-frontend-job
  neverc-test-plugin-output-job
  neverc-test-plugin-output-job-missing
  neverc-test-plugin-output-job-extra
  neverc-test-plugin-fail-job
  neverc-test-plugin-task-lifecycle
  neverc-test-plugin-shutdown
  neverc-test-plugin-diagnostic
  neverc-test-plugin-wrong-abi
  neverc-plugin-example-driver-trace
  neverc-test-plugin-vfs
  neverc-test-plugin-source-phase
  neverc-test-plugin-token-rewrite
  neverc-test-plugin-prep-hooks
  neverc-test-plugin-parser-interceptor
  neverc-test-plugin-sema-interceptor
  neverc-test-plugin-parser-provider
  neverc-test-plugin-parser-provider-binary
  neverc-test-plugin-parser-provider-missing-root
  neverc-test-plugin-parser-phase-interceptor
  neverc-test-plugin-sema-provider
  neverc-test-plugin-sema-provider-custom
  neverc-test-plugin-irgen-provider
  neverc-test-plugin-ir-optimization-provider
  neverc-test-plugin-ir-optimization-invalid
  neverc-test-plugin-ir-pass
  neverc-test-plugin-ir-pass-invalid
  neverc-test-plugin-ir-analysis-cycle
  neverc-test-plugin-ir-analysis-recursive
  neverc-test-plugin-ir-analysis-error
  neverc-test-plugin-ir-analysis-destroy-order
  neverc-test-plugin-ir-analysis-mutation
  neverc-test-plugin-ir-analysis-concurrent
  neverc-test-plugin-lto-ir-pass
  neverc-test-plugin-lto-ir-pass-error
  neverc-test-plugin-mir-pass
  neverc-test-plugin-mir-pass-invalid
  neverc-test-plugin-mir-pass-module
  neverc-test-plugin-mir-pass-basic-block
  neverc-test-plugin-target-valid
  neverc-test-plugin-target-language
  neverc-test-plugin-assembly-provider
  neverc-test-plugin-object-post-write
  PluginTestTarget
  neverc-test-plugin-target-indirect
  neverc-test-plugin-target-coerce-expand
  neverc-test-plugin-target-ms-va
  neverc-test-plugin-target-indirect-aliased
  neverc-test-plugin-target-cc-plan
  neverc-test-plugin-frontend-lifecycle
  neverc-test-plugin-prep-provider
  neverc-test-plugin-prep-provider-missing-eof
  neverc-test-plugin-prep-provider-unmapped
  neverc-test-plugin-prep-provider-over-limit
  neverc-test-plugin-prep-provider-error
  neverc-test-plugin-minimal
  neverc-test-plugin-argument-observer
)

gtest_discover_tests(neverc-tests
  PROPERTIES
    TIMEOUT 300
  DISCOVERY_TIMEOUT 30
)

# Build module unit tests (Lexer, Parser, VariableEnv, RuleDB, DepGraph)
add_executable(neverc-build-tests BuildTests.cpp)

target_link_libraries(neverc-build-tests PRIVATE
  GTest::gtest GTest::gtest_main
  nevercBuild
)

target_compile_features(neverc-build-tests PRIVATE cxx_std_17)

gtest_discover_tests(neverc-build-tests
  PROPERTIES
    TIMEOUT 120
  DISCOVERY_TIMEOUT 30
)

# Object merger unit tests (ELF, COFF, MachO fuzzing + edge cases)
add_executable(neverc-merge-tests MergeTests.cpp)

target_link_libraries(neverc-merge-tests PRIVATE
  GTest::gtest GTest::gtest_main
  nevercMerge
  ${NEVERC_COMPILER_RT_BUILTINS}
)

target_include_directories(neverc-merge-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)

# NEVERC_BINARY lets the differential suite spawn the bundled compiler+linker
# to produce real objects and an independent LLD `-r` merge to cross-check the
# in-process merger against.  Tests that need it GTEST_SKIP when it is absent.
# TEST_SOURCE_DIR locates the committed merge-fuzzer regression corpus
# (merge-corpus/) that the replay test feeds back through the merger.
target_compile_definitions(neverc-merge-tests PRIVATE
  NEVERC_BINARY="${_NEVERC_BINARY}"
  TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

neverc_tests_link_neverc(neverc-merge-tests)

target_compile_features(neverc-merge-tests PRIVATE cxx_std_17)

gtest_discover_tests(neverc-merge-tests
  PROPERTIES
    TIMEOUT 120
  DISCOVERY_TIMEOUT 30
)

# Optional libFuzzer target for continuous coverage-guided fuzzing of the
# object merger + independent self-verifier.  OFF by default (it needs a
# fuzzer-capable compiler and its own run loop, so it must not join the normal
# gtest build).  Enable with:
#   cmake -DNEVERC_ENABLE_MERGE_FUZZER=ON ...
#   ninja neverc-merge-fuzzer
#   ./bin/neverc-merge-fuzzer -max_len=16384 corpus/
# For full coverage feedback into the merger itself, configure the whole build
# with -fsanitize=fuzzer-no-link so libnevercMerge is instrumented too.
option(NEVERC_ENABLE_MERGE_FUZZER
  "Build the libFuzzer target for the object merger" OFF)
if(NEVERC_ENABLE_MERGE_FUZZER)
  add_executable(neverc-merge-fuzzer MergeFuzzer.cpp)
  target_link_libraries(neverc-merge-fuzzer PRIVATE
    nevercMerge
    ${NEVERC_COMPILER_RT_BUILTINS}
  )
  target_include_directories(neverc-merge-fuzzer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(neverc-merge-fuzzer PRIVATE cxx_std_17)
  target_compile_options(neverc-merge-fuzzer PRIVATE -fsanitize=fuzzer-no-link)
  target_link_options(neverc-merge-fuzzer PRIVATE -fsanitize=fuzzer)
  # Deliberately NO add_dependencies(... neverc): unlike neverc-merge-tests, the
  # fuzzer calls neverc::merge::mergeObjects in-process and never spawns the
  # NEVERC_BINARY, so it only needs libnevercMerge (+ its LLVM Object/Support
  # deps).  Forcing a dependency on the full compiler would make every
  # `ninja neverc-merge-fuzzer` (and the CI fuzz job) build all of clang/codegen/
  # the linker for nothing, turning a ~minutes instrumented build into a ~hour
  # one.  Keeping the fuzzer standalone is what lets the merge-fuzz CI workflow
  # build just the code under test with ASan/UBSan/coverage.
endif()

# Standard library runtime tests — compile std/tests/*.c with neverc, run, verify
add_executable(neverc-std-tests
  NeverCTestFixture.cpp
  StdLibTests.cpp
)

target_link_libraries(neverc-std-tests PRIVATE GTest::gtest GTest::gtest_main)

target_compile_features(neverc-std-tests PRIVATE cxx_std_17)

target_compile_definitions(neverc-std-tests PRIVATE
  NEVERC_BINARY="${_NEVERC_BINARY}"
  TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  TEST_C_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../C"
  NEVERC_PLUGINSDK_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../pluginsdk"
  NEVERC_NEVERC_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include"
)

neverc_tests_link_neverc(neverc-std-tests)
if(TARGET neverc-std-headers)
  add_dependencies(neverc-std-tests neverc-std-headers)
endif()

gtest_discover_tests(neverc-std-tests
  PROPERTIES
    TIMEOUT 600
  DISCOVERY_TIMEOUT 30
)
