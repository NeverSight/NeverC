set(NEVERC_TEST_SOURCES
  NeverCTestFixture.cpp
  BasicTests.cpp
  HostTests.cpp
  DriverTests.cpp
  LTOTests.cpp
  LinkerTests.cpp
  OverrideTests.cpp
  ConformanceTests.cpp
  ShellcodeTests.cpp
  ShellcodeStressTests.cpp
  ShellcodeCrossTargetTests.cpp
  DebugTests.cpp
  BenchmarkTests.cpp
  MimallocTests.cpp
  CustomCallConvTests.cpp
  SEHTests.cpp
  X86PrivilegedIntrinTests.cpp
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
)

neverc_tests_link_neverc(neverc-tests)

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
