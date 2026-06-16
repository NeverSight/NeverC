/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvk_runtime_bc.c — NVK kernel runtime bitcode compilation unit.
 *
 * Compiled to LLVM bitcode at NeverC build time and embedded into the
 * compiler binary.  NvkKernelRuntimeLinkerPass links this bitcode into
 * each user TU, providing the definitions for all NVK_RT_FN functions
 * and NVK_RT_VAR variables declared as `extern` in the public headers.
 *
 * Build command (run by CMake bootstrap target):
 *   neverc -c -emit-llvm -O0 -fno-lto --target=aarch64-linux-gnu
 *          -ffreestanding -std=gnu11 -D__KERNEL__ -DMODULE -D_NVK_IMPL -w
 *          -I<runtime/android/kernel/arm64/include>
 *          -I<runtime/android/kernel/include>
 *          nvk_runtime_bc.c -o nvk_runtime.bc
 */

#define _NVK_IMPL
#include <nvk.h>
