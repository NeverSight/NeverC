include(LLVM-Build)

add_subdirectory(IR)
add_subdirectory(IRPrinter)
add_subdirectory(IRReader)
add_subdirectory(CodeGen)
add_subdirectory(BinaryFormat)
add_subdirectory(Bitcode)
add_subdirectory(Bitstream)
add_subdirectory(Frontend)
add_subdirectory(Transforms)
add_subdirectory(Linker)
add_subdirectory(Analysis)
add_subdirectory(LTO)
add_subdirectory(MC)
add_subdirectory(Object)
add_subdirectory(Option)
add_subdirectory(Remarks)
add_subdirectory(DebugInfo)
add_subdirectory(Target)
add_subdirectory(ProfileData)
add_subdirectory(Passes)
add_subdirectory(TargetParser)
add_subdirectory(TextAPI)

add_subdirectory(WindowsDriver)

# Special components which don't have any source attached but aggregate other
# components
add_llvm_component_group(all-targets LINK_COMPONENTS ${LLVM_TARGETS_TO_BUILD})
add_llvm_component_group(Engine)

# The native target may not be enabled when cross compiling
if(TARGET ${LLVM_NATIVE_ARCH})
  add_llvm_component_group(Native LINK_COMPONENTS ${LLVM_NATIVE_ARCH})
  add_llvm_component_group(NativeCodeGen LINK_COMPONENTS ${LLVM_NATIVE_ARCH}CodeGen)
else()
  add_llvm_component_group(Native)
  add_llvm_component_group(NativeCodeGen)
endif()

# Component post-processing
LLVMBuildResolveComponentsLink()
