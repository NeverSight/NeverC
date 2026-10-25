/* SPDX-License-Identifier: GPL-2.0 */
/* Exercise caller-owned SDK objects on a disposable GKI QEMU guest.
 * Build like gki-qemu-smoke-module.c and use run-gki-qemu-smoke.sh.
 * The zero-import loader fixture remains the independent loader-only check.
 */
#include <nvkmod.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/semaphore.h>
#include <linux/string.h>
#ifdef NEVERC_GKI_RUNTIME_SMOKE
#include <nvk_compat.h>
#include <nvk_binder.h>
#include <linux/errno.h>
#endif

static int timer_fired;

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
	(void)timer;
	__atomic_store_n(&timer_fired, 1, __ATOMIC_RELEASE);
	return HRTIMER_NORESTART;
}

int init_module(void)
{
	struct mutex mutex;
	struct semaphore semaphore;
	struct rw_semaphore rwsem;
	struct hrtimer timer;
	struct rb_node child = { 0 }, parent = { .rb_left = &child };
	struct rb_root root = { .rb_node = &parent };
	char text[8];
	int attempts;

#ifdef NEVERC_GKI_RUNTIME_SMOKE
	if (NEVERC_KRT_BOOTSTRAP() ||
	    neverc_krt_check_kernel_match() != NEVERC_KRT_VER_EXACT)
		return -6;
#if NEVERC_KRT_PROFILE_BINDER_FILTER_BACKEND == NEVERC_KRT_PROFILE_BINDER_FILTER_UNSUPPORTED
	if (neverc_krt_binder_init() != -EOPNOTSUPP)
		return -7;
#endif
#endif
	mutex_init(&mutex);
	mutex_lock(&mutex);
	mutex_unlock(&mutex);
	if (!mutex_trylock(&mutex))
		return -1;
	mutex_unlock(&mutex);
	mutex_destroy(&mutex);
	sema_init(&semaphore, 1);
	if (down_trylock(&semaphore))
		return -2;
	up(&semaphore);
	init_rwsem(&rwsem);
	down_write(&rwsem);
	up_write(&rwsem);
	down_read(&rwsem);
	up_read(&rwsem);
	if (rb_first(&root) != &child || rb_last(&root) != &parent)
		return -3;
	memset(text, 0x7f, sizeof(text));
	strncpy(text, "gki", sizeof(text));
	if (memcmp(text, "gki\0\0\0\0\0", sizeof(text)))
		return -4;
#if NEVERC_KRT_LINUX_AT_LEAST(6, 18, 0)
	hrtimer_setup(&timer, timer_callback, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
#else
	hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer.function = timer_callback;
#endif
	hrtimer_start(&timer, ms_to_ktime(1), HRTIMER_MODE_REL);
	for (attempts = 0; attempts < 100 &&
	     !__atomic_load_n(&timer_fired, __ATOMIC_ACQUIRE); attempts++)
		msleep(1);
	hrtimer_cancel(&timer);
	return __atomic_load_n(&timer_fired, __ATOMIC_ACQUIRE) ? 0 : -5;
}

void cleanup_module(void) {}

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("NeverC GKI SDK object and callback smoke module");
NEVERC_KRT_DEFINE_MODULE("neverc_gki_smoke");
