/*===- APInt.c - Arbitrary precision integer (pure C) -----------*- C -*-===*/
#include "include/csupport/lapint.h"
#include "include/csupport/types.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define APINT_WORD_SIZE 64
#define APINT_BITS_PER_WORD ((unsigned)APINT_WORD_SIZE)

static void csupport_apint_negate(uint64_t *dst, const uint64_t *src,
                                  unsigned words) {
  uint64_t carry = 1;
  for (unsigned i = 0; i < words; i++) {
    uint64_t val = ~src[i] + carry;
    carry = (~src[i] == UINT64_MAX && carry) ? 1 : 0;
    dst[i] = val;
  }
}

unsigned csupport_apint_count_leading_zeros(const uint64_t *data,
                                            unsigned words,
                                            unsigned bit_width) {
  unsigned count = 0;
  for (int i = (int)words - 1; i >= 0; i--) {
    if (data[i] == 0) { count += 64; continue; }
    count += (unsigned)__builtin_clzll(data[i]);
    break;
  }
  unsigned excess = words * 64 - bit_width;
  return count > excess ? count - excess : 0;
}

unsigned csupport_apint_count_leading_ones(const uint64_t *data,
                                           unsigned words,
                                           unsigned bit_width) {
  unsigned high_bits = bit_width % 64;
  unsigned shift;
  if (!high_bits) { high_bits = 64; shift = 0; }
  else { shift = 64 - high_bits; }
  int i = (int)words - 1;
  uint64_t v = data[i] << shift;
  unsigned count = v == UINT64_MAX ? 64 : (unsigned)__builtin_clzll(~v);
  if (count == high_bits) {
    for (i--; i >= 0; --i) {
      if (data[i] == UINT64_MAX) count += 64;
      else { count += (unsigned)__builtin_clzll(~data[i]); break; }
    }
  }
  return count;
}

unsigned csupport_apint_count_trailing_zeros(const uint64_t *data,
                                             unsigned words,
                                             unsigned bit_width) {
  unsigned count = 0;
  for (unsigned i = 0; i < words; i++) {
    if (data[i] == 0) { count += 64; continue; }
    count += (unsigned)__builtin_ctzll(data[i]);
    break;
  }
  return count < bit_width ? count : bit_width;
}

unsigned csupport_apint_count_trailing_ones(const uint64_t *data,
                                            unsigned words) {
  unsigned count = 0;
  for (unsigned i = 0; i < words; i++) {
    if (data[i] == UINT64_MAX) { count += 64; continue; }
    count += (unsigned)__builtin_ctzll(~data[i]);
    break;
  }
  return count;
}

unsigned csupport_apint_popcount(const uint64_t *data, unsigned words) {
  unsigned count = 0;
  for (unsigned i = 0; i < words; i++)
    count += (unsigned)__builtin_popcountll(data[i]);
  return count;
}

void csupport_apint_and_assign(uint64_t *dst, const uint64_t *rhs,
                                unsigned words) {
  for (unsigned i = 0; i < words; i++) dst[i] &= rhs[i];
}

void csupport_apint_or_assign(uint64_t *dst, const uint64_t *rhs,
                               unsigned words) {
  for (unsigned i = 0; i < words; i++) dst[i] |= rhs[i];
}

void csupport_apint_xor_assign(uint64_t *dst, const uint64_t *rhs,
                                unsigned words) {
  for (unsigned i = 0; i < words; i++) dst[i] ^= rhs[i];
}

void csupport_apint_flip_all(uint64_t *dst, unsigned words) {
  for (unsigned i = 0; i < words; i++) dst[i] ^= UINT64_MAX;
}

int csupport_apint_intersects(const uint64_t *lhs, const uint64_t *rhs,
                               unsigned words) {
  for (unsigned i = 0; i < words; i++)
    if ((lhs[i] & rhs[i]) != 0) return 1;
  return 0;
}

int csupport_apint_is_subset_of(const uint64_t *lhs, const uint64_t *rhs,
                                 unsigned words) {
  for (unsigned i = 0; i < words; i++)
    if ((lhs[i] & ~rhs[i]) != 0) return 0;
  return 1;
}

void csupport_apint_set_bits(uint64_t *dst, unsigned lo_bit, unsigned hi_bit) {
  if (lo_bit == hi_bit)
    return;
  unsigned lo_word = lo_bit / 64;
  unsigned hi_word = hi_bit / 64;
  uint64_t lo_mask = UINT64_MAX << (lo_bit % 64);
  unsigned hi_shift = hi_bit % 64;
  if (hi_shift != 0) {
    uint64_t hi_mask = UINT64_MAX >> (64 - hi_shift);
    if (hi_word == lo_word)
      lo_mask &= hi_mask;
    else
      dst[hi_word] |= hi_mask;
  }
  dst[lo_word] |= lo_mask;
  for (unsigned w = lo_word + 1; w < hi_word; ++w)
    dst[w] = UINT64_MAX;
}

unsigned csupport_apint_sufficient_bits(const char *str, unsigned len,
                                        unsigned radix) {
  unsigned is_negative = 0;
  if (len > 0 && (str[0] == '-' || str[0] == '+')) {
    is_negative = (str[0] == '-');
    len--;
  }
  if (radix == 2) return len + is_negative;
  if (radix == 8) return len * 3 + is_negative;
  if (radix == 16) return len * 4 + is_negative;
  if (radix == 10) return (len == 1 ? 4 : len * 64 / 18) + is_negative;
  return (len == 1 ? 7 : len * 16 / 3) + is_negative;
}

void csupport_apint_shift_left(uint64_t *dst, const uint64_t *src,
                               unsigned words, unsigned shift) {
  if (shift == 0) { if (dst != src) memcpy(dst, src, words * 8); return; }
  unsigned word_shift = shift / 64;
  if (word_shift >= words) { memset(dst, 0, words * 8); return; }
  unsigned bit_shift = shift % 64;
  if (bit_shift == 0) {
    memmove(dst + word_shift, src, (words - word_shift) * 8);
  } else {
    for (int i = (int)words - 1; i >= (int)word_shift; i--) {
      dst[i] = src[i - word_shift] << bit_shift;
      if (i > (int)word_shift)
        dst[i] |= src[i - word_shift - 1] >> (64 - bit_shift);
    }
  }
  memset(dst, 0, word_shift * 8);
}

void csupport_apint_shift_right_logical(uint64_t *dst, const uint64_t *src,
                                        unsigned words, unsigned shift) {
  if (shift == 0) { if (dst != src) memcpy(dst, src, words * 8); return; }
  unsigned word_shift = shift / 64;
  if (word_shift >= words) { memset(dst, 0, words * 8); return; }
  unsigned bit_shift = shift % 64;
  unsigned words_to_move = words - word_shift;
  if (bit_shift == 0) {
    memmove(dst, src + word_shift, words_to_move * 8);
  } else {
    for (unsigned i = 0; i < words_to_move; i++) {
      dst[i] = src[i + word_shift] >> bit_shift;
      if (i + 1 < words_to_move)
        dst[i] |= src[i + word_shift + 1] << (64 - bit_shift);
    }
  }
  memset(dst + words_to_move, 0, word_shift * 8);
}

/* --- tc* bignum operations (match LLVM APInt::tc* semantics) --- */

void csupport_apint_tc_set(uint64_t *dst, uint64_t part, unsigned parts) {
  assert(parts > 0);
  dst[0] = part;
  for (unsigned i = 1; i < parts; i++) dst[i] = 0;
}

void csupport_apint_tc_assign(uint64_t *dst, const uint64_t *src,
                               unsigned parts) {
  for (unsigned i = 0; i < parts; i++) dst[i] = src[i];
}

int csupport_apint_tc_is_zero(const uint64_t *src, unsigned parts) {
  for (unsigned i = 0; i < parts; i++)
    if (src[i]) return 0;
  return 1;
}

int csupport_apint_tc_extract_bit(const uint64_t *parts, unsigned bit) {
  return (parts[bit / 64] & ((uint64_t)1 << (bit % 64))) != 0;
}

void csupport_apint_tc_set_bit(uint64_t *parts, unsigned bit) {
  parts[bit / 64] |= (uint64_t)1 << (bit % 64);
}

void csupport_apint_tc_clear_bit(uint64_t *parts, unsigned bit) {
  parts[bit / 64] &= ~((uint64_t)1 << (bit % 64));
}

unsigned csupport_apint_tc_lsb(const uint64_t *parts, unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    if (parts[i] != 0) {
      unsigned lsb = (unsigned)__builtin_ctzll(parts[i]);
      return lsb + i * APINT_BITS_PER_WORD;
    }
  }
  return UINT_MAX;
}

unsigned csupport_apint_tc_msb(const uint64_t *parts, unsigned n) {
  unsigned i = n;
  while (i > 0) {
    --i;
    if (parts[i] != 0) {
      unsigned msb = 63 - (unsigned)__builtin_clzll(parts[i]);
      return msb + i * APINT_BITS_PER_WORD;
    }
  }
  return UINT_MAX;
}

uint64_t csupport_apint_tc_add(uint64_t *dst, const uint64_t *rhs,
                                uint64_t c, unsigned parts) {
  assert(c <= 1);
  for (unsigned i = 0; i < parts; i++) {
    uint64_t l = dst[i];
    if (c) {
      dst[i] += rhs[i] + 1;
      c = (dst[i] <= l) ? 1 : 0;
    } else {
      dst[i] += rhs[i];
      c = (dst[i] < l) ? 1 : 0;
    }
  }
  return c;
}

uint64_t csupport_apint_tc_add_part(uint64_t *dst, uint64_t src,
                                     unsigned parts) {
  for (unsigned i = 0; i < parts; ++i) {
    dst[i] += src;
    if (dst[i] >= src) return 0;
    src = 1;
  }
  return 1;
}

uint64_t csupport_apint_tc_subtract(uint64_t *dst, const uint64_t *rhs,
                                     uint64_t c, unsigned parts) {
  assert(c <= 1);
  for (unsigned i = 0; i < parts; i++) {
    uint64_t l = dst[i];
    if (c) {
      dst[i] -= rhs[i] + 1;
      c = (dst[i] >= l) ? 1 : 0;
    } else {
      dst[i] -= rhs[i];
      c = (dst[i] > l) ? 1 : 0;
    }
  }
  return c;
}

uint64_t csupport_apint_tc_subtract_part(uint64_t *dst, uint64_t src,
                                          unsigned parts) {
  for (unsigned i = 0; i < parts; ++i) {
    uint64_t d = dst[i];
    dst[i] -= src;
    if (src <= d) return 0;
    src = 1;
  }
  return 1;
}

void csupport_apint_tc_complement(uint64_t *dst, unsigned parts) {
  for (unsigned i = 0; i < parts; i++) dst[i] = ~dst[i];
}

void csupport_apint_tc_negate(uint64_t *dst, unsigned parts) {
  csupport_apint_tc_complement(dst, parts);
  csupport_apint_tc_add_part(dst, 1, parts);
}

int csupport_apint_tc_compare(const uint64_t *lhs, const uint64_t *rhs,
                               unsigned parts) {
  unsigned i = parts;
  while (i > 0) {
    --i;
    if (lhs[i] != rhs[i])
      return (lhs[i] > rhs[i]) ? 1 : -1;
  }
  return 0;
}

/* ---- Multiply / Divide kernels ---- */

static inline uint64_t apint_low_half(uint64_t part) {
  return part & 0xFFFFFFFFULL;
}

static inline uint64_t apint_high_half(uint64_t part) {
  return part >> 32;
}

int csupport_apint_tc_multiply_part(uint64_t *dst, const uint64_t *src,
                                    uint64_t multiplier, uint64_t carry,
                                    unsigned src_parts, unsigned dst_parts,
                                    int add) {
  assert(dst <= src || dst >= src + src_parts);
  assert(dst_parts <= src_parts + 1);

  unsigned n = dst_parts < src_parts ? dst_parts : src_parts;

  for (unsigned i = 0; i < n; i++) {
    uint64_t src_part = src[i];
    uint64_t low, mid, high;
    if (multiplier == 0 || src_part == 0) {
      low = carry;
      high = 0;
    } else {
      low = apint_low_half(src_part) * apint_low_half(multiplier);
      high = apint_high_half(src_part) * apint_high_half(multiplier);

      mid = apint_low_half(src_part) * apint_high_half(multiplier);
      high += apint_high_half(mid);
      mid <<= 32;
      if (low + mid < low) high++;
      low += mid;

      mid = apint_high_half(src_part) * apint_low_half(multiplier);
      high += apint_high_half(mid);
      mid <<= 32;
      if (low + mid < low) high++;
      low += mid;

      if (low + carry < low) high++;
      low += carry;
    }

    if (add) {
      if (low + dst[i] < low) high++;
      dst[i] += low;
    } else {
      dst[i] = low;
    }
    carry = high;
  }

  if (src_parts < dst_parts) {
    assert(src_parts + 1 == dst_parts);
    dst[src_parts] = carry;
    return 0;
  }

  if (carry) return 1;

  if (multiplier)
    for (unsigned i = dst_parts; i < src_parts; i++)
      if (src[i]) return 1;

  return 0;
}

int csupport_apint_tc_multiply(uint64_t *dst, const uint64_t *lhs,
                                const uint64_t *rhs, unsigned parts) {
  assert(dst != lhs && dst != rhs);

  int overflow = 0;
  csupport_apint_tc_set(dst, 0, parts);

  for (unsigned i = 0; i < parts; i++)
    overflow |= csupport_apint_tc_multiply_part(
        &dst[i], lhs, rhs[i], 0, parts, parts - i, 1);

  return overflow;
}

void csupport_apint_tc_full_multiply(uint64_t *dst, const uint64_t *lhs,
                                     const uint64_t *rhs,
                                     unsigned lhs_parts, unsigned rhs_parts) {
  if (lhs_parts > rhs_parts) {
    csupport_apint_tc_full_multiply(dst, rhs, lhs, rhs_parts, lhs_parts);
    return;
  }

  assert(dst != lhs && dst != rhs);

  csupport_apint_tc_set(dst, 0, rhs_parts);

  for (unsigned i = 0; i < lhs_parts; i++)
    csupport_apint_tc_multiply_part(
        &dst[i], rhs, lhs[i], 0, rhs_parts, rhs_parts + 1, 1);
}

int csupport_apint_tc_divide(uint64_t *lhs, const uint64_t *rhs,
                              uint64_t *remainder, uint64_t *scratch,
                              unsigned parts) {
  assert(lhs != remainder && lhs != scratch && remainder != scratch);

  unsigned msb = csupport_apint_tc_msb(rhs, parts);
  if (msb == UINT_MAX) return 1; /* division by zero */

  unsigned shift_count = parts * APINT_BITS_PER_WORD - (msb + 1);
  unsigned n = shift_count / APINT_BITS_PER_WORD;
  uint64_t mask = (uint64_t)1 << (shift_count % APINT_BITS_PER_WORD);

  csupport_apint_tc_assign(scratch, rhs, parts);
  csupport_apint_shift_left(scratch, scratch, parts, shift_count);
  csupport_apint_tc_assign(remainder, lhs, parts);
  csupport_apint_tc_set(lhs, 0, parts);

  for (;;) {
    int cmp = csupport_apint_tc_compare(remainder, scratch, parts);
    if (cmp >= 0) {
      csupport_apint_tc_subtract(remainder, scratch, 0, parts);
      lhs[n] |= mask;
    }

    if (shift_count == 0) break;
    shift_count--;
    csupport_apint_shift_right_logical(scratch, scratch, parts, 1);
    mask >>= 1;
    if (mask == 0) {
      mask = (uint64_t)1 << (APINT_BITS_PER_WORD - 1);
      n--;
    }
  }

  return 0;
}

/* ---- Knuth division (Algorithm D from TAOCP Vol 2, 4.3.1) ---- */

static inline uint32_t lo32(uint64_t v) { return (uint32_t)v; }
static inline uint32_t hi32(uint64_t v) { return (uint32_t)(v >> 32); }
static inline uint64_t make64(uint32_t hi, uint32_t lo) {
  return ((uint64_t)hi << 32) | (uint64_t)lo;
}

void csupport_knuth_div(uint32_t *u, uint32_t *v, uint32_t *q, uint32_t *r,
                        unsigned m, unsigned n) {
  assert(u && v && q);
  assert(u != v && u != q && v != q);
  assert(n > 1);

  const uint64_t b = (uint64_t)1 << 32;

  unsigned shift = (unsigned)__builtin_clz(v[n - 1]);
  uint32_t v_carry = 0, u_carry = 0;
  if (shift) {
    for (unsigned i = 0; i < m + n; ++i) {
      uint32_t u_tmp = u[i] >> (32 - shift);
      u[i] = (u[i] << shift) | u_carry;
      u_carry = u_tmp;
    }
    for (unsigned i = 0; i < n; ++i) {
      uint32_t v_tmp = v[i] >> (32 - shift);
      v[i] = (v[i] << shift) | v_carry;
      v_carry = v_tmp;
    }
  }
  u[m + n] = u_carry;

  int j = (int)m;
  do {
    uint64_t dividend = make64(u[j + n], u[j + n - 1]);
    uint64_t qp = dividend / v[n - 1];
    uint64_t rp = dividend % v[n - 1];
    if (qp == b || qp * v[n - 2] > b * rp + u[j + n - 2]) {
      qp--;
      rp += v[n - 1];
      if (rp < b && (qp == b || qp * v[n - 2] > b * rp + u[j + n - 2]))
        qp--;
    }

    int64_t borrow = 0;
    for (unsigned i = 0; i < n; ++i) {
      uint64_t p = (uint64_t)qp * (uint64_t)v[i];
      int64_t subres = (int64_t)u[j + i] - borrow - lo32(p);
      u[j + i] = lo32((uint64_t)subres);
      borrow = (int64_t)(hi32(p) - hi32((uint64_t)subres));
    }
    int is_neg = (int64_t)u[j + n] < borrow;
    u[j + n] -= lo32((uint64_t)borrow);

    q[j] = lo32(qp);
    if (is_neg) {
      q[j]--;
      int carry = 0;
      for (unsigned i = 0; i < n; i++) {
        uint32_t limit = u[j + i] < v[i] ? u[j + i] : v[i];
        u[j + i] += v[i] + carry;
        carry = u[j + i] < limit || (carry && u[j + i] == limit);
      }
      u[j + n] += (uint32_t)carry;
    }
  } while (--j >= 0);

  if (r) {
    if (shift) {
      uint32_t carry = 0;
      for (int i = (int)n - 1; i >= 0; i--) {
        r[i] = (u[i] >> shift) | carry;
        carry = u[i] << (32 - shift);
      }
    } else {
      for (int i = (int)n - 1; i >= 0; i--)
        r[i] = u[i];
    }
  }
}

void csupport_apint_divide(const uint64_t *lhs, unsigned lhs_words,
                            const uint64_t *rhs, unsigned rhs_words,
                            uint64_t *quotient, uint64_t *remainder) {
  assert(lhs_words >= rhs_words);

  unsigned nn = rhs_words * 2;
  unsigned mm = (lhs_words * 2) - nn;

  uint32_t SPACE[128];
  uint32_t *U = NULL, *V = NULL, *Q = NULL, *R = NULL;
  if ((remainder ? 4 : 3) * nn + 2 * mm + 1 <= 128) {
    U = &SPACE[0];
    V = &SPACE[mm + nn + 1];
    Q = &SPACE[(mm + nn + 1) + nn];
    if (remainder)
      R = &SPACE[(mm + nn + 1) + nn + (mm + nn)];
  } else {
    U = (uint32_t *)malloc((mm + nn + 1) * sizeof(uint32_t));
    V = (uint32_t *)malloc(nn * sizeof(uint32_t));
    Q = (uint32_t *)malloc((mm + nn) * sizeof(uint32_t));
    if (remainder)
      R = (uint32_t *)malloc(nn * sizeof(uint32_t));
  }

  memset(U, 0, (mm + nn + 1) * sizeof(uint32_t));
  for (unsigned i = 0; i < lhs_words; ++i) {
    U[i * 2] = lo32(lhs[i]);
    U[i * 2 + 1] = hi32(lhs[i]);
  }
  U[mm + nn] = 0;

  memset(V, 0, nn * sizeof(uint32_t));
  for (unsigned i = 0; i < rhs_words; ++i) {
    V[i * 2] = lo32(rhs[i]);
    V[i * 2 + 1] = hi32(rhs[i]);
  }

  memset(Q, 0, (mm + nn) * sizeof(uint32_t));
  if (R) memset(R, 0, nn * sizeof(uint32_t));

  for (unsigned i = nn; i > 0 && V[i - 1] == 0; i--) { nn--; mm++; }
  for (unsigned i = mm + nn; i > 0 && U[i - 1] == 0; i--) {
    if (mm == 0) break;
    mm--;
  }

  assert(nn != 0);
  if (nn == 1) {
    uint32_t divisor = V[0];
    uint32_t rem = 0;
    for (int i = (int)mm; i >= 0; i--) {
      uint64_t partial = make64(rem, U[i]);
      if (partial == 0) { Q[i] = 0; rem = 0; }
      else if (partial < divisor) { Q[i] = 0; rem = lo32(partial); }
      else if (partial == divisor) { Q[i] = 1; rem = 0; }
      else { Q[i] = lo32(partial / divisor); rem = lo32(partial - (uint64_t)Q[i] * divisor); }
    }
    if (R) R[0] = rem;
  } else {
    csupport_knuth_div(U, V, Q, R, mm, nn);
  }

  if (quotient) {
    for (unsigned i = 0; i < lhs_words; ++i)
      quotient[i] = make64(Q[i * 2 + 1], Q[i * 2]);
  }
  if (remainder) {
    for (unsigned i = 0; i < rhs_words; ++i)
      remainder[i] = make64(R[i * 2 + 1], R[i * 2]);
  }

  if (U != &SPACE[0]) {
    free(U); free(V); free(Q); free(R);
  }
}

/* ---- Byte swap / reverse bits ---- */

void csupport_apint_byte_swap(uint64_t *dst, const uint64_t *src,
                               unsigned bit_width) {
  unsigned num_words = (bit_width + 63) / 64;
  unsigned byte_count = (bit_width + 7) / 8;
  const uint8_t *in = (const uint8_t *)src;
  uint8_t *out = (uint8_t *)dst;
  uint8_t tmp[256];
  assert(byte_count <= 256);
  for (unsigned i = 0; i < byte_count; i++)
    tmp[i] = in[byte_count - 1 - i];
  memset(dst, 0, num_words * sizeof(uint64_t));
  memcpy(out, tmp, byte_count);
}

static uint64_t reverse_bits_64(uint64_t v) {
  v = ((v >> 1) & 0x5555555555555555ULL) | ((v & 0x5555555555555555ULL) << 1);
  v = ((v >> 2) & 0x3333333333333333ULL) | ((v & 0x3333333333333333ULL) << 2);
  v = ((v >> 4) & 0x0F0F0F0F0F0F0F0FULL) | ((v & 0x0F0F0F0F0F0F0F0FULL) << 4);
  v = ((v >> 8) & 0x00FF00FF00FF00FFULL) | ((v & 0x00FF00FF00FF00FFULL) << 8);
  v = ((v >> 16) & 0x0000FFFF0000FFFFULL) | ((v & 0x0000FFFF0000FFFFULL) << 16);
  v = (v >> 32) | (v << 32);
  return v;
}

void csupport_apint_reverse_bits(uint64_t *dst, const uint64_t *src,
                                  unsigned bit_width) {
  unsigned num_words = (bit_width + 63) / 64;
  unsigned total_bits = num_words * 64;
  unsigned excess = total_bits - bit_width;

  for (unsigned i = 0; i < num_words; i++)
    dst[i] = reverse_bits_64(src[num_words - 1 - i]);

  if (excess > 0)
    csupport_apint_shift_right_logical(dst, dst, num_words, excess);
}

/* ---- String conversion ---- */

unsigned csupport_apint_get_digit(char c, unsigned radix) {
  unsigned r;
  if (radix == 16 || radix == 36) {
    r = (unsigned)(c - '0');
    if (r <= 9) return r;
    r = (unsigned)(c - 'A');
    if (r <= radix - 11U) return r + 10;
    r = (unsigned)(c - 'a');
    if (r <= radix - 11U) return r + 10;
    radix = 10;
  }
  r = (unsigned)(c - '0');
  if (r < radix) return r;
  return UINT_MAX;
}

void csupport_apint_to_string(const uint64_t *data, unsigned bit_width,
                               int is_signed, unsigned radix,
                               char *buf, size_t buflen, size_t *out_len) {
  assert(radix >= 2 && radix <= 36);
  assert(buflen > 0);

  unsigned num_words = (bit_width + APINT_BITS_PER_WORD - 1) / APINT_BITS_PER_WORD;

  if (bit_width == 0 || (num_words == 1 && data[0] == 0)) {
    buf[0] = '0';
    *out_len = 1;
    return;
  }

  int negative = 0;
  uint64_t tmp_storage[128];
  uint64_t *tmp = tmp_storage;
  if (num_words > 128)
    tmp = (uint64_t *)malloc(num_words * sizeof(uint64_t));

  if (is_signed && (data[num_words - 1] >> ((bit_width - 1) % 64)) & 1) {
    negative = 1;
    csupport_apint_negate(tmp, data, num_words);
    uint64_t mask = (bit_width % 64 == 0) ? UINT64_MAX
                    : ((uint64_t)1 << (bit_width % 64)) - 1;
    tmp[num_words - 1] &= mask;
  } else {
    memcpy(tmp, data, num_words * sizeof(uint64_t));
  }

  size_t max_digits = bit_width + 1;
  char digits_storage[256];
  char *digits = digits_storage;
  if (max_digits > sizeof(digits_storage))
    digits = (char *)malloc(max_digits);
  int pos = 0;
  const char *digit_chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  if (num_words == 1) {
    uint64_t val = tmp[0];
    if (val == 0) {
      digits[pos++] = '0';
    } else {
      while (val > 0) {
        digits[pos++] = digit_chars[val % radix];
        val /= radix;
      }
    }
  } else {
    while (!csupport_apint_tc_is_zero(tmp, num_words)) {
      uint64_t r = 0;
      for (int i = (int)num_words - 1; i >= 0; i--) {
        __uint128_t cur = ((__uint128_t)r << 64) | tmp[i];
        tmp[i] = (uint64_t)(cur / radix);
        r = (uint64_t)(cur % radix);
      }
      digits[pos++] = digit_chars[r];
    }
  }

  size_t total = (size_t)pos + (negative ? 1 : 0);
  if (total > buflen) total = buflen;

  size_t idx = 0;
  if (negative && idx < buflen) buf[idx++] = '-';
  for (int i = pos - 1; i >= 0 && idx < buflen; i--)
    buf[idx++] = digits[i];
  *out_len = idx;

  if (digits != digits_storage) free(digits);
  if (tmp != tmp_storage) free(tmp);
}

void csupport_apint_ashr(uint64_t *dst, unsigned num_words,
                         unsigned bit_width, unsigned shift_amt) {
  if (!shift_amt) return;

  int negative = (dst[num_words - 1] >> (((bit_width - 1) % 64))) & 1;

  if (shift_amt >= bit_width) {
    memset(dst, negative ? 0xFF : 0, num_words * sizeof(uint64_t));
    unsigned used = bit_width % 64;
    if (used > 0 && num_words > 0)
      dst[num_words - 1] &= ((uint64_t)1 << used) - 1;
    return;
  }

  unsigned word_shift = shift_amt / 64;
  unsigned bit_shift = shift_amt % 64;

  unsigned words_to_move = num_words - word_shift;
  if (words_to_move != 0) {
    /* Sign extend the last word's active bits */
    unsigned last_word_bits = ((bit_width - 1) % 64) + 1;
    if (last_word_bits < 64) {
      int64_t s = (int64_t)(dst[num_words - 1] << (64 - last_word_bits));
      dst[num_words - 1] = (uint64_t)(s >> (64 - last_word_bits));
    }

    if (bit_shift == 0) {
      for (unsigned i = 0; i < words_to_move; i++)
        dst[i] = dst[i + word_shift];
    } else {
      for (unsigned i = 0; i + 1 < words_to_move; i++)
        dst[i] = (dst[i + word_shift] >> bit_shift) |
                 (dst[i + word_shift + 1] << (64 - bit_shift));
      dst[words_to_move - 1] = dst[word_shift + words_to_move - 1] >> bit_shift;
      /* Sign extend the result */
      if (64 - bit_shift < 64) {
        int64_t s = (int64_t)(dst[words_to_move - 1] << bit_shift);
        dst[words_to_move - 1] = (uint64_t)(s >> bit_shift);
      }
    }
  }

  memset(dst + words_to_move, negative ? 0xFF : 0,
         word_shift * sizeof(uint64_t));

  /* Clear unused bits in the top word */
  unsigned used = bit_width % 64;
  if (used > 0 && num_words > 0)
    dst[num_words - 1] &= ((uint64_t)1 << used) - 1;
}

void csupport_apint_insert_bits(uint64_t *dst, unsigned dst_words,
                                const uint64_t *src, unsigned src_words,
                                unsigned src_bit_width, unsigned bit_position) {
  if (src_bit_width == 0) return;

  unsigned lo_bit = bit_position % 64;
  unsigned lo_word = bit_position / 64;
  unsigned hi1_word = (bit_position + src_bit_width - 1) / 64;

  if (lo_word == hi1_word) {
    uint64_t mask = UINT64_MAX >> (64 - src_bit_width);
    uint64_t val = src_words == 1 ? src[0] : src[0];
    dst[lo_word] &= ~(mask << lo_bit);
    dst[lo_word] |= (val << lo_bit);
    return;
  }

  if (lo_bit == 0) {
    unsigned whole_words = src_bit_width / 64;
    memcpy(dst + lo_word, src, whole_words * sizeof(uint64_t));
    unsigned remaining = src_bit_width % 64;
    if (remaining != 0) {
      uint64_t mask = UINT64_MAX >> (64 - remaining);
      uint64_t val = src[whole_words < src_words ? whole_words : src_words - 1];
      dst[hi1_word] &= ~mask;
      dst[hi1_word] |= (val & mask);
    }
    return;
  }

  /* General case: set individual bits */
  for (unsigned i = 0; i < src_bit_width; i++) {
    unsigned src_word = i / 64;
    unsigned src_bit_off = i % 64;
    int bit_val = (src_word < src_words) ?
                  ((src[src_word] >> src_bit_off) & 1) : 0;
    unsigned dst_idx = bit_position + i;
    unsigned dw = dst_idx / 64;
    unsigned db = dst_idx % 64;
    if (bit_val)
      dst[dw] |= ((uint64_t)1 << db);
    else
      dst[dw] &= ~((uint64_t)1 << db);
  }
}

void csupport_apint_insert_bits64(uint64_t *dst, unsigned dst_words,
                                  uint64_t val, unsigned bit_position,
                                  unsigned num_bits) {
  uint64_t mask = (num_bits >= 64) ? UINT64_MAX : ((uint64_t)1 << num_bits) - 1;
  val &= mask;

  unsigned lo_bit = bit_position % 64;
  unsigned lo_word = bit_position / 64;
  unsigned hi_word = (bit_position + num_bits - 1) / 64;

  if (lo_word == hi_word) {
    dst[lo_word] &= ~(mask << lo_bit);
    dst[lo_word] |= val << lo_bit;
    return;
  }

  dst[lo_word] &= ~(mask << lo_bit);
  dst[lo_word] |= val << lo_bit;
  dst[hi_word] &= ~(mask >> (64 - lo_bit));
  dst[hi_word] |= val >> (64 - lo_bit);
}

void csupport_apint_extract_bits(const uint64_t *src, unsigned src_words,
                                 uint64_t *dst, unsigned dst_words,
                                 unsigned num_bits, unsigned bit_position) {
  unsigned lo_bit = bit_position % 64;
  unsigned lo_word = bit_position / 64;

  if (lo_bit == 0 && dst_words == 1) {
    dst[0] = src[lo_word];
    unsigned used = num_bits % 64;
    if (used > 0)
      dst[0] &= ((uint64_t)1 << used) - 1;
    return;
  }

  for (unsigned word = 0; word < dst_words; word++) {
    uint64_t w0 = src[lo_word + word];
    uint64_t w1 = (lo_word + word + 1 < src_words)
                  ? src[lo_word + word + 1] : 0;
    if (lo_bit == 0)
      dst[word] = w0;
    else
      dst[word] = (w0 >> lo_bit) | (w1 << (64 - lo_bit));
  }

  /* Clear unused bits in top word */
  unsigned used = num_bits % 64;
  if (used > 0 && dst_words > 0)
    dst[dst_words - 1] &= ((uint64_t)1 << used) - 1;
}

uint64_t csupport_apint_extract_bits_zext(const uint64_t *src,
                                          unsigned src_words,
                                          unsigned num_bits,
                                          unsigned bit_position) {
  uint64_t mask = (num_bits >= 64) ? UINT64_MAX : ((uint64_t)1 << num_bits) - 1;
  unsigned lo_bit = bit_position % 64;
  unsigned lo_word = bit_position / 64;
  unsigned hi_word = (bit_position + num_bits - 1) / 64;

  if (lo_word == hi_word)
    return (src[lo_word] >> lo_bit) & mask;

  uint64_t ret = src[lo_word] >> lo_bit;
  ret |= src[hi_word] << (64 - lo_bit);
  return ret & mask;
}

void csupport_apint_sext_words(uint64_t *dst, unsigned dst_words,
                               const uint64_t *src, unsigned src_words,
                               unsigned src_bit_width) {
  memcpy(dst, src, src_words * sizeof(uint64_t));

  /* Sign extend the last source word */
  unsigned last_bits = ((src_bit_width - 1) % 64) + 1;
  if (last_bits < 64) {
    int64_t s = (int64_t)(dst[src_words - 1] << (64 - last_bits));
    dst[src_words - 1] = (uint64_t)(s >> (64 - last_bits));
  }

  int negative = (int64_t)dst[src_words - 1] < 0;
  memset(dst + src_words, negative ? 0xFF : 0,
         (dst_words - src_words) * sizeof(uint64_t));
}

void csupport_apint_trunc_words(uint64_t *dst, const uint64_t *src,
                                unsigned dst_words, unsigned width) {
  unsigned full_words = width / 64;
  for (unsigned i = 0; i < full_words && i < dst_words; i++)
    dst[i] = src[i];
  unsigned remaining = (0 - width) % 64;
  if (remaining != 0 && full_words < dst_words)
    dst[full_words] = src[full_words] << remaining >> remaining;
}

int csupport_apint_from_string(uint64_t *dst, unsigned bit_width,
                                const char *str, size_t str_len,
                                unsigned radix) {
  assert(radix >= 2 && radix <= 36);
  unsigned num_words = (bit_width + APINT_BITS_PER_WORD - 1) / APINT_BITS_PER_WORD;
  csupport_apint_tc_set(dst, 0, num_words);

  size_t i = 0;
  int negative = 0;
  if (i < str_len && (str[i] == '-' || str[i] == '+')) {
    negative = (str[i] == '-');
    i++;
  }

  for (; i < str_len; i++) {
    unsigned digit = csupport_apint_get_digit(str[i], radix);
    if (digit == UINT_MAX) return -1;
    /* dst = dst * radix + digit */
    uint64_t carry = digit;
    for (unsigned w = 0; w < num_words; w++) {
      __uint128_t prod = (__uint128_t)dst[w] * radix + carry;
      dst[w] = (uint64_t)prod;
      carry = (uint64_t)(prod >> 64);
    }
  }

  if (negative)
    csupport_apint_tc_negate(dst, num_words);

  uint64_t mask = (bit_width % 64 == 0) ? UINT64_MAX
                  : ((uint64_t)1 << (bit_width % 64)) - 1;
  if (num_words > 0)
    dst[num_words - 1] &= mask;

  return 0;
}

/*--- multi-word negate in-place ---*/
void csupport_apint_negate_slow(uint64_t *dst, unsigned num_words,
                                 unsigned bit_width) {
  csupport_apint_tc_negate(dst, num_words);
  uint64_t mask = (bit_width % 64 == 0) ? UINT64_MAX
                  : ((uint64_t)1 << (bit_width % 64)) - 1;
  if (num_words > 0)
    dst[num_words - 1] &= mask;
}

/*--- multi-word left shift in-place ---*/
void csupport_apint_shl_slow(uint64_t *dst, const uint64_t *src,
                              unsigned num_words, unsigned shift_amt,
                              unsigned bit_width) {
  if (shift_amt >= bit_width) {
    memset(dst, 0, num_words * sizeof(uint64_t));
    return;
  }
  if (shift_amt == 0) {
    if (dst != src) memcpy(dst, src, num_words * sizeof(uint64_t));
    return;
  }
  unsigned word_shift = shift_amt / 64;
  unsigned bit_shift = shift_amt % 64;

  if (bit_shift == 0) {
    for (int i = (int)num_words - 1; i >= (int)word_shift; --i)
      dst[i] = src[i - word_shift];
  } else {
    for (int i = (int)num_words - 1; i > (int)word_shift; --i)
      dst[i] = (src[i - word_shift] << bit_shift) |
               (src[i - word_shift - 1] >> (64 - bit_shift));
    dst[word_shift] = src[0] << bit_shift;
  }
  for (unsigned i = 0; i < word_shift; i++)
    dst[i] = 0;

  uint64_t mask = (bit_width % 64 == 0) ? UINT64_MAX
                  : ((uint64_t)1 << (bit_width % 64)) - 1;
  if (num_words > 0)
    dst[num_words - 1] &= mask;
}

/*--- multi-word logical right shift in-place ---*/
void csupport_apint_lshr_slow(uint64_t *dst, const uint64_t *src,
                                unsigned num_words, unsigned shift_amt,
                                unsigned bit_width) {
  if (shift_amt >= bit_width) {
    memset(dst, 0, num_words * sizeof(uint64_t));
    return;
  }
  if (shift_amt == 0) {
    if (dst != src) memcpy(dst, src, num_words * sizeof(uint64_t));
    return;
  }
  unsigned word_shift = shift_amt / 64;
  unsigned bit_shift = shift_amt % 64;
  unsigned limit = num_words - word_shift;

  if (bit_shift == 0) {
    for (unsigned i = 0; i < limit; i++)
      dst[i] = src[i + word_shift];
  } else {
    unsigned i;
    for (i = 0; i + 1 < limit; i++)
      dst[i] = (src[i + word_shift] >> bit_shift) |
               (src[i + word_shift + 1] << (64 - bit_shift));
    dst[i] = src[i + word_shift] >> bit_shift;
  }
  for (unsigned i = limit; i < num_words; i++)
    dst[i] = 0;
}

/*--- Greatest common divisor (binary GCD / Stein's algorithm) ---*/
void csupport_apint_gcd(uint64_t *result, const uint64_t *a_in,
                         const uint64_t *b_in, unsigned num_words,
                         unsigned bit_width) {
  uint64_t a[64], b[64];
  assert(num_words <= 64);
  memcpy(a, a_in, num_words * sizeof(uint64_t));
  memcpy(b, b_in, num_words * sizeof(uint64_t));

  int a_zero = csupport_apint_tc_is_zero(a, num_words);
  int b_zero = csupport_apint_tc_is_zero(b, num_words);

  if (a_zero) { memcpy(result, b, num_words * sizeof(uint64_t)); return; }
  if (b_zero) { memcpy(result, a, num_words * sizeof(uint64_t)); return; }

  unsigned shift = 0;
  while (1) {
    unsigned a_tz = csupport_apint_count_trailing_zeros(a, num_words, bit_width);
    unsigned b_tz = csupport_apint_count_trailing_zeros(b, num_words, bit_width);
    unsigned min_tz = a_tz < b_tz ? a_tz : b_tz;
    shift += min_tz;

    csupport_apint_shift_right_logical(a, a, num_words, a_tz);
    csupport_apint_shift_right_logical(b, b, num_words, b_tz);

    if (csupport_apint_tc_compare(a, b, num_words) < 0) {
      uint64_t tmp[64];
      memcpy(tmp, a, num_words * sizeof(uint64_t));
      memcpy(a, b, num_words * sizeof(uint64_t));
      memcpy(b, tmp, num_words * sizeof(uint64_t));
    }
    csupport_apint_tc_subtract(a, b, 0, num_words);
    if (csupport_apint_tc_is_zero(a, num_words)) break;
  }

  csupport_apint_shift_left(result, b, num_words, shift);
  uint64_t mask = (bit_width % 64 == 0) ? UINT64_MAX
                  : ((uint64_t)1 << (bit_width % 64)) - 1;
  if (num_words > 0)
    result[num_words - 1] &= mask;
}

/*--- Bit rotation (left/right) ---*/
void csupport_apint_rotl(uint64_t *result, const uint64_t *src,
                          unsigned num_words, unsigned bit_width,
                          unsigned rotate_amt) {
  if (bit_width == 0 || rotate_amt == 0) {
    if (result != src) memcpy(result, src, num_words * sizeof(uint64_t));
    return;
  }
  rotate_amt %= bit_width;
  if (rotate_amt == 0) {
    if (result != src) memcpy(result, src, num_words * sizeof(uint64_t));
    return;
  }
  uint64_t hi[64], lo[64];
  assert(num_words <= 64);
  csupport_apint_shl_slow(hi, src, num_words, rotate_amt, bit_width);
  csupport_apint_lshr_slow(lo, src, num_words, bit_width - rotate_amt, bit_width);
  for (unsigned i = 0; i < num_words; i++)
    result[i] = hi[i] | lo[i];
}

void csupport_apint_rotr(uint64_t *result, const uint64_t *src,
                          unsigned num_words, unsigned bit_width,
                          unsigned rotate_amt) {
  if (bit_width == 0) {
    if (result != src) memcpy(result, src, num_words * sizeof(uint64_t));
    return;
  }
  rotate_amt %= bit_width;
  csupport_apint_rotl(result, src, num_words, bit_width,
                       bit_width - rotate_amt);
}

/*--- tcExtract: copy a range of bits from src to dst ---*/
static uint64_t low_bit_mask(unsigned bits) {
  assert(bits != 0 && bits <= 64);
  return ~(uint64_t)0 >> (64 - bits);
}

void csupport_apint_tc_extract(uint64_t *dst, unsigned dst_count,
                                const uint64_t *src, unsigned src_bits,
                                unsigned src_lsb) {
  unsigned dst_parts = (src_bits + APINT_BITS_PER_WORD - 1) / APINT_BITS_PER_WORD;
  assert(dst_parts <= dst_count);

  unsigned first_src_part = src_lsb / APINT_BITS_PER_WORD;
  csupport_apint_tc_assign(dst, src + first_src_part, dst_parts);

  unsigned shift = src_lsb % APINT_BITS_PER_WORD;
  csupport_apint_shift_right_logical(dst, dst, dst_parts, shift);

  unsigned n = dst_parts * APINT_BITS_PER_WORD - shift;
  if (n < src_bits) {
    uint64_t mask = low_bit_mask(src_bits - n);
    dst[dst_parts - 1] |=
        ((src[first_src_part + dst_parts] & mask) << (n % APINT_BITS_PER_WORD));
  } else if (n > src_bits) {
    if (src_bits % APINT_BITS_PER_WORD)
      dst[dst_parts - 1] &= low_bit_mask(src_bits % APINT_BITS_PER_WORD);
  }

  while (dst_parts < dst_count)
    dst[dst_parts++] = 0;
}

/*--- tcIncrement: add 1 to a bignum, return carry ---*/
uint64_t csupport_apint_tc_increment(uint64_t *dst, unsigned parts) {
  unsigned i;
  for (i = 0; i < parts; i++) {
    if (++dst[i] != 0)
      return 0;
  }
  return 1;
}

/*--- equalSlowCase: multi-word equality ---*/
void csupport_apint_equal_slow(const uint64_t *lhs, const uint64_t *rhs,
                                unsigned words, int *result) {
  *result = (memcmp(lhs, rhs, words * sizeof(uint64_t)) == 0) ? 1 : 0;
}

/*--- Convert multi-word APInt to double (IEEE 754) ---*/
double csupport_apint_round_to_double(const uint64_t *words,
                                       unsigned num_words,
                                       unsigned bit_width, int is_signed) {
  unsigned clz = csupport_apint_count_leading_zeros(words, num_words, bit_width);
  unsigned active_bits = bit_width - clz;

  if (active_bits <= 64) {
    if (is_signed && bit_width <= 64) {
      int64_t sext = (int64_t)(words[0] << (64 - bit_width)) >> (64 - bit_width);
      return (double)sext;
    }
    return (double)words[0];
  }

  int is_neg = is_signed && ((words[(bit_width - 1) / 64] >> ((bit_width - 1) % 64)) & 1);

  uint64_t tmp[32];
  assert(num_words <= 32);
  memcpy(tmp, words, num_words * sizeof(uint64_t));
  if (is_neg)
    csupport_apint_negate_slow(tmp, num_words, bit_width);

  clz = csupport_apint_count_leading_zeros(tmp, num_words, bit_width);
  unsigned n = bit_width - clz;

  if (n == 0) return is_neg ? -0.0 : 0.0;

  uint64_t ieee_exp = n - 1;
  if (ieee_exp > 1023) {
    union { uint64_t i; double d; } u;
    u.i = is_neg ? UINT64_C(0xFFF0000000000000) : UINT64_C(0x7FF0000000000000);
    return u.d;
  }
  uint64_t biased_exp = ieee_exp + 1023;

  unsigned hi_word = (n - 1) / 64;
  unsigned bit_in_word = (n - 1) % 64;
  uint64_t mantissa;
  if (hi_word == 0) {
    mantissa = tmp[0];
    if (n > 53)
      mantissa >>= n - 53;
    else if (n < 53)
      mantissa <<= 53 - n;
  } else if (bit_in_word >= 52) {
    mantissa = tmp[hi_word] >> (bit_in_word - 52);
  } else {
    uint64_t hibits = tmp[hi_word] << (52 - bit_in_word);
    uint64_t lobits = tmp[hi_word - 1] >> (12 + bit_in_word);
    mantissa = hibits | lobits;
  }

  uint64_t sign_bit = is_neg ? (UINT64_C(1) << 63) : 0;
  union { uint64_t i; double d; } u;
  u.i = sign_bit | (biased_exp << 52) | (mantissa & ((UINT64_C(1) << 52) - 1));
  return u.d;
}

/*--- Concatenate two APInt values ---*/
void csupport_apint_concat(uint64_t *result, unsigned result_words,
                             const uint64_t *hi, unsigned hi_bits,
                             const uint64_t *lo, unsigned lo_bits) {
  unsigned total_bits = hi_bits + lo_bits;
  unsigned lo_words = (lo_bits + 63) / 64;
  (void)total_bits;

  memset(result, 0, result_words * sizeof(uint64_t));
  memcpy(result, lo, lo_words * sizeof(uint64_t));

  uint64_t lo_mask = (lo_bits % 64 == 0) ? UINT64_MAX
                     : ((uint64_t)1 << (lo_bits % 64)) - 1;
  if (lo_words > 0)
    result[lo_words - 1] &= lo_mask;

  unsigned hi_words = (hi_bits + 63) / 64;
  uint64_t shifted_hi[64];
  assert(result_words <= 64);
  memset(shifted_hi, 0, result_words * sizeof(uint64_t));
  memcpy(shifted_hi, hi, hi_words * sizeof(uint64_t));
  csupport_apint_shift_left(shifted_hi, shifted_hi, result_words, lo_bits);
  for (unsigned i = 0; i < result_words; i++)
    result[i] |= shifted_hi[i];
}
