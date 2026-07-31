#ifndef NEVERC_PLATFORM_H
#define NEVERC_PLATFORM_H

/*
 * Cross-platform abstractions for NeverC std library.
 * Supports: macOS, iOS, Linux, Android, Windows, FreeBSD.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(_WIN32)
  #define NEVERC_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
  #include <TargetConditionals.h>
  #if TARGET_OS_IPHONE
    #define NEVERC_PLATFORM_IOS 1
  #else
    #define NEVERC_PLATFORM_MACOS 1
  #endif
  #define NEVERC_PLATFORM_APPLE 1
#elif defined(__ANDROID__)
  #define NEVERC_PLATFORM_ANDROID 1
  #define NEVERC_PLATFORM_POSIX 1
#elif defined(__linux__)
  #define NEVERC_PLATFORM_LINUX 1
  #define NEVERC_PLATFORM_POSIX 1
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  #define NEVERC_PLATFORM_BSD 1
  #define NEVERC_PLATFORM_POSIX 1
#else
  #define NEVERC_PLATFORM_POSIX 1
#endif

/* Secure random: use the platform's CSPRNG */
#if defined(NEVERC_PLATFORM_WINDOWS)
  /* Link with bcrypt.lib */
#ifndef NOMINMAX
#define NOMINMAX
#endif
  #include <windows.h>
  #include <bcrypt.h>
  #pragma comment(lib, "bcrypt.lib")
  static inline int neverc_platform_random(unsigned char *buf, size_t len) {
      if (!buf && len != 0) return -1;
      if (len == 0) return 0;
      while (len > 0) {
          ULONG chunk = len > 0xffffffffUL ? 0xffffffffUL : (ULONG)len;
          if (BCryptGenRandom(NULL, buf, chunk,
                              BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
              return -1;
          buf += chunk;
          len -= chunk;
      }
      return 0;
  }
#elif defined(NEVERC_PLATFORM_APPLE)
  #include <CommonCrypto/CommonRandom.h>
  static inline int neverc_platform_random(unsigned char *buf, size_t len) {
      if (!buf && len != 0) return -1;
      if (len == 0) return 0;
      return CCRandomGenerateBytes(buf, len) == kCCSuccess ? 0 : -1;
  }
#elif defined(NEVERC_PLATFORM_LINUX) || defined(NEVERC_PLATFORM_ANDROID)
  #include <errno.h>
  #include <sys/random.h>
  static inline int neverc_platform_random(unsigned char *buf, size_t len) {
      if (!buf && len != 0) return -1;
      if (len == 0) return 0;
      size_t off = 0;
      while (off < len) {
          ssize_t n = getrandom(buf + off, len - off, 0);
          if (n < 0 && errno == EINTR) continue;
          if (n <= 0) return -1;
          off += (size_t)n;
      }
      return 0;
  }
#else
  #include <errno.h>
  #include <fcntl.h>
  #include <unistd.h>
  static inline int neverc_platform_random(unsigned char *buf, size_t len) {
      if (!buf && len != 0) return -1;
      if (len == 0) return 0;
      int flags = O_RDONLY;
  #ifdef O_CLOEXEC
      flags |= O_CLOEXEC;
  #endif
      int fd = open("/dev/urandom", flags);
      if (fd < 0) return -1;
      size_t off = 0;
      while (off < len) {
          ssize_t n = read(fd, buf + off, len - off);
          if (n < 0 && errno == EINTR) continue;
          if (n <= 0) {
              close(fd);
              return -1;
          }
          off += (size_t)n;
      }
      return close(fd) == 0 ? 0 : -1;
  }
#endif

/* Secure memory zeroing — cannot be optimized away by the compiler.
   Use for zeroing sensitive cryptographic key material. */
static inline void neverc_platform_secure_zero(void *ptr, size_t len) {
#if defined(NEVERC_PLATFORM_WINDOWS)
    SecureZeroMemory(ptr, len);
#else
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
#endif
}

/* Atomic operations.
   NeverC and Clang on Windows also define _MSC_VER for ABI compatibility.
   Avoid pulling in <intrin.h> for them because the SSE header chain
   (xmmintrin.h → mm_malloc.h → UCRT malloc.h) relies on UCRT internal
   macros that aren't available outside MSVC.  Use __atomic_* builtins. */
#if defined(_MSC_VER) && !defined(__clang__) && !defined(__neverc__)
  #include <intrin.h>
  #define NEVERC_ATOMIC_LOAD32(addr)          _InterlockedOr((volatile long*)(addr), 0)
  #define NEVERC_ATOMIC_STORE32(addr, val)    _InterlockedExchange((volatile long*)(addr), (long)(val))
  #define NEVERC_ATOMIC_ADD32(addr, delta)    (_InterlockedExchangeAdd((volatile long*)(addr), (long)(delta)) + (delta))
  #define NEVERC_ATOMIC_SWAP32(addr, val)     _InterlockedExchange((volatile long*)(addr), (long)(val))
  #define NEVERC_ATOMIC_CAS32(addr, old, new) (_InterlockedCompareExchange((volatile long*)(addr), (long)(new), (long)(old)) == (long)(old))
  #define NEVERC_ATOMIC_LOAD64(addr)          _InterlockedOr64((volatile long long*)(addr), 0)
  #define NEVERC_ATOMIC_STORE64(addr, val)    _InterlockedExchange64((volatile long long*)(addr), (long long)(val))
  #define NEVERC_ATOMIC_ADD64(addr, delta)    (_InterlockedExchangeAdd64((volatile long long*)(addr), (long long)(delta)) + (delta))
  #define NEVERC_ATOMIC_SWAP64(addr, val)     _InterlockedExchange64((volatile long long*)(addr), (long long)(val))
  #define NEVERC_ATOMIC_CAS64(addr, old, new) (_InterlockedCompareExchange64((volatile long long*)(addr), (long long)(new), (long long)(old)) == (long long)(old))
#else
  #define NEVERC_ATOMIC_LOAD32(addr)          __atomic_load_n(addr, __ATOMIC_SEQ_CST)
  #define NEVERC_ATOMIC_STORE32(addr, val)    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST)
  #define NEVERC_ATOMIC_ADD32(addr, delta)    (__atomic_fetch_add(addr, delta, __ATOMIC_SEQ_CST) + (delta))
  #define NEVERC_ATOMIC_SWAP32(addr, val)     __atomic_exchange_n(addr, val, __ATOMIC_SEQ_CST)
  #define NEVERC_ATOMIC_CAS32(addr, old, new) ({ typeof(old) _o = (old); __atomic_compare_exchange_n(addr, &_o, new, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); })
  #define NEVERC_ATOMIC_LOAD64(addr)          __atomic_load_n(addr, __ATOMIC_SEQ_CST)
  #define NEVERC_ATOMIC_STORE64(addr, val)    __atomic_store_n(addr, val, __ATOMIC_SEQ_CST)
  #define NEVERC_ATOMIC_ADD64(addr, delta)    (__atomic_fetch_add(addr, delta, __ATOMIC_SEQ_CST) + (delta))
  #define NEVERC_ATOMIC_SWAP64(addr, val)     __atomic_exchange_n(addr, val, __ATOMIC_SEQ_CST)
  #define NEVERC_ATOMIC_CAS64(addr, old, new) ({ typeof(old) _o = (old); __atomic_compare_exchange_n(addr, &_o, new, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); })
#endif

#endif /* NEVERC_PLATFORM_H */
