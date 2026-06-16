/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvk_rt.h — NVK runtime linkage macros.
 *
 * Included by all NVK runtime headers.  Controls whether non-inline
 * functions and global variables are emitted as extern declarations
 * (for user code) or as actual definitions (for the bitcode module).
 *
 * When _NVK_IMPL is defined (only by nvk_runtime_bc.c), non-inline
 * function bodies and variable definitions are included.  Otherwise
 * only extern declarations are emitted — the NvkKernelRuntimeLinkerPass
 * links the precompiled bitcode at compile time to resolve them.
 */
#ifndef NVK_RT_H
#define NVK_RT_H

#ifdef _NVK_IMPL
/*
 * Building the bitcode module: wrap function definitions that should
 * go into the precompiled bitcode.
 *
 *   NVK_RT_VAR  nvk_modalloc_fn _nvk_modalloc;
 *   NVK_RT_FN int nvk_hook_install(...) { ... }
 *
 * expands to plain (non-static) definitions with external linkage.
 */
#define NVK_RT_FN    /* non-static definition */
#define NVK_RT_VAR   /* non-static definition */
#else
/*
 * User code: only extern declarations, no function bodies.
 *
 *   NVK_RT_VAR  nvk_modalloc_fn _nvk_modalloc;
 *   NVK_RT_FN int nvk_hook_install(...);   // declaration only
 *
 * Function bodies must be guarded with:
 *   #ifdef _NVK_IMPL
 *   ... body ...
 *   #endif
 */
#define NVK_RT_FN    extern
#define NVK_RT_VAR   extern
#endif

#endif /* NVK_RT_H */
