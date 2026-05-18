/*===-- csupport/xxhash.h - xxHash interface ---------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information. *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_XXHASH_H
#define CSUPPORT_XXHASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t csupport_xxHash64(const uint8_t *data, size_t len);
uint64_t csupport_xxh3_64bits(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_XXHASH_H */
