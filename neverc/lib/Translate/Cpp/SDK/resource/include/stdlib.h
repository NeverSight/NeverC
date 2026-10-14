/* NeverC C++ SDK declarations required to parse libc++ <random>.
 * Copyright (C) 2026 NeverC contributors.
 * SPDX-License-Identifier: AGPL-3.0-only
 * These declarations do not authorize a core-v2 C runtime call.
 */
#ifndef _NEVERC_CPP_CORE_V2_SDK
#include_next <stdlib.h>
#else
#ifndef NEVERC_CPP_SDK_STDLIB_H
#define NEVERC_CPP_SDK_STDLIB_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;
int abs(int);
long labs(long);
long long llabs(long long);
div_t div(int, int);
ldiv_t ldiv(long, long);
lldiv_t lldiv(long long, long long);
#ifdef __cplusplus
}
#endif
#endif /* NEVERC_CPP_SDK_STDLIB_H */
#endif /* _NEVERC_CPP_CORE_V2_SDK */
