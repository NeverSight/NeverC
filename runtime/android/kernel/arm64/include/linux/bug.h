/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_BUG_H
#define _NVK_LINUX_BUG_H

#include <linux/printk.h>

#define BUILD_BUG_ON(cond) _Static_assert(!(cond), "BUILD_BUG_ON: " #cond)
#define WARN_ON(cond)                                                         \
	({                                                                     \
		int __c = !!(cond);                                            \
		if (__c)                                                       \
			pr_warn("WARN_ON: %s\n", #cond);                       \
		__c;                                                           \
	})
#define WARN_ON_ONCE(cond) WARN_ON(cond)
#define BUG()                                                                 \
	do {                                                                  \
		pr_err("BUG at %s:%d\n", __FILE__, __LINE__);                  \
		__builtin_trap();                                              \
	} while (0)
#define BUG_ON(cond)                                                          \
	do {                                                                  \
		if (cond)                                                     \
			BUG();                                                 \
	} while (0)

#endif /* _NVK_LINUX_BUG_H */
