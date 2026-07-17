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
  PluginArgumentPhaseTests.cpp
  PluginToolChainTests.cpp
  PluginSourcePhaseTests.cpp
  PluginTokenInterceptorTests.cpp
  PluginPrepHookInterceptorTests.cpp
  PluginParserInterceptorTests.cpp
  PluginParserProviderTests.cpp
  PluginSemaInterceptorTests.cpp
  PluginPreprocessorProviderTests.cpp
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

function(add_neverc_registry_fixture target)
  add_library(${target} MODULE Inputs/Plugin/RegistryFixturePlugin.c)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(${target} PRIVATE c_std_11)
  set_target_properties(${target} PROPERTIES PREFIX "")
endfunction()

add_neverc_registry_fixture(neverc-test-plugin-minimal)
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
  PluginPhaseExecutionTests.cpp
  PluginPhaseGraphTests.cpp
  PluginPhaseSchemaTests.cpp
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
target_include_directories(neverc-plugin-core-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/lib/Invoke
)
target_compile_features(neverc-plugin-core-tests PRIVATE cxx_std_17)
target_compile_definitions(neverc-plugin-core-tests PRIVATE
  NEVERC_TEST_MINIMAL_PLUGIN="$<TARGET_FILE:neverc-test-plugin-minimal>"
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
set_tests_properties(
  PluginPhaseSchemaGeneratorTests
  PluginPrepSchemaGeneratorTests
  PluginASTSchemaGeneratorTests
  PluginPrepObserverCoverageTests
  PluginCoverageManifestUnitTests
  PluginCoverageManifest
  PROPERTIES TIMEOUT 30
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
  NEVERC_TEST_PARSER_PROVIDER_MISSING_ROOT_PLUGIN="$<TARGET_FILE:neverc-test-plugin-parser-provider-missing-root>"
  NEVERC_TEST_PARSER_PHASE_INTERCEPTOR_PLUGIN="$<TARGET_FILE:neverc-test-plugin-parser-phase-interceptor>"
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
  neverc-test-plugin-parser-provider-missing-root
  neverc-test-plugin-parser-phase-interceptor
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
