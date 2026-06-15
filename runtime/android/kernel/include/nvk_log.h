/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVK_LOG_H
#define NVK_LOG_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/printk.h>

enum nvk_log_level {
	NVK_LOG_SILENT = 0,
	NVK_LOG_ERROR  = 1,
	NVK_LOG_WARN   = 2,
	NVK_LOG_INFO   = 3,
	NVK_LOG_DEBUG  = 4,
	NVK_LOG_TRACE  = 5,
};

#ifndef NVK_LOG_TAG
#define NVK_LOG_TAG "nvk"
#endif

#ifndef NVK_LOG_DEFAULT_LEVEL
#define NVK_LOG_DEFAULT_LEVEL NVK_LOG_INFO
#endif

static volatile int _nvk_log_level = NVK_LOG_DEFAULT_LEVEL;

static __always_inline void nvk_log_set_level(int level)
{
	__atomic_store_n(&_nvk_log_level, level, __ATOMIC_RELEASE);
}

static __always_inline int nvk_log_get_level(void)
{
	return __atomic_load_n(&_nvk_log_level, __ATOMIC_ACQUIRE);
}

#define nvk_log_err(fmt, ...)                                       \
	do {                                                         \
		if (nvk_log_get_level() >= NVK_LOG_ERROR)           \
			pr_err(NVK_LOG_TAG ": " fmt, ##__VA_ARGS__);\
	} while (0)

#define nvk_log_warn(fmt, ...)                                      \
	do {                                                         \
		if (nvk_log_get_level() >= NVK_LOG_WARN)            \
			pr_warn(NVK_LOG_TAG ": " fmt, ##__VA_ARGS__);\
	} while (0)

#define nvk_log_info(fmt, ...)                                      \
	do {                                                         \
		if (nvk_log_get_level() >= NVK_LOG_INFO)            \
			pr_info(NVK_LOG_TAG ": " fmt, ##__VA_ARGS__);\
	} while (0)

#define nvk_log_dbg(fmt, ...)                                       \
	do {                                                         \
		if (nvk_log_get_level() >= NVK_LOG_DEBUG)           \
			pr_info(NVK_LOG_TAG ": [D] " fmt, ##__VA_ARGS__);\
	} while (0)

#define nvk_log_trace(fmt, ...)                                     \
	do {                                                         \
		if (nvk_log_get_level() >= NVK_LOG_TRACE)           \
			pr_info(NVK_LOG_TAG ": [T] " fmt, ##__VA_ARGS__);\
	} while (0)

#define nvk_log_once(fmt, ...)                                      \
	do {                                                         \
		static int __logged;                                 \
		if (!__logged) {                                     \
			__logged = 1;                                \
			nvk_log_info(fmt, ##__VA_ARGS__);            \
		}                                                    \
	} while (0)

#define nvk_log_ratelimit(fmt, ...)                                 \
	do {                                                         \
		static u64 __last_jiffies;                           \
		u64 __now;                                           \
		__asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(__now));\
		if (__now - __last_jiffies > 100000000ULL) {          \
			__last_jiffies = __now;                      \
			nvk_log_info(fmt, ##__VA_ARGS__);            \
		}                                                    \
	} while (0)

static __always_inline void nvk_log_hexdump(const char *prefix,
					    const void *buf, size_t len)
{
	const unsigned char *p = (const unsigned char *)buf;
	size_t i;
	char line[80];
	int pos;

	if (nvk_log_get_level() < NVK_LOG_DEBUG)
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

#endif /* NVK_LOG_H */
