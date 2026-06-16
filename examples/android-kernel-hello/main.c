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
 * Deploy: adb push nvk_hello.ko /data/local/tests/
 *         adb shell su -c 'insmod /data/local/tests/nvk_hello.ko'
 *         adb shell su -c 'lsmod | grep nvk_hello'
 *         adb shell su -c 'rmmod nvk_hello'
 */
#include <nvkmod.h>

static int nvk_hello_init(void)
{
	int ret = NVK_BOOTSTRAP();
	if (ret) return ret;
	pr_info("nvk_hello: loaded (kernel %s)\n", NVK_KERNEL_STR);
	return 0;
}

static void nvk_hello_exit(void)
{
	pr_info("nvk_hello: unloaded\n");
}

module_init(nvk_hello_init);
module_exit(nvk_hello_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("NeverC");
MODULE_DESCRIPTION("Minimal NeverC Android kernel module");

NVK_DEFINE_MODULE("nvk_hello");
