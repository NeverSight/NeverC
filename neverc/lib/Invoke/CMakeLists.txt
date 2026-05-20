set(LLVM_LINK_COMPONENTS
  BinaryFormat
  MC
  Object
  Option
  Support
  TargetParser
  WindowsDriver
  )

if(WIN32)
  # MSVC.cpp uses version.dll.
  set(system_libs version)
endif()

# Mainstream toolchains and archs only.
set(NEVERC_INVOKE_TOOLCHAIN_SOURCES
  ToolChains/CommonArgs.cpp
  ToolChains/Darwin.cpp
  ToolChains/Gnu.cpp
  ToolChains/Linux.cpp
  ToolChains/MSVC.cpp
  ToolChains/NeverC.cpp
)

# Only X86, AArch64.
set(NEVERC_INVOKE_ARCH_SOURCES
  ToolChains/Arch/AArch64.cpp
  ToolChains/Arch/X86.cpp
)

add_neverc_library(nevercInvoke
  Action/Action.cpp
  Action/Job.cpp
  Action/Phases.cpp
  Action/Tool.cpp
  Core/Compilation.cpp
  Core/Driver.cpp
  Core/InMemoryFileStore.cpp
  Core/ToolChain.cpp
  Multilib/Multilib.cpp
  Multilib/MultilibBuilder.cpp
  Option/DriverOptions.cpp
  Option/OptionUtils.cpp
  Option/Types.cpp
  ${NEVERC_INVOKE_ARCH_SOURCES}
  ${NEVERC_INVOKE_TOOLCHAIN_SOURCES}

  DEPENDS
  NeverCDriverOptions

  LINK_LIBS
  nevercFoundation
  ${system_libs}
  )

target_include_directories(nevercInvoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
