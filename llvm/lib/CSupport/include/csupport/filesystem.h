/*===-- csupport/filesystem.h - File-system C ABI --------------*- C -*-===*/
/*
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions. See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef CSUPPORT_FILESYSTEM_H
#define CSUPPORT_FILESYSTEM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum csupport_creation_disposition {
  CSUPPORT_CD_CREATE_ALWAYS = 0,
  CSUPPORT_CD_CREATE_NEW = 1,
  CSUPPORT_CD_OPEN_EXISTING = 2,
  CSUPPORT_CD_OPEN_ALWAYS = 3
} csupport_creation_disposition_t;

typedef enum csupport_file_access {
  CSUPPORT_FA_READ = 1,
  CSUPPORT_FA_WRITE = 2,
  CSUPPORT_FA_READ_WRITE = CSUPPORT_FA_READ | CSUPPORT_FA_WRITE
} csupport_file_access_t;

typedef enum csupport_access_mode {
  CSUPPORT_AM_EXIST = 0,
  CSUPPORT_AM_WRITE = 1,
  CSUPPORT_AM_EXECUTE = 2
} csupport_access_mode_t;

typedef enum csupport_mapped_file_mode {
  CSUPPORT_MFM_READONLY = 0,
  CSUPPORT_MFM_READWRITE = 1,
  CSUPPORT_MFM_PRIVATE = 2
} csupport_mapped_file_mode_t;

typedef unsigned csupport_open_flags_t;
enum {
  CSUPPORT_OF_NONE = 0,
  CSUPPORT_OF_TEXT = 1,
  CSUPPORT_OF_CRLF = 2,
  CSUPPORT_OF_TEXT_WITH_CRLF = CSUPPORT_OF_TEXT | CSUPPORT_OF_CRLF,
  CSUPPORT_OF_APPEND = 4,
  CSUPPORT_OF_DELETE = 8,
  CSUPPORT_OF_CHILD_INHERIT = 16,
  CSUPPORT_OF_UPDATE_ATIME = 32,
  CSUPPORT_OF_EXCLUSIVE = 64,
  CSUPPORT_OF_NO_INHERIT = 128
};

unsigned csupport_get_umask(void);
int csupport_resize_file(int fd, uint64_t size);

#ifdef __cplusplus
}
#endif

#endif
