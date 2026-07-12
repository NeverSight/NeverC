/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_SYSFS_H
#define _NEVERC_KRT_LINUX_SYSFS_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <nvkmod_version.h>

struct kobject;
struct bin_attribute;

struct attribute {
	const char *name;
	umode_t mode;
};

struct attribute_group {
	const char *name;
	umode_t (*is_visible)(struct kobject *, struct attribute *, int);
	umode_t (*is_bin_visible)(struct kobject *, struct bin_attribute *, int);
#if NEVERC_KRT_KERNEL >= 618
	/*
	 * Linux 6.18 inserted bin_size before attrs.  Keep the callback opaque:
	 * callers that do not publish binary attributes must leave it zero.
	 */
	void *__bin_size;
#endif
	struct attribute **attrs;
	struct bin_attribute **bin_attrs;
};

_Static_assert(sizeof(struct attribute) == 16,
	       "unexpected GKI attribute layout");
_Static_assert(__builtin_offsetof(struct attribute, mode) == 8,
	       "unexpected GKI attribute.mode offset");
#if NEVERC_KRT_KERNEL >= 618
_Static_assert(sizeof(struct attribute_group) == 48,
	       "unexpected GKI 6.18 attribute_group layout");
_Static_assert(__builtin_offsetof(struct attribute_group, attrs) == 32,
	       "unexpected GKI 6.18 attribute_group.attrs offset");
#else
_Static_assert(sizeof(struct attribute_group) == 40,
	       "unexpected GKI 5.10-6.12 attribute_group layout");
_Static_assert(__builtin_offsetof(struct attribute_group, attrs) == 24,
	       "unexpected GKI 5.10-6.12 attribute_group.attrs offset");
#endif

int sysfs_create_file_ns(struct kobject *kobj, const struct attribute *attr,
			 const void *ns);
void sysfs_remove_file_ns(struct kobject *kobj, const struct attribute *attr,
			  const void *ns);
int sysfs_create_group(struct kobject *kobj, const struct attribute_group *grp);
void sysfs_remove_group(struct kobject *kobj, const struct attribute_group *grp);

static __always_inline int
sysfs_create_file(struct kobject *kobj, const struct attribute *attr)
{
	return sysfs_create_file_ns(kobj, attr, (const void *)0);
}

static __always_inline void
sysfs_remove_file(struct kobject *kobj, const struct attribute *attr)
{
	sysfs_remove_file_ns(kobj, attr, (const void *)0);
}

#define __ATTR(_name, _mode, _show, _store) {                                 \
	.attr = { .name = #_name, .mode = _mode },                            \
	.show = _show, .store = _store,                                       \
}
#define __ATTR_RO(_name) __ATTR(_name, 0444, _name##_show, (void *)0)
#define __ATTR_RW(_name) __ATTR(_name, 0644, _name##_show, _name##_store)

#endif /* _NEVERC_KRT_LINUX_SYSFS_H */
