# runtime/ — platform-specific SDK assets (headers + libraries).
#
# Copied into the build tree so the NeverC binary can discover them
# relative to its own install location:
#   <InstalledDir>/../runtime/windows/shared/<sdk>/  (headers)
#   <InstalledDir>/../runtime/windows/<arch>/<sdk>/   (libs)
#
# These targets always re-copy on every build.  We intentionally do NOT use
# stamp files / dependency tracking here: the asset trees contain thousands
# of files and globbing them at configure time is fragile (CONFIGURE_DEPENDS
# misses new files until the next reconfigure).  An unconditional rm + copy
# is cheap (~1-2s for the linux sysroot) and guarantees any edit under
# runtime/ is reflected in build/runtime/ on the next `ninja`.

set(RUNTIME_SRC  "${CMAKE_CURRENT_SOURCE_DIR}")
set(RUNTIME_DST  "${LLVM_LIBRARY_OUTPUT_INTDIR}/../runtime")

# Helper: register a directory tree for build-copy + install.
# COPY_SPEC is a list of "src_subdir:dst_subdir" pairs.
macro(runtime_sdk_target _name)
  set(_cmds)

  foreach(_spec ${ARGN})
    string(REPLACE ":" ";" _parts "${_spec}")
    list(GET _parts 0 _src)
    list(GET _parts 1 _dst)

    list(APPEND _cmds
      COMMAND "${CMAKE_COMMAND}" -E rm -rf "${RUNTIME_DST}/${_dst}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${RUNTIME_DST}/${_dst}"
      COMMAND "${CMAKE_COMMAND}"
        -DSRC="${RUNTIME_SRC}/${_src}"
        -DDST="${RUNTIME_DST}/${_dst}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/copy_runtime_tree.cmake")

    install(DIRECTORY "${RUNTIME_SRC}/${_src}/"
            DESTINATION "runtime/${_dst}"
            COMPONENT ${_name})
  endforeach()

  # add_custom_target with no OUTPUT is always considered out of date,
  # so its COMMANDs run on every build (this is what we want).
  add_custom_target(${_name} ALL
    ${_cmds}
    COMMENT "Copying ${_name} to build tree..."
  )

  if(NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-${_name}
                             DEPENDS ${_name}
                             COMPONENT ${_name})
  endif()
endmacro()

# ---------------------------------------------------------------------------
# Windows / shared headers
# ---------------------------------------------------------------------------
runtime_sdk_target(neverc-runtime-shared-msvc
  "windows/shared/msvc/crt/include:windows/shared/msvc/crt/include"
  "windows/shared/msvc/sdk/include:windows/shared/msvc/sdk/include"
)

runtime_sdk_target(neverc-runtime-shared-wdk
  "windows/shared/wdk/include:windows/shared/wdk/include"
)

# ---------------------------------------------------------------------------
# Windows / x64 libs
# ---------------------------------------------------------------------------
runtime_sdk_target(neverc-runtime-x64-msvc
  "windows/x64/msvc/crt/lib:windows/x64/msvc/crt/lib"
  "windows/x64/msvc/sdk/lib:windows/x64/msvc/sdk/lib"
)

runtime_sdk_target(neverc-runtime-x64-wdk
  "windows/x64/wdk/lib:windows/x64/wdk/lib"
)

# ---------------------------------------------------------------------------
# Windows / arm64 libs
# ---------------------------------------------------------------------------
runtime_sdk_target(neverc-runtime-arm64-msvc
  "windows/arm64/msvc/crt/lib:windows/arm64/msvc/crt/lib"
  "windows/arm64/msvc/sdk/lib:windows/arm64/msvc/sdk/lib"
)

# ---------------------------------------------------------------------------
# Linux / x64 sysroot  (Ubuntu 22.04 headers + libraries)
# ---------------------------------------------------------------------------
if(IS_DIRECTORY "${RUNTIME_SRC}/linux/x64")
  runtime_sdk_target(neverc-runtime-linux-x64
    "linux/x64:linux/x64"
  )
endif()

# ---------------------------------------------------------------------------
# Linux / arm64 sysroot
# ---------------------------------------------------------------------------
if(IS_DIRECTORY "${RUNTIME_SRC}/linux/arm64")
  runtime_sdk_target(neverc-runtime-linux-arm64
    "linux/arm64:linux/arm64"
  )
endif()

# ---------------------------------------------------------------------------
# Android / arm64 sysroot  (NDK r26c, API 21+)
# ---------------------------------------------------------------------------
if(IS_DIRECTORY "${RUNTIME_SRC}/android/arm64")
  runtime_sdk_target(neverc-runtime-android-arm64
    "android/arm64:android/arm64"
  )
endif()
