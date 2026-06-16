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
 * Deploy: adb push nvk_driver.ko /data/local/tests/
 *         adb shell su -c 'insmod /data/local/tests/nvk_driver.ko'
 *         adb shell su -c 'dmesg | tail'
 *         adb shell su -c 'rmmod nvk_driver'
 */
/*
 * Full enc/dec override example: rotate + ADD cipher.
 *
 * Overriding _nvk_ptr_enc/_nvk_ptr_dec directly replaces the entire
 * pointer encryption scheme.  The default _nvk_xor_opaque is NOT used
 * (enc/dec are the consumers of xor_opaque; overriding them bypasses it).
 *
 * Forward-declare _nvk_cache_key here — C allows multiple tentative
 * definitions of the same static variable in one translation unit;
 * kallsyms.h's declaration merges with this one at link time.
 */
static unsigned long _nvk_cache_key;

static inline __attribute__((always_inline))
unsigned long nvk_rot_enc(unsigned long addr)
{
	unsigned long k = __atomic_load_n(&_nvk_cache_key, __ATOMIC_RELAXED);
	unsigned long r = addr + k;
	r = (r << 7) | (r >> 57);
	return r;
}

static inline __attribute__((always_inline))
unsigned long nvk_rot_dec(unsigned long enc)
{
	unsigned long k = __atomic_load_n(&_nvk_cache_key, __ATOMIC_RELAXED);
	unsigned long r = (enc >> 7) | (enc << 57);
	r = r - k;
	return r;
}

#define _nvk_ptr_enc nvk_rot_enc
#define _nvk_ptr_dec nvk_rot_dec

#include <nvkmod.h>

static int nvk_driver_init(void)
{
	void *init_task_addr;
	int ret;

	ret = NVK_BOOTSTRAP();
	if (ret)
		return ret;

	pr_info("nvk_driver: loaded on %s\n", NVK_KERNEL_STR);
	pr_info("nvk_driver: kallsyms_lookup_name @ %lx\n",
		(unsigned long)nvk_kallsyms_lookup_name);

	init_task_addr = NVK_LOOKUP("init_task");
	pr_info("nvk_driver: &init_task = %lx (fresh)\n",
		(unsigned long)init_task_addr);

	init_task_addr = NVK_LOOKUP("init_task");
	pr_info("nvk_driver: &init_task = %lx (cached)\n",
		(unsigned long)init_task_addr);

	return 0;
}

static void nvk_driver_exit(void)
{
	nvk_sym_cache_clear();
	pr_info("nvk_driver: unloaded\n");
}

module_init(nvk_driver_init);
module_exit(nvk_driver_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("NeverC Android kernel driver template (dynamic kallsyms)");

NVK_DEFINE_MODULE("nvk_driver");
