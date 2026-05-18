#ifndef __ARM_ACLE_H
#define __ARM_ACLE_H

#ifndef __ARM_ACLE
#error "ACLE intrinsics support not enabled."
#endif

#include <stdint.h>

/* 8 SYNCHRONIZATION, BARRIER AND HINT INTRINSICS */
/* 8.3 Memory barriers */
#if !__has_builtin(__dmb)
#define __dmb(i) __builtin_arm_dmb(i)
#endif
#if !__has_builtin(__dsb)
#define __dsb(i) __builtin_arm_dsb(i)
#endif
#if !__has_builtin(__isb)
#define __isb(i) __builtin_arm_isb(i)
#endif

/* 8.4 Hints */

#if !__has_builtin(__wfi)
static __inline__ void __attribute__((__always_inline__, __nodebug__))
__wfi(void) {
  __builtin_arm_wfi();
}
#endif

#if !__has_builtin(__wfe)
static __inline__ void
    __attribute__((__always_inline__, __nodebug__)) __wfe(void) {
  __builtin_arm_wfe();
}
#endif

#if !__has_builtin(__sev)
static __inline__ void
    __attribute__((__always_inline__, __nodebug__)) __sev(void) {
  __builtin_arm_sev();
}
#endif

#if !__has_builtin(__sevl)
static __inline__ void
    __attribute__((__always_inline__, __nodebug__)) __sevl(void) {
  __builtin_arm_sevl();
}
#endif

#if !__has_builtin(__yield)
static __inline__ void
    __attribute__((__always_inline__, __nodebug__)) __yield(void) {
  __builtin_arm_yield();
}
#endif

/* 8.5 Swap */
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__swp(uint32_t __x, volatile uint32_t *__p) {
  uint32_t v;
  do
    v = __builtin_arm_ldrex(__p);
  while (__builtin_arm_strex(__x, __p));
  return v;
}

/* 8.6 Memory prefetch intrinsics */
/* 8.6.1 Data prefetch */
#define __pld(addr) __pldx(0, 0, 0, addr)

#define __pldx(access_kind, cache_level, retention_policy, addr)               \
  __builtin_arm_prefetch(addr, access_kind, cache_level, retention_policy, 1)

/* 8.6.2 Instruction prefetch */
#define __pli(addr) __plix(0, 0, addr)

#define __plix(cache_level, retention_policy, addr)                            \
  __builtin_arm_prefetch(addr, 0, cache_level, retention_policy, 0)

/* 8.7 NOP */
#if !defined(_MSC_VER) || !defined(__aarch64__)
static __inline__ void
    __attribute__((__always_inline__, __nodebug__)) __nop(void) {
  __builtin_arm_nop();
}
#endif

/* 9 DATA-PROCESSING INTRINSICS */
/* 9.2 Miscellaneous data-processing intrinsics */
/* ROR */
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__ror(uint32_t __x, uint32_t __y) {
  __y %= 32;
  if (__y == 0)
    return __x;
  return (__x >> __y) | (__x << (32 - __y));
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__rorll(uint64_t __x, uint32_t __y) {
  __y %= 64;
  if (__y == 0)
    return __x;
  return (__x >> __y) | (__x << (64 - __y));
}

static __inline__ unsigned long __attribute__((__always_inline__, __nodebug__))
__rorl(unsigned long __x, uint32_t __y) {
#if __SIZEOF_LONG__ == 4
  return __ror(__x, __y);
#else
  return __rorll(__x, __y);
#endif
}

/* CLZ */
static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__clz(uint32_t __t) {
  return __builtin_arm_clz(__t);
}

static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__clzl(unsigned long __t) {
#if __SIZEOF_LONG__ == 4
  return __builtin_arm_clz(__t);
#else
  return __builtin_arm_clz64(__t);
#endif
}

static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__clzll(uint64_t __t) {
  return __builtin_arm_clz64(__t);
}

/* CLS */
static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__cls(uint32_t __t) {
  return __builtin_arm_cls(__t);
}

static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__clsl(unsigned long __t) {
#if __SIZEOF_LONG__ == 4
  return __builtin_arm_cls(__t);
#else
  return __builtin_arm_cls64(__t);
#endif
}

static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__clsll(uint64_t __t) {
  return __builtin_arm_cls64(__t);
}

/* REV */
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__rev(uint32_t __t) {
  return __builtin_bswap32(__t);
}

static __inline__ unsigned long __attribute__((__always_inline__, __nodebug__))
__revl(unsigned long __t) {
#if __SIZEOF_LONG__ == 4
  return __builtin_bswap32(__t);
#else
  return __builtin_bswap64(__t);
#endif
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__revll(uint64_t __t) {
  return __builtin_bswap64(__t);
}

/* REV16 */
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__rev16(uint32_t __t) {
  return __ror(__rev(__t), 16);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__rev16ll(uint64_t __t) {
  return (((uint64_t)__rev16(__t >> 32)) << 32) |
         (uint64_t)__rev16((uint32_t)__t);
}

static __inline__ unsigned long __attribute__((__always_inline__, __nodebug__))
__rev16l(unsigned long __t) {
#if __SIZEOF_LONG__ == 4
  return __rev16(__t);
#else
  return __rev16ll(__t);
#endif
}

/* REVSH */
static __inline__ int16_t __attribute__((__always_inline__, __nodebug__))
__revsh(int16_t __t) {
  return (int16_t)__builtin_bswap16((uint16_t)__t);
}

/* RBIT */
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__rbit(uint32_t __t) {
  return __builtin_arm_rbit(__t);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__rbitll(uint64_t __t) {
  return __builtin_arm_rbit64(__t);
}

static __inline__ unsigned long __attribute__((__always_inline__, __nodebug__))
__rbitl(unsigned long __t) {
#if __SIZEOF_LONG__ == 4
  return __rbit(__t);
#else
  return __rbitll(__t);
#endif
}

/* 8.6 Floating-point data-processing intrinsics */
#if (defined(__ARM_FEATURE_DIRECTED_ROUNDING) &&                               \
     (__ARM_FEATURE_DIRECTED_ROUNDING)) &&                                     \
    (defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE)
static __inline__ double
    __attribute__((__always_inline__, __nodebug__)) __rintn(double __a) {
  return __builtin_roundeven(__a);
}

static __inline__ float __attribute__((__always_inline__, __nodebug__))
__rintnf(float __a) {
  return __builtin_roundevenf(__a);
}
#endif

/* 9.7 CRC32 intrinsics */
#if (defined(__ARM_FEATURE_CRC32) && __ARM_FEATURE_CRC32) ||                   \
    (defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE)
static __inline__ uint32_t
    __attribute__((__always_inline__, __nodebug__,
                   target("crc"))) __crc32b(uint32_t __a, uint8_t __b) {
  return __builtin_arm_crc32b(__a, __b);
}

static __inline__ uint32_t
    __attribute__((__always_inline__, __nodebug__, target("crc")))
    __crc32h(uint32_t __a, uint16_t __b) {
  return __builtin_arm_crc32h(__a, __b);
}

static __inline__ uint32_t
    __attribute__((__always_inline__, __nodebug__, target("crc")))
    __crc32w(uint32_t __a, uint32_t __b) {
  return __builtin_arm_crc32w(__a, __b);
}

static __inline__ uint32_t
    __attribute__((__always_inline__, __nodebug__, target("crc")))
    __crc32d(uint32_t __a, uint64_t __b) {
  return __builtin_arm_crc32d(__a, __b);
}

static __inline__ uint32_t
    __attribute__((__always_inline__, __nodebug__, target("crc")))
    __crc32cb(uint32_t __a, uint8_t __b) {
  return __builtin_arm_crc32cb(__a, __b);
}

static __inline__ uint32_t
    __attribute__((__always_inline__, __nodebug__, target("crc")))
    __crc32ch(uint32_t __a, uint16_t __b) {
  return __builtin_arm_crc32ch(__a, __b);
}

static __inline__ uint32_t
    __attribute__((__always_inline__, __nodebug__, target("crc")))
    __crc32cw(uint32_t __a, uint32_t __b) {
  return __builtin_arm_crc32cw(__a, __b);
}

static __inline__ uint32_t
    __attribute__((__always_inline__, __nodebug__, target("crc")))
    __crc32cd(uint32_t __a, uint64_t __b) {
  return __builtin_arm_crc32cd(__a, __b);
}
#endif

/* Armv8.3-A Javascript conversion intrinsic */
#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__,
                                         target("v8.3a"))) __jcvt(double __a) {
  return __builtin_arm_jcvt(__a);
}
#endif

/* Armv8.5-A FP rounding intrinsics */
#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
static __inline__ float __attribute__((__always_inline__, __nodebug__,
                                       target("v8.5a"))) __rint32zf(float __a) {
  return __builtin_arm_rint32zf(__a);
}

static __inline__ double
    __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
    __rint32z(double __a) {
  return __builtin_arm_rint32z(__a);
}

static __inline__ float
    __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
    __rint64zf(float __a) {
  return __builtin_arm_rint64zf(__a);
}

static __inline__ double
    __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
    __rint64z(double __a) {
  return __builtin_arm_rint64z(__a);
}

static __inline__ float
    __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
    __rint32xf(float __a) {
  return __builtin_arm_rint32xf(__a);
}

static __inline__ double
    __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
    __rint32x(double __a) {
  return __builtin_arm_rint32x(__a);
}

static __inline__ float
    __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
    __rint64xf(float __a) {
  return __builtin_arm_rint64xf(__a);
}

static __inline__ double
    __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
    __rint64x(double __a) {
  return __builtin_arm_rint64x(__a);
}
#endif

/* Armv8.7-A load/store 64-byte intrinsics */
#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
typedef struct {
  uint64_t val[8];
} data512_t;

static __inline__ data512_t
    __attribute__((__always_inline__, __nodebug__, target("ls64")))
    __arm_ld64b(const void *__addr) {
  data512_t __value;
  __builtin_arm_ld64b(__addr, __value.val);
  return __value;
}
static __inline__ void
    __attribute__((__always_inline__, __nodebug__, target("ls64")))
    __arm_st64b(void *__addr, data512_t __value) {
  __builtin_arm_st64b(__addr, __value.val);
}
static __inline__ uint64_t
    __attribute__((__always_inline__, __nodebug__, target("ls64")))
    __arm_st64bv(void *__addr, data512_t __value) {
  return __builtin_arm_st64bv(__addr, __value.val);
}
static __inline__ uint64_t
    __attribute__((__always_inline__, __nodebug__, target("ls64")))
    __arm_st64bv0(void *__addr, data512_t __value) {
  return __builtin_arm_st64bv0(__addr, __value.val);
}
#endif

/* 10.1 Special register intrinsics */
#define __arm_rsr(sysreg) __builtin_arm_rsr(sysreg)
#define __arm_rsr64(sysreg) __builtin_arm_rsr64(sysreg)
#define __arm_rsr128(sysreg) __builtin_arm_rsr128(sysreg)
#define __arm_rsrp(sysreg) __builtin_arm_rsrp(sysreg)
#define __arm_rsrf(sysreg) __builtin_bit_cast(float, __arm_rsr(sysreg))
#define __arm_rsrf64(sysreg) __builtin_bit_cast(double, __arm_rsr64(sysreg))
#define __arm_wsr(sysreg, v) __builtin_arm_wsr(sysreg, v)
#define __arm_wsr64(sysreg, v) __builtin_arm_wsr64(sysreg, v)
#define __arm_wsr128(sysreg, v) __builtin_arm_wsr128(sysreg, v)
#define __arm_wsrp(sysreg, v) __builtin_arm_wsrp(sysreg, v)
#define __arm_wsrf(sysreg, v) __arm_wsr(sysreg, __builtin_bit_cast(uint32_t, v))
#define __arm_wsrf64(sysreg, v)                                                \
  __arm_wsr64(sysreg, __builtin_bit_cast(uint64_t, v))

/* Memory Tagging Extensions (MTE) Intrinsics */
#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
#define __arm_mte_create_random_tag(__ptr, __mask)                             \
  __builtin_arm_irg(__ptr, __mask)
#define __arm_mte_increment_tag(__ptr, __tag_offset)                           \
  __builtin_arm_addg(__ptr, __tag_offset)
#define __arm_mte_exclude_tag(__ptr, __excluded)                               \
  __builtin_arm_gmi(__ptr, __excluded)
#define __arm_mte_get_tag(__ptr) __builtin_arm_ldg(__ptr)
#define __arm_mte_set_tag(__ptr) __builtin_arm_stg(__ptr)
#define __arm_mte_ptrdiff(__ptra, __ptrb) __builtin_arm_subp(__ptra, __ptrb)

/* Memory Operations Intrinsics */
#define __arm_mops_memset_tag(__tagged_address, __value, __size)               \
  __builtin_arm_mops_memset_tag(__tagged_address, __value, __size)
#endif

/* Transactional Memory Extension (TME) Intrinsics */
#if defined(__ARM_FEATURE_TME) && __ARM_FEATURE_TME

#define _TMFAILURE_REASON 0x00007fffu
#define _TMFAILURE_RTRY 0x00008000u
#define _TMFAILURE_CNCL 0x00010000u
#define _TMFAILURE_MEM 0x00020000u
#define _TMFAILURE_IMP 0x00040000u
#define _TMFAILURE_ERR 0x00080000u
#define _TMFAILURE_SIZE 0x00100000u
#define _TMFAILURE_NEST 0x00200000u
#define _TMFAILURE_DBG 0x00400000u
#define _TMFAILURE_INT 0x00800000u
#define _TMFAILURE_TRIVIAL 0x01000000u

#define __tstart() __builtin_arm_tstart()
#define __tcommit() __builtin_arm_tcommit()
#define __tcancel(__arg) __builtin_arm_tcancel(__arg)
#define __ttest() __builtin_arm_ttest()

#endif /* __ARM_FEATURE_TME */

/* Armv8.5-A Random number generation intrinsics */
#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
static __inline__ int __attribute__((__always_inline__, __nodebug__,
                                     target("rand"))) __rndr(uint64_t *__p) {
  return __builtin_arm_rndr(__p);
}
static __inline__ int
    __attribute__((__always_inline__, __nodebug__, target("rand")))
    __rndrrs(uint64_t *__p) {
  return __builtin_arm_rndrrs(__p);
}
#endif

#endif /* __ARM_ACLE_H */
