/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVKMOD_VERSION_H
#define NVKMOD_VERSION_H

#ifndef NVK_KERNEL
#define NVK_KERNEL 510
#endif

/* offsetof(struct module, name): enum state(4) + pad(4) + struct list_head(16) */
#ifndef NVK_OFF_NAME
#define NVK_OFF_NAME 24
#endif

#if NVK_KERNEL == 510
/* Android 12, Linux 5.10 (GKI android12-5.10) */
#  ifndef NVK_VERMAGIC
#    define NVK_VERMAGIC                                                        \
       "5.10.0-android12-9 SMP preempt mod_unload aarch64"
#  endif
#  ifndef NVK_KERNEL_STR
#    define NVK_KERNEL_STR "android12-5.10"
#  endif
/* Computed from the prepared android12-5.10 GKI tree via
 * tools/gen_struct_module_offsets.c (CONFIG_CFI_CLANG=y, MODULE_UNLOAD=y, ...). */
#  ifndef NVK_OFF_INIT
#    define NVK_OFF_INIT 400 /* 0x190 */
#  endif
#  ifndef NVK_OFF_EXIT
#    define NVK_OFF_EXIT 960 /* 0x3C0 */
#  endif
#  ifndef NVK_MODULE_SIZE
#    define NVK_MODULE_SIZE 1024 /* 0x400, sizeof(struct module) */
#  endif

#elif NVK_KERNEL == 515
/* Android 13, Linux 5.15 (GKI android13-5.15) */
#  ifndef NVK_VERMAGIC
#    define NVK_VERMAGIC                                                        \
       "5.15.0-android13-8 SMP preempt mod_unload aarch64"
#  endif
#  ifndef NVK_KERNEL_STR
#    define NVK_KERNEL_STR "android13-5.15"
#  endif
/* Verified from GKI android13-5.15 (release 5.15.206) via gki_defconfig +
 * modules_prepare + tools/gen_struct_module_offsets.c (clang-18). */
#  ifndef NVK_OFF_INIT
#    define NVK_OFF_INIT 376 /* 0x178 */
#  endif
#  ifndef NVK_OFF_EXIT
#    define NVK_OFF_EXIT 872 /* 0x368 */
#  endif
#  ifndef NVK_MODULE_SIZE
#    define NVK_MODULE_SIZE 960 /* 0x3C0, sizeof(struct module) */
#  endif

#elif NVK_KERNEL == 601
/* Android 14, Linux 6.1 (GKI android14-6.1) */
#  ifndef NVK_VERMAGIC
#    define NVK_VERMAGIC                                                        \
       "6.1.0-android14-11 SMP preempt mod_unload aarch64"
#  endif
#  ifndef NVK_KERNEL_STR
#    define NVK_KERNEL_STR "android14-6.1"
#  endif
/* Verified from GKI android14-6.1 (release 6.1.172) via gki_defconfig +
 * modules_prepare + tools/gen_struct_module_offsets.c (clang-18). */
#  ifndef NVK_OFF_INIT
#    define NVK_OFF_INIT 368 /* 0x170 */
#  endif
#  ifndef NVK_OFF_EXIT
#    define NVK_OFF_EXIT 968 /* 0x3C8 */
#  endif
#  ifndef NVK_MODULE_SIZE
#    define NVK_MODULE_SIZE 1024 /* 0x400, sizeof(struct module) */
#  endif

#elif NVK_KERNEL == 606
/* Android 15, Linux 6.6 (GKI android15-6.6) */
#  ifndef NVK_VERMAGIC
#    define NVK_VERMAGIC                                                        \
       "6.6.0-android15-8 SMP preempt mod_unload aarch64"
#  endif
#  ifndef NVK_KERNEL_STR
#    define NVK_KERNEL_STR "android15-6.6"
#  endif
/* Verified from GKI android15-6.6 (release 6.6.138) via gki_defconfig +
 * modules_prepare + tools/gen_struct_module_offsets.c (clang-18). */
#  ifndef NVK_OFF_INIT
#    define NVK_OFF_INIT 392 /* 0x188 */
#  endif
#  ifndef NVK_OFF_EXIT
#    define NVK_OFF_EXIT 1448 /* 0x5A8 */
#  endif
#  ifndef NVK_MODULE_SIZE
#    define NVK_MODULE_SIZE 1536 /* 0x600, sizeof(struct module) */
#  endif

#elif NVK_KERNEL == 612
/* Android 16, Linux 6.12 (GKI android16-6.12) */
#  ifndef NVK_VERMAGIC
#    define NVK_VERMAGIC                                                        \
       "6.12.0-android16-0 SMP preempt mod_unload aarch64"
#  endif
#  ifndef NVK_KERNEL_STR
#    define NVK_KERNEL_STR "android16-6.12"
#  endif
/* Verified from GKI android16-6.12 (release 6.12.81) via gki_defconfig +
 * modules_prepare + tools/gen_struct_module_offsets.c (clang-18). */
#  ifndef NVK_OFF_INIT
#    define NVK_OFF_INIT 392 /* 0x188 */
#  endif
#  ifndef NVK_OFF_EXIT
#    define NVK_OFF_EXIT 1504 /* 0x5E0 */
#  endif
#  ifndef NVK_MODULE_SIZE
#    define NVK_MODULE_SIZE 1600 /* 0x640, sizeof(struct module) */
#  endif

#else
#  error                                                                        \
      "Unknown NVK_KERNEL; use 510 / 515 / 601 / 606 / 612 or define presets manually"
#endif

/*
 * Total bytes reserved for the this_module blob.  Only needs to cover up to
 * max(NVK_OFF_INIT, NVK_OFF_EXIT)+8; the kernel zero-fills the remainder of its
 * own (possibly larger) struct module.  Generously rounded up.
 */
#ifndef NVK_MODULE_SIZE
#define NVK_MODULE_SIZE 0x600
#endif

#endif /* NVKMOD_VERSION_H */
