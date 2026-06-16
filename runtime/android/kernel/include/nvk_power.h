/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_POWER_H
#define NVK_POWER_H

#include <linux/types.h>
#include <nvk_rt.h>
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

NVK_RT_VAR nvk_reg_pm_fn      _nvk_register_pm;
NVK_RT_VAR nvk_unreg_pm_fn    _nvk_unregister_pm;
NVK_RT_VAR nvk_reg_reboot_fn  _nvk_register_reboot;
NVK_RT_VAR nvk_unreg_reboot_fn _nvk_unregister_reboot;
NVK_RT_VAR int                _nvk_power_inited;

int nvk_power_init(void);


/* ------------------------------------------------------------------ */
/*  PM (suspend / resume) notifier                                    */
/* ------------------------------------------------------------------ */

typedef void (*nvk_pm_callback_t)(unsigned long event);

struct nvk_pm_notifier {
	struct nvk_notifier_block nb;
	nvk_pm_callback_t         callback;
	int                       registered;
};

int _nvk_pm_trampoline(void *nb_ptr, unsigned long event, void *unused);


int nvk_pm_register(struct nvk_pm_notifier *pm,
			   nvk_pm_callback_t cb, int priority);


void nvk_pm_unregister(struct nvk_pm_notifier *pm);



/* ------------------------------------------------------------------ */
/*  Reboot / shutdown notifier                                        */
/* ------------------------------------------------------------------ */

typedef void (*nvk_reboot_callback_t)(unsigned long event);

struct nvk_reboot_notifier {
	struct nvk_notifier_block nb;
	nvk_reboot_callback_t     callback;
	int                       registered;
};

int _nvk_reboot_trampoline(void *nb_ptr, unsigned long event,
				  void *unused);


int nvk_reboot_register(struct nvk_reboot_notifier *rn,
			       nvk_reboot_callback_t cb, int priority);


void nvk_reboot_unregister(struct nvk_reboot_notifier *rn);


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
