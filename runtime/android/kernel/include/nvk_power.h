/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_POWER_H
#define NEVERC_KRT_POWER_H

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
#define NEVERC_KRT_PM_HIBERNATION_PREPARE  0x0001
#define NEVERC_KRT_PM_POST_HIBERNATION     0x0002
#define NEVERC_KRT_PM_SUSPEND_PREPARE      0x0003
#define NEVERC_KRT_PM_POST_SUSPEND         0x0004
#define NEVERC_KRT_PM_RESTORE_PREPARE      0x0005
#define NEVERC_KRT_PM_POST_RESTORE         0x0006

/* Reboot event codes (matching kernel's include/linux/reboot.h) */
#define NEVERC_KRT_SYS_RESTART   0x0001
#define NEVERC_KRT_SYS_HALT      0x0002
#define NEVERC_KRT_SYS_POWER_OFF 0x0003

/* Notifier return values */
#define NEVERC_KRT_NOTIFY_DONE  0x0000
#define NEVERC_KRT_NOTIFY_OK    0x0001
#define NEVERC_KRT_NOTIFY_STOP  0x8000

/*
 * Opaque notifier_block layout — stable across 5.10–6.12:
 *   struct notifier_block {
 *       int (*notifier_call)(struct notifier_block *, unsigned long, void *);
 *       struct notifier_block *next;
 *       int priority;
 *   };
 */
struct neverc_krt_notifier_block {
	unsigned long notifier_call;
	unsigned long next;
	int           priority;
	int           _pad;
};

int neverc_krt_power_init(void);


/* ------------------------------------------------------------------ */
/*  PM (suspend / resume) notifier                                    */
/* ------------------------------------------------------------------ */

typedef void (*neverc_krt_pm_callback_t)(unsigned long event);

struct neverc_krt_pm_notifier {
	struct neverc_krt_notifier_block nb;
	neverc_krt_pm_callback_t         callback;
	int                       registered;
};



int neverc_krt_pm_register(struct neverc_krt_pm_notifier *pm,
			   neverc_krt_pm_callback_t cb, int priority);


void neverc_krt_pm_unregister(struct neverc_krt_pm_notifier *pm);



/* ------------------------------------------------------------------ */
/*  Reboot / shutdown notifier                                        */
/* ------------------------------------------------------------------ */

typedef void (*neverc_krt_reboot_callback_t)(unsigned long event);

struct neverc_krt_reboot_notifier {
	struct neverc_krt_notifier_block nb;
	neverc_krt_reboot_callback_t     callback;
	int                       registered;
};


int neverc_krt_reboot_register(struct neverc_krt_reboot_notifier *rn,
			       neverc_krt_reboot_callback_t cb, int priority);


void neverc_krt_reboot_unregister(struct neverc_krt_reboot_notifier *rn);


/* Convenience: is the system going down? (check from PM or reboot cb) */
static __always_inline int neverc_krt_is_shutdown_event(unsigned long event)
{
	return event == NEVERC_KRT_SYS_HALT || event == NEVERC_KRT_SYS_POWER_OFF;
}

static __always_inline int neverc_krt_is_suspend_event(unsigned long event)
{
	return event == NEVERC_KRT_PM_SUSPEND_PREPARE ||
	       event == NEVERC_KRT_PM_HIBERNATION_PREPARE;
}

static __always_inline int neverc_krt_is_resume_event(unsigned long event)
{
	return event == NEVERC_KRT_PM_POST_SUSPEND ||
	       event == NEVERC_KRT_PM_POST_HIBERNATION ||
	       event == NEVERC_KRT_PM_POST_RESTORE;
}

#endif /* NEVERC_KRT_POWER_H */
