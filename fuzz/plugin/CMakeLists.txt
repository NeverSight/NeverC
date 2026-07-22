if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR
    "NEVERC_ENABLE_PLUGIN_FUZZERS requires Clang with libFuzzer support")
endif()

add_custom_target(neverc-plugin-fuzzers)
set_target_properties(neverc-plugin-fuzzers PROPERTIES FOLDER "Fuzzers")

function(add_neverc_plugin_fuzzer target source smoke_name)
  add_executable(${target} EXCLUDE_FROM_ALL
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

  if(NEVERC_INCLUDE_TESTS)
    add_test(NAME "PluginFuzzSmoke.${smoke_name}"
      COMMAND $<TARGET_FILE:${target}>
        -runs=256
        -max_len=4096
        -print_funcs=0
    )
    set_tests_properties("PluginFuzzSmoke.${smoke_name}" PROPERTIES
      LABELS "plugin;fuzz;smoke"
      TIMEOUT 60
    )
  endif()
endfunction()

add_neverc_plugin_fuzzer(plugin-prep-token-builder-fuzzer
  PrepTokenBuilderFuzzer.cpp Prep)
add_neverc_plugin_fuzzer(plugin-ast-mutation-fuzzer
  ASTMutationFuzzer.cpp AST)
add_neverc_plugin_fuzzer(plugin-ir-builder-fuzzer
  IRBuilderFuzzer.cpp IR)
target_link_libraries(plugin-ir-builder-fuzzer PRIVATE
  nevercPluginIR
)
add_neverc_plugin_fuzzer(plugin-mir-builder-fuzzer
  MIRBuilderFuzzer.cpp MIR)
target_link_libraries(plugin-mir-builder-fuzzer PRIVATE
  LLVM${LLVM_NATIVE_ARCH}CodeGen
  LLVM${LLVM_NATIVE_ARCH}Desc
  LLVM${LLVM_NATIVE_ARCH}Info
  nevercPluginIR
)

add_neverc_plugin_fuzzer(plugin-target-descriptor-fuzzer
  TargetDescriptorFuzzer.cpp Target)
target_link_libraries(plugin-target-descriptor-fuzzer PRIVATE
  nevercPluginTarget
)

add_neverc_plugin_fuzzer(plugin-mc-builder-fuzzer
  MCBuilderFuzzer.cpp MC)
target_link_libraries(plugin-mc-builder-fuzzer PRIVATE
  nevercPluginMCObject
  nevercPluginTarget
)

add_neverc_plugin_fuzzer(plugin-object-graph-fuzzer
  ObjectGraphFuzzer.cpp Object)
target_link_libraries(plugin-object-graph-fuzzer PRIVATE
  nevercPluginMCObject
  nevercPluginTarget
)

add_neverc_plugin_fuzzer(plugin-object-reader-fuzzer
  ObjectReaderFuzzer.cpp ObjectReader)
target_link_libraries(plugin-object-reader-fuzzer PRIVATE
  nevercPluginMCObject
  nevercPluginTarget
)

add_neverc_plugin_fuzzer(plugin-dyncode-request-fuzzer
  PluginDynCodeRequestFuzzer.cpp DynCodeRequest)
target_include_directories(plugin-dyncode-request-fuzzer PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/lib/DynCode
)
target_link_libraries(plugin-dyncode-request-fuzzer PRIVATE
  nevercDynCode
  nevercPluginTarget
)
