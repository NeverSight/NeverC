/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NeverC Android kernel driver template (GKI .ko) with dynamic symbol
 * resolution.
 *
 * The driver imports only register_kprobe / unregister_kprobe (exported and
 * ABI-stable across all current GKI kernels) and resolves everything else at
 * runtime via kallsyms_lookup_name().  This keeps a single source / single
 * binary working across android12-5.10 ... android15-6.6.
 *
 * Build:  neverc make            (or: make ; make KERNEL=601 for 6.1, etc.)
 * Deploy: adb push neverc_krt_driver.ko /data/local/tests/
 *         adb shell su -c 'insmod /data/local/tests/neverc_krt_driver.ko'
 *         adb shell su -c 'dmesg | tail'
 *         adb shell su -c 'rmmod neverc_krt_driver'
 */
/*
 * Full enc/dec override example: rotate + ADD cipher.
 *
 * Overriding _neverc_krt_ptr_enc/_neverc_krt_ptr_dec directly replaces the entire
 * pointer encryption scheme.  The default _neverc_krt_xor_opaque is NOT used
 * (enc/dec are the consumers of xor_opaque; overriding them skips it).
 *
 * Forward-declare _neverc_krt_cache_key so the custom enc/dec functions
 * below can reference it before kallsyms.h is included.  Must match
 * the extern declaration in kallsyms.h (not static).
 */
extern unsigned long _neverc_krt_cache_key;

static inline __attribute__((always_inline))
unsigned long neverc_krt_rot_enc(unsigned long addr)
{
	unsigned long k = __atomic_load_n(&_neverc_krt_cache_key, __ATOMIC_RELAXED);
	unsigned long r = addr + k;
	r = (r << 7) | (r >> 57);
	return r;
}

static inline __attribute__((always_inline))
unsigned long neverc_krt_rot_dec(unsigned long enc)
{
	unsigned long k = __atomic_load_n(&_neverc_krt_cache_key, __ATOMIC_RELAXED);
	unsigned long r = (enc >> 7) | (enc << 57);
	r = r - k;
	return r;
}

#define _neverc_krt_ptr_enc neverc_krt_rot_enc
#define _neverc_krt_ptr_dec neverc_krt_rot_dec

#include <nvkmod.h>

static int neverc_krt_driver_init(void)
{
	void *init_task_addr;
	int ret;

	ret = NEVERC_KRT_BOOTSTRAP();
	if (ret)
		return ret;

	pr_info("neverc_krt_driver: loaded on %s\n", NEVERC_KRT_KERNEL_STR);
	pr_info("neverc_krt_driver: kallsyms_lookup_name @ %lx\n",
		(unsigned long)neverc_krt_kallsyms_lookup_name);

	init_task_addr = NEVERC_KRT_LOOKUP("init_task");
	pr_info("neverc_krt_driver: &init_task = %lx (fresh)\n",
		(unsigned long)init_task_addr);

	init_task_addr = NEVERC_KRT_LOOKUP("init_task");
	pr_info("neverc_krt_driver: &init_task = %lx (cached)\n",
		(unsigned long)init_task_addr);

	return 0;
}

static void neverc_krt_driver_exit(void)
{
	neverc_krt_sym_cache_clear();
	pr_info("neverc_krt_driver: unloaded\n");
}

module_init(neverc_krt_driver_init);
module_exit(neverc_krt_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC Android kernel driver template (dynamic kallsyms)");

NEVERC_KRT_DEFINE_MODULE("neverc_krt_driver");
