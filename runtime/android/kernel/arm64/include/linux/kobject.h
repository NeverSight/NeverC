/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_KOBJECT_H
#define _NVK_LINUX_KOBJECT_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/kref.h>

struct kobject;  /* opaque (layout version-dependent) */
struct kobj_type;
struct kset;

int kobject_init_and_add(struct kobject *kobj, void *ktype,
			 struct kobject *parent, const char *fmt, ...);
void kobject_put(struct kobject *kobj);
struct kobject *kobject_create_and_add(const char *name,
				      struct kobject *parent);
void kobject_del(struct kobject *kobj);

extern struct kobject *kernel_kobj;

#endif /* _NVK_LINUX_KOBJECT_H */
