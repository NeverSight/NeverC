//===-- llvm/Support/DataTypes.h - Define fixed size types ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Provides the fixed-size integer typedefs ([u]int{8,16,32,64}_t and limits)
// historically exported by llvm-c/DataTypes.h. The llvm-c/* public headers
// were removed in NeverC, so we now pull them directly from the host C11
// <stdint.h> / <inttypes.h> — which are guaranteed available by the C++17
// language standard we build with.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DATATYPES_H
#define LLVM_SUPPORT_DATATYPES_H

#include <inttypes.h>
#include <stdint.h>
#include <sys/types.h>

// MSVC does not provide ssize_t in <sys/types.h>. Source it from <BaseTsd.h>'s
// SSIZE_T so all LLVM headers that use ssize_t (e.g. raw_ostream's
// raw_fd_stream::read) compile on Windows.
#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

// Set defaults for constants which we cannot find on some platforms.
#if !defined(INT64_MAX)
#define INT64_MAX 9223372036854775807LL
#endif
#if !defined(INT64_MIN)
#define INT64_MIN ((-INT64_MAX) - 1)
#endif
#if !defined(UINT64_MAX)
#define UINT64_MAX 0xffffffffffffffffULL
#endif

#endif // LLVM_SUPPORT_DATATYPES_H
