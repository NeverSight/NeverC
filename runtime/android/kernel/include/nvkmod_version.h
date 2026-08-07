/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVKMOD_VERSION_H
#define NVKMOD_VERSION_H

#include <linux/compiler.h>

#define NEVERC_KRT_SDK_VERSION_MAJOR 3
#define NEVERC_KRT_SDK_VERSION_MINOR 0
#define NEVERC_KRT_SDK_VERSION_PATCH 0
#define NEVERC_KRT_SDK_VERSION \
	((NEVERC_KRT_SDK_VERSION_MAJOR << 16) | \
	 (NEVERC_KRT_SDK_VERSION_MINOR << 8) | \
	 NEVERC_KRT_SDK_VERSION_PATCH)

/*
 * Profile IDs are compatibility handles, not ordered kernel versions.  Their
 * semantic identity, configuration, ABI capabilities, and layout facts live in
 * the generated profile contract below.
 */
#include <nvk_profile_ids.h>

#ifndef NEVERC_KRT_KERNEL
#  ifdef NVK_KERNEL
#    define NEVERC_KRT_KERNEL NVK_KERNEL
#  else
/* The SDK has an explicit source-compatibility default.  Android compiler mode
 * itself still requires a concrete marker and fails closed without one. */
#    define NEVERC_KRT_KERNEL NEVERC_KRT_PROFILE_ANDROID12_5_10_KMI9
#  endif
#endif

#include <nvk_profile_config.h>

/*
 * Handwritten compatibility headers branch on semantic Linux versions, never
 * on the opaque profile handle.  The full upstream version is available for
 * source-API gates; exact Android/KMI/release identity remains a separate
 * runtime activation requirement in the generated profile table.
 */
#define NEVERC_KRT_MAKE_LINUX_VERSION(major, minor, patch) \
	(((major) << 16) + ((minor) << 8) + \
	 ((patch) > 255 ? 255 : (patch)))
#define NEVERC_KRT_LINUX_API_VERSION \
	NEVERC_KRT_MAKE_LINUX_VERSION(NEVERC_KRT_PROFILE_LINUX_MAJOR, \
				      NEVERC_KRT_PROFILE_LINUX_MINOR, \
				      NEVERC_KRT_PROFILE_LINUX_PATCH)
#define NEVERC_KRT_LINUX_AT_LEAST(major, minor, patch) \
	(NEVERC_KRT_LINUX_API_VERSION >= \
	 NEVERC_KRT_MAKE_LINUX_VERSION(major, minor, patch))
#define NEVERC_KRT_LINUX_BEFORE(major, minor, patch) \
	(NEVERC_KRT_LINUX_API_VERSION < \
	 NEVERC_KRT_MAKE_LINUX_VERSION(major, minor, patch))
#define NEVERC_KRT_LINUX_IS_SERIES(major, minor) \
	(NEVERC_KRT_PROFILE_LINUX_MAJOR == (major) && \
	 NEVERC_KRT_PROFILE_LINUX_MINOR == (minor))

/*
 * nvk_profile_config.h publishes the stable NEVERC_KRT_* aliases only after
 * checking that any numeric predefinition agrees with the selected profile.
 * Layout, configuration, KCFI, and release strings form one pinned contract;
 * a new GKI or OEM contract must be added to the catalog with matching release
 * and manifest evidence instead of overriding individual facts per TU.
 */

/*
 * Preserve the selected source contract for the compiler.  The marker is
 * re-entrant so command-line/config includes and SDK headers converge on one
 * mode/profile pair without depending on include order.
 */
#ifdef __ASSEMBLER__
#include <nvk_profile_contract_asm.h>
#else
#include <nvk_profile_marker.h>
#endif

/*
 * The embedded runtime bitcode consumes generated descriptors and named ABI
 * capabilities; it never compares these compatibility handles numerically.
 * Bootstrap activates layouts only after the pinned kernel release token,
 * Android/KMI identity, and runtime page size all match exactly.  Unknown, future,
 * malformed, or mismatched identities fail closed with no nearest-version,
 * maximum-size, or vermagic fallback.
 */

#endif /* NVKMOD_VERSION_H */
