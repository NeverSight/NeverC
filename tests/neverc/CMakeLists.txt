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

add_executable(neverc-tests ${NEVERC_TEST_SOURCES})

target_link_libraries(neverc-tests PRIVATE GTest::gtest GTest::gtest_main)

# Dependency-free contract headers (e.g. LTOCacheContract.h) shared with
# the product code so tests never restate env var names or file naming.
target_include_directories(neverc-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
)

target_compile_features(neverc-tests PRIVATE cxx_std_17)

target_compile_definitions(neverc-tests PRIVATE
  NEVERC_BINARY="$<TARGET_FILE:neverc>"
  TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  TEST_C_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../C"
  NEVERC_PLUGINSDK_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../pluginsdk"
  NEVERC_NEVERC_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include"
)

if(TARGET neverc)
  add_dependencies(neverc-tests neverc)
endif()

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

target_compile_features(neverc-merge-tests PRIVATE cxx_std_17)

gtest_discover_tests(neverc-merge-tests
  PROPERTIES
    TIMEOUT 120
  DISCOVERY_TIMEOUT 30
)

# Standard library runtime tests — compile std/tests/*.c with neverc, run, verify
add_executable(neverc-std-tests
  NeverCTestFixture.cpp
  StdLibTests.cpp
)

target_link_libraries(neverc-std-tests PRIVATE GTest::gtest GTest::gtest_main)

target_compile_features(neverc-std-tests PRIVATE cxx_std_17)

target_compile_definitions(neverc-std-tests PRIVATE
  NEVERC_BINARY="$<TARGET_FILE:neverc>"
  TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  TEST_C_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../C"
  NEVERC_PLUGINSDK_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../pluginsdk"
  NEVERC_NEVERC_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include"
)

if(TARGET neverc)
  add_dependencies(neverc-std-tests neverc)
endif()
if(TARGET neverc-std-headers)
  add_dependencies(neverc-std-tests neverc-std-headers)
endif()

gtest_discover_tests(neverc-std-tests
  PROPERTIES
    TIMEOUT 600
  DISCOVERY_TIMEOUT 30
)
