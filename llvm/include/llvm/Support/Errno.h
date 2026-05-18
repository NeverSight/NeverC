//===- llvm/Support/Errno.h - Portable+convenient errno handling -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares some portable and convenient functions to deal with errno.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ERRNO_H
#define LLVM_SUPPORT_ERRNO_H

#include <errno.h>
#include <string.h>
#include <string>

extern "C" int csupport_strerror(int errnum, char *buf, size_t buflen);

namespace llvm {
namespace sys {

/// Like the no-argument version above, but uses \p errnum instead of errno.
inline std::string StrError(int errnum) {
  if (errnum == 0)
    return {};
  char buf[2000];
  csupport_strerror(errnum, buf, sizeof(buf));
  return std::string(buf);
}

/// Returns a string representation of the errno value, using whatever
/// thread-safe variant of strerror() is available.  Be sure to call this
/// immediately after the function that set errno, or errno may have been
/// overwritten by an intervening call.
inline std::string StrError() { return StrError(errno); }

template <typename FailT, typename Fun, typename... Args>
inline decltype(auto) RetryAfterSignal(const FailT &Fail, const Fun &F,
                                       const Args &...As) {
  decltype(F(As...)) Res;
  do {
    errno = 0;
    Res = F(As...);
  } while (Res == Fail && errno == EINTR);
  return Res;
}

} // namespace sys
} // namespace llvm

#endif // LLVM_SUPPORT_ERRNO_H
