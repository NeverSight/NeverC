/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_UACCESS_H
#define _NVK_LINUX_UACCESS_H

#include <linux/types.h>

unsigned long copy_to_user(void __user *to, const void *from, unsigned long n);
unsigned long copy_from_user(void *to, const void __user *from, unsigned long n);

#define get_user(x, ptr)                                                      \
	({                                                                     \
		__typeof__(*(ptr)) __v;                                        \
		unsigned long __e = copy_from_user(&__v, (ptr), sizeof(__v));  \
		(x) = __v;                                                     \
		__e ? -EFAULT : 0;                                             \
	})
#define put_user(x, ptr)                                                      \
	({                                                                     \
		__typeof__(*(ptr)) __v = (x);                                  \
		copy_to_user((ptr), &__v, sizeof(__v)) ? -EFAULT : 0;         \
	})

#endif /* _NVK_LINUX_UACCESS_H */
