/*===- ScaledNumber.c - Scaled number arithmetic (pure C) -------*- C -*-===*/
#include "include/csupport/lscaled_lnumber.h"
#include "include/csupport/buffer.h"
#include "include/csupport/types.h"
#include <stdint.h>
#include <string.h>
#include <assert.h>

int csupport_count_leading_zeros_64(uint64_t v) {
  if (v == 0) return 64;
  return __builtin_clzll(v);
}

int csupport_count_trailing_zeros_64(uint64_t v) {
  if (v == 0) return 64;
  return __builtin_ctzll(v);
}

void csupport_multiply_u64(uint64_t lhs, uint64_t rhs, uint64_t *hi,
                           uint64_t *lo) {
#if defined(__SIZEOF_INT128__) && !defined(_WIN32)
  __uint128_t prod = (__uint128_t)lhs * rhs;
  *lo = (uint64_t)prod;
  *hi = (uint64_t)(prod >> 64);
#else
  uint32_t a_lo = (uint32_t)lhs, a_hi = (uint32_t)(lhs >> 32);
  uint32_t b_lo = (uint32_t)rhs, b_hi = (uint32_t)(rhs >> 32);
  uint64_t p0 = (uint64_t)a_lo * b_lo;
  uint64_t p1 = (uint64_t)a_lo * b_hi;
  uint64_t p2 = (uint64_t)a_hi * b_lo;
  uint64_t p3 = (uint64_t)a_hi * b_hi;
  uint64_t mid = p1 + (p0 >> 32);
  mid += p2;
  if (mid < p2) p3 += (uint64_t)1 << 32;
  *lo = (mid << 32) | (uint32_t)p0;
  *hi = p3 + (mid >> 32);
#endif
}

uint64_t csupport_divide_u128_by_u64(uint64_t dividend_hi,
                                     uint64_t dividend_lo, uint64_t divisor,
                                     uint64_t *remainder) {
#if defined(__SIZEOF_INT128__) && !defined(_WIN32)
  __uint128_t dividend = ((__uint128_t)dividend_hi << 64) | dividend_lo;
  *remainder = (uint64_t)(dividend % divisor);
  return (uint64_t)(dividend / divisor);
#else
  assert(dividend_hi < divisor && "Overflow in division");
  if (dividend_hi == 0) {
    *remainder = dividend_lo % divisor;
    return dividend_lo / divisor;
  }
  int shift = csupport_count_leading_zeros_64(divisor);
  if (shift) {
    divisor <<= shift;
    dividend_hi = (dividend_hi << shift) | (dividend_lo >> (64 - shift));
    dividend_lo <<= shift;
  }
  uint64_t q = 0;
  *remainder = dividend_hi;
  for (int i = 63; i >= 0; --i) {
    *remainder = (*remainder << 1) | ((dividend_lo >> i) & 1);
    if (*remainder >= divisor) {
      *remainder -= divisor;
      q |= (uint64_t)1 << i;
    }
  }
  *remainder >>= shift;
  return q;
#endif
}

csupport_scaled_pair_t csupport_scaled_get_rounded64(uint64_t digits,
                                                     int16_t scale,
                                                     int round_up) {
  csupport_scaled_pair_t r;
  if (!round_up) { r.value = digits; r.exponent = scale; return r; }
  if (digits == UINT64_MAX) {
    r.value = UINT64_C(1) << 63;
    r.exponent = scale + 1;
    return r;
  }
  r.value = digits + 1;
  r.exponent = scale;
  return r;
}

csupport_scaled_pair_t csupport_scaled_get_adjusted64(uint64_t digits,
                                                      int16_t scale) {
  csupport_scaled_pair_t r;
  r.value = digits;
  r.exponent = scale;
  return r;
}

csupport_scaled_pair_t csupport_scaled_multiply64(uint64_t lhs, uint64_t rhs) {
  uint64_t upper, lower;
  csupport_multiply_u64(lhs, rhs, &upper, &lower);

  csupport_scaled_pair_t r;
  if (!upper) {
    r.value = lower;
    r.exponent = 0;
    return r;
  }

  int leading_zeros = csupport_count_leading_zeros_64(upper);
  int shift = 64 - leading_zeros;
  if (leading_zeros)
    upper = (upper << leading_zeros) | (lower >> shift);
  int round_up = shift && (lower & (UINT64_C(1) << (shift - 1)));
  return csupport_scaled_get_rounded64(upper, (int16_t)shift, round_up);
}

csupport_scaled_pair_t csupport_scaled_divide32(uint32_t dividend,
                                                uint32_t divisor) {
  assert(dividend && "expected non-zero dividend");
  assert(divisor && "expected non-zero divisor");

  uint64_t dividend64 = dividend;
  int shift = 0;
  int zeros = csupport_count_leading_zeros_64(dividend64);
  if (zeros) {
    shift -= zeros;
    dividend64 <<= zeros;
  }
  uint64_t quotient = dividend64 / divisor;
  uint64_t remainder = dividend64 % divisor;

  if (quotient > UINT32_MAX) {
    int clz = csupport_count_leading_zeros_64(quotient);
    int adj = 64 - 32 - clz;
    uint32_t q32 = (uint32_t)(quotient >> adj);
    int round = adj && (quotient & ((uint64_t)1 << (adj - 1))) != 0;
    return csupport_scaled_get_rounded64((uint64_t)q32,
                                         (int16_t)(shift + adj), round);
  }

  uint64_t half_divisor = (divisor >> 1) + (divisor & 1);
  csupport_scaled_pair_t r;
  if (remainder >= half_divisor) {
    return csupport_scaled_get_rounded64((uint64_t)(uint32_t)quotient,
                                         (int16_t)shift, 1);
  }
  r.value = (uint32_t)quotient;
  r.exponent = (int16_t)shift;
  return r;
}

csupport_scaled_pair_t csupport_scaled_divide64(uint64_t dividend,
                                                uint64_t divisor) {
  assert(dividend && "expected non-zero dividend");
  assert(divisor && "expected non-zero divisor");

  int shift = 0;
  int zeros = csupport_count_trailing_zeros_64(divisor);
  if (zeros) {
    shift -= zeros;
    divisor >>= zeros;
  }

  if (divisor == 1) {
    csupport_scaled_pair_t r = { dividend, (int16_t)shift };
    return r;
  }

  zeros = csupport_count_leading_zeros_64(dividend);
  if (zeros) {
    shift -= zeros;
    dividend <<= zeros;
  }

  uint64_t quotient = dividend / divisor;
  dividend %= divisor;

  while (!(quotient >> 63) && dividend) {
    int is_overflow = (dividend >> 63) != 0;
    dividend <<= 1;
    --shift;

    quotient <<= 1;
    if (is_overflow || divisor <= dividend) {
      quotient |= 1;
      dividend -= divisor;
    }
  }

  uint64_t half_divisor = (divisor >> 1) + (divisor & 1);
  return csupport_scaled_get_rounded64(quotient, (int16_t)shift,
                                       dividend >= half_divisor);
}

int csupport_scaled_compare(uint64_t l, uint64_t r, int scale_diff) {
  assert(scale_diff >= 0 && "wrong argument order");
  assert(scale_diff < 64 && "numbers too far apart");

  uint64_t l_adjusted = l >> scale_diff;
  if (l_adjusted < r) return -1;
  if (l_adjusted > r) return 1;
  return l > (l_adjusted << scale_diff) ? 1 : 0;
}

/* Where the number ends once the digits that say nothing are dropped.  A lone
   '.' is not left behind: "1." keeps the zero that makes it "1.0". */
static size_t scaled_significant_length(const char *str, size_t len) {
  size_t non_zero = len;
  while (non_zero > 0 && str[non_zero - 1] == '0')
    --non_zero;
  if (non_zero > 0 && str[non_zero - 1] == '.')
    ++non_zero;
  return non_zero;
}

size_t csupport_scaled_strip_trailing_zeros(const char *str, size_t len,
                                             char *out, size_t out_cap) {
  csupport_obuf_t kept = csupport_obuf(out, out_cap);
  csupport_obuf_write(&kept, str, scaled_significant_length(str, len));
  return csupport_obuf_finish(&kept);
}

/* Rounding and carrying need the whole number in hand, so the digits are laid
   out here rather than in the caller's buffer, which may be too small to hold
   them and is not this routine's to reason about.  The size is a bound, not a
   guess: the error term is multiplied by ten every round -- by five while the
   extra bits last, and there are fewer than 56 of those -- so it reaches zero
   within `width` rounds of the first kind, and `width` is the digit type's bit
   count.  That caps the fraction at 120 digits however much precision is
   asked for, and above the point a uint64 spends at most 20. */
#define SCALED_DIGITS_MAX 192

size_t csupport_scaled_format_digits(uint64_t above, uint64_t below,
                                     uint64_t extra, int extra_shift,
                                     unsigned precision, int width,
                                     char *out, size_t out_cap) {
  char digits[SCALED_DIGITS_MAX];
  csupport_obuf_t laid_out = csupport_obuf(digits, sizeof(digits));
  size_t digits_out = 0;

  if (above) {
    char tmp[24];
    int tpos = 0;
    uint64_t a = above;
    while (a) { tmp[tpos++] = '0' + (char)(a % 10); a /= 10; }
    digits_out = (size_t)tpos;
    for (int i = tpos - 1; i >= 0; --i) csupport_obuf_put(&laid_out, tmp[i]);
  } else {
    csupport_obuf_put(&laid_out, '0');
  }

  if (!below) {
    csupport_obuf_write(&laid_out, ".0", 2);
    csupport_obuf_t whole = csupport_obuf(out, out_cap);
    csupport_obuf_write(&whole, digits, csupport_obuf_finish(&laid_out));
    return csupport_obuf_finish(&whole);
  }

  csupport_obuf_put(&laid_out, '.');
  uint64_t error = UINT64_C(1) << (64 - width);
  extra = (below & 0xf) << 56 | (extra >> 8);
  below >>= 4;
  size_t since_dot = 0;
  do {
    if (extra_shift) { --extra_shift; error *= 5; }
    else error *= 10;
    below *= 10;
    extra *= 10;
    below += (extra >> 60);
    extra = extra & (UINT64_MAX >> 4);
    char digit = '0' + (char)(below >> 60);
    csupport_obuf_put(&laid_out, digit);
    below = below & (UINT64_MAX >> 4);
    if (digits_out || digit != '0') ++digits_out;
    ++since_dot;
  } while (error && (below << 4 | extra >> 60) >= error / 2 &&
           (!precision || digits_out <= precision || since_dot < 2));

  size_t pos = csupport_obuf_finish(&laid_out);
  /* Unreachable given the bound above; kept so that a bound proved wrong one
     day gives a short number rather than a smashed frame. */
  if (pos >= sizeof(digits))
    pos = sizeof(digits) - 1;

  size_t kept = pos;
  if (precision && digits_out > precision) {
    size_t after_dot = 0;
    for (size_t i = 0; i < pos; i++) { if (digits[i] == '.') { after_dot = i + 1; break; } }
    size_t truncate = pos - (digits_out - precision);
    if (truncate < after_dot + 1) truncate = after_dot + 1;

    if (truncate < pos) {
      kept = truncate;
      if (digits[truncate] >= '5') {
        int carry = 1;
        for (size_t i = truncate; i > 0; --i) {
          if (digits[i - 1] == '.') continue;
          if (digits[i - 1] == '9') { digits[i - 1] = '0'; continue; }
          ++digits[i - 1];
          carry = 0;
          break;
        }
        if (carry) {
          memmove(digits + 1, digits, truncate);
          digits[0] = '1';
          kept = truncate + 1;
        }
      }
    }
  }

  csupport_obuf_t whole = csupport_obuf(out, out_cap);
  csupport_obuf_write(&whole, digits, scaled_significant_length(digits, kept));
  return csupport_obuf_finish(&whole);
}
