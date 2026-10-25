add_executable(neverc-x86-backend-tests AtomicMemoryTests.cpp TransactionTests.cpp)
target_link_libraries(neverc-x86-backend-tests PRIVATE
  GTest::gtest
  GTest::gtest_main
  LLVMX86CodeGen
  LLVMX86Desc
  LLVMX86Info
)
target_compile_features(neverc-x86-backend-tests PRIVATE cxx_std_17)
llvm_update_compile_flags(neverc-x86-backend-tests)
gtest_discover_tests(neverc-x86-backend-tests
  PROPERTIES TIMEOUT 60
  DISCOVERY_TIMEOUT 300
)
