/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_OF_H
#define _NEVERC_KRT_LINUX_OF_H

#include <linux/types.h>

struct device_node; /* opaque */
struct property;    /* opaque */

struct device_node *of_find_compatible_node(struct device_node *from,
					    const char *type,
					    const char *compatible);
struct device_node *of_find_node_by_path(const char *path);
struct device_node *of_find_node_by_name(struct device_node *from,
					 const char *name);
void of_node_put(struct device_node *node);

int of_property_read_u32(const struct device_node *np, const char *propname,
			 u32 *out_value);
int of_property_read_u64(const struct device_node *np, const char *propname,
			 u64 *out_value);
int of_property_read_string(const struct device_node *np, const char *propname,
			    const char **out_string);
bool of_property_read_bool(const struct device_node *np, const char *propname);
int of_property_count_elems_of_size(const struct device_node *np,
				    const char *propname, int elem_size);

#endif /* _NEVERC_KRT_LINUX_OF_H */
