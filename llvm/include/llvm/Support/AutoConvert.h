//===- AutoConvert.h - Auto conversion between ASCII/EBCDIC -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains functions used for auto conversion between
// ASCII/EBCDIC codepages specific to z/OS.
//
//===----------------------------------------------------------------------===//i

#ifndef LLVM_SUPPORT_AUTOCONVERT_H
#define LLVM_SUPPORT_AUTOCONVERT_H

#ifdef __MVS__
#include <_Ccsid.h>
/* std::error_code eliminated: functions now return int (0=success,
 * errno=failure) */

#define CCSID_IBM_1047 1047
#define CCSID_UTF_8 1208
#define CCSID_ISO8859_1 819

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
int enableAutoConversion(int FD);
int disableAutoConversion(int FD);
int restoreStdHandleAutoConversion(int FD);
#ifdef __cplusplus
}
#endif // __cplusplus

#ifdef __cplusplus
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace llvm {

int disableAutoConversion(int FD);
int enableAutoConversion(int FD);
int restoreStdHandleAutoConversion(int FD);
int setFileTag(int FD, int CCSID, bool Text);

namespace detail {
inline int *getSavedStdHandleAutoConversionMode() {
  static int arr[3] = {-1, -1, -1};
  return arr;
}
inline int disableAutoConversionImpl(int FD) {
  static const struct f_cnvrt Convert = {SETCVTOFF, 0, 0};
  return fcntl(FD, F_CONTROL_CVT, &Convert);
}
inline int enableAutoConversionImpl(int FD) {
  struct f_cnvrt Query = {QUERYCVT, 0, 0};
  if (fcntl(FD, F_CONTROL_CVT, &Query) == -1)
    return -1;
  if (Query.pccsid == CCSID_ISO8859_1 &&
      (Query.fccsid == CCSID_UTF_8 || Query.fccsid == CCSID_ISO8859_1))
    return 0;
  int *saved = getSavedStdHandleAutoConversionMode();
  if ((FD == STDIN_FILENO || FD == STDOUT_FILENO || FD == STDERR_FILENO) &&
      saved[FD] == -1)
    saved[FD] = Query.cvtcmd;
  if (FD == STDOUT_FILENO || FD == STDERR_FILENO)
    Query.cvtcmd = SETCVTON;
  else
    Query.cvtcmd = SETCVTALL;
  Query.pccsid =
      (FD == STDIN_FILENO || FD == STDOUT_FILENO || FD == STDERR_FILENO)
          ? 0
          : CCSID_UTF_8;
  Query.fccsid = (Query.fccsid == FT_UNTAGGED) ? CCSID_IBM_1047 : Query.fccsid;
  return fcntl(FD, F_CONTROL_CVT, &Query);
}
} // namespace detail

inline int disableAutoConversion(int FD) {
  if (detail::disableAutoConversionImpl(FD) == -1)
    return errno;
  return 0;
}
inline int enableAutoConversion(int FD) {
  if (detail::enableAutoConversionImpl(FD) == -1)
    return errno;
  return 0;
}
inline int restoreStdHandleAutoConversion(int FD) {
  assert(FD == STDIN_FILENO || FD == STDOUT_FILENO || FD == STDERR_FILENO);
  int *saved = detail::getSavedStdHandleAutoConversionMode();
  if (saved[FD] == -1)
    return 0;
  struct f_cnvrt Cvt = {saved[FD], 0, 0};
  if (fcntl(FD, F_CONTROL_CVT, &Cvt) == -1)
    return errno;
  return 0;
}
inline int setFileTag(int FD, int CCSID, bool Text) {
  assert((!Text || (CCSID != FT_UNTAGGED && CCSID != FT_BINARY)) &&
         "FT_UNTAGGED and FT_BINARY are not allowed for text files");
  struct file_tag Tag;
  Tag.ft_ccsid = CCSID;
  Tag.ft_txtflag = Text;
  Tag.ft_deferred = 0;
  Tag.ft_rsvflags = 0;
  if (fcntl(FD, F_SETTAG, &Tag) == -1)
    return errno;
  return 0;
}

} // namespace llvm
#endif // __cplusplus

#endif // __MVS__

#endif // LLVM_SUPPORT_AUTOCONVERT_H
