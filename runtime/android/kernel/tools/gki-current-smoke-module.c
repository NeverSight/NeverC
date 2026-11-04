/* SPDX-License-Identifier: GPL-2.0 */
/* No explicit bootstrap: the first current read is in another TU's hardirq. */
#include <nvkmod.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>

extern int neverc_gki_current_result;
enum hrtimer_restart neverc_gki_current_callback(struct hrtimer *timer);

/* Report the result without initializing any NVK service: this also proves
 * stable-register families work when kprobe exports are unavailable. */
#if NEVERC_KRT_LINUX_AT_LEAST(5, 15, 0)
extern int current_test_printk(const char *fmt, ...) __asm__("_printk");
#else
extern int current_test_printk(const char *fmt, ...) __asm__("printk");
#endif

static enum hrtimer_restart current_callback(struct hrtimer *timer)
{
	return neverc_gki_current_callback(timer);
}

static int current_init(void)
{
	struct hrtimer timer;
	int attempts, result;

#if NEVERC_KRT_LINUX_AT_LEAST(6, 18, 0)
	hrtimer_setup(&timer, current_callback, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL_HARD);
#else
	hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_HARD);
	timer.function = current_callback;
#endif
	hrtimer_start(&timer, ms_to_ktime(1), HRTIMER_MODE_REL_HARD);
	for (attempts = 0; attempts < 100 &&
	     !__atomic_load_n(&neverc_gki_current_result, __ATOMIC_ACQUIRE); attempts++)
		msleep(1);
	hrtimer_cancel(&timer);
	result = __atomic_load_n(&neverc_gki_current_result, __ATOMIC_ACQUIRE);
	if (result != 1)
		return -1;
	current_test_printk("NEVERC_GKI_CURRENT_IRQ_PASS\n");
	return 0;
}

static void current_exit(void) {}
module_init(current_init);
module_exit(current_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("NeverC first current access from a hardirq callback");
NEVERC_KRT_DEFINE_MODULE("neverc_gki_smoke");
