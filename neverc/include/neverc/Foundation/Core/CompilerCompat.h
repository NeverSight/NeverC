#ifndef NEVERC_FOUNDATION_CORE_COMPILERCOMPAT_H
#define NEVERC_FOUNDATION_CORE_COMPILERCOMPAT_H

/// \file CompilerCompat.h
/// Cross-compiler compatibility macros for intrinsics that differ between
/// GCC/Clang and MSVC.

#ifdef _MSC_VER
#include <intrin.h>

#define NEVERC_PREFETCH_LOCALITY_0 _MM_HINT_NTA
#define NEVERC_PREFETCH_LOCALITY_1 _MM_HINT_T2
#define NEVERC_PREFETCH_LOCALITY_2 _MM_HINT_T1
#define NEVERC_PREFETCH_LOCALITY_3 _MM_HINT_T0

/// MSVC equivalent of __builtin_prefetch. The rw (read/write) parameter is
/// ignored since _mm_prefetch does not distinguish.
#define NEVERC_PREFETCH(addr, rw, locality)                                    \
  _mm_prefetch(reinterpret_cast<const char *>(addr),                           \
               NEVERC_PREFETCH_LOCALITY_##locality)

#else

#define NEVERC_PREFETCH(addr, rw, locality) __builtin_prefetch(addr, rw, locality)

#endif // _MSC_VER

#endif // NEVERC_FOUNDATION_CORE_COMPILERCOMPAT_H
