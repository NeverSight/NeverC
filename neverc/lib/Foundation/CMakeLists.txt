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

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---------------------------------------------------------------------------
# Embedded-runtime path remapping
# ---------------------------------------------------------------------------
# Each runtime below is compiled on the machine that builds the compiler and
# then embedded into it, so any path recorded while bootstrapping travels on
# into every program a user later builds.  Fold the roots those paths can come
# from onto one virtual prefix.
#
# Clang resolves prefix maps last-match-wins, so the general roots belong
# first: a bootstrap step needing a more precise mapping appends its own
# afterwards and takes precedence.  The std and NVK runtimes do exactly that,
# naming their sources relative to the install tree so that source-level
# debugging keeps working.  Together with DW_AT_comp_dir mapped to the virtual
# prefix, a debugger reaches those sources through a single rule:
#
#     settings set target.source-map /neverc <install-prefix>
set(NEVERC_RUNTIME_VIRTUAL_PREFIX "/neverc")
get_filename_component(_neverc_repo_root "${NEVERC_SOURCE_DIR}/.." ABSOLUTE)

# Held as <from>=<to> pairs so that each consumer renders its own spelling:
# the targets in this file pass -ffile-prefix-map straight to neverc, while
# the Python bootstrap drivers take --prefix-map.
set(NEVERC_BOOTSTRAP_PATH_MAPS
  "${_neverc_repo_root}=${NEVERC_RUNTIME_VIRTUAL_PREFIX}"
  "${CMAKE_BINARY_DIR}=${NEVERC_RUNTIME_VIRTUAL_PREFIX}/build"
  # Bootstrap steps run here, making this the directory that would otherwise
  # reach users as DW_AT_comp_dir.
  "${CMAKE_CURRENT_BINARY_DIR}=${NEVERC_RUNTIME_VIRTUAL_PREFIX}"
)

set(NEVERC_BOOTSTRAP_PREFIX_MAP_FLAGS)
set(NEVERC_BOOTSTRAP_PREFIX_MAP_ARGS)
foreach(_path_map IN LISTS NEVERC_BOOTSTRAP_PATH_MAPS)
  list(APPEND NEVERC_BOOTSTRAP_PREFIX_MAP_FLAGS "-ffile-prefix-map=${_path_map}")
  list(APPEND NEVERC_BOOTSTRAP_PREFIX_MAP_ARGS --prefix-map "${_path_map}")
endforeach()

# ---------------------------------------------------------------------------
# BuiltinString precompiled bitcode
# ---------------------------------------------------------------------------

set(BUILTIN_STRING_PRELUDE_DIR
    "${NEVERC_SOURCE_DIR}/include/neverc/Foundation/Builtin/BuiltinStringPrelude")
set(BUILTIN_STRING_SCRIPT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/Builtin")

# Collect all .inc fragment paths for dependency tracking.
set(BUILTIN_STRING_INC_FRAGMENTS)
foreach(stem Type EncryptDecrypt Allocation Accessors Capacity Compare Search
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
#    (empty bitcode placeholders).
#
#    To produce optimised builds with the precompiled bitcode path:
#       ninja neverc                        # stage 1 (legacy path)
#       ninja neverc-bootstrap-string-bc    # compile per-target bitcode
#       ninja neverc                        # stage 2 (bitcode path)
# va_list lowering, size_t, aggregate layout and calling conventions are
# target-specific even when the source itself is freestanding. Retargeting one
# host IR blob is therefore invalid; keep one runtime per supported OS/arch.
set(_STRING_VARIANTS
  "linux_x64:x86_64-unknown-linux-gnu"
  "linux_arm64:aarch64-unknown-linux-gnu"
  "darwin_x64:x86_64-apple-macosx11.0"
  "darwin_arm64:arm64-apple-macosx11.0"
  "win_x64:x86_64-pc-windows-msvc"
  "win_arm64:aarch64-pc-windows-msvc"
)
set(_STRING_BOOTSTRAP_TARGETS)
foreach(_string_spec IN LISTS _STRING_VARIANTS)
  string(REPLACE ":" ";" _string_parts "${_string_spec}")
  list(GET _string_parts 0 _string_tag)
  list(GET _string_parts 1 _string_triple)
  set(_string_runtime_bc
      "${CMAKE_CURRENT_BINARY_DIR}/neverc_string_runtime_${_string_tag}.bc")
  set(_string_bitcode_h
      "${CMAKE_CURRENT_BINARY_DIR}/BuiltinStringBitcode_${_string_tag}.h")
  if(NOT EXISTS "${_string_bitcode_h}")
    file(WRITE "${_string_bitcode_h}"
      "/* Empty placeholder — run 'ninja neverc-bootstrap-string-bc' to populate. */\n"
      "static const unsigned char kStringRuntimeBitcode_${_string_tag}[] = {0};\n"
      "static const unsigned int kStringRuntimeBitcode_${_string_tag}_len = 0;\n")
  endif()

  # Keep the embedded call graph unoptimized for link-time selection while
  # allowing the consumer module's optimization pipeline to inline it.
  add_custom_target(neverc-bootstrap-string-bc-${_string_tag}
    COMMAND $<TARGET_FILE:neverc>
            -c -emit-llvm -O0
            -fno-builtin-string -fno-builtin-mimalloc -fno-lto
            -disable-O0-optnone -finline-functions
            -ffreestanding -std=gnu11
            -Wno-invalid-noreturn
            --target=${_string_triple}
            ${NEVERC_BOOTSTRAP_PREFIX_MAP_FLAGS}
            "${BUILTIN_STRING_RUNTIME_C}"
            -o "${_string_runtime_bc}"
    COMMAND ${Python3_EXECUTABLE}
            "${BUILTIN_STRING_SCRIPT_DIR}/bin2c.py"
            "${_string_runtime_bc}"
            -o "${_string_bitcode_h}"
            --name "kStringRuntimeBitcode_${_string_tag}"
    COMMAND ${CMAKE_COMMAND} -E touch
            "${CMAKE_CURRENT_SOURCE_DIR}/Builtin/BuiltinString.cpp"
    DEPENDS neverc "${BUILTIN_STRING_RUNTIME_C}"
    COMMENT
      "[NeverC] Bootstrap: compiling string runtime for ${_string_tag} (${_string_triple})"
  )
  list(APPEND _STRING_BOOTSTRAP_TARGETS
       neverc-bootstrap-string-bc-${_string_tag})
endforeach()

add_custom_target(neverc-bootstrap-string-bc
  DEPENDS ${_STRING_BOOTSTRAP_TARGETS}
  COMMENT "[NeverC] Bootstrap: all string runtime target variants"
)

# ---------------------------------------------------------------------------
# BuiltinMimalloc precompiled bitcode (per-OS + per-arch)
# ---------------------------------------------------------------------------
set(BUILTIN_MIMALLOC_SCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Builtin")

# Placeholders — written once, overwritten by bootstrap.
# Keys are the bin2c / header suffix. mimalloc IR embeds arch-specific inline
# asm, so every supported OS/arch pair needs its own blob.
set(_MI_VARIANTS
  "linux_x64:linux:x86_64-unknown-linux-gnu"
  "linux_arm64:linux:aarch64-unknown-linux-gnu"
  "darwin_x64:darwin:x86_64-apple-macosx11.0"
  "darwin_arm64:darwin:arm64-apple-macosx11.0"
  "win_x64:win:x86_64-pc-windows-msvc"
  "win_arm64:win:aarch64-pc-windows-msvc"
)

foreach(_mi_spec IN LISTS _MI_VARIANTS)
  string(REPLACE ":" ";" _mi_parts "${_mi_spec}")
  list(GET _mi_parts 0 _mi_tag)
  set(_mi_hdr "${CMAKE_CURRENT_BINARY_DIR}/BuiltinMimallocBitcode_${_mi_tag}.h")
  if(NOT EXISTS "${_mi_hdr}")
    file(WRITE "${_mi_hdr}"
      "/* Empty placeholder — run 'ninja neverc-bootstrap-mimalloc-bc' to populate. */\n"
      "static const unsigned char kMimallocBitcode_${_mi_tag}[] = {0};\n"
      "static const unsigned int kMimallocBitcode_${_mi_tag}_len = 0;\n")
  endif()
  list(APPEND BUILTIN_MIMALLOC_BITCODE_HDRS "${_mi_hdr}")
endforeach()

# Bootstrap targets — require a first-stage neverc build and mimalloc source.
# Usage:
#   ninja neverc                          # stage 1
#   ninja neverc-bootstrap-mimalloc-bc    # compile per-OS+arch bitcode
#   ninja neverc                          # stage 2 (embeds bitcode)
#
# The IR pass still resets data layout & triple at merge time and strips
# host-specific attributes, but arch-specific inline asm cannot be retargeted,
# so each OS/arch pair gets its own blob.
if(TARGET mimalloc-static)
  get_target_property(_MI_SRC_ROOT mimalloc-static SOURCE_DIR)
  if(NOT _MI_SRC_ROOT)
    message(FATAL_ERROR
      "NeverC: unable to locate mimalloc source directory for bitcode bootstrap")
  endif()
  set(_MI_SOURCE_PREFIX_MAP
      "-ffile-prefix-map=${_MI_SRC_ROOT}=${NEVERC_RUNTIME_VIRTUAL_PREFIX}/third-party/mimalloc")

  set(_MI_BOOTSTRAP_TARGETS)
  foreach(_mi_spec IN LISTS _MI_VARIANTS)
    string(REPLACE ":" ";" _mi_parts "${_mi_spec}")
    list(GET _mi_parts 0 _mi_tag)
    list(GET _mi_parts 1 _mi_os)
    list(GET _mi_parts 2 _mi_triple)

    # Shared generated C wrapper per OS (defines differ by OS, not arch).
    set(_mi_runtime_c
        "${CMAKE_CURRENT_BINARY_DIR}/MimallocRuntime_${_mi_os}.c")
    set(_mi_runtime_bc
        "${CMAKE_CURRENT_BINARY_DIR}/neverc_mimalloc_${_mi_tag}.bc")
    set(_mi_bitcode_h
        "${CMAKE_CURRENT_BINARY_DIR}/BuiltinMimallocBitcode_${_mi_tag}.h")

    set(_mi_extra_flags)
    set(_mi_depends neverc "${_mi_runtime_c}")
    if(_mi_os STREQUAL "darwin")
      # The bundled macOS SDK is universal even though it is staged under the
      # native arm64 directory. Point both bootstrap variants at it explicitly
      # so Darwin x64 bitcode can also be generated on Linux/Windows hosts.
      list(APPEND _mi_extra_flags
          "-isysroot"
          "${LLVM_LIBRARY_OUTPUT_INTDIR}/../runtime/macos/arm64")
    elseif(_mi_os STREQUAL "win")
      # Bundled MSVC um/ lacks winternl.h; provide a tiny stub on the include
      # path. Newer UCRT macros used by mimalloc's MSVC override path are also
      # absent from the bundled CRT — define them empty for IR emission.
      set(_mi_win_stub_dir
          "${BUILTIN_MIMALLOC_SCRIPT_DIR}/mimalloc_win_stubs")
      list(APPEND _mi_extra_flags
          "-I${_mi_win_stub_dir}"
          "-D_CRT_HYBRIDPATCHABLE="
          "-D_CRT_NOEXCEPT=")
      list(APPEND _mi_depends "${_mi_win_stub_dir}/winternl.h")
    endif()

    if(NOT TARGET neverc-gen-mimalloc-src-${_mi_os})
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
      add_custom_target(neverc-gen-mimalloc-src-${_mi_os}
        DEPENDS "${_mi_runtime_c}")
    endif()

    add_custom_target(neverc-bootstrap-mimalloc-bc-${_mi_tag}
      COMMAND $<TARGET_FILE:neverc>
              -c -emit-llvm -O2
              -fno-builtin-mimalloc -fno-lto
              -ffreestanding -std=gnu11
              --target=${_mi_triple}
              -I "${_MI_SRC_ROOT}/include"
              -I "${_MI_SRC_ROOT}/src"
              ${_mi_extra_flags}
              ${NEVERC_BOOTSTRAP_PREFIX_MAP_FLAGS}
              "${_MI_SOURCE_PREFIX_MAP}"
              -w
              "${_mi_runtime_c}"
              -o "${_mi_runtime_bc}"
      COMMAND ${Python3_EXECUTABLE}
              "${BUILTIN_MIMALLOC_SCRIPT_DIR}/bin2c.py"
              "${_mi_runtime_bc}"
              -o "${_mi_bitcode_h}"
              --name "kMimallocBitcode_${_mi_tag}"
      COMMAND ${CMAKE_COMMAND} -E touch
              "${CMAKE_CURRENT_SOURCE_DIR}/Builtin/BuiltinMimalloc.cpp"
      DEPENDS ${_mi_depends} neverc-gen-mimalloc-src-${_mi_os}
      COMMENT "[NeverC] Bootstrap: compiling mimalloc for ${_mi_tag} (${_mi_triple})"
    )
    list(APPEND _MI_BOOTSTRAP_TARGETS neverc-bootstrap-mimalloc-bc-${_mi_tag})
  endforeach()

  # Compatibility aliases used by callers that select variants by OS.
  add_custom_target(neverc-bootstrap-mimalloc-bc-linux
    DEPENDS neverc-bootstrap-mimalloc-bc-linux_x64
            neverc-bootstrap-mimalloc-bc-linux_arm64)
  add_custom_target(neverc-bootstrap-mimalloc-bc-darwin
    DEPENDS neverc-bootstrap-mimalloc-bc-darwin_x64
            neverc-bootstrap-mimalloc-bc-darwin_arm64)
  add_custom_target(neverc-bootstrap-mimalloc-bc-win
    DEPENDS neverc-bootstrap-mimalloc-bc-win_x64
            neverc-bootstrap-mimalloc-bc-win_arm64)

  add_custom_target(neverc-bootstrap-mimalloc-bc
    DEPENDS ${_MI_BOOTSTRAP_TARGETS}
    COMMENT "[NeverC] Bootstrap: all mimalloc per-OS+arch bitcode targets"
  )
endif()

# ---------------------------------------------------------------------------
# BuiltinStd precompiled bitcode (single merged blob)
# ---------------------------------------------------------------------------
set(BUILTIN_STD_SCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Std")
set(BUILTIN_STD_SRC_DIR    "${NEVERC_SOURCE_DIR}/../std/src")
set(BUILTIN_STD_INC_DIR    "${NEVERC_SOURCE_DIR}/../std/include")

file(GLOB_RECURSE BUILTIN_STD_SOURCES CONFIGURE_DEPENDS
     "${BUILTIN_STD_SRC_DIR}/*.c")

# One blob per OS/arch pair, matching the string and mimalloc runtimes: the
# emitted IR encodes the target's aggregate layout, va_list lowering and
# calling conventions, so a host blob cannot be retargeted at build time.
set(_STD_VARIANTS
  "linux_x64:x86_64-unknown-linux-gnu"
  "linux_arm64:aarch64-unknown-linux-gnu"
  "darwin_x64:x86_64-apple-macosx11.0"
  "darwin_arm64:arm64-apple-macosx11.0"
  "win_x64:x86_64-pc-windows-msvc"
  "win_arm64:aarch64-pc-windows-msvc"
)

set(BUILTIN_STD_BITCODE_HDRS)
set(_STD_BOOTSTRAP_TARGETS)
foreach(_std_spec IN LISTS _STD_VARIANTS)
  string(REPLACE ":" ";" _std_parts "${_std_spec}")
  list(GET _std_parts 0 _std_tag)
  list(GET _std_parts 1 _std_triple)
  set(_std_hdr "${CMAKE_CURRENT_BINARY_DIR}/BuiltinStdBitcode_${_std_tag}.h")
  if(NOT EXISTS "${_std_hdr}")
    file(WRITE "${_std_hdr}"
      "/* Empty placeholder — run 'ninja neverc-bootstrap-std-bc' to populate. */\n"
      "static const StdBitcodeEntry kStdBitcodeEntries_${_std_tag}[] = {{0, 0, 0}};\n"
      "static const unsigned int kStdBitcodeEntryCount_${_std_tag} = 0;\n")
  endif()
  list(APPEND BUILTIN_STD_BITCODE_HDRS "${_std_hdr}")

  add_custom_target(neverc-bootstrap-std-bc-${_std_tag}
    COMMAND ${Python3_EXECUTABLE}
            "${BUILTIN_STD_SCRIPT_DIR}/build_std_bitcode.py"
            "$<TARGET_FILE:neverc>"
            "${BUILTIN_STD_SRC_DIR}"
            "${BUILTIN_STD_INC_DIR}"
            ${NEVERC_BOOTSTRAP_PREFIX_MAP_ARGS}
            --target ${_std_triple}
            --tag ${_std_tag}
            -o "${_std_hdr}"
    COMMAND ${CMAKE_COMMAND} -E touch
            "${CMAKE_CURRENT_SOURCE_DIR}/Std/BuiltinStd.cpp"
    DEPENDS neverc ${BUILTIN_STD_SOURCES}
    COMMENT
      "[NeverC] Bootstrap: compiling std library for ${_std_tag} (${_std_triple})"
  )
  list(APPEND _STD_BOOTSTRAP_TARGETS neverc-bootstrap-std-bc-${_std_tag})
endforeach()

add_custom_target(neverc-bootstrap-std-bc
  DEPENDS ${_STD_BOOTSTRAP_TARGETS}
  COMMENT "[NeverC] Bootstrap: all std library target variants"
)

# ---------------------------------------------------------------------------
# NVK Kernel Runtime precompiled bitcode (aarch64 only)
# ---------------------------------------------------------------------------
set(BUILTIN_NVK_KERNEL_SCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Builtin")
set(BUILTIN_NVK_KERNEL_RUNTIME_DIR
    "${NEVERC_SOURCE_DIR}/../runtime/android/kernel")
file(GLOB_RECURSE BUILTIN_NVK_KERNEL_SOURCES CONFIGURE_DEPENDS
    "${BUILTIN_NVK_KERNEL_RUNTIME_DIR}/src/*.c")
file(GLOB_RECURSE BUILTIN_NVK_KERNEL_HEADERS CONFIGURE_DEPENDS
    "${BUILTIN_NVK_KERNEL_RUNTIME_DIR}/src/*.h"
    "${BUILTIN_NVK_KERNEL_RUNTIME_DIR}/src/*.inc"
    "${BUILTIN_NVK_KERNEL_RUNTIME_DIR}/include/*.h"
    "${BUILTIN_NVK_KERNEL_RUNTIME_DIR}/arm64/include/*.h")
set(BUILTIN_NVK_KERNEL_INPUTS
    ${BUILTIN_NVK_KERNEL_SOURCES}
    ${BUILTIN_NVK_KERNEL_HEADERS}
    "${BUILTIN_NVK_KERNEL_SCRIPT_DIR}/build_nvk_kernel_bitcode.py")

set(BUILTIN_NVK_KERNEL_BITCODE_H
    "${CMAKE_CURRENT_BINARY_DIR}/BuiltinNvkKernelBitcode.h")
# Bracket form so embedded semicolons are not treated as CMake list separators.
set(_NEVERC_NVK_KERNEL_BITCODE_PLACEHOLDER
[=[/* Empty placeholder — run 'ninja neverc-bootstrap-nvk-kernel-bc' to populate. */
static const unsigned char kNvkKernelBC[] = {0};
static const unsigned int kNvkKernelBCLen = 0;
static const char *const kNvkKernelSymbols[] = {nullptr};
static const unsigned int kNvkKernelSymbolCount = 0;
]=])
if(EXISTS "${BUILTIN_NVK_KERNEL_BITCODE_H}")
  file(READ "${BUILTIN_NVK_KERNEL_BITCODE_H}" _NEVERC_NVK_KERNEL_BITCODE_CONTENTS)
  if(NOT _NEVERC_NVK_KERNEL_BITCODE_CONTENTS MATCHES "kNvkKernelSymbols")
    message(STATUS
      "NeverC: resetting stale NVK bitcode header without symbol index")
    file(WRITE "${BUILTIN_NVK_KERNEL_BITCODE_H}"
         "${_NEVERC_NVK_KERNEL_BITCODE_PLACEHOLDER}")
  endif()
  unset(_NEVERC_NVK_KERNEL_BITCODE_CONTENTS)
else()
  file(WRITE "${BUILTIN_NVK_KERNEL_BITCODE_H}"
       "${_NEVERC_NVK_KERNEL_BITCODE_PLACEHOLDER}")
endif()
unset(_NEVERC_NVK_KERNEL_BITCODE_PLACEHOLDER)

add_custom_target(neverc-bootstrap-nvk-kernel-bc
  COMMAND ${Python3_EXECUTABLE}
          "${BUILTIN_NVK_KERNEL_SCRIPT_DIR}/build_nvk_kernel_bitcode.py"
          "$<TARGET_FILE:neverc>"
          "${BUILTIN_NVK_KERNEL_RUNTIME_DIR}"
          ${NEVERC_BOOTSTRAP_PREFIX_MAP_ARGS}
          -o "${BUILTIN_NVK_KERNEL_BITCODE_H}"
  COMMAND ${CMAKE_COMMAND} -E touch
          "${CMAKE_CURRENT_SOURCE_DIR}/Builtin/BuiltinNvkKernel.cpp"
  DEPENDS neverc ${BUILTIN_NVK_KERNEL_INPUTS}
  COMMENT "[NeverC] Bootstrap: compiling NVK kernel runtime to embedded bitcode"
)

add_neverc_library(nevercFoundation
  Core/AndroidKernelModuleReleaseNames.cpp
  Core/Diagnostic.cpp
  Core/FileManager.cpp
  Core/IdentifierTable.cpp
  Core/LangOptions.cpp
  Core/OutputBundleTransaction.cpp
  Core/OutputCoordinator.cpp
  Core/OutputPathLease.cpp
  Core/OutputPlatform.cpp
  Core/OutputTransaction.cpp
  Core/Version.cpp

  Builtin/BuiltinString.cpp
  Builtin/BuiltinMimalloc.cpp
  Builtin/BuiltinNvkKernel.cpp

  Std/StdModule.cpp
  Std/BuiltinStd.cpp

  Target/TargetInfo.cpp
  Target/AArch64.cpp
  Target/X86.cpp
  Target/OSDefines.cpp

  ${version_inc}
  ${BUILTIN_STRING_THIN_DECLS}
  ${BUILTIN_STRING_BITCODE_H}
  ${BUILTIN_MIMALLOC_BITCODE_HDRS}
  ${BUILTIN_STD_BITCODE_HDRS}
  ${BUILTIN_NVK_KERNEL_BITCODE_H}

  DEPENDS
  NeverCDriverOptions
  )

target_include_directories(nevercFoundation
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
  )

target_link_libraries(nevercFoundation
  PRIVATE
  ${LLVM_ATOMIC_LIB}
)
