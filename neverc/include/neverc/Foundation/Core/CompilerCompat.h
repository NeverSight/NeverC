#ifndef NEVERC_FOUNDATION_CORE_COMPILERCOMPAT_H
#define NEVERC_FOUNDATION_CORE_COMPILERCOMPAT_H

/// \file CompilerCompat.h
/// Cross-compiler compatibility macros for intrinsics that differ between
/// GCC/Clang and MSVC.

#if defined(__clang__) || defined(__GNUC__)

#define NEVERC_PREFETCH(addr, rw, locality) __builtin_prefetch(addr, rw, locality)

#elif defined(_MSC_VER)
#include <intrin.h>

#if defined(_M_ARM64) || defined(_M_ARM64EC)

#define NEVERC_PREFETCH(addr, rw, locality)                                    \
  __prefetch(reinterpret_cast<const void *>(addr))

#else // x86/x64 MSVC

#define NEVERC_PREFETCH_LOCALITY_0 _MM_HINT_NTA
#define NEVERC_PREFETCH_LOCALITY_1 _MM_HINT_T2
#define NEVERC_PREFETCH_LOCALITY_2 _MM_HINT_T1
#define NEVERC_PREFETCH_LOCALITY_3 _MM_HINT_T0

#define NEVERC_PREFETCH(addr, rw, locality)                                    \
  _mm_prefetch(reinterpret_cast<const char *>(addr),                           \
               NEVERC_PREFETCH_LOCALITY_##locality)

#endif // _M_ARM64
#endif // __clang__ || __GNUC__ / _MSC_VER

/// Lambda-compatible always_inline. Unlike LLVM_ATTRIBUTE_ALWAYS_INLINE, omits
/// the \c inline keyword which is invalid on lambda expressions.
#if defined(__clang__) || defined(__GNUC__)
#define NEVERC_LAMBDA_ALWAYS_INLINE __attribute__((always_inline))
#else
#define NEVERC_LAMBDA_ALWAYS_INLINE
#endif

#endif // NEVERC_FOUNDATION_CORE_COMPILERCOMPAT_H
