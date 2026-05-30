# runtime/ — platform-specific SDK assets (headers + libraries).
#
# Copied into the build tree so the NeverC binary can discover them
# relative to its own install location:
#   <InstalledDir>/../runtime/windows/shared/<sdk>/  (headers)
#   <InstalledDir>/../runtime/windows/<arch>/<sdk>/   (libs)
#
# Uses stamp files to avoid re-copying on every build.
# Delete a stamp to force a refresh:
#   rm <build>/runtime/*.stamp

set(RUNTIME_SRC  "${CMAKE_CURRENT_SOURCE_DIR}")
set(RUNTIME_DST  "${LLVM_LIBRARY_OUTPUT_INTDIR}/../runtime")

# Helper: register a directory tree for build-copy + install.
# COPY_SPEC is a list of "src_subdir:dst_subdir" pairs.
macro(runtime_sdk_target _name _stamp_name)
  set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/${_stamp_name}.stamp")
  set(_cmds)
  set(_install_srcs)

  foreach(_spec ${ARGN})
    string(REPLACE ":" ";" _parts "${_spec}")
    list(GET _parts 0 _src)
    list(GET _parts 1 _dst)

    list(APPEND _cmds
      COMMAND "${CMAKE_COMMAND}" -E rm -rf "${RUNTIME_DST}/${_dst}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${RUNTIME_DST}/${_dst}"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${RUNTIME_SRC}/${_src}" "${RUNTIME_DST}/${_dst}")

    install(DIRECTORY "${RUNTIME_SRC}/${_src}/"
            DESTINATION "runtime/${_dst}"
            COMPONENT ${_name})
  endforeach()

  add_custom_command(
    OUTPUT "${_stamp}"
    ${_cmds}
    COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
    COMMENT "Copying ${_name} to build tree..."
  )
  add_custom_target(${_name} ALL DEPENDS "${_stamp}")

  if(NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-${_name}
                             DEPENDS ${_name}
                             COMPONENT ${_name})
  endif()
endmacro()

# ---------------------------------------------------------------------------
# Windows / shared headers
# ---------------------------------------------------------------------------
runtime_sdk_target(neverc-runtime-shared-msvc  shared-msvc
  "windows/shared/msvc/crt/include:windows/shared/msvc/crt/include"
  "windows/shared/msvc/sdk/include:windows/shared/msvc/sdk/include"
)

runtime_sdk_target(neverc-runtime-shared-wdk  shared-wdk
  "windows/shared/wdk/include:windows/shared/wdk/include"
)

# ---------------------------------------------------------------------------
# Windows / x64 libs
# ---------------------------------------------------------------------------
runtime_sdk_target(neverc-runtime-x64-msvc  x64-msvc
  "windows/x64/msvc/crt/lib:windows/x64/msvc/crt/lib"
  "windows/x64/msvc/sdk/lib:windows/x64/msvc/sdk/lib"
)

runtime_sdk_target(neverc-runtime-x64-wdk  x64-wdk
  "windows/x64/wdk/lib:windows/x64/wdk/lib"
)

# ---------------------------------------------------------------------------
# Windows / arm64 libs
# ---------------------------------------------------------------------------
runtime_sdk_target(neverc-runtime-arm64-msvc  arm64-msvc
  "windows/arm64/msvc/crt/lib:windows/arm64/msvc/crt/lib"
  "windows/arm64/msvc/sdk/lib:windows/arm64/msvc/sdk/lib"
)
