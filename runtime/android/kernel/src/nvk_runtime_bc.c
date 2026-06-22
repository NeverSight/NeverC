/* SPDX-License-Identifier: GPL-2.0 */
/*
 * neverc_krt_runtime_bc.c — NVK kernel runtime bitcode compilation unit.
 *
 * Compiled to LLVM bitcode at NeverC build time and embedded into the
 * compiler binary.  NvkKernelRuntimeLinkerPass links this bitcode into
 * each user TU, providing the definitions for all public API functions
 * and shared variables declared as `extern` in the public headers.
 *
 * Build command (run by CMake bootstrap target):
 *   neverc -c -emit-llvm -O0 -fno-lto --target=aarch64-linux-gnu
 *          -ffreestanding -std=gnu11 -D__KERNEL__ -DMODULE -D_NEVERC_KRT_IMPL -w
 *          -I<runtime/android/kernel/arm64/include>
 *          -I<runtime/android/kernel/include>
 *          neverc_krt_runtime_bc.c -o neverc_krt_runtime.bc
 */

#ifndef _NEVERC_KRT_IMPL
#define _NEVERC_KRT_IMPL
#endif
#include <nvk.h>
