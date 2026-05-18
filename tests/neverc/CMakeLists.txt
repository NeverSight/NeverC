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
)

add_executable(neverc-tests ${NEVERC_TEST_SOURCES})

target_link_libraries(neverc-tests PRIVATE GTest::gtest GTest::gtest_main)

target_compile_features(neverc-tests PRIVATE cxx_std_17)

target_compile_definitions(neverc-tests PRIVATE
  NEVERC_BINARY="$<TARGET_FILE:neverc>"
  TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  TEST_C_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../C"
)

if(TARGET neverc)
  add_dependencies(neverc-tests neverc)
endif()

gtest_discover_tests(neverc-tests
  PROPERTIES
    TIMEOUT 300
  DISCOVERY_TIMEOUT 30
)
