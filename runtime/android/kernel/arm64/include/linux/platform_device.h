/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_PLATFORM_DEVICE_H
#define _NEVERC_KRT_LINUX_PLATFORM_DEVICE_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/module.h>

struct platform_device; /* opaque */

/*
 * platform_driver layout varies across GKI 5.10–6.18:
 *   - remove() return type: int (5.10–6.6) → void (6.12+)
 *   - struct device_driver grows with KABI and new fields
 *   - 6.12+ wraps remove/remove_new in a union
 *
 * Embedded struct device_driver requires a full definition that varies
 * across kernel versions.  Declare opaque; users needing platform
 * drivers should construct the layout for their selected GKI profile
 * and build the struct at the correct offsets for their target kernel.
 */
struct platform_driver;

int __platform_driver_register(struct platform_driver *drv,
			       struct module *owner);
#define platform_driver_register(drv) \
	__platform_driver_register((drv), THIS_MODULE)
void platform_driver_unregister(struct platform_driver *drv);

struct resource *platform_get_resource(struct platform_device *dev,
				       unsigned int type, unsigned int num);
int platform_get_irq(struct platform_device *dev, unsigned int num);

#endif /* _NEVERC_KRT_LINUX_PLATFORM_DEVICE_H */
