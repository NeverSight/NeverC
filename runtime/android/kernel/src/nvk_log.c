/* SPDX-License-Identifier: GPL-2.0 */
#include <nvk.h>

volatile int _neverc_krt_log_level = NEVERC_KRT_LOG_DEFAULT_LEVEL;

void neverc_krt_log_hexdump(const char *prefix, const void *buf, size_t len)
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
