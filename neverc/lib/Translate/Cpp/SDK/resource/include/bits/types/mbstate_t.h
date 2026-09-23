/*===-- NeverC C++ SDK multibyte state type -------------------------------===*\
|*
|* Copyright (C) 2026 NeverC contributors.
|* SPDX-License-Identifier: AGPL-3.0-only
|*
|* Declare the target C runtime's mbstate_t layout without importing host
|* headers. These definitions match the pinned Darwin SDK, glibc and UCRT
|* headers used by the supported core-v2 targets. Runtime conversion calls
|* remain outside the core-v2 translation boundary.
|*
\*===----------------------------------------------------------------------===*/

#ifndef NEVERC_CPP_SDK_MBSTATE_T_H
#define NEVERC_CPP_SDK_MBSTATE_T_H

#if defined(__APPLE__)
typedef union {
  char __mbstate8[128];
  long long _mbstateL;
} __mbstate_t;
typedef __mbstate_t __darwin_mbstate_t;
typedef __darwin_mbstate_t mbstate_t;
#elif defined(_WIN32)
typedef struct _Mbstatet {
  unsigned long _Wchar;
  unsigned short _Byte, _State;
} _Mbstatet;
typedef _Mbstatet mbstate_t;
#elif defined(__linux__)
#ifndef __WINT_TYPE__
#define __WINT_TYPE__ unsigned int
#endif
typedef struct {
  int __count;
  union {
    __WINT_TYPE__ __wch;
    char __wchb[4];
  } __value;
} __mbstate_t;
typedef __mbstate_t mbstate_t;
#else
#error "NeverC C++ SDK has no mbstate_t layout for this target"
#endif

#endif /* NEVERC_CPP_SDK_MBSTATE_T_H */
