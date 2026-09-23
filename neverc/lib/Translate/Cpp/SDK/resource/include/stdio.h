/*===-- NeverC C++ SDK stdio constants -----------------------------------===*\
|*
|* Copyright (C) 2026 NeverC contributors.
|* SPDX-License-Identifier: AGPL-3.0-only
|*
|* The pinned libc++ char_traits header needs EOF while parsing string views.
|* The core-v2 profile has no C stdio runtime surface.
|*
\*===----------------------------------------------------------------------===*/

#ifndef NEVERC_CPP_SDK_STDIO_H
#define NEVERC_CPP_SDK_STDIO_H

#define EOF (-1)

/* Required to keep libc++'s C remove overload distinct from std::remove. */
#ifdef __cplusplus
extern "C" {
#endif
int remove(const char *);
#ifdef __cplusplus
}
#endif

#endif /* NEVERC_CPP_SDK_STDIO_H */
