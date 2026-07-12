/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_PM_H
#define _NEVERC_KRT_LINUX_PM_H

#include <linux/types.h>

struct device;

/*
 * This layout is stable in the official arm64 GKI configurations from
 * Android 12 / 5.10 through Android 17 / 6.18.  Keep every callback: omitting
 * the late and noirq slots shifts runtime_* and makes the kernel call the
 * wrong function.
 */
struct dev_pm_ops {
	int (*prepare)(struct device *dev);
	void (*complete)(struct device *dev);
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);
	int (*freeze)(struct device *dev);
	int (*thaw)(struct device *dev);
	int (*poweroff)(struct device *dev);
	int (*restore)(struct device *dev);
	int (*suspend_late)(struct device *dev);
	int (*resume_early)(struct device *dev);
	int (*freeze_late)(struct device *dev);
	int (*thaw_early)(struct device *dev);
	int (*poweroff_late)(struct device *dev);
	int (*restore_early)(struct device *dev);
	int (*suspend_noirq)(struct device *dev);
	int (*resume_noirq)(struct device *dev);
	int (*freeze_noirq)(struct device *dev);
	int (*thaw_noirq)(struct device *dev);
	int (*poweroff_noirq)(struct device *dev);
	int (*restore_noirq)(struct device *dev);
	int (*runtime_suspend)(struct device *dev);
	int (*runtime_resume)(struct device *dev);
	int (*runtime_idle)(struct device *dev);
	u64 __kabi_reserved1;
};

_Static_assert(sizeof(struct dev_pm_ops) == 192,
	       "unexpected GKI dev_pm_ops layout");

#define SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)                            \
	.suspend = (suspend_fn),                                               \
	.resume = (resume_fn),                                                 \
	.freeze = (suspend_fn),                                                \
	.thaw = (resume_fn),                                                   \
	.poweroff = (suspend_fn),                                              \
	.restore = (resume_fn),

#define LATE_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)                       \
	.suspend_late = (suspend_fn),                                         \
	.resume_early = (resume_fn),                                          \
	.freeze_late = (suspend_fn),                                          \
	.thaw_early = (resume_fn),                                            \
	.poweroff_late = (suspend_fn),                                        \
	.restore_early = (resume_fn),

#define NOIRQ_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)                      \
	.suspend_noirq = (suspend_fn),                                       \
	.resume_noirq = (resume_fn),                                         \
	.freeze_noirq = (suspend_fn),                                        \
	.thaw_noirq = (resume_fn),                                           \
	.poweroff_noirq = (suspend_fn),                                      \
	.restore_noirq = (resume_fn),

#define RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn)                        \
	.runtime_suspend = (suspend_fn),                                     \
	.runtime_resume = (resume_fn),                                       \
	.runtime_idle = (idle_fn),

#define SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)                        \
	SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)
#define SET_LATE_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)                   \
	LATE_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)
#define SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)                  \
	NOIRQ_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)
#define SET_RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn)                    \
	RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn)

#endif /* _NEVERC_KRT_LINUX_PM_H */
