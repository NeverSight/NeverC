# core_files list contains the headers shared by all platforms.
# Please consider adding new platform specific headers
# to platform specific lists below.
set(core_files
  float.h
  inttypes.h
  iso646.h
  limits.h
  stdalign.h
  stdarg.h
  __stdarg___gnuc_va_list.h
  __stdarg___va_copy.h
  __stdarg_va_arg.h
  __stdarg_va_copy.h
  __stdarg_va_list.h
  stdatomic.h
  stdbool.h
  stdckdint.h
  stddef.h
  __stddef_max_align_t.h
  __stddef_null.h
  __stddef_nullptr_t.h
  __stddef_offsetof.h
  __stddef_ptrdiff_t.h
  __stddef_rsize_t.h
  __stddef_size_t.h
  __stddef_unreachable.h
  __stddef_wchar_t.h
  __stddef_wint_t.h
  stdint.h
  stdnoreturn.h
  tgmath.h
  unwind.h
  unistd.h
  fcntl.h
  string.h
  stdlib.h
  sys/mman.h
  sys/stat.h
  neverc/kernel.h
  varargs.h
  )

set(aarch64_common_files
  # AArch64 ACLE compatibility header.
  arm_acle.h
  )

set(aarch64_only_files
  arm64intr.h
  arm_neon_sve_bridge.h
  )


set(x86_files
# Intrinsics
  adcintrin.h
  adxintrin.h
  ammintrin.h
  avx2intrin.h
  avx512bf16intrin.h
  avx512bitalgintrin.h
  avx512bwintrin.h
  avx512cdintrin.h
  avx512dqintrin.h
  avx512fintrin.h
  avx512fp16intrin.h
  avx512ifmaintrin.h
  avx512ifmavlintrin.h
  avx512vbmi2intrin.h
  avx512vbmiintrin.h
  avx512vbmivlintrin.h
  avx512vlbf16intrin.h
  avx512vlbitalgintrin.h
  avx512vlbwintrin.h
  avx512vlcdintrin.h
  avx512vldqintrin.h
  avx512vlfp16intrin.h
  avx512vlintrin.h
  avx512vlvbmi2intrin.h
  avx512vlvnniintrin.h
  avx512vlvp2intersectintrin.h
  avx512vnniintrin.h
  avx512vp2intersectintrin.h
  avx512vpopcntdqintrin.h
  avx512vpopcntdqvlintrin.h
  avxifmaintrin.h
  avxintrin.h
  avxneconvertintrin.h
  avxvnniint16intrin.h
  avxvnniint8intrin.h
  avxvnniintrin.h
  bmi2intrin.h
  bmiintrin.h
  cetintrin.h
  cldemoteintrin.h
  clflushoptintrin.h
  clwbintrin.h
  clzerointrin.h
  crc32intrin.h
  emmintrin.h
  f16cintrin.h
  fmaintrin.h
  fxsrintrin.h
  gfniintrin.h
  ia32intrin.h
  immintrin.h
  invpcidintrin.h
  lzcntintrin.h
  mmintrin.h
  movdirintrin.h
  mwaitxintrin.h
  nmmintrin.h
  pmmintrin.h
  popcntintrin.h
  prfchiintrin.h
  prfchwintrin.h
  ptwriteintrin.h
  rdpruintrin.h
  rdseedintrin.h
  serializeintrin.h
  shaintrin.h
  smmintrin.h
  tmmintrin.h
  vaesintrin.h
  vpclmulqdqintrin.h
  waitpkgintrin.h
  wbnoinvdintrin.h
  __wmmintrin_aes.h
  wmmintrin.h
  __wmmintrin_pclmul.h
  x86gprintrin.h
  x86intrin.h
  xmmintrin.h
  xsavecintrin.h
  xsaveintrin.h
  xsaveoptintrin.h
  xsavesintrin.h
# others
  cet.h
  cpuid.h
  )

set(windows_only_files
  intrin.h
  vadefs.h
  varargs_msvc.h
  windows.h
)

set(utility_files
  mm_malloc.h
)

# Only x86/x64, AArch64.
set(files
  ${core_files}
  ${aarch64_common_files}
  ${aarch64_only_files}
  ${x86_files}
  ${windows_only_files}
  ${utility_files}
  )
set(llvm_libc_wrapper_files)

include(GetNeverCResourceDir)
get_neverc_resource_dir(output_dir PREFIX ${LLVM_LIBRARY_OUTPUT_INTDIR}/.. SUBDIR include)
set(out_files)

set(aarch64_common_generated_files)
set(aarch64_only_generated_files)

function(copy_header_to_output_dir src_dir file)
  set(src ${src_dir}/${file})
  set(dst ${output_dir}/${file})
  add_custom_command(OUTPUT ${dst}
    DEPENDS ${src}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src} ${dst}
    COMMENT "Copying NeverC's ${file}...")
  list(APPEND out_files ${dst})
  set(out_files ${out_files} PARENT_SCOPE)
endfunction(copy_header_to_output_dir)



# Copy header files from the source directory to the build directory
foreach( f ${files} ${llvm_libc_wrapper_files})
  copy_header_to_output_dir(${CMAKE_CURRENT_SOURCE_DIR} ${f})
endforeach( f )

# Copy pre-built ARM header files to the build directory
if(AArch64 IN_LIST LLVM_TARGETS_TO_BUILD)
  copy_header_to_output_dir(${CMAKE_CURRENT_SOURCE_DIR} arm_neon.h)
  copy_header_to_output_dir(${CMAKE_CURRENT_SOURCE_DIR} arm_fp16.h)
  copy_header_to_output_dir(${CMAKE_CURRENT_SOURCE_DIR} arm_sve.h)
  copy_header_to_output_dir(${CMAKE_CURRENT_SOURCE_DIR} arm_sme_draft_spec_subject_to_change.h)
  copy_header_to_output_dir(${CMAKE_CURRENT_SOURCE_DIR} arm_bf16.h)
  copy_header_to_output_dir(${CMAKE_CURRENT_SOURCE_DIR} arm_vector_types.h)

  # Add headers to target specific lists
  list(APPEND aarch64_common_generated_files
    "${output_dir}/arm_neon.h"
    "${output_dir}/arm_fp16.h"
    )

  list(APPEND aarch64_only_generated_files
    "${output_dir}/arm_sve.h"
    "${output_dir}/arm_sme_draft_spec_subject_to_change.h"
    "${output_dir}/arm_bf16.h"
    "${output_dir}/arm_vector_types.h"
    )
endif()


function(add_header_target target_name file_list)
  add_custom_target(${target_name} DEPENDS ${file_list})
  set_target_properties(${target_name} PROPERTIES
    FOLDER "Misc"
    RUNTIME_OUTPUT_DIRECTORY "${output_dir}")
endfunction()

# The neverc executable depends on these headers explicitly; keep them out of
# the default ALL target so non-compiler CMake targets do not generate them.
add_custom_target("neverc-resource-headers" DEPENDS ${out_files})
set_target_properties("neverc-resource-headers" PROPERTIES
  FOLDER "Misc"
  RUNTIME_OUTPUT_DIRECTORY "${output_dir}")
add_dependencies("neverc-resource-headers"
                 "core-resource-headers"
                 "aarch64-common-resource-headers"
                 "aarch64-resource-headers"
                 "x86-resource-headers"
                 "windows-resource-headers"
                 "utility-resource-headers")
# Core/common headers
add_header_target("core-resource-headers" ${core_files})
add_header_target("aarch64-common-resource-headers" "${aarch64_common_files};${aarch64_common_generated_files}")
add_header_target("aarch64-resource-headers" "${aarch64_only_files};${aarch64_only_generated_files}")
add_header_target("x86-resource-headers" "${x86_files}")
add_header_target("windows-resource-headers" ${windows_only_files})
add_header_target("utility-resource-headers" ${utility_files})
get_neverc_resource_dir(header_install_dir SUBDIR include)

#############################################################
# Install rules for the catch-all NeverC resource-headers target
install(
  FILES ${files}
  DESTINATION ${header_install_dir}
  COMPONENT neverc-resource-headers)

#############################################################
# Install rules for separate header lists
install(
  FILES ${core_files}
  DESTINATION ${header_install_dir}
  EXCLUDE_FROM_ALL
  COMPONENT core-resource-headers)

install(
  FILES ${aarch64_common_files} ${aarch64_common_generated_files}
  DESTINATION ${header_install_dir}
  EXCLUDE_FROM_ALL
  COMPONENT aarch64-common-resource-headers)

install(
  FILES ${aarch64_only_files} ${aarch64_only_generated_files}
  DESTINATION ${header_install_dir}
  EXCLUDE_FROM_ALL
  COMPONENT aarch64-resource-headers)

install(
  FILES ${x86_files}
  DESTINATION ${header_install_dir}
  EXCLUDE_FROM_ALL
  COMPONENT x86-resource-headers)

install(
  FILES ${utility_files}
  DESTINATION ${header_install_dir}
  EXCLUDE_FROM_ALL
  COMPONENT utility-resource-headers)

install(
  FILES ${windows_only_files}
  DESTINATION ${header_install_dir}
  EXCLUDE_FROM_ALL
  COMPONENT windows-resource-headers)
#############################################################

if (NOT LLVM_ENABLE_IDE)
  add_llvm_install_targets(install-neverc-resource-headers
                           DEPENDS neverc-resource-headers
                           COMPONENT neverc-resource-headers)

  add_llvm_install_targets(install-core-resource-headers
                           DEPENDS core-resource-headers
                           COMPONENT core-resource-headers)
  add_llvm_install_targets(install-aarch64-common-resource-headers
                           DEPENDS aarch64-common-resource-headers
                           COMPONENT aarch64-common-resource-headers)
  add_llvm_install_targets(install-aarch64-resource-headers
                           DEPENDS aarch64-resource-headers
                           COMPONENT aarch64-resource-headers)
  add_llvm_install_targets(install-x86-resource-headers
                           DEPENDS x86-resource-headers
                           COMPONENT x86-resource-headers)
  add_llvm_install_targets(install-windows-resource-headers
                           DEPENDS windows-resource-headers
                           COMPONENT windows-resource-headers)
  add_llvm_install_targets(install-utility-resource-headers
                           DEPENDS utility-resource-headers
                           COMPONENT utility-resource-headers)
endif()

source_group("NeverC Runtime Headers" FILES ${files})
