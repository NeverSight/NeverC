/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_STRING_H
#define _NEVERC_KRT_LINUX_STRING_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <nvkmod_version.h>

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
/*
 * strcat was removed from GKI ksymtab in 6.18.
 * Provide inline fallback; prefer strncat in new code.
 */
static __always_inline char *strcat(char *dest, const char *src)
{
	char *d = dest;
	while (*d) d++;
	while ((*d++ = *src++) != '\0')
		;
	return dest;
}
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
int snprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);
int scnprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);

/*
 * strlcpy was removed in 6.12+ (replaced by strscpy which is inline).
 * Provide an inline fallback for cross-version compat.
 */
#if NEVERC_KRT_KERNEL >= 612
static __always_inline size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);
	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}
#else
size_t strlcpy(char *dest, const char *src, size_t size);
#endif

#define __neverc_krt_memset  __builtin_memset
#define __neverc_krt_memcpy  __builtin_memcpy
#define __neverc_krt_memmove __builtin_memmove
#define __neverc_krt_memcmp  __builtin_memcmp
#define __neverc_krt_strlen  __builtin_strlen
#define __neverc_krt_strcmp   __builtin_strcmp
#define __neverc_krt_strncmp __builtin_strncmp
#define __neverc_krt_strcpy  __builtin_strcpy

static __always_inline void neverc_krt_memzero(void *s, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	while (n--) *p++ = 0;
}

#endif /* _NEVERC_KRT_LINUX_STRING_H */
