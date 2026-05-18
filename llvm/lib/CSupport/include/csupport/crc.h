/*===-- csupport/crc.h - CRC-32 implementation ------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions. See https://llvm.org/LICENSE.txt for license information. *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                   *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef CSUPPORT_CRC_H
#define CSUPPORT_CRC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t csupport_crc32(uint32_t CRC, const uint8_t *Data, size_t Size);
uint32_t csupport_crc32_initial(const uint8_t *Data, size_t Size);

#ifdef __cplusplus
}
#endif

#endif /* CSUPPORT_CRC_H */
