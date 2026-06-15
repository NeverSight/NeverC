/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_PM_H
#define _NVK_LINUX_PM_H

#include <linux/types.h>

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
	int (*runtime_suspend)(struct device *dev);
	int (*runtime_resume)(struct device *dev);
	int (*runtime_idle)(struct device *dev);
};

struct device;

#define SET_SYSTEM_SLEEP_PM_OPS(suspend, resume) \
	.suspend = suspend, .resume = resume

#endif /* _NVK_LINUX_PM_H */
