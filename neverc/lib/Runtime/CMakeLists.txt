set(LLVM_LINK_COMPONENTS
  Support
  )

add_neverc_library(nevercRuntime
  RuntimeManager.cpp
  )
