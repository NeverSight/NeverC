/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_CAPABILITY_H
#define _NEVERC_KRT_LINUX_CAPABILITY_H

#include <linux/types.h>
#include <linux/cred.h>

bool capable(int cap);
bool ns_capable(void *ns, int cap);
bool file_ns_capable(const struct file *file, void *ns, int cap);

#endif /* _NEVERC_KRT_LINUX_CAPABILITY_H */
