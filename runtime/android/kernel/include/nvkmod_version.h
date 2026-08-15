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
 * Bootstrap reports an exact match when Linux patch, Android/KMI identity,
 * and runtime page size match.  The catalog release token is measurement
 * evidence and is not part of Exact.  When the build has explicitly
 * selected a profile, an observed OEM release in the same Linux
 * major.minor series and with the same page size may activate as
 * NEVERC_KRT_VER_COMPAT.  The Android generation must also match when
 * the banner names one; patch, KMI, and token are ignored.  A different
 * Android generation that also changes loader-visible struct module /
 * vermagic is a separate compile-time family.
 * Unobservable, cross-series, page-size-mismatched, and wrong-generation
 * identities fail closed.
 */

#endif /* NVKMOD_VERSION_H */

/*
 * Alias remapping lives in nvk_profile_ids.h outside that header's guard.
 * Re-include it after this facade's guard so a later #include <nvkmod_version.h>
 * still remaps 51012/51513 when the first pass ran before those spellings
 * were visible.
 */
#include <nvk_profile_ids.h>
