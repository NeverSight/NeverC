/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_OF_H
#define _NEVERC_KRT_LINUX_OF_H

#include <linux/types.h>
#include <linux/compiler.h>

struct device_node; /* opaque */
struct property;    /* opaque */

struct device_node *of_find_compatible_node(struct device_node *from,
					    const char *type,
					    const char *compatible);
struct device_node *of_find_node_opts_by_path(const char *path,
					      const char **opts);
struct device_node *of_find_node_by_name(struct device_node *from,
					 const char *name);

struct property *of_find_property(const struct device_node *np,
				  const char *name, int *lenp);
int of_property_read_variable_u32_array(const struct device_node *np,
					const char *propname,
					u32 *out_values,
					size_t sz_min, size_t sz_max);
int of_property_read_u64(const struct device_node *np, const char *propname,
			 u64 *out_value);
int of_property_read_string(const struct device_node *np, const char *propname,
			    const char **out_string);
int of_property_count_elems_of_size(const struct device_node *np,
				    const char *propname, int elem_size);

static __always_inline struct device_node *
of_find_node_by_path(const char *path)
{
	return of_find_node_opts_by_path(path, (const char **)0);
}

/*
 * Official arm64 GKI profiles build without CONFIG_OF_DYNAMIC, making
 * of_node_put() the upstream no-op inline helper.
 */
static __always_inline void of_node_put(struct device_node *node)
{
	(void)node;
}

static __always_inline int
of_property_read_u32(const struct device_node *np, const char *propname,
		     u32 *out_value)
{
	int ret = of_property_read_variable_u32_array(np, propname, out_value,
						      1, 0);
	return ret < 0 ? ret : 0;
}

static __always_inline bool
of_property_read_bool(const struct device_node *np, const char *propname)
{
	return of_find_property(np, propname, (int *)0) != (struct property *)0;
}

#endif /* _NEVERC_KRT_LINUX_OF_H */
