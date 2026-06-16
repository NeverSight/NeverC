/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_PLATFORM_DEVICE_H
#define _NEVERC_KRT_LINUX_PLATFORM_DEVICE_H

#include <linux/types.h>
#include <linux/device.h>

struct platform_device; /* opaque */
struct module;

struct platform_driver {
	int (*probe)(struct platform_device *);
	int (*remove)(struct platform_device *);
	void (*shutdown)(struct platform_device *);
	int (*suspend)(struct platform_device *, u32 state);
	int (*resume)(struct platform_device *);
	struct device_driver driver;
	void *id_table;
};

int platform_driver_register(struct platform_driver *drv);
void platform_driver_unregister(struct platform_driver *drv);

struct resource *platform_get_resource(struct platform_device *dev,
				       unsigned int type, unsigned int num);
int platform_get_irq(struct platform_device *dev, unsigned int num);

/* Resource types. */
#define IORESOURCE_MEM  0x00000200
#define IORESOURCE_IRQ  0x00000400

struct resource {
	unsigned long start;
	unsigned long end;
	const char *name;
	unsigned long flags;
	struct resource *parent, *sibling, *child;
};

#endif /* _NEVERC_KRT_LINUX_PLATFORM_DEVICE_H */
