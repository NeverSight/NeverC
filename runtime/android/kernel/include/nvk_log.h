/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_LOG_H
#define NEVERC_KRT_LOG_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/printk.h>
#include <nvk_timer.h>

enum neverc_krt_log_level {
	NEVERC_KRT_LOG_SILENT = 0,
	NEVERC_KRT_LOG_ERROR  = 1,
	NEVERC_KRT_LOG_WARN   = 2,
	NEVERC_KRT_LOG_INFO   = 3,
	NEVERC_KRT_LOG_DEBUG  = 4,
	NEVERC_KRT_LOG_TRACE  = 5,
};

#ifndef NEVERC_KRT_LOG_TAG
#define NEVERC_KRT_LOG_TAG "nvk"
#endif

#ifndef NEVERC_KRT_LOG_DEFAULT_LEVEL
#define NEVERC_KRT_LOG_DEFAULT_LEVEL NEVERC_KRT_LOG_INFO
#endif

NEVERC_KRT_RT_VAR volatile int _neverc_krt_log_level;

__always_inline void neverc_krt_log_set_level(int level)
{
	__atomic_store_n(&_neverc_krt_log_level, level, __ATOMIC_RELEASE);
}

__always_inline int neverc_krt_log_get_level(void)
{
	return __atomic_load_n(&_neverc_krt_log_level, __ATOMIC_ACQUIRE);
}

#ifdef NEVERC_KRT_LOG_STRIP
#define neverc_krt_log_err(...)    ((void)0)
#define neverc_krt_log_warn(...)   ((void)0)
#define neverc_krt_log_info(...)   ((void)0)
#define neverc_krt_log_dbg(...)    ((void)0)
#define neverc_krt_log_trace(...)  ((void)0)
#define neverc_krt_log_once(...)   ((void)0)
#define neverc_krt_log_ratelimit(...)     ((void)0)
#define neverc_krt_log_ratelimit_n(...)   ((void)0)
#define neverc_krt_log_hexdump(...)       ((void)0)
#else

#define neverc_krt_log_err(fmt, ...)                                       \
	do {                                                         \
		if (neverc_krt_log_get_level() >= NEVERC_KRT_LOG_ERROR)           \
			pr_err(NEVERC_KRT_LOG_TAG ": " fmt, ##__VA_ARGS__);\
	} while (0)

#define neverc_krt_log_warn(fmt, ...)                                      \
	do {                                                         \
		if (neverc_krt_log_get_level() >= NEVERC_KRT_LOG_WARN)            \
			pr_warn(NEVERC_KRT_LOG_TAG ": " fmt, ##__VA_ARGS__);\
	} while (0)

#define neverc_krt_log_info(fmt, ...)                                      \
	do {                                                         \
		if (neverc_krt_log_get_level() >= NEVERC_KRT_LOG_INFO)            \
			pr_info(NEVERC_KRT_LOG_TAG ": " fmt, ##__VA_ARGS__);\
	} while (0)

#define neverc_krt_log_dbg(fmt, ...)                                       \
	do {                                                         \
		if (neverc_krt_log_get_level() >= NEVERC_KRT_LOG_DEBUG)           \
			pr_info(NEVERC_KRT_LOG_TAG ": [D] " fmt, ##__VA_ARGS__);\
	} while (0)

#define neverc_krt_log_trace(fmt, ...)                                     \
	do {                                                         \
		if (neverc_krt_log_get_level() >= NEVERC_KRT_LOG_TRACE)           \
			pr_info(NEVERC_KRT_LOG_TAG ": [T] " fmt, ##__VA_ARGS__);\
	} while (0)

#define _NVK_LOG_CAT2(a, b) a##b
#define _NVK_LOG_CAT(a, b) _NVK_LOG_CAT2(a, b)

#define neverc_krt_log_once(fmt, ...)                                      \
	do {                                                         \
		static int _NVK_LOG_CAT(__nvk_lo_, __LINE__);        \
		if (!_NVK_LOG_CAT(__nvk_lo_, __LINE__)) {            \
			_NVK_LOG_CAT(__nvk_lo_, __LINE__) = 1;       \
			neverc_krt_log_info(fmt, ##__VA_ARGS__);            \
		}                                                    \
	} while (0)

#define neverc_krt_log_ratelimit(fmt, ...)                                 \
	do {                                                         \
		static u64 _NVK_LOG_CAT(__nvk_rl_, __LINE__);        \
		u64 __now = neverc_krt_arch_counter();               \
		if (__now - _NVK_LOG_CAT(__nvk_rl_, __LINE__) > 100000000ULL) {\
			_NVK_LOG_CAT(__nvk_rl_, __LINE__) = __now;   \
			neverc_krt_log_info(fmt, ##__VA_ARGS__);            \
		}                                                    \
	} while (0)

#define neverc_krt_log_ratelimit_n(n, interval_ns, fmt, ...)               \
	do {                                                         \
		static u64 _NVK_LOG_CAT(__nvk_rlt_, __LINE__);       \
		static int _NVK_LOG_CAT(__nvk_rlc_, __LINE__);       \
		u64 __now = neverc_krt_arch_counter();               \
		u64 __freq = (u64)neverc_krt_arch_counter_freq();    \
		u64 __ticks = (u64)(interval_ns) * __freq / 1000000000ULL;\
		if (__now - _NVK_LOG_CAT(__nvk_rlt_, __LINE__) > __ticks) {\
			_NVK_LOG_CAT(__nvk_rlt_, __LINE__) = __now;  \
			_NVK_LOG_CAT(__nvk_rlc_, __LINE__) = 0;      \
		}                                                    \
		if (_NVK_LOG_CAT(__nvk_rlc_, __LINE__) < (n)) {      \
			_NVK_LOG_CAT(__nvk_rlc_, __LINE__)++;        \
			neverc_krt_log_info(fmt, ##__VA_ARGS__);            \
		}                                                    \
	} while (0)

void neverc_krt_log_hexdump(const char *prefix,
			    const void *buf, size_t len);

#endif /* NEVERC_KRT_LOG_STRIP */

#endif /* NEVERC_KRT_LOG_H */
