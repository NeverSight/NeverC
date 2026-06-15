/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_SYSFS_H
#define _NVK_LINUX_SYSFS_H

#include <linux/types.h>

struct kobject;

struct attribute {
	const char *name;
	umode_t mode;
};

struct attribute_group {
	const char *name;
	umode_t (*is_visible)(struct kobject *, struct attribute *, int);
	struct attribute **attrs;
};

int sysfs_create_file(struct kobject *kobj, const struct attribute *attr);
void sysfs_remove_file(struct kobject *kobj, const struct attribute *attr);
int sysfs_create_group(struct kobject *kobj, const struct attribute_group *grp);
void sysfs_remove_group(struct kobject *kobj, const struct attribute_group *grp);

#define __ATTR(_name, _mode, _show, _store) {                                 \
	.attr = { .name = #_name, .mode = _mode },                            \
	.show = _show, .store = _store,                                       \
}
#define __ATTR_RO(_name) __ATTR(_name, 0444, _name##_show, (void *)0)
#define __ATTR_RW(_name) __ATTR(_name, 0644, _name##_show, _name##_store)

#endif /* _NVK_LINUX_SYSFS_H */
