/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVKMOD_VERSION_H
#define NVKMOD_VERSION_H

#include <linux/compiler.h>

#define NEVERC_KRT_SDK_VERSION_MAJOR 3
#define NEVERC_KRT_SDK_VERSION_MINOR 0
#define NEVERC_KRT_SDK_VERSION_PATCH 0
#define NEVERC_KRT_SDK_VERSION \
	((NEVERC_KRT_SDK_VERSION_MAJOR << 16) | (NEVERC_KRT_SDK_VERSION_MINOR << 8) | \
	 NEVERC_KRT_SDK_VERSION_PATCH)

#ifndef NEVERC_KRT_KERNEL
#  ifdef NVK_KERNEL
#    define NEVERC_KRT_KERNEL NVK_KERNEL
#  else
#    define NEVERC_KRT_KERNEL 510
#  endif
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
#  ifndef NEVERC_KRT_FOPS_SIZE
#    define NEVERC_KRT_FOPS_SIZE 288
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
 * modules_prepare + tools/gen_struct_module_offsets.c.
 * CONFIG_DEBUG_INFO_BTF_MODULES=y adds btf_data_size+btf_data (+16 bytes)
 * between init and exit. */
#  ifndef NEVERC_KRT_OFF_INIT
#    define NEVERC_KRT_OFF_INIT 376 /* 0x178 */
#  endif
#  ifndef NEVERC_KRT_OFF_EXIT
#    define NEVERC_KRT_OFF_EXIT 888 /* 0x378 */
#  endif
#  ifndef NEVERC_KRT_MODULE_SIZE
#    define NEVERC_KRT_MODULE_SIZE 960 /* 0x3C0, sizeof(struct module) */
#  endif
#  ifndef NEVERC_KRT_FILE_DENTRY_OFF
#    define NEVERC_KRT_FILE_DENTRY_OFF 0x18
#  endif
#  ifndef NEVERC_KRT_FOPS_SIZE
#    define NEVERC_KRT_FOPS_SIZE 288
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
 * modules_prepare + tools/gen_struct_module_offsets.c.
 * CONFIG_DEBUG_INFO_BTF_MODULES=y adds btf_data_size+btf_data (+16 bytes). */
#  ifndef NEVERC_KRT_OFF_INIT
#    define NEVERC_KRT_OFF_INIT 368 /* 0x170 */
#  endif
#  ifndef NEVERC_KRT_OFF_EXIT
#    define NEVERC_KRT_OFF_EXIT 984 /* 0x3D8 */
#  endif
#  ifndef NEVERC_KRT_MODULE_SIZE
#    define NEVERC_KRT_MODULE_SIZE 1088 /* 0x440, sizeof(struct module) */
#  endif
#  ifndef NEVERC_KRT_FILE_DENTRY_OFF
#    define NEVERC_KRT_FILE_DENTRY_OFF 0x18
#  endif
#  ifndef NEVERC_KRT_FOPS_SIZE
#    define NEVERC_KRT_FOPS_SIZE 272
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
 * modules_prepare + tools/gen_struct_module_offsets.c.
 * CONFIG_DEBUG_INFO_BTF_MODULES=y adds btf_data_size+btf_data (+16 bytes). */
#  ifndef NEVERC_KRT_OFF_INIT
#    define NEVERC_KRT_OFF_INIT 392 /* 0x188 */
#  endif
#  ifndef NEVERC_KRT_OFF_EXIT
#    define NEVERC_KRT_OFF_EXIT 1464 /* 0x5B8 */
#  endif
#  ifndef NEVERC_KRT_MODULE_SIZE
#    define NEVERC_KRT_MODULE_SIZE 1536 /* 0x600, sizeof(struct module) */
#  endif
#  ifndef NEVERC_KRT_FILE_DENTRY_OFF
#    define NEVERC_KRT_FILE_DENTRY_OFF 0xB0
#  endif
#  ifndef NEVERC_KRT_FOPS_SIZE
#    define NEVERC_KRT_FOPS_SIZE 264
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
 * modules_prepare + tools/gen_struct_module_offsets.c.
 * CONFIG_DEBUG_INFO_BTF_MODULES=y adds btf_data_size+btf_base_data_size+
 * btf_data+btf_base_data (+24 bytes, 4 fields vs 2 in older kernels). */
#  ifndef NEVERC_KRT_OFF_INIT
#    define NEVERC_KRT_OFF_INIT 392 /* 0x188 */
#  endif
#  ifndef NEVERC_KRT_OFF_EXIT
#    define NEVERC_KRT_OFF_EXIT 1528 /* 0x5F8 */
#  endif
#  ifndef NEVERC_KRT_MODULE_SIZE
#    define NEVERC_KRT_MODULE_SIZE 1600 /* 0x640, sizeof(struct module) */
#  endif
#  ifndef NEVERC_KRT_FILE_DENTRY_OFF
#    define NEVERC_KRT_FILE_DENTRY_OFF 0x48
#  endif
#  ifndef NEVERC_KRT_FOPS_SIZE
#    define NEVERC_KRT_FOPS_SIZE 264
#  endif
/* offsetof(struct task_struct, thread_info.cpu) — verified from GKI 6.12
 * asm-offsets.h (TSK_TI_CPU) and BTF layout evidence. */
#  ifndef NEVERC_KRT_TASK_CPU
#    define NEVERC_KRT_TASK_CPU 40
#  endif

#elif NEVERC_KRT_KERNEL == 618
/* Android 17, Linux 6.18 (GKI android17-6.18, launched 2025-11-30, KMI gen 5).
 * Verified from GKI android17-6.18 (release 6.18.24) via gki_defconfig +
 * modules_prepare + tools/verify_gki_offsets.sh --print.
 * struct module grew +64 bytes over 6.12: module_memory refactoring.
 *
 * vermagic is device-specific: stock GKI uses "6.18.24-android17-5 …".
 * OEM kernels may set CONFIG_LOCALVERSION (e.g. "-4k") — use
 * neverc_krt_patch_vermagic() at runtime or
 * -DNEVERC_KRT_VERMAGIC='"…"' at build. */
#  ifndef NEVERC_KRT_VERMAGIC
#    define NEVERC_KRT_VERMAGIC                                                        \
       "6.18.24-android17-5 SMP preempt mod_unload modversions aarch64"
#  endif
#  ifndef NEVERC_KRT_KERNEL_STR
#    define NEVERC_KRT_KERNEL_STR "android17-6.18"
#  endif
#  ifndef NEVERC_KRT_OFF_INIT
#    define NEVERC_KRT_OFF_INIT 376 /* 0x178 */
#  endif
#  ifndef NEVERC_KRT_OFF_EXIT
#    define NEVERC_KRT_OFF_EXIT 1536 /* 0x600 */
#  endif
#  ifndef NEVERC_KRT_MODULE_SIZE
#    define NEVERC_KRT_MODULE_SIZE 1664 /* 0x680, sizeof(struct module) */
#  endif
#  ifndef NEVERC_KRT_FILE_DENTRY_OFF
#    define NEVERC_KRT_FILE_DENTRY_OFF 0x48
#  endif
#  ifndef NEVERC_KRT_FOPS_SIZE
#    define NEVERC_KRT_FOPS_SIZE 272 /* mmap_prepare added in 6.18 */
#  endif
/* offsetof(struct task_struct, thread_info.cpu) — verified from GKI 6.18
 * asm-offsets.h (TSK_TI_CPU) and BTF layout evidence. */
#  ifndef NEVERC_KRT_TASK_CPU
#    define NEVERC_KRT_TASK_CPU 40
#  endif

#else
#  error                                                                        \
      "Unknown NEVERC_KRT_KERNEL; use 510 / 515 / 601 / 606 / 612 / 618 or define presets manually"
#endif

/*
 * offsetof(struct task_struct, thread_info.preempt_count), verified from each
 * configured profile's BTF/DWARF manifest.  Android 12's thread_info still
 * contains addr_limit; it was removed before Android 13.
 */
#ifndef NEVERC_KRT_TASK_PREEMPT_COUNT
#  if NEVERC_KRT_KERNEL == 510
#    define NEVERC_KRT_TASK_PREEMPT_COUNT 24
#  else
#    define NEVERC_KRT_TASK_PREEMPT_COUNT 16
#  endif
#endif

/*
 * Configuration facts shared by the prepared official arm64 GKI outputs for
 * Android 12 through Android 17.  These values were read from each output's
 * generated autoconf.h/.config, not inferred from the kernel version:
 *
 *   CONFIG_NR_CPUS=32, CONFIG_ARM64_4K_PAGES=y,
 *   CONFIG_ARM64_VA_BITS=39, CONFIG_ARM64_PA_BITS=48,
 *   CONFIG_PGTABLE_LEVELS=3.
 *
 * An OEM profile with a different configuration must override the relevant
 * NEVERC_KRT_* value explicitly before including SDK headers.
 */
#ifndef NEVERC_KRT_NR_CPUS
#define NEVERC_KRT_NR_CPUS 32
#endif
#ifndef NEVERC_KRT_PAGE_SHIFT
#define NEVERC_KRT_PAGE_SHIFT 12
#endif
#ifndef NEVERC_KRT_VA_BITS
#define NEVERC_KRT_VA_BITS 39
#endif
#ifndef NEVERC_KRT_PA_BITS
#define NEVERC_KRT_PA_BITS 48
#endif
#ifndef NEVERC_KRT_PGTABLE_LEVELS
#define NEVERC_KRT_PGTABLE_LEVELS 3
#endif

/*
 * struct dentry d_name.name offset — stable across 5.10-6.18:
 *   d_flags(4) + d_seq(4) + d_hash(16) + d_parent(8) + d_name.hash_len(8)
 */
#ifndef NEVERC_KRT_DENTRY_DNAME_OFF
#define NEVERC_KRT_DENTRY_DNAME_OFF 0x28
#endif

/*
 * struct sock_common skc_dport / skc_num offsets — stable across 5.10-6.18:
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
#define NEVERC_KRT_MODULE_SIZE 0x680
#endif

/*
 * Runtime-settable version parameters.  The bitcode is compiled once
 * (with the default NEVERC_KRT_KERNEL=510), so compile-time #if chains
 * inside .c files would always evaluate against 5.10 — wrong when the
 * user targets 6.6 or 6.12.
 *
 * Primary detection: neverc_krt_mem_init() auto-detects the running
 * kernel version from linux_banner at runtime.  This works correctly
 * for all paths (including neverc_krt_init_all()) without depending
 * on compile-time constants.
 *
 * NEVERC_KRT_BOOTSTRAP() passes the caller's compile-time
 * NEVERC_KRT_KERNEL into the runtime before subsystem initialization.
 * Code that enters through neverc_krt_init_all() instead falls back to
 * banner detection in neverc_krt_mem_init().
 *
 * Safe default: if never set (0), callers fall back to the maximum
 * struct module size across 5.10-6.18 (0x680 = 1664).
 */

#endif /* NVKMOD_VERSION_H */
