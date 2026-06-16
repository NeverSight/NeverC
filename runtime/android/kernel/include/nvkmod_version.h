/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVKMOD_VERSION_H
#define NVKMOD_VERSION_H

#define NEVERC_KRT_SDK_VERSION_MAJOR 3
#define NEVERC_KRT_SDK_VERSION_MINOR 0
#define NEVERC_KRT_SDK_VERSION_PATCH 0
#define NEVERC_KRT_SDK_VERSION \
	((NEVERC_KRT_SDK_VERSION_MAJOR << 16) | (NEVERC_KRT_SDK_VERSION_MINOR << 8) | \
	 NEVERC_KRT_SDK_VERSION_PATCH)

#ifndef NEVERC_KRT_KERNEL
#define NEVERC_KRT_KERNEL 510
#endif

/* offsetof(struct module, list): enum state(4) + pad(4) */
#ifndef NEVERC_KRT_OFF_LIST
#define NEVERC_KRT_OFF_LIST 8
#endif

/* offsetof(struct module, name): enum state(4) + pad(4) + struct list_head(16) */
#ifndef NEVERC_KRT_OFF_NAME
#define NEVERC_KRT_OFF_NAME 24
#endif

#if NEVERC_KRT_KERNEL == 510
/* Android 12, Linux 5.10 (GKI android12-5.10, KMI gen 9) */
#  ifndef NEVERC_KRT_VERMAGIC
#    define NEVERC_KRT_VERMAGIC                                                        \
       "5.10.198-android12-9 SMP preempt mod_unload modversions aarch64"
#  endif
#  ifndef NEVERC_KRT_KERNEL_STR
#    define NEVERC_KRT_KERNEL_STR "android12-5.10"
#  endif
/* Computed from the prepared android12-5.10 GKI tree via
 * tools/gen_struct_module_offsets.c (CONFIG_CFI_CLANG=y, MODULE_UNLOAD=y, ...). */
#  ifndef NEVERC_KRT_OFF_INIT
#    define NEVERC_KRT_OFF_INIT 400 /* 0x190 */
#  endif
#  ifndef NEVERC_KRT_OFF_EXIT
#    define NEVERC_KRT_OFF_EXIT 960 /* 0x3C0 */
#  endif
#  ifndef NEVERC_KRT_MODULE_SIZE
#    define NEVERC_KRT_MODULE_SIZE 1024 /* 0x400, sizeof(struct module) */
#  endif
#  ifndef NEVERC_KRT_FILE_DENTRY_OFF
#    define NEVERC_KRT_FILE_DENTRY_OFF 0x18
#  endif

#elif NEVERC_KRT_KERNEL == 515
/* Android 13, Linux 5.15 (GKI android13-5.15, KMI gen 8) */
#  ifndef NEVERC_KRT_VERMAGIC
#    define NEVERC_KRT_VERMAGIC                                                        \
       "5.15.206-android13-8 SMP preempt mod_unload modversions aarch64"
#  endif
#  ifndef NEVERC_KRT_KERNEL_STR
#    define NEVERC_KRT_KERNEL_STR "android13-5.15"
#  endif
/* Verified from GKI android13-5.15 (release 5.15.206) via gki_defconfig +
 * modules_prepare + tools/gen_struct_module_offsets.c (clang-18). */
#  ifndef NEVERC_KRT_OFF_INIT
#    define NEVERC_KRT_OFF_INIT 376 /* 0x178 */
#  endif
#  ifndef NEVERC_KRT_OFF_EXIT
#    define NEVERC_KRT_OFF_EXIT 872 /* 0x368 */
#  endif
#  ifndef NEVERC_KRT_MODULE_SIZE
#    define NEVERC_KRT_MODULE_SIZE 960 /* 0x3C0, sizeof(struct module) */
#  endif
#  ifndef NEVERC_KRT_FILE_DENTRY_OFF
#    define NEVERC_KRT_FILE_DENTRY_OFF 0x18
#  endif

#elif NEVERC_KRT_KERNEL == 601
/* Android 14, Linux 6.1 (GKI android14-6.1, KMI gen 11) */
#  ifndef NEVERC_KRT_VERMAGIC
#    define NEVERC_KRT_VERMAGIC                                                        \
       "6.1.172-android14-11 SMP preempt mod_unload modversions aarch64"
#  endif
#  ifndef NEVERC_KRT_KERNEL_STR
#    define NEVERC_KRT_KERNEL_STR "android14-6.1"
#  endif
/* Verified from GKI android14-6.1 (release 6.1.172) via gki_defconfig +
 * modules_prepare + tools/gen_struct_module_offsets.c (clang-18). */
#  ifndef NEVERC_KRT_OFF_INIT
#    define NEVERC_KRT_OFF_INIT 368 /* 0x170 */
#  endif
#  ifndef NEVERC_KRT_OFF_EXIT
#    define NEVERC_KRT_OFF_EXIT 968 /* 0x3C8 */
#  endif
#  ifndef NEVERC_KRT_MODULE_SIZE
#    define NEVERC_KRT_MODULE_SIZE 1024 /* 0x400, sizeof(struct module) */
#  endif
#  ifndef NEVERC_KRT_FILE_DENTRY_OFF
#    define NEVERC_KRT_FILE_DENTRY_OFF 0x18
#  endif

#elif NEVERC_KRT_KERNEL == 606
/* Android 15, Linux 6.6 (GKI android15-6.6, KMI gen 8) */
#  ifndef NEVERC_KRT_VERMAGIC
#    define NEVERC_KRT_VERMAGIC                                                        \
       "6.6.138-android15-8 SMP preempt mod_unload modversions aarch64"
#  endif
#  ifndef NEVERC_KRT_KERNEL_STR
#    define NEVERC_KRT_KERNEL_STR "android15-6.6"
#  endif
/* Verified from GKI android15-6.6 (release 6.6.138) via gki_defconfig +
 * modules_prepare + tools/gen_struct_module_offsets.c (clang-18). */
#  ifndef NEVERC_KRT_OFF_INIT
#    define NEVERC_KRT_OFF_INIT 392 /* 0x188 */
#  endif
#  ifndef NEVERC_KRT_OFF_EXIT
#    define NEVERC_KRT_OFF_EXIT 1448 /* 0x5A8 */
#  endif
#  ifndef NEVERC_KRT_MODULE_SIZE
#    define NEVERC_KRT_MODULE_SIZE 1536 /* 0x600, sizeof(struct module) */
#  endif
#  ifndef NEVERC_KRT_FILE_DENTRY_OFF
#    define NEVERC_KRT_FILE_DENTRY_OFF 0xA0
#  endif

#elif NEVERC_KRT_KERNEL == 612
/* Android 16, Linux 6.12 (GKI android16-6.12, KMI gen 6) */
#  ifndef NEVERC_KRT_VERMAGIC
#    define NEVERC_KRT_VERMAGIC                                                        \
       "6.12.81-android16-6 SMP preempt mod_unload modversions aarch64"
#  endif
#  ifndef NEVERC_KRT_KERNEL_STR
#    define NEVERC_KRT_KERNEL_STR "android16-6.12"
#  endif
/* Verified from GKI android16-6.12 (release 6.12.81) via gki_defconfig +
 * modules_prepare + tools/gen_struct_module_offsets.c (clang-18). */
#  ifndef NEVERC_KRT_OFF_INIT
#    define NEVERC_KRT_OFF_INIT 392 /* 0x188 */
#  endif
#  ifndef NEVERC_KRT_OFF_EXIT
#    define NEVERC_KRT_OFF_EXIT 1504 /* 0x5E0 */
#  endif
#  ifndef NEVERC_KRT_MODULE_SIZE
#    define NEVERC_KRT_MODULE_SIZE 1600 /* 0x640, sizeof(struct module) */
#  endif
#  ifndef NEVERC_KRT_FILE_DENTRY_OFF
#    define NEVERC_KRT_FILE_DENTRY_OFF 0x48
#  endif

#else
#  error                                                                        \
      "Unknown NEVERC_KRT_KERNEL; use 510 / 515 / 601 / 606 / 612 or define presets manually"
#endif

/*
 * struct dentry d_name.name offset — stable across 5.10-6.12:
 *   d_flags(4) + d_seq(4) + d_hash(16) + d_parent(8) + d_name.hash_len(8)
 */
#ifndef NEVERC_KRT_DENTRY_DNAME_OFF
#define NEVERC_KRT_DENTRY_DNAME_OFF 0x28
#endif

/*
 * struct sock_common skc_dport / skc_num offsets — stable across 5.10-6.12:
 *   skc_addrpair(8) + skc_hash(4) + skc_dport(2) + skc_num(2)
 */
#ifndef NEVERC_KRT_SKC_DPORT_OFF
#define NEVERC_KRT_SKC_DPORT_OFF 12
#endif
#ifndef NEVERC_KRT_SKC_NUM_OFF
#define NEVERC_KRT_SKC_NUM_OFF 14
#endif

/*
 * Total bytes reserved for the this_module blob.  Only needs to cover up to
 * max(NEVERC_KRT_OFF_INIT, NEVERC_KRT_OFF_EXIT)+8; the kernel zero-fills the remainder of its
 * own (possibly larger) struct module.  Generously rounded up.
 */
#ifndef NEVERC_KRT_MODULE_SIZE
#define NEVERC_KRT_MODULE_SIZE 0x600
#endif

/*
 * Runtime-settable version parameters.  The bitcode is compiled once
 * (with the default NEVERC_KRT_KERNEL=510), so compile-time #if chains
 * inside .c files would always evaluate against 5.10 — wrong when the
 * user targets 6.6 or 6.12.
 *
 * These globals are set by _neverc_krt_version_setup() (an inline in
 * this header, compiled with the USER's -DNEVERC_KRT_KERNEL=xxx).
 * The bitcode .c files read them at runtime instead of using
 * NEVERC_KRT_MODULE_SIZE / NEVERC_KRT_KERNEL directly.
 *
 * Safe default: if never set (0), callers fall back to the maximum
 * struct module size across 5.10-6.12 (0x640 = 1600).
 */
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_module_size;
NEVERC_KRT_RT_VAR int           _neverc_krt_kernel_ver;
NEVERC_KRT_RT_VAR unsigned long _neverc_krt_file_dentry_off;

static __always_inline unsigned long _neverc_krt_get_module_size(void)
{
	unsigned long sz = __atomic_load_n(&_neverc_krt_module_size,
					   __ATOMIC_RELAXED);
	return sz ? sz : 0x640;
}

static __always_inline void _neverc_krt_version_setup(void)
{
	if (!__atomic_load_n(&_neverc_krt_kernel_ver, __ATOMIC_ACQUIRE)) {
		if (!__atomic_load_n(&_neverc_krt_file_dentry_off,
				     __ATOMIC_RELAXED))
			__atomic_store_n(&_neverc_krt_file_dentry_off,
					 NEVERC_KRT_FILE_DENTRY_OFF,
					 __ATOMIC_RELAXED);
		__atomic_store_n(&_neverc_krt_module_size,
				 NEVERC_KRT_MODULE_SIZE, __ATOMIC_RELAXED);
		__atomic_store_n(&_neverc_krt_kernel_ver,
				 NEVERC_KRT_KERNEL, __ATOMIC_RELEASE);
	}
}

#endif /* NVKMOD_VERSION_H */
