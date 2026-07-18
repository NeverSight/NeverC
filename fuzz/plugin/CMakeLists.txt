if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR
    "NEVERC_ENABLE_PLUGIN_FUZZERS requires Clang with libFuzzer support")
endif()

add_custom_target(neverc-plugin-fuzzers)
set_target_properties(neverc-plugin-fuzzers PROPERTIES FOLDER "Fuzzers")

function(add_neverc_plugin_fuzzer target source)
  add_executable(${target}
    ${source}
    PluginFrontendFuzzSupport.cpp
  )
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include
  )
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_options(${target} PRIVATE -fsanitize=fuzzer-no-link)
  target_link_options(${target} PRIVATE -fsanitize=fuzzer)
  target_link_libraries(${target} PRIVATE
    nevercCompiler
    nevercPluginCore
    nevercPluginFrontend
    ${NEVERC_COMPILER_RT_BUILTINS}
  )
  add_dependencies(neverc-plugin-fuzzers ${target})
  set_target_properties(${target} PROPERTIES FOLDER "Fuzzers/Plugin")
endfunction()

add_neverc_plugin_fuzzer(neverc-plugin-token-builder-fuzzer
  TokenBuilderFuzzer.cpp)
add_neverc_plugin_fuzzer(neverc-plugin-ast-mutation-fuzzer
  ASTMutationFuzzer.cpp)
