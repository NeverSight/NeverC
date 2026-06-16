/* SPDX-License-Identifier: GPL-2.0 */
/*
 * neverc_krt_rt.h — NVK runtime linkage macros.
 *
 * Included by all NVK runtime headers.  Controls whether non-inline
 * functions and global variables are emitted as extern declarations
 * (for user code) or as actual definitions (for the bitcode module).
 *
 * When _NEVERC_KRT_IMPL is defined (only by neverc_krt_runtime_bc.c), non-inline
 * function bodies and variable definitions are included.  Otherwise
 * only extern declarations are emitted — the NvkKernelRuntimeLinkerPass
 * links the precompiled bitcode at compile time to resolve them.
 */
#ifndef NEVERC_KRT_RT_H
#define NEVERC_KRT_RT_H

#ifdef _NEVERC_KRT_IMPL
/*
 * Building the bitcode module: wrap function definitions that should
 * go into the precompiled bitcode.
 *
 *   NEVERC_KRT_RT_VAR  neverc_krt_modalloc_fn _neverc_krt_modalloc;
 *   NEVERC_KRT_RT_FN int neverc_krt_hook_install(...) { ... }
 *
 * expands to plain (non-static) definitions with external linkage.
 */
#define NEVERC_KRT_RT_FN    /* non-static definition */
#define NEVERC_KRT_RT_VAR   /* non-static definition */
#else
/*
 * User code: only extern declarations, no function bodies.
 *
 *   NEVERC_KRT_RT_VAR  neverc_krt_modalloc_fn _neverc_krt_modalloc;
 *   NEVERC_KRT_RT_FN int neverc_krt_hook_install(...);   // declaration only
 *
 * Function bodies must be guarded with:
 *   #ifdef _NEVERC_KRT_IMPL
 *   ... body ...
 *   #endif
 */
#define NEVERC_KRT_RT_FN    extern
#define NEVERC_KRT_RT_VAR   extern
#endif

#endif /* NEVERC_KRT_RT_H */
