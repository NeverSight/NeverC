/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_MINMAX_H
#define _NVK_LINUX_MINMAX_H

#define min(a, b) ({                                                          \
	__typeof__(a) _a = (a);                                               \
	__typeof__(b) _b = (b);                                               \
	_a < _b ? _a : _b;                                                    \
})

#define max(a, b) ({                                                          \
	__typeof__(a) _a = (a);                                               \
	__typeof__(b) _b = (b);                                               \
	_a > _b ? _a : _b;                                                    \
})

#define min_t(type, a, b) ({                                                  \
	type _a = (a); type _b = (b);                                         \
	_a < _b ? _a : _b;                                                    \
})

#define max_t(type, a, b) ({                                                  \
	type _a = (a); type _b = (b);                                         \
	_a > _b ? _a : _b;                                                    \
})

#define clamp(val, lo, hi) min(max(val, lo), hi)
#define clamp_t(type, val, lo, hi) min_t(type, max_t(type, val, lo), hi)
#define clamp_val(val, lo, hi) clamp(val, lo, hi)

#define swap(a, b) ({                                                         \
	__typeof__(a) _tmp = (a);                                             \
	(a) = (b);                                                            \
	(b) = _tmp;                                                           \
})

#endif /* _NVK_LINUX_MINMAX_H */
