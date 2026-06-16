/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Minimal NeverC Android kernel module (GKI .ko).
 *
 * Imports zero kernel symbols, so it loads on any GKI kernel whose vermagic
 * matches: it is the cleanest end-to-end validation that compile -> relocatable
 * link -> .ko -> insmod works (a successful insmod means init() was found at the
 * right struct module offset and returned 0).
 *
 * Build:  neverc make            (or: make)
 * Deploy: adb push neverc_krt_hello.ko /data/local/tests/
 *         adb shell su -c 'insmod /data/local/tests/neverc_krt_hello.ko'
 *         adb shell su -c 'lsmod | grep neverc_krt_hello'
 *         adb shell su -c 'rmmod neverc_krt_hello'
 */
#include <nvkmod.h>

static int neverc_krt_hello_init(void)
{
	int ret = NEVERC_KRT_BOOTSTRAP();
	if (ret) return ret;
	pr_info("neverc_krt_hello: loaded (kernel %s)\n", NEVERC_KRT_KERNEL_STR);
	return 0;
}

static void neverc_krt_hello_exit(void)
{
	pr_info("neverc_krt_hello: unloaded\n");
}

module_init(neverc_krt_hello_init);
module_exit(neverc_krt_hello_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("Minimal NeverC Android kernel module");

NEVERC_KRT_DEFINE_MODULE("neverc_krt_hello");
