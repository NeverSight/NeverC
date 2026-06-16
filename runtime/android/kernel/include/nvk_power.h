/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_POWER_H
#define NVK_POWER_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/kallsyms.h>
#include <nvk_mem.h>

/*
 * Power management event notification.
 * Allows modules to clean up / persist state before suspend and
 * re-initialize after resume, and to perform last-chance cleanup
 * before reboot / shutdown.
 *
 * GKI kernels 5.10–6.12 all export register_pm_notifier and
 * register_reboot_notifier with stable ABIs.
 */

/* PM event codes (matching kernel's include/linux/suspend.h) */
#define NVK_PM_HIBERNATION_PREPARE  0x0001
#define NVK_PM_POST_HIBERNATION     0x0002
#define NVK_PM_SUSPEND_PREPARE      0x0003
#define NVK_PM_POST_SUSPEND         0x0004
#define NVK_PM_RESTORE_PREPARE      0x0005
#define NVK_PM_POST_RESTORE         0x0006

/* Reboot event codes (matching kernel's include/linux/reboot.h) */
#define NVK_SYS_RESTART   0x0001
#define NVK_SYS_HALT      0x0002
#define NVK_SYS_POWER_OFF 0x0003

/* Notifier return values */
#define NVK_NOTIFY_DONE  0x0000
#define NVK_NOTIFY_OK    0x0001
#define NVK_NOTIFY_STOP  0x8000

/*
 * Opaque notifier_block layout — stable across 5.10–6.12:
 *   struct notifier_block {
 *       int (*notifier_call)(struct notifier_block *, unsigned long, void *);
 *       struct notifier_block *next;
 *       int priority;
 *   };
 */
struct nvk_notifier_block {
	unsigned long notifier_call;
	unsigned long next;
	int           priority;
	int           _pad;
};

typedef int (*nvk_reg_pm_fn)(void *nb);
typedef int (*nvk_unreg_pm_fn)(void *nb);
typedef int (*nvk_reg_reboot_fn)(void *nb);
typedef int (*nvk_unreg_reboot_fn)(void *nb);

static nvk_reg_pm_fn      _nvk_register_pm;
static nvk_unreg_pm_fn    _nvk_unregister_pm;
static nvk_reg_reboot_fn  _nvk_register_reboot;
static nvk_unreg_reboot_fn _nvk_unregister_reboot;
static int                _nvk_power_inited;

static int nvk_power_init(void)
{
	if (_nvk_power_inited) return 0;

	_nvk_register_pm =
		(nvk_reg_pm_fn)NVK_LOOKUP("register_pm_notifier");
	_nvk_unregister_pm =
		(nvk_unreg_pm_fn)NVK_LOOKUP("unregister_pm_notifier");
	_nvk_register_reboot =
		(nvk_reg_reboot_fn)NVK_LOOKUP("register_reboot_notifier");
	_nvk_unregister_reboot =
		(nvk_unreg_reboot_fn)NVK_LOOKUP("unregister_reboot_notifier");

	_nvk_power_inited = 1;
	return 0;
}

/* ------------------------------------------------------------------ */
/*  PM (suspend / resume) notifier                                    */
/* ------------------------------------------------------------------ */

typedef void (*nvk_pm_callback_t)(unsigned long event);

struct nvk_pm_notifier {
	struct nvk_notifier_block nb;
	nvk_pm_callback_t         callback;
	int                       registered;
};

static int _nvk_pm_trampoline(void *nb_ptr, unsigned long event, void *unused)
{
	struct nvk_pm_notifier *pm = (struct nvk_pm_notifier *)(
		(char *)nb_ptr -
		__builtin_offsetof(struct nvk_pm_notifier, nb));
	if (pm->callback)
		pm->callback(event);
	return NVK_NOTIFY_DONE;
}

static int nvk_pm_register(struct nvk_pm_notifier *pm,
			   nvk_pm_callback_t cb, int priority)
{
	if (!pm || !cb) return -1;
	if (!_nvk_register_pm) return -2;

	__builtin_memset(pm, 0, sizeof(*pm));
	pm->callback = cb;
	pm->nb.notifier_call = (unsigned long)_nvk_pm_trampoline;
	pm->nb.priority = priority;

	int ret = _nvk_register_pm(&pm->nb);
	if (ret == 0)
		pm->registered = 1;
	return ret;
}

static void nvk_pm_unregister(struct nvk_pm_notifier *pm)
{
	if (!pm || !pm->registered) return;
	if (_nvk_unregister_pm)
		_nvk_unregister_pm(&pm->nb);
	pm->registered = 0;
}


/* ------------------------------------------------------------------ */
/*  Reboot / shutdown notifier                                        */
/* ------------------------------------------------------------------ */

typedef void (*nvk_reboot_callback_t)(unsigned long event);

struct nvk_reboot_notifier {
	struct nvk_notifier_block nb;
	nvk_reboot_callback_t     callback;
	int                       registered;
};

static int _nvk_reboot_trampoline(void *nb_ptr, unsigned long event,
				  void *unused)
{
	struct nvk_reboot_notifier *rn = (struct nvk_reboot_notifier *)(
		(char *)nb_ptr -
		__builtin_offsetof(struct nvk_reboot_notifier, nb));
	if (rn->callback)
		rn->callback(event);
	return NVK_NOTIFY_DONE;
}

static int nvk_reboot_register(struct nvk_reboot_notifier *rn,
			       nvk_reboot_callback_t cb, int priority)
{
	if (!rn || !cb) return -1;
	if (!_nvk_register_reboot) return -2;

	__builtin_memset(rn, 0, sizeof(*rn));
	rn->callback = cb;
	rn->nb.notifier_call = (unsigned long)_nvk_reboot_trampoline;
	rn->nb.priority = priority;

	int ret = _nvk_register_reboot(&rn->nb);
	if (ret == 0)
		rn->registered = 1;
	return ret;
}

static void nvk_reboot_unregister(struct nvk_reboot_notifier *rn)
{
	if (!rn || !rn->registered) return;
	if (_nvk_unregister_reboot)
		_nvk_unregister_reboot(&rn->nb);
	rn->registered = 0;
}

/* Convenience: is the system going down? (check from PM or reboot cb) */
static __always_inline int nvk_is_shutdown_event(unsigned long event)
{
	return event == NVK_SYS_HALT || event == NVK_SYS_POWER_OFF;
}

static __always_inline int nvk_is_suspend_event(unsigned long event)
{
	return event == NVK_PM_SUSPEND_PREPARE ||
	       event == NVK_PM_HIBERNATION_PREPARE;
}

static __always_inline int nvk_is_resume_event(unsigned long event)
{
	return event == NVK_PM_POST_SUSPEND ||
	       event == NVK_PM_POST_HIBERNATION ||
	       event == NVK_PM_POST_RESTORE;
}

#endif /* NVK_POWER_H */
