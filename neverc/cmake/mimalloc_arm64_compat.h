/*===-- mimalloc_arm64_compat.h - MSVC ARM64 intrinsic shims for Clang -----===
 *
 * Upstream Clang (as of LLVM 20) does not implement the MSVC __ldar/__stlr
 * family of ARM64 load-acquire / store-release intrinsics. mimalloc's
 * atomic.h uses them on _M_ARM64, so builds with Clang as host compiler
 * fail.
 *
 * Provide equivalents via GCC-style atomic builtins, which lower to the
 * same ldar/stlr ARM64 instructions.
 *
 * This header is force-included into mimalloc via AddMimalloc.cmake only
 * when building on ARM64 Windows with Clang.
 *===---------------------------------------------------------------------===*/
#ifndef NEVERC_MIMALLOC_ARM64_COMPAT_H
#define NEVERC_MIMALLOC_ARM64_COMPAT_H

#if defined(__clang__) && defined(_M_ARM64)

#if !__has_builtin(__ldar8)
static __inline unsigned char __ldar8(volatile const unsigned char *p) {
  return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
#endif
#if !__has_builtin(__ldar16)
static __inline unsigned short __ldar16(volatile const unsigned short *p) {
  return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
#endif
#if !__has_builtin(__ldar32)
static __inline unsigned int __ldar32(volatile const unsigned int *p) {
  return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
#endif
#if !__has_builtin(__ldar64)
static __inline unsigned __int64 __ldar64(volatile const unsigned __int64 *p) {
  return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
#endif

#if !__has_builtin(__stlr8)
static __inline void __stlr8(volatile unsigned char *p, unsigned char v) {
  __atomic_store_n(p, v, __ATOMIC_RELEASE);
}
#endif
#if !__has_builtin(__stlr16)
static __inline void __stlr16(volatile unsigned short *p, unsigned short v) {
  __atomic_store_n(p, v, __ATOMIC_RELEASE);
}
#endif
#if !__has_builtin(__stlr32)
static __inline void __stlr32(volatile unsigned int *p, unsigned int v) {
  __atomic_store_n(p, v, __ATOMIC_RELEASE);
}
#endif
#if !__has_builtin(__stlr64)
static __inline void __stlr64(volatile unsigned __int64 *p, unsigned __int64 v) {
  __atomic_store_n(p, v, __ATOMIC_RELEASE);
}
#endif

#endif /* __clang__ && _M_ARM64 */
#endif /* NEVERC_MIMALLOC_ARM64_COMPAT_H */
