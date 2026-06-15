/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_OVERFLOW_H
#define _NVK_LINUX_OVERFLOW_H

#include <linux/types.h>
#include <linux/compiler.h>

#define check_add_overflow(a, b, d) __builtin_add_overflow(a, b, d)
#define check_sub_overflow(a, b, d) __builtin_sub_overflow(a, b, d)
#define check_mul_overflow(a, b, d) __builtin_mul_overflow(a, b, d)

#define struct_size(p, member, count)                                          \
	({                                                                    \
		size_t __sz = sizeof(*(p)) +                                  \
			      sizeof(*(p)->member) * (size_t)(count);         \
		__sz;                                                         \
	})

#define array_size(a, b) ({                                                   \
	size_t __a = (a), __b = (b), __r;                                     \
	check_mul_overflow(__a, __b, &__r) ? (size_t)-1 : __r;               \
})

#define size_add(a, b) ({                                                     \
	size_t __a = (a), __b = (b), __r;                                     \
	check_add_overflow(__a, __b, &__r) ? (size_t)-1 : __r;               \
})

#endif /* _NVK_LINUX_OVERFLOW_H */
