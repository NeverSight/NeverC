set(LLVM_LINK_COMPONENTS
  Support
  TargetParser
  )

# The VC revision include that we want to generate.
set(version_inc "${CMAKE_CURRENT_BINARY_DIR}/VCSVersion.inc")

if(LLVM_APPEND_VC_REV OR LLVM_FORCE_VC_REVISION OR LLVM_FORCE_VC_REPOSITORY OR
   (LLVM_VC_REPOSITORY AND LLVM_VC_REVISION) OR
   (NEVERC_VC_REPOSITORY AND NEVERC_VC_REVISION))
  find_first_existing_vc_file("${LLVM_MAIN_SRC_DIR}" llvm_vc)
  find_first_existing_vc_file("${NEVERC_SOURCE_DIR}" neverc_vc)
  set(generate_vcs_version_script "${LLVM_CMAKE_DIR}/GenerateVersionFromVCS.cmake")

  if(llvm_vc AND LLVM_APPEND_VC_REV)
    set(llvm_source_dir ${LLVM_MAIN_SRC_DIR})
  endif()
  if (LLVM_VC_REPOSITORY AND LLVM_VC_REVISION)
    set(llvm_source_dir ${LLVM_SOURCE_DIR})
    set(llvm_vc_repository ${LLVM_VC_REPOSITORY})
    set(llvm_vc_revision ${LLVM_VC_REVISION})
  endif()
  if(neverc_vc AND LLVM_APPEND_VC_REV)
    set(neverc_source_dir ${NEVERC_SOURCE_DIR})
  endif()
  if (NEVERC_VC_REPOSITORY AND NEVERC_VC_REVISION)
    set(neverc_source_dir ${NEVERC_SOURCE_DIR})
    set(neverc_vc_repository ${NEVERC_VC_REPOSITORY})
    set(neverc_vc_revision ${NEVERC_VC_REVISION})
  endif()

  # Create custom target to generate the VC revision include.
  add_custom_command(OUTPUT "${version_inc}"
    DEPENDS "${llvm_vc}" "${neverc_vc}" "${generate_vcs_version_script}"
    COMMAND ${CMAKE_COMMAND} "-DNAMES=\"LLVM;NEVERC\""
                             "-DLLVM_SOURCE_DIR=${llvm_source_dir}"
                             "-DNEVERC_SOURCE_DIR=${neverc_source_dir}"
                             "-DNEVERC_VC_REPOSITORY=${neverc_vc_repository}"
                             "-DNEVERC_VC_REVISION=${neverc_vc_revision}"
                             "-DHEADER_FILE=${version_inc}"
                             "-DLLVM_VC_REPOSITORY=${llvm_vc_repository}"
                             "-DLLVM_VC_REVISION=${llvm_vc_revision}"
                             "-DLLVM_FORCE_VC_REVISION=${LLVM_FORCE_VC_REVISION}"
                             "-DLLVM_FORCE_VC_REPOSITORY=${LLVM_FORCE_VC_REPOSITORY}"
                             -P "${generate_vcs_version_script}")
else()
  file(WRITE "${version_inc}"
    "#undef LLVM_REVISION\n"
    "#undef LLVM_REPOSITORY\n"
    "#undef NEVERC_REVISION\n"
    "#undef NEVERC_REPOSITORY\n")
endif()

# Mark the generated header as being generated.
set_source_files_properties("${version_inc}"
  PROPERTIES GENERATED TRUE
             HEADER_FILE_ONLY TRUE)

if(NEVERC_VENDOR)
  set_source_files_properties(Core/Version.cpp
    PROPERTIES COMPILE_DEFINITIONS "NEVERC_VENDOR=\"${NEVERC_VENDOR} \"")
endif()

# ---------------------------------------------------------------------------
# BuiltinString precompiled bitcode
# ---------------------------------------------------------------------------
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(BUILTIN_STRING_PRELUDE_DIR
    "${NEVERC_SOURCE_DIR}/include/neverc/Foundation/Builtin/BuiltinStringPrelude")
set(BUILTIN_STRING_SCRIPT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/Builtin")

# Collect all .inc fragment paths for dependency tracking.
set(BUILTIN_STRING_INC_FRAGMENTS)
foreach(stem Type Allocation Accessors Capacity Compare Search
             Mutation Utility Encoding WebCodec Format)
  list(APPEND BUILTIN_STRING_INC_FRAGMENTS
       "${BUILTIN_STRING_PRELUDE_DIR}/${stem}.inc")
endforeach()

# 1. Generate thin-header extern declarations.
set(BUILTIN_STRING_THIN_DECLS
    "${CMAKE_CURRENT_BINARY_DIR}/BuiltinStringThinHeaderDecls.inc")
add_custom_command(
  OUTPUT "${BUILTIN_STRING_THIN_DECLS}"
  COMMAND ${Python3_EXECUTABLE}
          "${BUILTIN_STRING_SCRIPT_DIR}/gen_thin_header.py"
          "${BUILTIN_STRING_PRELUDE_DIR}"
          -o "${BUILTIN_STRING_THIN_DECLS}"
  DEPENDS "${BUILTIN_STRING_SCRIPT_DIR}/gen_thin_header.py"
          ${BUILTIN_STRING_INC_FRAGMENTS}
  COMMENT "[NeverC] Generating BuiltinString thin-header declarations"
)

# 2. Generate the standalone C compilation unit.
set(BUILTIN_STRING_RUNTIME_C
    "${CMAKE_CURRENT_BINARY_DIR}/BuiltinStringRuntime.c")
add_custom_command(
  OUTPUT "${BUILTIN_STRING_RUNTIME_C}"
  COMMAND ${Python3_EXECUTABLE}
          "${BUILTIN_STRING_SCRIPT_DIR}/gen_string_runtime.py"
          "${BUILTIN_STRING_PRELUDE_DIR}"
          -o "${BUILTIN_STRING_RUNTIME_C}"
  DEPENDS "${BUILTIN_STRING_SCRIPT_DIR}/gen_string_runtime.py"
          ${BUILTIN_STRING_INC_FRAGMENTS}
  COMMENT "[NeverC] Generating BuiltinStringRuntime.c"
)

# 3-4. Bootstrap bitcode: compiled by neverc itself AFTER the initial
#    build.  The initial build uses the legacy source-prelude path
#    (empty bitcode placeholder).
#
#    To produce optimised builds with the precompiled bitcode path:
#       ninja neverc                        # stage 1 (legacy path)
#       ninja neverc-bootstrap-string-bc    # compile bitcode with neverc
#       ninja neverc                        # stage 2 (bitcode path)
set(BUILTIN_STRING_BITCODE_H
    "${CMAKE_CURRENT_BINARY_DIR}/BuiltinStringBitcode.h")
if(NOT EXISTS "${BUILTIN_STRING_BITCODE_H}")
  file(WRITE "${BUILTIN_STRING_BITCODE_H}"
    "/* Empty placeholder — run 'ninja neverc-bootstrap-string-bc' to populate. */\n"
    "static const unsigned char kStringRuntimeBitcode[] = {0};\n"
    "static const unsigned int kStringRuntimeBitcode_len = 0;\n")
endif()

set(BUILTIN_STRING_RUNTIME_BC
    "${CMAKE_CURRENT_BINARY_DIR}/neverc_string_runtime.bc")
add_custom_target(neverc-bootstrap-string-bc
  COMMAND $<TARGET_FILE:neverc>
          -c -emit-llvm -O0 -fno-builtin-string -fno-lto
          -ffreestanding -std=gnu11
          -Wno-invalid-noreturn
          "${BUILTIN_STRING_RUNTIME_C}"
          -o "${BUILTIN_STRING_RUNTIME_BC}"
  COMMAND ${Python3_EXECUTABLE}
          "${BUILTIN_STRING_SCRIPT_DIR}/bin2c.py"
          "${BUILTIN_STRING_RUNTIME_BC}"
          -o "${BUILTIN_STRING_BITCODE_H}"
          --name kStringRuntimeBitcode
  COMMAND ${CMAKE_COMMAND} -E touch
          "${CMAKE_CURRENT_SOURCE_DIR}/Builtin/BuiltinString.cpp"
  DEPENDS neverc "${BUILTIN_STRING_RUNTIME_C}"
  COMMENT "[NeverC] Bootstrap: compiling string runtime with neverc itself"
)

# ---------------------------------------------------------------------------
# BuiltinMimalloc precompiled bitcode (per-OS)
# ---------------------------------------------------------------------------
set(BUILTIN_MIMALLOC_SCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Builtin")

# Per-OS placeholder headers — written once, overwritten by bootstrap.
foreach(_mi_os linux darwin win)
  set(_mi_hdr "${CMAKE_CURRENT_BINARY_DIR}/BuiltinMimallocBitcode_${_mi_os}.h")
  if(NOT EXISTS "${_mi_hdr}")
    file(WRITE "${_mi_hdr}"
      "/* Empty placeholder — run 'ninja neverc-bootstrap-mimalloc-bc' to populate. */\n"
      "static const unsigned char kMimallocBitcode_${_mi_os}[] = {0};\n"
      "static const unsigned int kMimallocBitcode_${_mi_os}_len = 0;\n")
  endif()
  list(APPEND BUILTIN_MIMALLOC_BITCODE_HDRS "${_mi_hdr}")
endforeach()

# Bootstrap targets — require a first-stage neverc build and mimalloc source.
# Usage:
#   ninja neverc                          # stage 1
#   ninja neverc-bootstrap-mimalloc-bc    # compile per-OS bitcode
#   ninja neverc                          # stage 2 (embeds bitcode)
#
# The bitcode is compiled per-OS using the host triple (same approach as
# BuiltinString).  mimalloc's source is arch-neutral C — the IR pass
# resets data layout & triple at merge time, and strips host-specific
# attributes, so cross-arch usage works for the same OS.
if(TARGET mimalloc-static)
  set(_MI_SRC_ROOT "${mimalloc_SOURCE_DIR}")

  foreach(_mi_os linux darwin win)
    if(_mi_os STREQUAL "linux")
      set(_mi_triple "x86_64-unknown-linux-gnu")
    elseif(_mi_os STREQUAL "darwin")
      set(_mi_triple "arm64-apple-macosx11.0")
    elseif(_mi_os STREQUAL "win")
      set(_mi_triple "x86_64-pc-windows-msvc")
    endif()

    set(_mi_runtime_c
        "${CMAKE_CURRENT_BINARY_DIR}/MimallocRuntime_${_mi_os}.c")
    set(_mi_runtime_bc
        "${CMAKE_CURRENT_BINARY_DIR}/neverc_mimalloc_${_mi_os}.bc")
    set(_mi_bitcode_h
        "${CMAKE_CURRENT_BINARY_DIR}/BuiltinMimallocBitcode_${_mi_os}.h")

    add_custom_command(
      OUTPUT "${_mi_runtime_c}"
      COMMAND ${Python3_EXECUTABLE}
              "${BUILTIN_MIMALLOC_SCRIPT_DIR}/gen_mimalloc_source.py"
              "${_MI_SRC_ROOT}"
              -o "${_mi_runtime_c}"
              --os ${_mi_os}
      DEPENDS "${BUILTIN_MIMALLOC_SCRIPT_DIR}/gen_mimalloc_source.py"
      COMMENT "[NeverC] Generating MimallocRuntime_${_mi_os}.c"
    )

    add_custom_target(neverc-bootstrap-mimalloc-bc-${_mi_os}
      COMMAND $<TARGET_FILE:neverc>
              -c -emit-llvm -O2
              -fno-builtin-mimalloc -fno-lto
              -ffreestanding -std=gnu11
              --target=${_mi_triple}
              -I "${_MI_SRC_ROOT}/include"
              -I "${_MI_SRC_ROOT}/src"
              -w
              "${_mi_runtime_c}"
              -o "${_mi_runtime_bc}"
      COMMAND ${Python3_EXECUTABLE}
              "${BUILTIN_MIMALLOC_SCRIPT_DIR}/bin2c.py"
              "${_mi_runtime_bc}"
              -o "${_mi_bitcode_h}"
              --name "kMimallocBitcode_${_mi_os}"
      COMMAND ${CMAKE_COMMAND} -E touch
              "${CMAKE_CURRENT_SOURCE_DIR}/Builtin/BuiltinMimalloc.cpp"
      DEPENDS neverc "${_mi_runtime_c}"
      COMMENT "[NeverC] Bootstrap: compiling mimalloc for ${_mi_os} (${_mi_triple})"
    )
  endforeach()

  add_custom_target(neverc-bootstrap-mimalloc-bc
    DEPENDS neverc-bootstrap-mimalloc-bc-linux
            neverc-bootstrap-mimalloc-bc-darwin
            neverc-bootstrap-mimalloc-bc-win
    COMMENT "[NeverC] Bootstrap: all mimalloc per-OS bitcode targets"
  )
endif()

add_neverc_library(nevercFoundation
  Core/Diagnostic.cpp
  Core/FileManager.cpp
  Core/IdentifierTable.cpp
  Core/LangOptions.cpp
  Builtin/BuiltinString.cpp
  Builtin/BuiltinMimalloc.cpp

  Target/TargetInfo.cpp
  Target/AArch64.cpp
  Target/X86.cpp
  Target/OSDefines.cpp

  Core/Version.cpp
  ${version_inc}
  ${BUILTIN_STRING_THIN_DECLS}
  ${BUILTIN_STRING_BITCODE_H}
  ${BUILTIN_MIMALLOC_BITCODE_HDRS}

  DEPENDS
  NeverCDriverOptions
  )


target_include_directories(nevercFoundation
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Target
    ${CMAKE_CURRENT_BINARY_DIR}
  )

target_link_libraries(nevercFoundation
  PRIVATE
  ${LLVM_ATOMIC_LIB}
)
