#ifndef __POPCNTINTRIN_H
#define __POPCNTINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__, __target__("popcnt")))

#define __DEFAULT_FN_ATTRS_CONSTEXPR __DEFAULT_FN_ATTRS

/// Counts the number of bits in the source operand having a value of 1.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> POPCNT </c> instruction.
///
/// \param __A
///    An unsigned 32-bit integer operand.
/// \returns A 32-bit integer containing the number of bits with value 1 in the
///    source operand.
static __inline__ int __DEFAULT_FN_ATTRS_CONSTEXPR
_mm_popcnt_u32(unsigned int __A) {
  return __builtin_popcount(__A);
}

#ifdef __x86_64__
/// Counts the number of bits in the source operand having a value of 1.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> POPCNT </c> instruction.
///
/// \param __A
///    An unsigned 64-bit integer operand.
/// \returns A 64-bit integer containing the number of bits with value 1 in the
///    source operand.
static __inline__ long long __DEFAULT_FN_ATTRS_CONSTEXPR
_mm_popcnt_u64(unsigned long long __A) {
  return __builtin_popcountll(__A);
}
#endif /* __x86_64__ */

#undef __DEFAULT_FN_ATTRS
#undef __DEFAULT_FN_ATTRS_CONSTEXPR

#endif /* __POPCNTINTRIN_H */
