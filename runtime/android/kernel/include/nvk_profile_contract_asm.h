/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Deliberately no whole-file guard.  A driver-forced inclusion can precede a
 * source-local profile selection; nvkmod_version.h includes this file again
 * after applying its source-compatibility default.  The emitted guard is set
 * only after the native record exists.
 */
#if defined(__ASSEMBLER__) && \
    !defined(NEVERC_KRT_PROFILE_ASM_CONTRACT_EMITTED)
#if !defined(NEVERC_KRT_KERNEL) && defined(NVK_KERNEL)
#define NEVERC_KRT_KERNEL NVK_KERNEL
#endif

#if defined(NEVERC_KRT_KERNEL)
#include <nvk_profile_config.h>
#if defined(NEVERC_KRT_PROFILE_CONFIGURED)
/* Keep this serialization identical to AndroidKernelProfileContract::encode. */
	.pushsection .neverc.android.kernel.profile,"a",@progbits
	.balign 8
	.quad ((NEVERC_KRT_PROFILE_ID << 32) | (1 << 16) | \
	       (NEVERC_KRT_PROFILE_SCS_MODE << 8) | \
	       NEVERC_KRT_PROFILE_KCFI_MODE)
	.popsection;
#define NEVERC_KRT_PROFILE_ASM_CONTRACT_EMITTED 1
#endif
#endif
#endif
