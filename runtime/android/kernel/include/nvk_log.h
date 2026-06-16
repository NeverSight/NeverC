/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NEVERC_KRT_LOG_H
#define NEVERC_KRT_LOG_H

#include <linux/types.h>
#include <nvk_rt.h>
#include <linux/compiler.h>
#include <linux/printk.h>

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

static __always_inline void neverc_krt_log_set_level(int level)
{
	__atomic_store_n(&_neverc_krt_log_level, level, __ATOMIC_RELEASE);
}

static __always_inline int neverc_krt_log_get_level(void)
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

#define neverc_krt_log_once(fmt, ...)                                      \
	do {                                                         \
		NEVERC_KRT_RT_VAR int __logged;                                 \
		if (!__logged) {                                     \
			__logged = 1;                                \
			neverc_krt_log_info(fmt, ##__VA_ARGS__);            \
		}                                                    \
	} while (0)

#define neverc_krt_log_ratelimit(fmt, ...)                                 \
	do {                                                         \
		NEVERC_KRT_RT_VAR u64 __last_ts;                                \
		u64 __now;                                           \
		__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(__now));\
		if (__now - __last_ts > 100000000ULL) {               \
			__last_ts = __now;                           \
			neverc_krt_log_info(fmt, ##__VA_ARGS__);            \
		}                                                    \
	} while (0)

#define neverc_krt_log_ratelimit_n(n, interval_ns, fmt, ...)               \
	do {                                                         \
		NEVERC_KRT_RT_VAR u64 __rl_ts;                                  \
		NEVERC_KRT_RT_VAR int __rl_cnt;                                 \
		u64 __now;                                           \
		__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(__now));\
		u64 __freq;                                          \
		__asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(__freq));\
		u64 __ticks = (u64)(interval_ns) * __freq / 1000000000ULL;\
		if (__now - __rl_ts > __ticks) {                     \
			__rl_ts = __now;                             \
			__rl_cnt = 0;                                \
		}                                                    \
		if (__rl_cnt < (n)) {                                \
			__rl_cnt++;                                  \
			neverc_krt_log_info(fmt, ##__VA_ARGS__);            \
		}                                                    \
	} while (0)

static __always_inline void neverc_krt_log_hexdump(const char *prefix,
					    const void *buf, size_t len)
{
	const unsigned char *p = (const unsigned char *)buf;
	size_t i;
	char line[80];
	int pos;

	if (neverc_krt_log_get_level() < NEVERC_KRT_LOG_DEBUG)
		return;

	for (i = 0; i < len; i += 16) {
		static const char hex[] = "0123456789abcdef";
		size_t j;
		pos = 0;
		for (j = 0; j < 16 && (i + j) < len; j++) {
			unsigned char b = p[i + j];
			line[pos++] = hex[b >> 4];
			line[pos++] = hex[b & 0xf];
			line[pos++] = ' ';
		}
		line[pos] = '\0';
		pr_info("%s: %04zx: %s\n", prefix, i, line);
	}
}

#endif /* NEVERC_KRT_LOG_STRIP */

#endif /* NEVERC_KRT_LOG_H */
