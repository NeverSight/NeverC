/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Deliberately no whole-file include guard: a driver-forced first inclusion may
 * run before a source-local NVK_KERNEL definition.  The emitted guard is set
 * only after markers have actually been materialized.
 */
#if !defined(NEVERC_KRT_PROFILE_MARKERS_EMITTED)
#if !defined(NEVERC_KRT_KERNEL) && defined(NVK_KERNEL)
#define NEVERC_KRT_KERNEL NVK_KERNEL
#endif
#if defined(NEVERC_KRT_KERNEL)
#include <nvk_profile_config.h>
#if defined(__neverc__) && defined(__KERNEL__) && defined(MODULE) &&           \
    !defined(__ASSEMBLER__) &&                                                 \
    !defined(NEVERC_KRT_SUPPRESS_KCFI_MODE_MARKER)
__attribute__((visibility("hidden")))
const unsigned int __neverc_krt_kcfi_mode_marker = NEVERC_KRT_PROFILE_KCFI_MODE;
__attribute__((visibility("hidden")))
const unsigned int __neverc_krt_profile_marker = NEVERC_KRT_PROFILE_ID;
__attribute__((visibility("hidden")))
const unsigned int __neverc_krt_scs_mode_marker = NEVERC_KRT_PROFILE_SCS_MODE;
#define NEVERC_KRT_PROFILE_MARKERS_EMITTED 1
#endif
#endif
#endif
