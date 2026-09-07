cmake_minimum_required(VERSION 3.20)
project(NeverCTranslatePrototype LANGUAGES C CXX)

# Full Clang is deliberately outside the stripped NeverC build and process.
find_package(LLVM 20.1.8 EXACT REQUIRED CONFIG)
find_package(Clang 20.1.8 EXACT REQUIRED CONFIG)
if(NOT LLVM_PACKAGE_VERSION VERSION_EQUAL "20.1.8")
  message(FATAL_ERROR "Prototype requires LLVM/Clang 20.1.8")
endif()

add_executable(neverc-cpp-prototype semantic-export.cpp)
target_compile_features(neverc-cpp-prototype PRIVATE cxx_std_17)
set_target_properties(neverc-cpp-prototype PROPERTIES
  CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(neverc-cpp-prototype SYSTEM PRIVATE
  ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_definitions(neverc-cpp-prototype PRIVATE ${LLVM_DEFINITIONS})
target_link_libraries(neverc-cpp-prototype PRIVATE clang-cpp LLVM)
