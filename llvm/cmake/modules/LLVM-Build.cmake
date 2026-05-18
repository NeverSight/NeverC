# Resolve cross-component dependencies, for each available component.
function(LLVMBuildResolveComponentsLink)

  # the native target may not be enabled when cross compiling
  if(TARGET ${LLVM_NATIVE_ARCH})
    get_property(llvm_has_jit_native TARGET ${LLVM_NATIVE_ARCH} PROPERTY LLVM_HAS_JIT)
  else()
    set(llvm_has_jit_native OFF)
  endif()

  if(llvm_has_jit_native)
    set_property(TARGET Engine APPEND PROPERTY LLVM_LINK_COMPONENTS "MCJIT" "Native")
  else()
    set_property(TARGET Engine APPEND PROPERTY LLVM_LINK_COMPONENTS "Interpreter")
  endif()

  get_property(llvm_components GLOBAL PROPERTY LLVM_COMPONENT_LIBS)
  foreach(llvm_component ${llvm_components})
    get_property(link_components TARGET ${llvm_component} PROPERTY LLVM_LINK_COMPONENTS)
    llvm_map_components_to_libnames(llvm_libs ${link_components})
    if(llvm_libs)
      get_property(libtype TARGET ${llvm_component} PROPERTY LLVM_LIBTYPE)
      target_link_libraries(${llvm_component} ${libtype} ${llvm_libs})
    endif()
  endforeach()
endfunction()
