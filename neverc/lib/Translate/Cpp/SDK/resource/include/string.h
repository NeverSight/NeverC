/*===-- NeverC C++ SDK string declarations ------------------------------===*\
|*
|* Copyright (C) 2026 NeverC contributors.
|* SPDX-License-Identifier: AGPL-3.0-only
|*
|* This deterministic header supplies the ISO C string declarations that
|* libc++ needs while parsing the platform-free C++17 core profile. It does
|* not select a target C runtime and is unavailable as a top-level core-v2
|* include. Directly lowered C++ algorithms therefore do not acquire a libc
|* link dependency through these declarations.
|*
\*===----------------------------------------------------------------------===*/

#ifndef NEVERC_CPP_SDK_STRING_H
#define NEVERC_CPP_SDK_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *__restrict destination, const void *__restrict source,
             size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
void *memchr(const void *memory, int value, size_t count);

char *strcpy(char *__restrict destination, const char *__restrict source);
char *strncpy(char *__restrict destination, const char *__restrict source,
              size_t count);
char *strcat(char *__restrict destination, const char *__restrict source);
char *strncat(char *__restrict destination, const char *__restrict source,
              size_t count);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
int strcoll(const char *left, const char *right);
size_t strxfrm(char *__restrict destination, const char *__restrict source,
               size_t count);
char *strchr(const char *string, int value);
size_t strcspn(const char *string, const char *reject);
char *strpbrk(const char *string, const char *accept);
char *strrchr(const char *string, int value);
size_t strspn(const char *string, const char *accept);
char *strstr(const char *string, const char *substring);
char *strtok(char *__restrict string, const char *__restrict separators);
char *strerror(int error);
size_t strlen(const char *string);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_CPP_SDK_STRING_H */
