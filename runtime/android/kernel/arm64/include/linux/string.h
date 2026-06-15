/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NVK_LINUX_STRING_H
#define _NVK_LINUX_STRING_H

#include <linux/types.h>

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
size_t strlcpy(char *dest, const char *src, size_t size);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
int snprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);
int scnprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);

#define __nvk_memset  __builtin_memset
#define __nvk_memcpy  __builtin_memcpy
#define __nvk_memmove __builtin_memmove
#define __nvk_memcmp  __builtin_memcmp
#define __nvk_strlen  __builtin_strlen
#define __nvk_strcmp   __builtin_strcmp
#define __nvk_strncmp __builtin_strncmp
#define __nvk_strcpy  __builtin_strcpy

static __always_inline void nvk_memzero(void *s, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	while (n--) *p++ = 0;
}

#endif /* _NVK_LINUX_STRING_H */
