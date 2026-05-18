/*===- APFloat.c - Arbitrary precision float (pure C) -----------*- C -*-===*/
#include "include/csupport/la_lp_lfloat.h"
#include "include/csupport/lapint.h"
#include "include/csupport/types.h"
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* llvm::APFloat::fltCategory — must match llvm/include/llvm/ADT/APFloat.h */
#define LLVM_FC_INFINITY 0
#define LLVM_FC_NAN 1
#define LLVM_FC_NORMAL 2
#define LLVM_FC_ZERO 3

int csupport_float_classify(double v) {
  if (v != v) return 0;
  if (v == 1.0/0.0 || v == -1.0/0.0) return 1;
  if (v == 0.0) return 2;
  double abs_v = v < 0 ? -v : v;
  if (abs_v < 2.2250738585072014e-308) return 3;
  return 4;
}

int csupport_float_is_nan(double v) { return v != v; }
int csupport_float_is_inf(double v) { return !csupport_float_is_nan(v) && csupport_float_is_nan(v - v); }
int csupport_float_is_zero(double v) { return v == 0.0; }
int csupport_float_is_negative(double v) { return v < 0.0 || (v == 0.0 && 1.0/v < 0.0); }

double csupport_float_round_to_integral(double v, int round_mode) {
  switch (round_mode) {
  case 0: return trunc(v);         /* rmTowardZero */
  case 1: return nearbyint(v);     /* rmNearestTiesToEven */
  case 2: return ceil(v);          /* rmTowardPositive */
  case 3: return floor(v);         /* rmTowardNegative */
  case 4: return round(v);         /* rmNearestTiesToAway */
  default: return nearbyint(v);
  }
}

const char *csupport_apfloat_error_msg(int err) {
  switch (err) {
  case CSUPPORT_APFLOAT_ERR_EXPONENT_NO_DIGITS: return "Exponent has no digits";
  case CSUPPORT_APFLOAT_ERR_INVALID_EXPONENT:    return "Invalid character in exponent";
  case CSUPPORT_APFLOAT_ERR_SIG_NO_DIGITS:       return "Significand has no digits";
  case CSUPPORT_APFLOAT_ERR_MULTIPLE_DOTS:       return "String contains multiple dots";
  case CSUPPORT_APFLOAT_ERR_INVALID_SIG:         return "Invalid character in significand";
  default:                                        return "Unknown APFloat error";
  }
}

static unsigned dec_digit_value(unsigned c) { return c - '0'; }

int csupport_apfloat_read_exponent(const char *begin, const char *end,
                                    int *result) {
  int is_negative;
  unsigned abs_exponent;
  const unsigned overlarge = 24000;
  const char *p = begin;

  if (p == end || ((*p == '-' || *p == '+') && (p + 1) == end)) {
    *result = 0;
    return CSUPPORT_APFLOAT_ERR_NONE;
  }
  is_negative = (*p == '-');
  if (*p == '-' || *p == '+') {
    p++;
    if (p == end) return CSUPPORT_APFLOAT_ERR_EXPONENT_NO_DIGITS;
  }
  abs_exponent = dec_digit_value((unsigned char)*p++);
  if (abs_exponent >= 10U) return CSUPPORT_APFLOAT_ERR_INVALID_EXPONENT;

  for (; p != end; ++p) {
    unsigned value = dec_digit_value((unsigned char)*p);
    if (value >= 10U) return CSUPPORT_APFLOAT_ERR_INVALID_EXPONENT;
    abs_exponent = abs_exponent * 10U + value;
    if (abs_exponent >= overlarge) { abs_exponent = overlarge; break; }
  }
  *result = is_negative ? -(int)abs_exponent : (int)abs_exponent;
  return CSUPPORT_APFLOAT_ERR_NONE;
}

int csupport_apfloat_total_exponent(const char *begin, const char *end,
                                     int exponent_adjustment, int *result) {
  int unsigned_exp;
  int negative, overflow, exponent;
  const char *p = begin;

  if (p == end) return CSUPPORT_APFLOAT_ERR_EXPONENT_NO_DIGITS;
  negative = *p == '-';
  if (*p == '-' || *p == '+') {
    p++;
    if (p == end) return CSUPPORT_APFLOAT_ERR_EXPONENT_NO_DIGITS;
  }
  unsigned_exp = 0;
  overflow = 0;
  for (; p != end; ++p) {
    unsigned value = dec_digit_value((unsigned char)*p);
    if (value >= 10U) return CSUPPORT_APFLOAT_ERR_INVALID_EXPONENT;
    unsigned_exp = unsigned_exp * 10 + (int)value;
    if (unsigned_exp > 32767) { overflow = 1; break; }
  }
  if (exponent_adjustment > 32767 || exponent_adjustment < -32768) overflow = 1;
  exponent = 0;
  if (!overflow) {
    exponent = unsigned_exp;
    if (negative) exponent = -exponent;
    exponent += exponent_adjustment;
    if (exponent > 32767 || exponent < -32768) overflow = 1;
  }
  if (overflow) exponent = negative ? -32768 : 32767;
  *result = exponent;
  return CSUPPORT_APFLOAT_ERR_NONE;
}

int csupport_apfloat_skip_leading_zeros(const char *begin, const char *end,
                                         const char **dot,
                                         const char **out) {
  const char *p = begin;
  *dot = end;
  while (p != end && *p == '0') p++;
  if (p != end && *p == '.') {
    *dot = p++;
    if ((size_t)(end - begin) == 1) return CSUPPORT_APFLOAT_ERR_SIG_NO_DIGITS;
    while (p != end && *p == '0') p++;
  }
  *out = p;
  return CSUPPORT_APFLOAT_ERR_NONE;
}

int csupport_apfloat_interpret_decimal(const char *begin, const char *end,
                                        csupport_decimal_info_t *D) {
  const char *dot = end;
  const char *p;
  int err;

  err = csupport_apfloat_skip_leading_zeros(begin, end, &dot, &p);
  if (err) return err;

  D->first_sig_digit = p;
  D->exponent = 0;
  D->normalized_exponent = 0;

  for (; p != end; ++p) {
    if (*p == '.') {
      if (dot != end) return CSUPPORT_APFLOAT_ERR_MULTIPLE_DOTS;
      dot = p++;
      if (p == end) break;
    }
    if (dec_digit_value((unsigned char)*p) >= 10U) break;
  }

  if (p != end) {
    if (*p != 'e' && *p != 'E') return CSUPPORT_APFLOAT_ERR_INVALID_SIG;
    if (p == begin) return CSUPPORT_APFLOAT_ERR_SIG_NO_DIGITS;
    if (dot != end && (size_t)(p - begin) == 1) return CSUPPORT_APFLOAT_ERR_SIG_NO_DIGITS;
    int exp_val;
    err = csupport_apfloat_read_exponent(p + 1, end, &exp_val);
    if (err) return err;
    D->exponent = exp_val;
    if (dot == end) dot = p;
  }

  if (p != D->first_sig_digit) {
    if (p != begin) {
      do
        do p--;
        while (p != begin && *p == '0');
      while (p != begin && *p == '.');
    }
    D->exponent += (int)((dot - p) - (dot > p ? 1 : 0));
    D->normalized_exponent =
        D->exponent +
        (int)((p - D->first_sig_digit) -
              (dot > D->first_sig_digit && dot < p ? 1 : 0));
  }
  D->last_sig_digit = p;
  return CSUPPORT_APFLOAT_ERR_NONE;
}

int csupport_apfloat_combine_lost_fractions(int more, int less) {
  if (less != CSUPPORT_LF_EXACTLY_ZERO) {
    if (more == CSUPPORT_LF_EXACTLY_ZERO)
      more = CSUPPORT_LF_LESS_THAN_HALF;
    else if (more == CSUPPORT_LF_EXACTLY_HALF)
      more = CSUPPORT_LF_MORE_THAN_HALF;
  }
  return more;
}

unsigned csupport_apfloat_huerr_bound(int inexact_multiply,
                                       unsigned huerr1, unsigned huerr2) {
  if (huerr1 + huerr2 == 0)
    return (unsigned)inexact_multiply * 2;
  return (unsigned)inexact_multiply + 2 * (huerr1 + huerr2);
}

int csupport_apfloat_lost_fraction_truncation(const uint64_t *parts,
                                               unsigned part_count,
                                               unsigned bits) {
  unsigned lsb = csupport_apint_tc_lsb(parts, part_count);
  if (bits <= lsb) return CSUPPORT_LF_EXACTLY_ZERO;
  if (bits == lsb + 1) return CSUPPORT_LF_EXACTLY_HALF;
  if (bits <= part_count * 64 && csupport_apint_tc_extract_bit(parts, bits - 1))
    return CSUPPORT_LF_MORE_THAN_HALF;
  return CSUPPORT_LF_LESS_THAN_HALF;
}

uint64_t csupport_apfloat_ulps_from_boundary(const uint64_t *parts,
                                              unsigned bits, int is_nearest) {
  unsigned count, part_bits;
  uint64_t part, boundary;

  bits--;
  count = bits / 64;
  part_bits = bits % 64 + 1;
  part = parts[count] & (~(uint64_t)0 >> (64 - part_bits));

  boundary = is_nearest ? (uint64_t)1 << (part_bits - 1) : 0;

  if (count == 0) {
    return (part - boundary <= boundary - part) ? part - boundary : boundary - part;
  }
  if (part == boundary) {
    while (--count)
      if (parts[count]) return ~(uint64_t)0;
    return parts[0];
  } else if (part == boundary - 1) {
    while (--count)
      if (~parts[count]) return ~(uint64_t)0;
    return -parts[0];
  }
  return ~(uint64_t)0;
}

unsigned csupport_apfloat_part_as_hex(char *dst, uint64_t part,
                                       unsigned count, const char *hex_digits) {
  unsigned result = count;
  assert(count != 0 && count <= 64 / 4);
  part >>= (64 - 4 * count);
  while (count-- > 0) {
    dst[count] = hex_digits[part & 0xf];
    part >>= 4;
  }
  return result;
}

char *csupport_apfloat_write_unsigned_decimal(char *dst, unsigned n) {
  char buff[40], *p = buff;
  do
    *p++ = '0' + n % 10;
  while (n /= 10);
  do
    *dst++ = *--p;
  while (p != buff);
  return dst;
}

char *csupport_apfloat_write_signed_decimal(char *dst, int value) {
  if (value < 0) {
    *dst++ = '-';
    dst = csupport_apfloat_write_unsigned_decimal(dst, -(unsigned)value);
  } else
    dst = csupport_apfloat_write_unsigned_decimal(dst, (unsigned)value);
  return dst;
}

int csupport_apfloat_trailing_hex_fraction(const char *p, const char *end,
                                            unsigned digit_value, int *result) {
  if (digit_value > 8) { *result = CSUPPORT_LF_MORE_THAN_HALF; return 0; }
  if (digit_value < 8 && digit_value > 0) { *result = CSUPPORT_LF_LESS_THAN_HALF; return 0; }

  while (p != end && (*p == '0' || *p == '.')) p++;
  if (p == end) return -1;

  unsigned hex = 0;
  char c = *p;
  if (c >= '0' && c <= '9') hex = (unsigned)(c - '0');
  else if (c >= 'a' && c <= 'f') hex = (unsigned)(c - 'a' + 10);
  else if (c >= 'A' && c <= 'F') hex = (unsigned)(c - 'A' + 10);
  else hex = UINT32_MAX;

  if (hex == UINT32_MAX)
    *result = (digit_value == 0) ? CSUPPORT_LF_EXACTLY_ZERO : CSUPPORT_LF_EXACTLY_HALF;
  else
    *result = (digit_value == 0) ? CSUPPORT_LF_LESS_THAN_HALF : CSUPPORT_LF_MORE_THAN_HALF;
  return 0;
}

int csupport_apfloat_is_significand_all_ones(const uint64_t *parts,
                                              unsigned precision) {
  unsigned part_count = (precision + 63) / 64;
  for (unsigned i = 0; i < part_count - 1; i++)
    if (~parts[i]) return 0;
  unsigned num_high_bits = part_count * 64 - precision + 1;
  uint64_t high_fill = ~(uint64_t)0 << (64 - num_high_bits);
  return (~(parts[part_count - 1] | high_fill)) == 0;
}

int csupport_apfloat_is_significand_all_ones_except_lsb(const uint64_t *parts,
                                                         unsigned precision) {
  if (parts[0] & 1) return 0;
  unsigned part_count = (precision + 63) / 64;
  for (unsigned i = 0; i < part_count - 1; i++) {
    if (~parts[i] & ~(uint64_t)(!i)) return 0;
  }
  unsigned num_high_bits = part_count * 64 - precision + 1;
  uint64_t high_fill = ~(uint64_t)0 << (64 - num_high_bits);
  return (~(parts[part_count - 1] | high_fill | 0x1)) == 0;
}

int csupport_apfloat_is_significand_all_zeros(const uint64_t *parts,
                                               unsigned precision) {
  unsigned part_count = (precision + 63) / 64;
  for (unsigned i = 0; i < part_count - 1; i++)
    if (parts[i]) return 0;
  unsigned num_high_bits = part_count * 64 - precision + 1;
  uint64_t high_mask = ~(uint64_t)0 >> num_high_bits;
  return (parts[part_count - 1] & high_mask) == 0;
}

int csupport_apfloat_is_significand_all_zeros_except_msb(const uint64_t *parts,
                                                          unsigned precision) {
  unsigned part_count = (precision + 63) / 64;
  for (unsigned i = 0; i < part_count - 1; i++)
    if (parts[i]) return 0;
  unsigned num_high_bits = part_count * 64 - precision + 1;
  return parts[part_count - 1] == (uint64_t)1 << (64 - num_high_bits);
}

void csupport_apfloat_tc_set_least_significant_bits(uint64_t *dst,
                                                     unsigned parts,
                                                     unsigned bits) {
  unsigned i = 0;
  while (bits > 64) {
    dst[i++] = ~(uint64_t)0;
    bits -= 64;
  }
  if (bits)
    dst[i++] = ~(uint64_t)0 >> (64 - bits);
  while (i < parts)
    dst[i++] = 0;
}

int csupport_apfloat_round_away_from_zero(int rounding_mode, int lost_fraction,
                                          int category, int sign_bit,
                                          const uint64_t *parts, unsigned bit) {
  assert(lost_fraction != CSUPPORT_LF_EXACTLY_ZERO);

  switch (rounding_mode) {
  case 0: /* rmTowardZero */
    return 0;
  case 1: /* rmNearestTiesToEven */
    if (lost_fraction == CSUPPORT_LF_MORE_THAN_HALF) return 1;
    if (lost_fraction == CSUPPORT_LF_EXACTLY_HALF &&
        category != LLVM_FC_ZERO)
      return csupport_apint_tc_extract_bit(parts, bit);
    return 0;
  case 2: /* rmTowardPositive */
    return !sign_bit;
  case 3: /* rmTowardNegative */
    return sign_bit;
  case 4: /* rmNearestTiesToAway */
    return lost_fraction == CSUPPORT_LF_EXACTLY_HALF ||
           lost_fraction == CSUPPORT_LF_MORE_THAN_HALF;
  default:
    return 0;
  }
}

int csupport_apfloat_convert_normal_to_hex(char *dst,
    const uint64_t *significand, unsigned parts_count,
    unsigned precision, int exponent_val, int sign_bit,
    unsigned sig_lsb,
    unsigned hex_digits, int upper_case, int rounding_mode,
    int category,
    unsigned *out_len) {
  unsigned value_bits, shift, output_digits;
  const char *hex_chars = upper_case ? "0123456789ABCDEF0" : "0123456789abcdef0";
  char *p;
  int round_up = 0;

  *dst++ = '0';
  *dst++ = upper_case ? 'X' : 'x';

  value_bits = precision + 3;
  shift = 64 - value_bits % 64;
  output_digits = (value_bits - sig_lsb + 3) / 4;

  if (hex_digits) {
    if (hex_digits < output_digits) {
      unsigned bits = value_bits - hex_digits * 4;
      int fraction = csupport_apfloat_lost_fraction_truncation(significand, parts_count, bits);
      round_up = csupport_apfloat_round_away_from_zero(rounding_mode, fraction,
                                                        category, sign_bit,
                                                        significand, bits);
    }
    output_digits = hex_digits;
  }

  p = ++dst;
  {
    unsigned count = (value_bits + 63) / 64;
    while (output_digits && count) {
      uint64_t part;
      if (--count == parts_count)
        part = 0;
      else
        part = significand[count] << shift;
      if (count && shift)
        part |= significand[count - 1] >> (64 - shift);
      unsigned cur = 64 / 4;
      if (cur > output_digits) cur = output_digits;
      dst += csupport_apfloat_part_as_hex(dst, part, cur, hex_chars);
      output_digits -= cur;
    }
  }

  if (round_up) {
    char *q = dst;
    do {
      q--;
      unsigned dv = 0;
      char c = *q;
      if (c >= '0' && c <= '9') dv = (unsigned)(c - '0');
      else if (c >= 'a' && c <= 'f') dv = (unsigned)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') dv = (unsigned)(c - 'A' + 10);
      *q = hex_chars[dv + 1];
    } while (*q == '0');
  } else {
    memset(dst, '0', output_digits);
    dst += output_digits;
  }

  p[-1] = p[0];
  if (dst - 1 == p)
    dst--;
  else
    p[0] = '.';

  *dst++ = upper_case ? 'P' : 'p';
  dst = csupport_apfloat_write_signed_decimal(dst, exponent_val);
  *out_len = (unsigned)(dst - (p - 2));
  return 0;
}

static int shift_right_internal(uint64_t *dst, unsigned parts, unsigned bits) {
  int lf = csupport_apfloat_lost_fraction_truncation(dst, parts, bits);
  csupport_apint_shift_right_logical(dst, dst, parts, bits);
  return lf;
}

int csupport_apfloat_divide_significand(
    uint64_t *lhs_sig, const uint64_t *rhs_sig,
    unsigned parts_count, unsigned precision, int *exponent_out) {
  uint64_t scratch[4];
  uint64_t *dividend;
  unsigned i, bit;
  int exp_val = *exponent_out;

  if (parts_count > 2)
    dividend = (uint64_t *)malloc(parts_count * 2 * sizeof(uint64_t));
  else
    dividend = scratch;

  uint64_t *divisor = dividend + parts_count;

  for (i = 0; i < parts_count; i++) {
    dividend[i] = lhs_sig[i];
    divisor[i] = rhs_sig[i];
    lhs_sig[i] = 0;
  }

  bit = precision - csupport_apint_tc_msb(divisor, parts_count) - 1;
  if (bit) {
    exp_val += (int)bit;
    csupport_apint_shift_left(divisor, divisor, parts_count, bit);
  }

  bit = precision - csupport_apint_tc_msb(dividend, parts_count) - 1;
  if (bit) {
    exp_val -= (int)bit;
    csupport_apint_shift_left(dividend, dividend, parts_count, bit);
  }

  if (csupport_apint_tc_compare(dividend, divisor, parts_count) < 0) {
    exp_val--;
    csupport_apint_shift_left(dividend, dividend, parts_count, 1);
  }

  for (bit = precision; bit; bit -= 1) {
    if (csupport_apint_tc_compare(dividend, divisor, parts_count) >= 0) {
      csupport_apint_tc_subtract(dividend, divisor, 0, parts_count);
      csupport_apint_tc_set_bit(lhs_sig, bit - 1);
    }
    csupport_apint_shift_left(dividend, dividend, parts_count, 1);
  }

  int cmp = csupport_apint_tc_compare(dividend, divisor, parts_count);
  int lost_fraction;
  if (cmp > 0)
    lost_fraction = CSUPPORT_LF_MORE_THAN_HALF;
  else if (cmp == 0)
    lost_fraction = CSUPPORT_LF_EXACTLY_HALF;
  else if (csupport_apint_tc_is_zero(dividend, parts_count))
    lost_fraction = CSUPPORT_LF_EXACTLY_ZERO;
  else
    lost_fraction = CSUPPORT_LF_LESS_THAN_HALF;

  if (parts_count > 2)
    free(dividend);

  *exponent_out = exp_val;
  return lost_fraction;
}

int csupport_apfloat_multiply_significand_simple(
    uint64_t *lhs_sig, const uint64_t *rhs_sig,
    unsigned parts_count, unsigned precision, int *exponent_out) {
  unsigned new_parts_count = ((precision * 2 + 1) + 63) / 64;
  uint64_t scratch[4];
  uint64_t *full_sig;
  int lost_fraction;
  unsigned omsb;

  if (new_parts_count > 4)
    full_sig = (uint64_t *)malloc(new_parts_count * sizeof(uint64_t));
  else
    full_sig = scratch;

  csupport_apint_tc_full_multiply(full_sig, lhs_sig, rhs_sig,
                                  parts_count, parts_count);

  lost_fraction = CSUPPORT_LF_EXACTLY_ZERO;
  omsb = csupport_apint_tc_msb(full_sig, new_parts_count) + 1;
  *exponent_out += 2;
  *exponent_out -= (int)(precision + 1);

  if (omsb > precision) {
    unsigned bits = omsb - precision;
    unsigned sig_parts = (omsb + 63) / 64;
    int lf = shift_right_internal(full_sig, sig_parts, bits);
    lost_fraction = csupport_apfloat_combine_lost_fractions(lf, lost_fraction);
    *exponent_out += (int)bits;
  }

  csupport_apint_tc_assign(lhs_sig, full_sig, parts_count);

  if (new_parts_count > 4)
    free(full_sig);

  return lost_fraction;
}

static int apfloat_shift_sig_right(uint64_t *sig, unsigned parts, int *exp,
                                   unsigned bits) {
  int lf = csupport_apfloat_lost_fraction_truncation(sig, parts, bits);
  csupport_apint_shift_right_logical(sig, sig, parts, bits);
  *exp += (int)bits;
  return lf;
}

static void apfloat_shift_sig_left(uint64_t *sig, unsigned parts, int *exp,
                                   unsigned bits) {
  if (bits) {
    csupport_apint_shift_left(sig, sig, parts, bits);
    *exp -= (int)bits;
  }
}

/* Compare magnitudes of two normal finite values (exponent then significand). */
static int apfloat_cmp_abs_magnitude(int lhs_exp, const uint64_t *lhs_sig,
                                     int rhs_exp, const uint64_t *rhs_sig,
                                     unsigned parts) {
  int c = lhs_exp - rhs_exp;
  if (c > 0)
    return 1;
  if (c < 0)
    return -1;
  c = csupport_apint_tc_compare(lhs_sig, rhs_sig, parts);
  if (c > 0)
    return 1;
  if (c < 0)
    return -1;
  return 0;
}

int csupport_apfloat_add_or_subtract_significand(
    uint64_t *lhs_sig, unsigned parts_count, int *lhs_exp, int *lhs_sign,
    const uint64_t *rhs_sig, int rhs_exp, int rhs_sign, int subtract) {
  uint64_t stack_temp[8];
  uint64_t *temp = stack_temp;
  uint64_t *alloc_temp = NULL;
  int lost_fraction;
  int bits;
  uint64_t borrow_u;

  if (parts_count > 8) {
    alloc_temp = (uint64_t *)malloc(parts_count * sizeof(uint64_t));
    temp = alloc_temp;
  }

  subtract ^= ((*lhs_sign) ^ rhs_sign) ? 1 : 0;
  bits = *lhs_exp - rhs_exp;

  if (subtract) {
    int temp_exp = rhs_exp;
    csupport_apint_tc_assign(temp, rhs_sig, parts_count);

    if (bits == 0) {
      lost_fraction = CSUPPORT_LF_EXACTLY_ZERO;
    } else if (bits > 0) {
      lost_fraction = apfloat_shift_sig_right(temp, parts_count, &temp_exp,
                                              (unsigned)(bits - 1));
      apfloat_shift_sig_left(lhs_sig, parts_count, lhs_exp, 1);
    } else {
      lost_fraction =
          apfloat_shift_sig_right(lhs_sig, parts_count, lhs_exp,
                                  (unsigned)(-bits - 1));
      apfloat_shift_sig_left(temp, parts_count, &temp_exp, 1);
    }

    borrow_u = (lost_fraction != CSUPPORT_LF_EXACTLY_ZERO) ? 1u : 0u;

    if (apfloat_cmp_abs_magnitude(*lhs_exp, lhs_sig, temp_exp, temp,
                                  parts_count) < 0) {
      uint64_t carry_u =
          csupport_apint_tc_subtract(temp, lhs_sig, borrow_u, parts_count);
      csupport_apint_tc_assign(lhs_sig, temp, parts_count);
      *lhs_sign ^= 1;
      assert(carry_u == 0);
      (void)carry_u;
    } else {
      uint64_t carry_u =
          csupport_apint_tc_subtract(lhs_sig, temp, borrow_u, parts_count);
      assert(carry_u == 0);
      (void)carry_u;
    }

    if (lost_fraction == CSUPPORT_LF_LESS_THAN_HALF)
      lost_fraction = CSUPPORT_LF_MORE_THAN_HALF;
    else if (lost_fraction == CSUPPORT_LF_MORE_THAN_HALF)
      lost_fraction = CSUPPORT_LF_LESS_THAN_HALF;
  } else {
    uint64_t carry_u;

    if (bits > 0) {
      int temp_exp = rhs_exp;
      csupport_apint_tc_assign(temp, rhs_sig, parts_count);
      lost_fraction =
          apfloat_shift_sig_right(temp, parts_count, &temp_exp, (unsigned)bits);
      carry_u = csupport_apint_tc_add(lhs_sig, temp, 0, parts_count);
    } else {
      lost_fraction = apfloat_shift_sig_right(lhs_sig, parts_count, lhs_exp,
                                              (unsigned)(-bits));
      carry_u = csupport_apint_tc_add(lhs_sig, rhs_sig, 0, parts_count);
    }
    assert(carry_u == 0);
    (void)carry_u;
  }

  if (alloc_temp)
    free(alloc_temp);
  return lost_fraction;
}

int csupport_apfloat_handle_overflow(
    uint64_t *significand, unsigned parts_count,
    unsigned precision, int rounding_mode, int *sign,
    int *category_out, int *exponent_out,
    int max_exponent, int min_exponent,
    int non_finite_behavior, int nan_encoding) {
  int opOverflow = 0x04, opInexact = 0x10;

  if (rounding_mode == 1 || rounding_mode == 4 ||
      (rounding_mode == 2 && !*sign) ||
      (rounding_mode == 3 && *sign)) {
    if (non_finite_behavior == 1) {
      *category_out = LLVM_FC_NAN;
      if (nan_encoding == 2)
        *exponent_out = min_exponent - 1;
      else
        *exponent_out = max_exponent;
      csupport_apfloat_make_nan(significand, parts_count, precision,
                                0, sign, NULL, 0,
                                non_finite_behavior, nan_encoding, 0);
    } else {
      *category_out = LLVM_FC_INFINITY;
    }
    return opOverflow | opInexact;
  }

  *category_out = LLVM_FC_NORMAL;
  *exponent_out = max_exponent;
  csupport_apfloat_tc_set_least_significant_bits(significand, parts_count,
                                                  precision);
  if (non_finite_behavior == 1 && nan_encoding == 1)
    csupport_apint_tc_clear_bit(significand, 0);

  return opInexact;
}

int csupport_apfloat_normalize(
    uint64_t *significand, unsigned parts_count,
    int *exponent, int *category, int *sign,
    int rounding_mode, int lost_fraction,
    unsigned precision, int max_exponent, int min_exponent,
    int non_finite_behavior, int nan_encoding) {
  unsigned omsb;
  int exponent_change;

  if (*category != LLVM_FC_NORMAL)
    return 0;

  omsb = csupport_apint_tc_msb(significand, parts_count) + 1;

  if (omsb) {
    exponent_change = (int)omsb - (int)precision;

    if (*exponent + exponent_change > max_exponent)
      return csupport_apfloat_handle_overflow(
          significand, parts_count, precision, rounding_mode, sign,
          category, exponent, max_exponent, min_exponent,
          non_finite_behavior, nan_encoding);

    if (*exponent + exponent_change < min_exponent)
      exponent_change = min_exponent - *exponent;

    if (exponent_change < 0) {
      csupport_apint_shift_left(significand, significand, parts_count,
                                (unsigned)(-exponent_change));
      *exponent += exponent_change;
      return 0;
    }

    if (exponent_change > 0) {
      int lf = shift_right_internal(significand, parts_count,
                                    (unsigned)exponent_change);
      *exponent += exponent_change;
      lost_fraction = csupport_apfloat_combine_lost_fractions(lf, lost_fraction);

      if (omsb > (unsigned)exponent_change)
        omsb -= (unsigned)exponent_change;
      else
        omsb = 0;
    }
  }

  if (non_finite_behavior == 1 && nan_encoding == 1 &&
      *exponent == max_exponent &&
      csupport_apfloat_is_significand_all_ones(significand, precision))
    return csupport_apfloat_handle_overflow(
        significand, parts_count, precision, rounding_mode, sign,
        category, exponent, max_exponent, min_exponent,
        non_finite_behavior, nan_encoding);

  if (lost_fraction == CSUPPORT_LF_EXACTLY_ZERO) {
    if (omsb == 0) {
      *category = LLVM_FC_ZERO;
      if (nan_encoding == 2) *sign = 0;
    }
    return 0;
  }

  if (csupport_apfloat_round_away_from_zero(rounding_mode, lost_fraction,
                                             *category, *sign,
                                             significand, 0)) {
    if (omsb == 0)
      *exponent = min_exponent;

    csupport_apint_tc_add_part(significand, 1, parts_count);
    omsb = csupport_apint_tc_msb(significand, parts_count) + 1;

    if (omsb == precision + 1) {
      if (*exponent == max_exponent) {
        int rm = *sign ? 3 : 2;
        return csupport_apfloat_handle_overflow(
            significand, parts_count, precision, rm, sign,
            category, exponent, max_exponent, min_exponent,
            non_finite_behavior, nan_encoding);
      }
      shift_right_internal(significand, parts_count, 1);
      (*exponent)++;
      return 0x10;
    }

    if (non_finite_behavior == 1 && nan_encoding == 1 &&
        *exponent == max_exponent &&
        csupport_apfloat_is_significand_all_ones(significand, precision))
      return csupport_apfloat_handle_overflow(
          significand, parts_count, precision, rounding_mode, sign,
          category, exponent, max_exponent, min_exponent,
          non_finite_behavior, nan_encoding);
  }

  if ((unsigned)omsb == precision)
    return 0x10;

  if (omsb == 0) {
    *category = LLVM_FC_ZERO;
    if (nan_encoding == 2) *sign = 0;
  }

  return 0x08 | 0x10;
}

int csupport_apfloat_specials_category(int lhs_cat, int rhs_cat,
                                        int lhs_sign, int rhs_sign,
                                        int subtract, int op_type,
                                        int non_finite_behavior,
                                        int *out_cat, int *out_sign,
                                        int *is_signaling_lhs,
                                        int *is_signaling_rhs) {
  int key = lhs_cat * 4 + rhs_cat;
  (void)is_signaling_lhs;
  (void)is_signaling_rhs;

  if (op_type == 0) {
    *out_cat = lhs_cat;
    *out_sign = lhs_sign;
    switch (key) {
    case 0*4+3: case 1*4+3: case 2*4+3:
      *out_cat = rhs_cat; *out_sign = rhs_sign; return 1;
    case 3*4+0: case 3*4+1: case 3*4+2: case 3*4+3:
      return 1;
    case 1*4+0: case 2*4+1: case 2*4+0:
      return 0;
    case 1*4+2: case 0*4+2:
      *out_cat = 2; *out_sign = rhs_sign ^ subtract; return 0;
    case 0*4+1:
      *out_sign = rhs_sign ^ subtract; return 0;
    case 0*4+0:
      return 0;
    case 2*4+2:
      if (((lhs_sign ^ rhs_sign) != 0) != subtract) {
        *out_cat = 3; return -1;
      }
      return 0;
    case 1*4+1:
      return 2;
    default: return -2;
    }
  }
  return -2;
}

void csupport_apfloat_make_largest(uint64_t *significand, unsigned part_count,
                                   unsigned precision, int non_finite_behavior,
                                   int nan_encoding) {
  memset(significand, 0xFF, sizeof(uint64_t) * (part_count - 1));
  unsigned unused_high_bits = part_count * 64 - precision;
  significand[part_count - 1] = (unused_high_bits < 64)
                                ? (~(uint64_t)0 >> unused_high_bits)
                                : 0;
  if (non_finite_behavior == 1 && nan_encoding == 1)
    significand[0] &= ~(uint64_t)1;
}

void csupport_apfloat_make_smallest(uint64_t *significand, unsigned part_count) {
  csupport_apint_tc_set(significand, 1, part_count);
}

void csupport_apfloat_make_smallest_normalized(uint64_t *significand,
                                               unsigned part_count,
                                               unsigned precision) {
  memset(significand, 0, part_count * sizeof(uint64_t));
  csupport_apint_tc_set_bit(significand, precision - 1);
}

void csupport_apfloat_make_zero(uint64_t *significand, unsigned part_count) {
  csupport_apint_tc_set(significand, 0, part_count);
}

void csupport_apfloat_make_inf(uint64_t *significand, unsigned part_count) {
  csupport_apint_tc_set(significand, 0, part_count);
}

int csupport_apfloat_compare_absolute(const uint64_t *lhs_sig,
                                      const uint64_t *rhs_sig,
                                      unsigned part_count) {
  return csupport_apint_tc_compare(lhs_sig, rhs_sig, part_count);
}

void csupport_apfloat_adjust_precision_buffer(char *buffer, unsigned *size,
                                              int *exp,
                                              unsigned format_precision) {
  unsigned N = *size;
  if (N <= format_precision) return;

  unsigned first_sig = N - format_precision;

  if (buffer[first_sig - 1] < '5') {
    while (first_sig < N && buffer[first_sig] == '0')
      first_sig++;
    *exp += (int)first_sig;
    unsigned new_size = N - first_sig;
    for (unsigned i = 0; i < new_size; i++)
      buffer[i] = buffer[first_sig + i];
    *size = new_size;
    return;
  }

  for (unsigned I = first_sig; I != N; ++I) {
    if (buffer[I] == '9') {
      first_sig++;
    } else {
      buffer[I]++;
      break;
    }
  }

  if (first_sig == N) {
    *exp += (int)first_sig;
    buffer[0] = '1';
    *size = 1;
    return;
  }

  *exp += (int)first_sig;
  unsigned new_size = N - first_sig;
  for (unsigned i = 0; i < new_size; i++)
    buffer[i] = buffer[first_sig + i];
  *size = new_size;
}

unsigned csupport_apfloat_power_of5(uint64_t *dst, unsigned power) {
  static const uint64_t first_eight_powers[] = {1, 5, 25, 125, 625, 3125, 15625, 78125};
  uint64_t pow5s[1024];
  pow5s[0] = 78125 * 5;

  unsigned parts_count[16] = {1, 0};
  uint64_t scratch[512], *p1, *p2, *pow5;
  unsigned result;

  p1 = dst;
  p2 = scratch;

  *p1 = first_eight_powers[power & 7];
  power >>= 3;

  result = 1;
  pow5 = pow5s;

  for (unsigned n = 0; power; power >>= 1, n++) {
    unsigned pc = parts_count[n];

    if (pc == 0) {
      pc = parts_count[n - 1];
      csupport_apint_tc_full_multiply(pow5, pow5 - pc, pow5 - pc, pc, pc);
      pc *= 2;
      if (pow5[pc - 1] == 0) pc--;
      parts_count[n] = pc;
    }

    if (power & 1) {
      uint64_t *tmp;
      csupport_apint_tc_full_multiply(p2, p1, pow5, result, pc);
      result += pc;
      if (p2[result - 1] == 0) result--;
      tmp = p1; p1 = p2; p2 = tmp;
    }

    pow5 += pc;
  }

  if (p1 != dst)
    csupport_apint_tc_assign(dst, p1, result);

  return result;
}

/*--- convertFromStringSpecials: parse inf/nan string representations ---*/
int csupport_apfloat_parse_special_string(const char *str, size_t len,
                                           int *out_category,
                                           int *out_is_negative,
                                           int *out_is_signaling,
                                           unsigned *out_radix,
                                           const char **payload_begin,
                                           size_t *payload_len) {
  if (len < 3) return 0;

  const char *s = str;
  size_t slen = len;
  int is_negative = 0;

  if (slen == 3 && (s[0] == 'i' && s[1] == 'n' && s[2] == 'f')) {
    *out_category = 1; /* infinity */
    *out_is_negative = 0;
    return 1;
  }
  if (slen == 8 && memcmp(s, "INFINITY", 8) == 0) {
    *out_category = 1;
    *out_is_negative = 0;
    return 1;
  }
  if (slen == 4 && s[0] == '+' && s[1] == 'I' && s[2] == 'n' && s[3] == 'f') {
    *out_category = 1;
    *out_is_negative = 0;
    return 1;
  }

  if (s[0] == '-') {
    is_negative = 1;
    s++;
    slen--;
    if (slen < 3) return 0;

    if ((slen == 3 && s[0] == 'i' && s[1] == 'n' && s[2] == 'f') ||
        (slen == 8 && memcmp(s, "INFINITY", 8) == 0) ||
        (slen == 3 && s[0] == 'I' && s[1] == 'n' && s[2] == 'f')) {
      *out_category = 1;
      *out_is_negative = 1;
      return 1;
    }
  }

  int is_signaling = (s[0] == 's' || s[0] == 'S');
  if (is_signaling) {
    s++;
    slen--;
    if (slen < 3) return 0;
  }

  if (!(slen >= 3 && ((s[0] == 'n' && s[1] == 'a' && s[2] == 'n') ||
                       (s[0] == 'N' && s[1] == 'a' && s[2] == 'N'))))
    return 0;

  s += 3;
  slen -= 3;

  *out_category = 2; /* NaN */
  *out_is_negative = is_negative;
  *out_is_signaling = is_signaling;

  if (slen == 0) {
    *payload_begin = NULL;
    *payload_len = 0;
    return 1;
  }

  const char *pstart = s;
  size_t plen = slen;

  if (s[0] == '(') {
    if (slen <= 2 || s[slen - 1] != ')')
      return 0;
    pstart = s + 1;
    plen = slen - 2;
  }

  unsigned radix = 10;
  if (pstart[0] == '0') {
    if (plen > 1 && (pstart[1] == 'x' || pstart[1] == 'X')) {
      pstart += 2;
      plen -= 2;
      radix = 16;
    } else {
      radix = 8;
    }
  }

  *out_radix = radix;
  *payload_begin = pstart;
  *payload_len = plen;
  return 1;
}

/*--- toString output formatting (buffer→string) ---*/
void csupport_apfloat_format_to_string(
    const char *buffer, unsigned n_digits, int exp,
    unsigned format_precision, unsigned format_max_padding,
    int truncate_zero, int is_negative,
    char *out, unsigned *out_len) {
  unsigned pos = 0;

  if (is_negative)
    out[pos++] = '-';

  int format_scientific;
  if (!format_max_padding) {
    format_scientific = 1;
  } else {
    if (exp >= 0) {
      format_scientific = ((unsigned)exp > format_max_padding ||
                           n_digits + (unsigned)exp > format_precision);
    } else {
      int msd = exp + (int)(n_digits - 1);
      if (msd >= 0) {
        format_scientific = 0;
      } else {
        format_scientific = ((unsigned)-msd) > format_max_padding;
      }
    }
  }

  if (format_scientific) {
    int e = exp + (int)(n_digits - 1);

    out[pos++] = buffer[n_digits - 1];
    out[pos++] = '.';
    if (n_digits == 1 && truncate_zero) {
      out[pos++] = '0';
    } else {
      for (unsigned i = 1; i < n_digits; ++i)
        out[pos++] = buffer[n_digits - 1 - i];
    }
    if (!truncate_zero && format_precision > n_digits - 1) {
      unsigned pad = format_precision - n_digits + 1;
      for (unsigned i = 0; i < pad; i++)
        out[pos++] = '0';
    }
    out[pos++] = truncate_zero ? 'E' : 'e';

    out[pos++] = e >= 0 ? '+' : '-';
    if (e < 0) e = -e;
    char expbuf[12];
    unsigned explen = 0;
    do {
      expbuf[explen++] = (char)('0' + (e % 10));
      e /= 10;
    } while (e);
    if (!truncate_zero && explen < 2)
      expbuf[explen++] = '0';
    for (unsigned i = 0; i < explen; ++i)
      out[pos++] = expbuf[explen - 1 - i];
  } else if (exp >= 0) {
    for (unsigned i = 0; i < n_digits; ++i)
      out[pos++] = buffer[n_digits - 1 - i];
    for (int i = 0; i < exp; ++i)
      out[pos++] = '0';
  } else {
    int n_whole = exp + (int)n_digits;
    unsigned i = 0;
    if (n_whole > 0) {
      for (; i < (unsigned)n_whole; ++i)
        out[pos++] = buffer[n_digits - i - 1];
      out[pos++] = '.';
    } else {
      unsigned n_zeros = 1 + (unsigned)(-n_whole);
      out[pos++] = '0';
      out[pos++] = '.';
      for (unsigned z = 1; z < n_zeros; ++z)
        out[pos++] = '0';
    }
    for (; i < n_digits; ++i)
      out[pos++] = buffer[n_digits - i - 1];
  }

  out[pos] = '\0';
  *out_len = pos;
}

/*--- roundAwayFromZero: determine rounding direction ---*/
int csupport_apfloat_round_away_from_zero_simple(int rounding_mode,
                                                   int lost_fraction,
                                                   int bit_at_boundary) {
  switch (rounding_mode) {
  case 0: /* rmTowardZero */
    return 0;
  case 1: /* rmNearestTiesToEven */
    if (lost_fraction == 3) return 1; /* MoreThanHalf */
    if (lost_fraction == 2) return bit_at_boundary; /* ExactlyHalf: tie-to-even */
    return 0;
  case 2: /* rmTowardPositive */
    return 0;
  case 3: /* rmTowardNegative */
    return 0;
  case 4: /* rmNearestTiesToAway */
    if (lost_fraction >= 2) return 1;
    return 0;
  default:
    return 0;
  }
}

/*--- F80 LongDouble → two-word bit packing ---*/
void csupport_apfloat_convert_f80_to_words(
    int category, int is_finite_nonzero, int exponent_val,
    int sign_bit, const uint64_t *significand_parts,
    uint64_t *words) {
  uint64_t myexponent, mysignificand;
  if (is_finite_nonzero) {
    myexponent = (uint64_t)(exponent_val + 16383);
    mysignificand = significand_parts[0];
    if (myexponent == 1 && !(mysignificand & 0x8000000000000000ULL))
      myexponent = 0;
  } else if (category == LLVM_FC_ZERO) {
    myexponent = 0;
    mysignificand = 0;
  } else if (category == LLVM_FC_INFINITY) {
    myexponent = 0x7fff;
    mysignificand = 0x8000000000000000ULL;
  } else if (category == LLVM_FC_NAN) {
    myexponent = 0x7fff;
    mysignificand = significand_parts[0];
  } else {
    /* LLVM fcNormal with !is_finite_nonzero — invalid; emit quiet NaN */
    myexponent = 0x7fff;
    mysignificand = significand_parts[0] ? significand_parts[0]
                                         : UINT64_C(0xC000000000000000);
  }
  words[0] = mysignificand;
  words[1] = ((uint64_t)(sign_bit & 1) << 15) | (myexponent & 0x7fffULL);
}

/*--- Hex string → significand parsing core ---*/
int csupport_apfloat_convert_from_hex_core(
    const char *begin, const char *end,
    uint64_t *significand, unsigned parts_count, unsigned integer_part_width,
    unsigned precision,
    int *exponent_out, int *category_out,
    int *lost_fraction_out) {
  unsigned bit_pos = parts_count * integer_part_width;
  int computed_trailing = 0;
  *lost_fraction_out = 0; /* lfExactlyZero */
  *category_out = LLVM_FC_NORMAL;

  memset(significand, 0, parts_count * sizeof(uint64_t));

  const char *dot = end;
  const char *p;
  int err = csupport_apfloat_skip_leading_zeros(begin, end, &dot, (const char**)&p);
  if (err) return err;
  const char *first_sig = p;

  while (p != end) {
    unsigned hex_value;
    if (*p == '.') {
      if (dot != end)
        return CSUPPORT_APFLOAT_ERR_MULTIPLE_DOTS;
      dot = p++;
      continue;
    }
    if (*p >= '0' && *p <= '9') hex_value = *p - '0';
    else if (*p >= 'a' && *p <= 'f') hex_value = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F') hex_value = *p - 'A' + 10;
    else break;
    p++;
    if (bit_pos) {
      bit_pos -= 4;
      significand[bit_pos / integer_part_width] |=
          ((uint64_t)hex_value << (bit_pos % integer_part_width));
    } else if (!computed_trailing) {
      int lf;
      err = csupport_apfloat_trailing_hex_fraction(p, end, hex_value, &lf);
      if (err) return err;
      *lost_fraction_out = lf;
      computed_trailing = 1;
    }
  }

  if (p == end) return CSUPPORT_APFLOAT_ERR_EXPONENT_NO_DIGITS;
  if (*p != 'p' && *p != 'P') return CSUPPORT_APFLOAT_ERR_INVALID_SIG;
  if (p == begin) return CSUPPORT_APFLOAT_ERR_SIG_NO_DIGITS;
  if (dot != end && p - begin == 1) return CSUPPORT_APFLOAT_ERR_SIG_NO_DIGITS;

  if (p != first_sig) {
    if (dot == end) dot = p;
    int exp_adj = (int)(dot - first_sig);
    if (exp_adj < 0) exp_adj++;
    exp_adj = exp_adj * 4 - 1;
    exp_adj += (int)precision;
    exp_adj -= (int)(parts_count * integer_part_width);

    int exp_result;
    err = csupport_apfloat_total_exponent(p + 1, end, exp_adj, &exp_result);
    if (err) return err;
    *exponent_out = exp_result;
  } else {
    *exponent_out = 0;
  }
  return CSUPPORT_APFLOAT_ERR_NONE;
}

/*--- Decimal string → bignum significand conversion core ---*/
int csupport_apfloat_convert_from_decimal_core(
    const char *first_sig, const char *last_sig,
    int normalized_exp, int exponent,
    uint64_t **dec_significand_out, unsigned *part_count_out) {
  unsigned char_count = (unsigned)(last_sig - first_sig) + 1;
  unsigned pc = ((1 + 196 * char_count / 59) + 63) / 64;
  uint64_t *dec_sig = (uint64_t *)calloc(pc + 1, sizeof(uint64_t));
  if (!dec_sig) return -1;

  unsigned parts = 0;
  const char *p = first_sig;

  while (p <= last_sig) {
    uint64_t val = 0, multiplier = 1;
    do {
      if (*p == '.') { p++; if (p > last_sig) break; continue; }
      unsigned dv = (unsigned)(unsigned char)*p - '0';
      p++;
      if (dv >= 10) { free(dec_sig); return CSUPPORT_APFLOAT_ERR_INVALID_SIG; }
      multiplier *= 10;
      val = val * 10 + dv;
    } while (p <= last_sig && multiplier <= (~(uint64_t)0 - 9) / 10);

    csupport_apint_tc_multiply_part(dec_sig, dec_sig, multiplier, val,
                                    parts, parts + 1, 0);
    if (dec_sig[parts]) parts++;
  }

  *dec_significand_out = dec_sig;
  *part_count_out = parts;
  return 0;
}

/*--- convertToSignExtendedInteger core ---*/
int csupport_apfloat_convert_to_sign_ext_int(
    const uint64_t *src_sig, unsigned src_part_count, unsigned precision,
    int exponent_val, int category, int sign_bit, int is_signed,
    int rounding_mode,
    uint64_t *dst, unsigned dst_part_count, unsigned width,
    int *is_exact) {
  unsigned truncated_bits;
  int lost_fraction;

  *is_exact = 0;

  if (category == LLVM_FC_INFINITY || category == LLVM_FC_NAN)
    return -1; /* opInvalidOp */

  unsigned needed = (width + 63) / 64;
  assert(needed <= dst_part_count);

  if (category == LLVM_FC_ZERO) {
    csupport_apint_tc_set(dst, 0, needed);
    *is_exact = !sign_bit;
    return 0; /* opOK */
  }

  /* fcNormal */
  if (exponent_val < 0) {
    csupport_apint_tc_set(dst, 0, needed);
    truncated_bits = precision - 1U - (unsigned)exponent_val;
  } else {
    unsigned bits = (unsigned)exponent_val + 1U;
    if (bits > width) return -1;
    if (bits < precision) {
      truncated_bits = precision - bits;
      csupport_apint_tc_extract(dst, needed, src_sig, bits, truncated_bits);
    } else {
      csupport_apint_tc_extract(dst, needed, src_sig, precision, 0);
      csupport_apint_shift_left(dst, dst, needed, bits - precision);
      truncated_bits = 0;
    }
  }

  if (truncated_bits) {
    lost_fraction = csupport_apfloat_lost_fraction_truncation(
        src_sig, src_part_count, truncated_bits);
    if (lost_fraction != 0) {
      int rz = csupport_apfloat_round_away_from_zero(
          rounding_mode, lost_fraction, category, sign_bit,
          src_sig, truncated_bits);
      if (rz) {
        if (csupport_apint_tc_increment(dst, needed))
          return -1;
      }
    }
  } else {
    lost_fraction = 0;
  }

  unsigned omsb = csupport_apint_tc_msb(dst, needed) + 1;
  if (sign_bit) {
    if (!is_signed) {
      if (omsb != 0) return -1;
    } else {
      if (omsb == width &&
          csupport_apint_tc_lsb(dst, needed) + 1 != omsb)
        return -1;
      if (omsb > width) return -1;
    }
    csupport_apint_tc_negate(dst, needed);
  } else {
    if (omsb >= width + (unsigned)!is_signed) return -1;
  }

  if (lost_fraction == 0) { *is_exact = 1; return 0; }
  return 1; /* opInexact */
}

/*--- getExactLog2Abs pure C ---*/
int csupport_apfloat_get_exact_log2_abs(
    const uint64_t *significand, unsigned part_count,
    unsigned precision, int exponent_val, int min_exponent,
    int is_finite, int is_zero) {
  if (!is_finite || is_zero) return INT_MIN;

  int pop_count = 0;
  unsigned i;
  for (i = 0; i < part_count; i++) {
    pop_count += __builtin_popcountll(significand[i]);
    if (pop_count > 1) return INT_MIN;
  }

  if (exponent_val != min_exponent) return exponent_val;

  unsigned countr = 0;
  for (i = 0; i < part_count; i++, countr += 64) {
    if (significand[i] != 0) {
      return exponent_val - (int)precision + (int)countr +
             __builtin_ctzll(significand[i]) + 1;
    }
  }
  return INT_MIN;
}

/*--- makeNaN: construct NaN significand in pure C ---*/
void csupport_apfloat_make_nan(uint64_t *significand, unsigned num_parts,
                                unsigned precision, int snan, int *sign,
                                const uint64_t *fill, unsigned fill_words,
                                int non_finite_behavior, int nan_encoding,
                                int is_x87) {
  unsigned qnan_bit = precision - 2;
  unsigned part, bits_to_preserve;

  if (non_finite_behavior == 1 /* NanOnly */) {
    snan = 0;
    if (nan_encoding == 2 /* NegativeZero */)
      *sign = 1;
    fill = NULL;
  }

  if (!fill || fill_words < num_parts)
    csupport_apint_tc_set(significand, 0, num_parts);
  if (fill) {
    unsigned copy_parts = fill_words < num_parts ? fill_words : num_parts;
    csupport_apint_tc_assign(significand, fill, copy_parts);
    bits_to_preserve = precision - 1;
    part = bits_to_preserve / 64;
    bits_to_preserve %= 64;
    significand[part] &= ((1ULL << bits_to_preserve) - 1);
    for (part++; part < num_parts; ++part)
      significand[part] = 0;
  } else if (non_finite_behavior == 1 && nan_encoding != 2) {
    unsigned i;
    for (i = 0; i < num_parts; i++)
      significand[i] = UINT64_MAX;
    bits_to_preserve = precision - 1;
    part = bits_to_preserve / 64;
    bits_to_preserve %= 64;
    significand[part] &= ((1ULL << bits_to_preserve) - 1);
    for (part++; part < num_parts; ++part)
      significand[part] = 0;
  }

  if (snan) {
    csupport_apint_tc_clear_bit(significand, qnan_bit);
    if (csupport_apint_tc_is_zero(significand, num_parts))
      csupport_apint_tc_set_bit(significand, qnan_bit - 1);
  } else if (nan_encoding == 2 /* NegativeZero */) {
    /* quiet NaN with all zero significand bits - do nothing */
  } else {
    csupport_apint_tc_set_bit(significand, qnan_bit);
  }

  if (is_x87)
    csupport_apint_tc_set_bit(significand, qnan_bit + 1);
}

/*--- convertIEEEFloatToAPInt pure C: pack float into uint64_t words ---*/
void csupport_apfloat_convert_ieee_to_words(
    int category, int is_finite_nonzero, int exponent_val,
    int sign_bit, const uint64_t *significand_parts,
    unsigned precision, unsigned size_in_bits,
    int min_exponent, int non_finite_behavior, int nan_encoding,
    uint64_t *words, unsigned num_words) {
  int bias = -(min_exponent - 1);
  unsigned trailing_sig_bits = precision - 1;
  unsigned exponent_bits = size_in_bits - 1 - trailing_sig_bits;
  uint64_t exponent_mask = (exponent_bits < 64) ? ((UINT64_C(1) << exponent_bits) - 1) : UINT64_MAX;
  uint64_t significand_mask_val;
  unsigned num_sig_parts = (trailing_sig_bits + 63) / 64;
  unsigned integer_bit_part = trailing_sig_bits / 64;
  uint64_t integer_bit = UINT64_C(1) << (trailing_sig_bits % 64);
  uint64_t myexponent;
  unsigned last_word = num_words - 1;
  unsigned i;

  significand_mask_val = (trailing_sig_bits % 64 == 0) ? 0 : (integer_bit - 1);

  if (is_finite_nonzero) {
    myexponent = (uint64_t)(exponent_val + bias);
    for (i = 0; i < num_sig_parts && i < num_words; i++)
      words[i] = significand_parts[i];
    for (; i < num_words; i++)
      words[i] = 0;
    if (myexponent == 1 &&
        !(significand_parts[integer_bit_part] & integer_bit))
      myexponent = 0;
  } else if (category == LLVM_FC_ZERO) {
    myexponent = (uint64_t)((min_exponent - 1) + bias);
    memset(words, 0, num_words * sizeof(uint64_t));
  } else if (category == LLVM_FC_INFINITY) {
    myexponent = exponent_mask;
    memset(words, 0, num_words * sizeof(uint64_t));
  } else if (category == LLVM_FC_NAN) {
    if (non_finite_behavior == 1 && nan_encoding == 2)
      myexponent = 0;
    else
      myexponent = exponent_mask;
    for (i = 0; i < num_sig_parts && i < num_words; i++)
      words[i] = significand_parts[i];
    for (; i < num_words; i++)
      words[i] = 0;
  } else {
    /* LLVM fcNormal with !is_finite_nonzero — emit quiet NaN */
    myexponent = exponent_mask;
    memset(words, 0, num_words * sizeof(uint64_t));
  }

  if (significand_mask_val != 0 && num_sig_parts > 0)
    words[num_sig_parts - 1] &= significand_mask_val;

  words[last_word] |= ((uint64_t)(sign_bit & 1)) << ((size_in_bits - 1) % 64);
  words[last_word] |= (myexponent & exponent_mask) << (trailing_sig_bits % 64);
}

/*--- divideSpecials dispatch table ---*/
/* Caller convention: r=0 → opOK (out_cat: LLVM_FC_ZERO); r=-1 → NaN prop; r=-2 → makeNaN+InvalidOp; r=-4 → DivByZero */
int csupport_apfloat_divide_specials(int lhs_cat, int rhs_cat,
                                      int *out_category) {
  int key = lhs_cat * 4 + rhs_cat;
  switch (key) {
  /* {Zero,Normal,Inf} / NaN → NaN propagation (assign rhs) */
  case LLVM_FC_ZERO*4+LLVM_FC_NAN:
  case LLVM_FC_NORMAL*4+LLVM_FC_NAN:
  case LLVM_FC_INFINITY*4+LLVM_FC_NAN:
  /* NaN / {Zero,Normal,Inf,NaN} → NaN propagation (keep lhs) */
  case LLVM_FC_NAN*4+LLVM_FC_ZERO:
  case LLVM_FC_NAN*4+LLVM_FC_NORMAL:
  case LLVM_FC_NAN*4+LLVM_FC_INFINITY:
  case LLVM_FC_NAN*4+LLVM_FC_NAN:
    return -1;
  /* Inf/Zero, Inf/Normal, Zero/Inf, Zero/Normal → opOK, keep current */
  case LLVM_FC_INFINITY*4+LLVM_FC_ZERO:
  case LLVM_FC_INFINITY*4+LLVM_FC_NORMAL:
  case LLVM_FC_ZERO*4+LLVM_FC_INFINITY:
  case LLVM_FC_ZERO*4+LLVM_FC_NORMAL:
    return 0;
  /* Normal/Inf → Zero */
  case LLVM_FC_NORMAL*4+LLVM_FC_INFINITY:
    *out_category = LLVM_FC_ZERO; return 0;
  /* Normal/Zero → DivByZero (caller sets Inf or NaN based on semantics) */
  case LLVM_FC_NORMAL*4+LLVM_FC_ZERO:
    return -4;
  /* Inf/Inf, Zero/Zero → InvalidOp → makeNaN */
  case LLVM_FC_INFINITY*4+LLVM_FC_INFINITY:
  case LLVM_FC_ZERO*4+LLVM_FC_ZERO:
    return -2;
  /* Normal/Normal → defer to full divide */
  case LLVM_FC_NORMAL*4+LLVM_FC_NORMAL:
    return 0;
  default:
    return -3;
  }
}

/*--- remainderSpecials dispatch table ---*/
/* Caller convention: r=0 → opOK; r=-1 → NaN prop; r=-2 → makeNaN+InvalidOp */
int csupport_apfloat_remainder_specials(int lhs_cat, int rhs_cat,
                                          int *out_category) {
  int key = lhs_cat * 4 + rhs_cat;
  switch (key) {
  /* {Zero,Normal,Inf} % NaN → NaN propagation (assign rhs) */
  case LLVM_FC_ZERO*4+LLVM_FC_NAN:
  case LLVM_FC_NORMAL*4+LLVM_FC_NAN:
  case LLVM_FC_INFINITY*4+LLVM_FC_NAN:
  /* NaN % {Zero,Normal,Inf,NaN} → NaN propagation (keep lhs) */
  case LLVM_FC_NAN*4+LLVM_FC_ZERO:
  case LLVM_FC_NAN*4+LLVM_FC_NORMAL:
  case LLVM_FC_NAN*4+LLVM_FC_INFINITY:
  case LLVM_FC_NAN*4+LLVM_FC_NAN:
    return -1;
  /* Zero%Inf, Zero%Normal, Normal%Inf → opOK */
  case LLVM_FC_ZERO*4+LLVM_FC_INFINITY:
  case LLVM_FC_ZERO*4+LLVM_FC_NORMAL:
  case LLVM_FC_NORMAL*4+LLVM_FC_INFINITY:
    return 0;
  /* Normal%Zero, Inf%Zero, Inf%Normal, Inf%Inf, Zero%Zero → InvalidOp */
  case LLVM_FC_NORMAL*4+LLVM_FC_ZERO:
  case LLVM_FC_INFINITY*4+LLVM_FC_ZERO:
  case LLVM_FC_INFINITY*4+LLVM_FC_NORMAL:
  case LLVM_FC_INFINITY*4+LLVM_FC_INFINITY:
  case LLVM_FC_ZERO*4+LLVM_FC_ZERO:
    return -2;
  /* Normal%Normal → proceed with computation (sentinel) */
  case LLVM_FC_NORMAL*4+LLVM_FC_NORMAL:
    return -5;
  default:
    return -3;
  }
}

/*--- modSpecials dispatch table ---*/
/* Caller convention: r=0 → opOK; r=-1 → NaN prop; r=-2 → makeNaN+InvalidOp */
int csupport_apfloat_mod_specials(int lhs_cat, int rhs_cat,
                                    int *out_category) {
  int key = lhs_cat * 4 + rhs_cat;
  switch (key) {
  /* {Zero,Normal,Inf} mod NaN → NaN propagation (assign rhs) */
  case LLVM_FC_ZERO*4+LLVM_FC_NAN:
  case LLVM_FC_NORMAL*4+LLVM_FC_NAN:
  case LLVM_FC_INFINITY*4+LLVM_FC_NAN:
  /* NaN mod {Zero,Normal,Inf,NaN} → NaN propagation (keep lhs) */
  case LLVM_FC_NAN*4+LLVM_FC_ZERO:
  case LLVM_FC_NAN*4+LLVM_FC_NORMAL:
  case LLVM_FC_NAN*4+LLVM_FC_INFINITY:
  case LLVM_FC_NAN*4+LLVM_FC_NAN:
    return -1;
  /* Zero%Inf, Zero%Normal, Normal%Inf → opOK */
  case LLVM_FC_ZERO*4+LLVM_FC_INFINITY:
  case LLVM_FC_ZERO*4+LLVM_FC_NORMAL:
  case LLVM_FC_NORMAL*4+LLVM_FC_INFINITY:
    return 0;
  /* Normal%Zero, Inf%Zero, Inf%Normal, Inf%Inf, Zero%Zero → InvalidOp */
  case LLVM_FC_NORMAL*4+LLVM_FC_ZERO:
  case LLVM_FC_INFINITY*4+LLVM_FC_ZERO:
  case LLVM_FC_INFINITY*4+LLVM_FC_NORMAL:
  case LLVM_FC_INFINITY*4+LLVM_FC_INFINITY:
  case LLVM_FC_ZERO*4+LLVM_FC_ZERO:
    return -2;
  /* Normal%Normal → proceed with computation */
  case LLVM_FC_NORMAL*4+LLVM_FC_NORMAL:
    return 0;
  default:
    return -3;
  }
}

/*--- addOrSubtractSpecials dispatch table ---*/
/* Caller convention: r=0 → opOK; r=-1 → assign RHS + NaN handling; r=-2 → makeNaN+InvalidOp; r=-5 → full add */
int csupport_apfloat_add_or_subtract_specials(int lhs_cat, int rhs_cat,
                                               int subtract,
                                               int lhs_sign, int rhs_sign,
                                               int *out_category,
                                               int *out_sign) {
  int key = lhs_cat * 4 + rhs_cat;
  switch (key) {
  /* {Zero,Normal,Inf} + NaN → assign RHS (NaN), then NaN handling */
  case LLVM_FC_ZERO*4+LLVM_FC_NAN:
  case LLVM_FC_NORMAL*4+LLVM_FC_NAN:
  case LLVM_FC_INFINITY*4+LLVM_FC_NAN:
    return -1;
  /* NaN + {Zero,Normal,Inf,NaN} → keep LHS (NaN), NaN handling */
  case LLVM_FC_NAN*4+LLVM_FC_ZERO:
  case LLVM_FC_NAN*4+LLVM_FC_NORMAL:
  case LLVM_FC_NAN*4+LLVM_FC_INFINITY:
  case LLVM_FC_NAN*4+LLVM_FC_NAN:
    return 0;
  /* Normal+Zero, Inf+Normal, Inf+Zero → opOK, keep LHS */
  case LLVM_FC_NORMAL*4+LLVM_FC_ZERO:
  case LLVM_FC_INFINITY*4+LLVM_FC_NORMAL:
  case LLVM_FC_INFINITY*4+LLVM_FC_ZERO:
    return 0;
  /* Normal+Inf, Zero+Inf → become Infinity with rhs sign */
  case LLVM_FC_NORMAL*4+LLVM_FC_INFINITY:
  case LLVM_FC_ZERO*4+LLVM_FC_INFINITY:
    *out_category = LLVM_FC_INFINITY;
    *out_sign = rhs_sign ^ subtract;
    return 0;
  /* Zero+Normal → assign RHS */
  case LLVM_FC_ZERO*4+LLVM_FC_NORMAL:
    *out_sign = rhs_sign ^ subtract;
    return -1;
  /* Zero+Zero → sign handled by caller */
  case LLVM_FC_ZERO*4+LLVM_FC_ZERO:
    return 0;
  /* Inf+Inf → check sign cancellation */
  case LLVM_FC_INFINITY*4+LLVM_FC_INFINITY:
    if (((lhs_sign ^ rhs_sign) != 0) != subtract) {
      return -2;
    }
    return 0;
  /* Normal+Normal → full significand add */
  case LLVM_FC_NORMAL*4+LLVM_FC_NORMAL:
    return -5;
  default:
    return -3;
  }
}

/*--- unpackIEEEWords: extract sign/exponent/significand/category from IEEE words ---*/
int csupport_apfloat_unpack_ieee(
    const uint64_t *words, unsigned num_words,
    unsigned precision, unsigned size_in_bits,
    int min_exponent, int non_finite_behavior, int nan_encoding,
    int *out_sign, int *out_exponent,
    uint64_t *out_significand, unsigned max_sig_parts) {
  unsigned trailing_sig_bits = precision - 1;
  unsigned stored_sig_parts = (trailing_sig_bits + 63) / 64;
  unsigned exponent_bits = size_in_bits - 1 - trailing_sig_bits;
  uint64_t exponent_mask = (exponent_bits < 64) ? ((UINT64_C(1) << exponent_bits) - 1) : UINT64_MAX;
  int bias = -(min_exponent - 1);
  uint64_t integer_bit = UINT64_C(1) << (trailing_sig_bits % 64);
  uint64_t significand_mask_val = (trailing_sig_bits % 64 == 0) ? 0 : (integer_bit - 1);
  unsigned i;
  int all_zero, all_ones;

  assert(stored_sig_parts <= max_sig_parts);

  memcpy(out_significand, words, stored_sig_parts * sizeof(uint64_t));
  if (significand_mask_val != 0)
    out_significand[stored_sig_parts - 1] &= significand_mask_val;

  uint64_t last_word = words[num_words - 1];
  uint64_t myexponent = (last_word >> (trailing_sig_bits % 64)) & exponent_mask;

  *out_sign = (int)(last_word >> ((size_in_bits - 1) % 64)) & 1;

  all_zero = 1;
  for (i = 0; i < stored_sig_parts; i++) {
    if (out_significand[i] != 0) { all_zero = 0; break; }
  }

  if (myexponent == 0 && all_zero) {
    if (nan_encoding == 2 /* NegativeZero */ && *out_sign) {
      *out_exponent = min_exponent - 1;
      return CSUPPORT_APFLOAT_CAT_NAN;
    }
    *out_exponent = 0;
    return CSUPPORT_APFLOAT_CAT_ZERO;
  }

  /* Check infinity (IEEE754 behavior only).
     For IEEE754, Inf has biased exponent = exponent_mask and zero significand. */
  if (non_finite_behavior == 0 /* IEEE754 */) {
    if (myexponent == exponent_mask && all_zero) {
      *out_exponent = (int)exponent_mask - bias;
      return CSUPPORT_APFLOAT_CAT_INF;
    }
  }

  /* Check NaN.
     For IEEE/AllOnes, NaN has biased exponent = exponent_mask.
     For NegativeZero encoding, NaN is sign=1 + all zeros (handled above). */
  {
    int is_nan = 0;
    int exponent_nan = (nan_encoding == 2) ? (min_exponent - 1)
                                           : (int)exponent_mask - bias;

    if (nan_encoding == 0 /* IEEE */) {
      is_nan = (myexponent == exponent_mask) && !all_zero;
    } else if (nan_encoding == 1 /* AllOnes */) {
      all_ones = 1;
      for (i = 0; i + 1 < stored_sig_parts; i++) {
        if (out_significand[i] != UINT64_MAX) { all_ones = 0; break; }
      }
      if (all_ones && significand_mask_val != 0) {
        all_ones = (out_significand[stored_sig_parts - 1] == significand_mask_val);
      }
      is_nan = (myexponent == exponent_mask) && all_ones;
    } else if (nan_encoding == 2 /* NegativeZero */) {
      is_nan = (myexponent == 0 && all_zero && *out_sign);
    }

    if (is_nan) {
      *out_exponent = exponent_nan;
      return CSUPPORT_APFLOAT_CAT_NAN;
    }
  }

  /* Normal or denormal */
  *out_exponent = (int)myexponent - bias;
  if (myexponent == 0) {
    *out_exponent = min_exponent;
    return CSUPPORT_APFLOAT_CAT_DENORM;
  }
  out_significand[stored_sig_parts - 1] |= integer_bit;
  return CSUPPORT_APFLOAT_CAT_NORMAL;
}

/*--- F80 (x87 extended) unpack ---*/
int csupport_apfloat_unpack_f80(
    const uint64_t *raw_words,
    int *out_sign, int *out_exponent,
    uint64_t *out_significand) {
  uint64_t i1 = raw_words[0];
  uint64_t i2 = raw_words[1];
  uint64_t myexponent = (i2 & 0x7fff);
  uint64_t mysignificand = i1;
  uint8_t myintegerbit = (uint8_t)(mysignificand >> 63);

  *out_sign = (int)(i2 >> 15);
  *out_significand = mysignificand;

  if (myexponent == 0 && mysignificand == 0) {
    return CSUPPORT_APFLOAT_CAT_ZERO;
  } else if (myexponent == 0x7fff && mysignificand == UINT64_C(0x8000000000000000)) {
    return CSUPPORT_APFLOAT_CAT_INF;
  } else if ((myexponent == 0x7fff && mysignificand != UINT64_C(0x8000000000000000)) ||
             (myexponent != 0x7fff && myexponent != 0 && myintegerbit == 0)) {
    *out_exponent = 16384; /* exponentNaN for x87 */
    return CSUPPORT_APFLOAT_CAT_NAN;
  } else {
    *out_exponent = (int)myexponent - 16383;
    if (myexponent == 0)
      *out_exponent = -16382;
    return CSUPPORT_APFLOAT_CAT_NORMAL;
  }
}

/*--- Compare categories dispatch ---*/
/* Returns: 0=equal, 1=greater, -1=less, -2=unordered, 2=needs_abs_compare */
/* Mirrors llvm::IEEEFloat::compare PackCategoriesIntoKey (llvm/ADT/APFloat.cpp). */
int csupport_apfloat_compare_categories(int lhs_cat, int rhs_cat,
                                         int lhs_sign, int rhs_sign) {
  /* LLVM fltCategory: fcInfinity=0, fcNaN=1, fcNormal=2, fcZero=3 */
  if (lhs_cat == LLVM_FC_NAN || rhs_cat == LLVM_FC_NAN)
    return -2;

  int key = lhs_cat * 4 + rhs_cat;
  switch (key) {
  case LLVM_FC_INFINITY * 4 + LLVM_FC_INFINITY:
    if (lhs_sign == rhs_sign)
      return 0;
    return lhs_sign ? -1 : 1;

  case LLVM_FC_INFINITY * 4 + LLVM_FC_NORMAL:
  case LLVM_FC_INFINITY * 4 + LLVM_FC_ZERO:
  case LLVM_FC_NORMAL * 4 + LLVM_FC_ZERO:
    if (lhs_sign)
      return -1;
    return 1;

  case LLVM_FC_NORMAL * 4 + LLVM_FC_INFINITY:
  case LLVM_FC_ZERO * 4 + LLVM_FC_INFINITY:
  case LLVM_FC_ZERO * 4 + LLVM_FC_NORMAL:
    if (rhs_sign)
      return 1;
    return -1;

  case LLVM_FC_ZERO * 4 + LLVM_FC_ZERO:
    return 0;

  case LLVM_FC_NORMAL * 4 + LLVM_FC_NORMAL:
    return 2;

  default:
    return -3;
  }
}

/*--- Specials dispatch: multiplySpecials ---*/
/* Caller convention: r=0 → opOK; r=-1 → NaN prop; r=-2 → makeNaN+InvalidOp */
int csupport_apfloat_multiply_specials(int lhs_cat, int rhs_cat,
                                        int *out_category) {
  int key = lhs_cat * 4 + rhs_cat;
  switch (key) {
  /* {Zero,Normal,Inf} × NaN → NaN propagation (assign rhs) */
  case LLVM_FC_ZERO*4+LLVM_FC_NAN:
  case LLVM_FC_NORMAL*4+LLVM_FC_NAN:
  case LLVM_FC_INFINITY*4+LLVM_FC_NAN:
  /* NaN × {Zero,Normal,Inf,NaN} → NaN propagation (keep lhs) */
  case LLVM_FC_NAN*4+LLVM_FC_ZERO:
  case LLVM_FC_NAN*4+LLVM_FC_NORMAL:
  case LLVM_FC_NAN*4+LLVM_FC_INFINITY:
  case LLVM_FC_NAN*4+LLVM_FC_NAN:
    return -1;
  /* {Normal,Inf} × {Inf,Normal}, Inf×Inf → Infinity */
  case LLVM_FC_NORMAL*4+LLVM_FC_INFINITY:
  case LLVM_FC_INFINITY*4+LLVM_FC_NORMAL:
  case LLVM_FC_INFINITY*4+LLVM_FC_INFINITY:
    *out_category = LLVM_FC_INFINITY; return 0;
  /* Normal×Zero, Zero×Normal, Zero×Zero → Zero */
  case LLVM_FC_NORMAL*4+LLVM_FC_ZERO:
  case LLVM_FC_ZERO*4+LLVM_FC_NORMAL:
  case LLVM_FC_ZERO*4+LLVM_FC_ZERO:
    *out_category = LLVM_FC_ZERO; return 0;
  /* Normal×Normal → defer to full multiply */
  case LLVM_FC_NORMAL*4+LLVM_FC_NORMAL:
    return 0;
  /* Zero×Inf, Inf×Zero → InvalidOp (0 × ∞ = NaN) */
  case LLVM_FC_ZERO*4+LLVM_FC_INFINITY:
  case LLVM_FC_INFINITY*4+LLVM_FC_ZERO:
    return -2;
  default:
    return -3;
  }
}

int csupport_apfloat_is_integer_significand(const uint64_t *parts,
                                             unsigned part_count,
                                             int exponent, unsigned precision) {
  int min_trailing = precision - 1 - exponent;
  if (min_trailing <= 0) return 1;
  if ((unsigned)min_trailing >= precision) return 0;
  unsigned trailing_bits = (unsigned)min_trailing;
  unsigned full_words = trailing_bits / 64;
  unsigned rem_bits = trailing_bits % 64;
  for (unsigned i = 0; i < full_words && i < part_count; i++) {
    if (parts[i] != 0) return 0;
  }
  if (full_words < part_count && rem_bits > 0) {
    uint64_t mask = ((uint64_t)1 << rem_bits) - 1;
    if (parts[full_words] & mask) return 0;
  }
  return 1;
}

int csupport_apfloat_classify(int category, int sign, int is_denormal) {
  switch (category) {
  case LLVM_FC_INFINITY:
    return sign ? -1 : 1;
  case LLVM_FC_NAN:
    return 0;
  case LLVM_FC_ZERO:
    return sign ? -2 : 2;
  case LLVM_FC_NORMAL:
    return is_denormal ? (sign ? -3 : 3) : (sign ? -4 : 4);
  default:
    return -99;
  }
}

size_t csupport_apfloat_format_hex(const uint64_t *significand,
                                    unsigned part_count, unsigned precision,
                                    int exponent, int sign,
                                    int uppercase,
                                    char *buf, size_t buflen) {
  if (!buf || buflen == 0) return 0;
  const char *hex = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
  size_t pos = 0;
  if (pos < buflen && sign) buf[pos++] = '-';
  if (pos + 2 < buflen) { buf[pos++] = '0'; buf[pos++] = uppercase ? 'X' : 'x'; }
  unsigned hex_digits = (precision + 3) / 4;
  if (hex_digits == 0) hex_digits = 1;
  unsigned bit_pos = precision - 1;
  int first = 1;
  for (unsigned d = 0; d < hex_digits && pos < buflen - 1; d++) {
    unsigned val = 0;
    for (unsigned b = 0; b < 4; b++) {
      if (bit_pos < precision) {
        unsigned w = bit_pos / 64;
        unsigned bi = bit_pos % 64;
        if (w < part_count && ((significand[w] >> bi) & 1))
          val |= (1u << (3 - b));
      }
      if (bit_pos == 0) break;
      bit_pos--;
    }
    if (first) {
      buf[pos++] = hex[val];
      if (hex_digits > 1 && pos < buflen) buf[pos++] = '.';
      first = 0;
    } else {
      buf[pos++] = hex[val];
    }
  }
  if (pos + 3 < buflen) {
    buf[pos++] = uppercase ? 'P' : 'p';
    int exp_val = exponent;
    if (exp_val < 0) {
      buf[pos++] = '-';
      exp_val = -exp_val;
    } else {
      buf[pos++] = '+';
    }
    char exp_buf[16];
    int exp_len = 0;
    if (exp_val == 0) {
      exp_buf[exp_len++] = '0';
    } else {
      while (exp_val > 0) {
        exp_buf[exp_len++] = '0' + (exp_val % 10);
        exp_val /= 10;
      }
    }
    for (int i = exp_len - 1; i >= 0 && pos < buflen; i--)
      buf[pos++] = exp_buf[i];
  }
  if (pos < buflen) buf[pos] = '\0';
  return pos;
}

static uint64_t *apf_sig_ptr(uint64_t *inline_part, uint64_t **heap_parts,
                             unsigned part_count) {
  return part_count > 1 ? *heap_parts : inline_part;
}

static void apf_free_sig(uint64_t **heap_parts, unsigned part_count) {
  if (part_count > 1 && *heap_parts) {
    free(*heap_parts);
    *heap_parts = NULL;
  }
}

static int apf_conv_shift_right(uint64_t *dst, unsigned parts, unsigned bits) {
  int lf = csupport_apfloat_lost_fraction_truncation(dst, parts, bits);
  csupport_apint_shift_right_logical(dst, dst, parts, bits);
  return lf;
}

int csupport_apfloat_convert_semantics(
    const csupport_flt_semantics_t *from_sem,
    const csupport_flt_semantics_t *to_sem,
    int from_is_x87_ext,
    int to_is_x87_ext,
    uint64_t *inline_part,
    uint64_t **heap_parts,
    unsigned old_part_count,
    int *exponent,
    int *category,
    int *sign,
    int rounding_mode,
    int is_signaling,
    int finite_nonzero,
    int *loses_info) {
  int lost_fraction = CSUPPORT_LF_EXACTLY_ZERO;
  unsigned new_pc = csupport_flt_part_count_for_bits(to_sem->precision + 1);
  unsigned old_pc = old_part_count;
  int shift = (int)to_sem->precision - (int)from_sem->precision;
  int fs = 0;
  uint64_t *sig;
  int x86_special_nan = 0;
  unsigned sig_parts = old_pc;

  sig = apf_sig_ptr(inline_part, heap_parts, old_pc);

  if (from_is_x87_ext && !to_is_x87_ext && *category == LLVM_FC_NAN &&
      (!(*sig & UINT64_C(0x8000000000000000)) ||
       !(*sig & UINT64_C(0x4000000000000000))))
    x86_special_nan = 1;

  if (shift < 0 && finite_nonzero) {
    int omsb = (int)csupport_apint_tc_msb(sig, old_pc) + 1;
    int exponent_change = omsb - (int)from_sem->precision;
    if (*exponent + exponent_change < to_sem->minExponent)
      exponent_change = to_sem->minExponent - *exponent;
    if (exponent_change < shift)
      exponent_change = shift;
    if (exponent_change < 0) {
      shift -= exponent_change;
      *exponent += exponent_change;
    } else if (omsb <= -shift) {
      exponent_change = omsb + shift - 1;
      shift -= exponent_change;
      *exponent += exponent_change;
    }
  }

  if (shift < 0 &&
      (finite_nonzero ||
       (*category == LLVM_FC_NAN &&
        from_sem->nonFiniteBehavior != CSUPPORT_FLT_NFB_NAN_ONLY)))
    lost_fraction =
        apf_conv_shift_right(sig, old_pc, (unsigned)(-shift));

  if (new_pc > old_pc) {
    uint64_t *new_parts = (uint64_t *)malloc(new_pc * sizeof(uint64_t));
    if (!new_parts)
      return 0x01;
    csupport_apint_tc_set(new_parts, 0, new_pc);
    if (finite_nonzero || *category == LLVM_FC_NAN)
      csupport_apint_tc_assign(new_parts, sig, old_pc);
    apf_free_sig(heap_parts, old_pc);
    *heap_parts = new_parts;
    sig_parts = new_pc;
    sig = new_parts;
  } else if (new_pc == 1 && old_pc != 1) {
    uint64_t new_part = 0;
    if (finite_nonzero || *category == LLVM_FC_NAN)
      new_part = sig[0];
    apf_free_sig(heap_parts, old_pc);
    *inline_part = new_part;
    sig_parts = 1;
    sig = inline_part;
  } else
    sig_parts = old_pc;

  if (shift > 0 && (finite_nonzero || *category == LLVM_FC_NAN))
    csupport_apint_shift_left(sig, sig, new_pc, (unsigned)shift);

  if (finite_nonzero) {
    int cat = *category, s = *sign ? 1 : 0, exp = *exponent;
    fs = csupport_apfloat_normalize(
        sig, sig_parts, &exp, &cat, &s, rounding_mode, lost_fraction,
        to_sem->precision, to_sem->maxExponent, to_sem->minExponent,
        (int)to_sem->nonFiniteBehavior, (int)to_sem->nanEncoding);
    *exponent = exp;
    *category = cat;
    *sign = s;
    *loses_info = (fs != 0);
  } else if (*category == LLVM_FC_NAN) {
    if (to_sem->nonFiniteBehavior == CSUPPORT_FLT_NFB_NAN_ONLY) {
      *loses_info =
          (from_sem->nonFiniteBehavior != CSUPPORT_FLT_NFB_NAN_ONLY);
      *category = LLVM_FC_NAN;
      *exponent = csupport_flt_exponent_nan(to_sem);
      csupport_apfloat_make_nan(
          sig, sig_parts, to_sem->precision, 0, sign, NULL, 0,
          (int)to_sem->nonFiniteBehavior, (int)to_sem->nanEncoding,
          to_is_x87_ext);
      return is_signaling ? 0x01 : 0;
    }

    if (from_sem->nanEncoding == CSUPPORT_FLT_NAN_NEG_ZERO &&
        to_sem->nanEncoding != CSUPPORT_FLT_NAN_NEG_ZERO) {
      *category = LLVM_FC_NAN;
      *exponent = csupport_flt_exponent_nan(to_sem);
      *sign = 0;
      csupport_apfloat_make_nan(
          sig, sig_parts, to_sem->precision, 0, sign, NULL, 0,
          (int)to_sem->nonFiniteBehavior, (int)to_sem->nanEncoding,
          to_is_x87_ext);
    }

    *loses_info = (lost_fraction != CSUPPORT_LF_EXACTLY_ZERO) || x86_special_nan;

    if (!x86_special_nan && to_is_x87_ext)
      csupport_apint_tc_set_bit(sig, to_sem->precision - 1);

    if (is_signaling) {
      if (to_sem->nonFiniteBehavior != CSUPPORT_FLT_NFB_NAN_ONLY)
        csupport_apint_tc_set_bit(sig, to_sem->precision - 2);
      fs = 0x01;
    } else
      fs = 0;
  } else if (*category == LLVM_FC_INFINITY &&
             to_sem->nonFiniteBehavior == CSUPPORT_FLT_NFB_NAN_ONLY) {
    *category = LLVM_FC_NAN;
    *exponent = csupport_flt_exponent_nan(to_sem);
    csupport_apfloat_make_nan(
        sig, sig_parts, to_sem->precision, 0, sign, NULL, 0,
        (int)to_sem->nonFiniteBehavior, (int)to_sem->nanEncoding,
        to_is_x87_ext);
    *loses_info = 1;
    fs = 0x10;
  } else if (*category == LLVM_FC_ZERO &&
             to_sem->nanEncoding == CSUPPORT_FLT_NAN_NEG_ZERO) {
    *loses_info =
        (from_sem->nanEncoding != CSUPPORT_FLT_NAN_NEG_ZERO && *sign);
    fs = *loses_info ? 0x10 : 0;
    *sign = 0;
  } else {
    *loses_info = 0;
    fs = 0;
  }

  return fs;
}

int csupport_apfloat_multiply_significand_fma(
    const csupport_flt_semantics_t *sem,
    uint64_t *lhs_sig,
    unsigned lhs_pc,
    int *lhs_exp,
    int *lhs_sign,
    const uint64_t *rhs_sig,
    int rhs_exp,
    const uint64_t *add_sig,
    unsigned add_pc,
    int add_exp,
    int add_cat,
    int add_sign,
    int addend_nonzero) {
  unsigned precision = sem->precision;
  unsigned new_pc = csupport_flt_part_count_for_bits(precision * 2 + 1);
  unsigned ext_prec_field = 2 * precision + 1;
  unsigned ext_wp = csupport_flt_part_count_for_bits(ext_prec_field + 1);
  unsigned full_words = new_pc;
  unsigned prod_words = 2 * lhs_pc;
  if (ext_wp > full_words)
    full_words = ext_wp;
  if (prod_words > full_words)
    full_words = prod_words;

  uint64_t scratch[16];
  uint64_t *full =
      (full_words <= 16) ? scratch : (uint64_t *)malloc(full_words * sizeof(uint64_t));
  unsigned omsb;
  int lost_fraction = CSUPPORT_LF_EXACTLY_ZERO;

  (void)rhs_exp;

  if (!full)
    return CSUPPORT_LF_EXACTLY_ZERO;

  memset(full, 0, full_words * sizeof(uint64_t));
  csupport_apint_tc_full_multiply(full, lhs_sig, rhs_sig, lhs_pc, lhs_pc);

  omsb = (unsigned)csupport_apint_tc_msb(full, new_pc) + 1;
  *lhs_exp += rhs_exp + 2;

  if (addend_nonzero) {
    csupport_flt_semantics_t ext_sem = *sem;
    int add_loses = 0;
    uint64_t add_il = 0;
    uint64_t *add_hp = NULL;
    int aexp = add_exp;
    int acat = add_cat;
    int asgn = add_sign;
    unsigned add_in_pc = add_pc;

    ext_sem.precision = ext_prec_field;

    if (omsb != ext_prec_field - 1) {
      assert(ext_prec_field > omsb);
      csupport_apint_shift_left(full, full, new_pc, (ext_prec_field - 1) - omsb);
      *lhs_exp -= (int)((ext_prec_field - 1) - omsb);
    }

    if (add_pc > 1) {
      add_hp = (uint64_t *)malloc(add_pc * sizeof(uint64_t));
      if (!add_hp) {
        if (full != scratch)
          free(full);
        return CSUPPORT_LF_EXACTLY_ZERO;
      }
      memcpy(add_hp, add_sig, add_pc * sizeof(uint64_t));
    } else
      add_il = add_sig[0];

    (void)csupport_apfloat_convert_semantics(
        sem, &ext_sem, 0, 0, &add_il, &add_hp, add_in_pc, &aexp, &acat, &asgn,
        0, 0,
        (add_cat == LLVM_FC_NORMAL) &&
            !csupport_apint_tc_is_zero(
                apf_sig_ptr(&add_il, &add_hp, add_in_pc), add_in_pc),
        &add_loses);
    (void)add_loses;

    {
      uint64_t *ap = apf_sig_ptr(&add_il, &add_hp, ext_wp);
      int sh_lf =
          csupport_apfloat_lost_fraction_truncation(ap, ext_wp, 1);
      csupport_apint_shift_right_logical(ap, ap, ext_wp, 1);
      (void)sh_lf;
      aexp += 1;
    }

    lost_fraction = csupport_apfloat_add_or_subtract_significand(
        full, ext_wp, lhs_exp, lhs_sign,
        apf_sig_ptr(&add_il, &add_hp, ext_wp), aexp, asgn, 0);

    if (add_hp)
      free(add_hp);

    omsb = (unsigned)csupport_apint_tc_msb(full, new_pc) + 1;
  }

  *lhs_exp -= (int)(precision + 1);

  if (omsb > precision) {
    unsigned bits = omsb - precision;
    unsigned sig_parts = csupport_flt_part_count_for_bits(omsb);
    int lf = apf_conv_shift_right(full, sig_parts, bits);
    lost_fraction =
        csupport_apfloat_combine_lost_fractions(lf, lost_fraction);
    *lhs_exp += (int)bits;
  }

  csupport_apint_tc_assign(lhs_sig, full, lhs_pc);

  if (full != scratch)
    free(full);

  return lost_fraction;
}

/*===-- fltSemantics constants and accessors ---===*/

const csupport_flt_semantics_t csupport_sem_ieee_half = {15, -14, 11, 16, 0, 0};
const csupport_flt_semantics_t csupport_sem_bfloat = {127, -126, 8, 16, 0, 0};
const csupport_flt_semantics_t csupport_sem_ieee_single = {127, -126, 24, 32, 0, 0};
const csupport_flt_semantics_t csupport_sem_ieee_double = {1023, -1022, 53, 64, 0, 0};
const csupport_flt_semantics_t csupport_sem_ieee_quad = {16383, -16382, 113, 128, 0, 0};
const csupport_flt_semantics_t csupport_sem_float8_e5m2 = {15, -14, 3, 8, 0, 0};
const csupport_flt_semantics_t csupport_sem_float8_e5m2fnuz = {
    15, -15, 3, 8, CSUPPORT_FLT_NFB_NAN_ONLY, CSUPPORT_FLT_NAN_NEG_ZERO};
const csupport_flt_semantics_t csupport_sem_float8_e4m3fn = {
    8, -6, 4, 8, CSUPPORT_FLT_NFB_NAN_ONLY, CSUPPORT_FLT_NAN_ALL_ONES};
const csupport_flt_semantics_t csupport_sem_float8_e4m3fnuz = {
    7, -7, 4, 8, CSUPPORT_FLT_NFB_NAN_ONLY, CSUPPORT_FLT_NAN_NEG_ZERO};
const csupport_flt_semantics_t csupport_sem_float8_e4m3b11fnuz = {
    4, -10, 4, 8, CSUPPORT_FLT_NFB_NAN_ONLY, CSUPPORT_FLT_NAN_NEG_ZERO};
const csupport_flt_semantics_t csupport_sem_float_tf32 = {127, -126, 11, 19, 0, 0};
const csupport_flt_semantics_t csupport_sem_x87_double_extended = {16383, -16382, 64, 80, 0, 0};
const csupport_flt_semantics_t csupport_sem_bogus = {0, 0, 0, 0, 0, 0};
const csupport_flt_semantics_t csupport_sem_ppc_double_double = {-1, 0, 0, 128, 0, 0};
const csupport_flt_semantics_t csupport_sem_ppc_double_double_legacy = {
    1023, -1022 + 53, 53 + 53, 128, 0, 0};

unsigned csupport_flt_semantics_precision(const csupport_flt_semantics_t *s) {
  return s->precision;
}
int32_t csupport_flt_semantics_max_exponent(const csupport_flt_semantics_t *s) {
  return s->maxExponent;
}
int32_t csupport_flt_semantics_min_exponent(const csupport_flt_semantics_t *s) {
  return s->minExponent;
}
unsigned csupport_flt_semantics_size_in_bits(const csupport_flt_semantics_t *s) {
  return s->sizeInBits;
}
unsigned csupport_flt_semantics_int_size_in_bits(
    const csupport_flt_semantics_t *s, int is_signed) {
  unsigned min = (unsigned)(s->maxExponent + 1);
  if (is_signed) ++min;
  return min;
}
int csupport_flt_semantics_is_representable_as_normal_in(
    const csupport_flt_semantics_t *src, const csupport_flt_semantics_t *dst) {
  if (src->maxExponent >= dst->maxExponent ||
      src->minExponent <= dst->minExponent)
    return 0;
  return dst->precision >= src->precision;
}

int32_t csupport_flt_exponent_zero(const csupport_flt_semantics_t *s) {
  return s->minExponent - 1;
}
int32_t csupport_flt_exponent_inf(const csupport_flt_semantics_t *s) {
  return s->maxExponent + 1;
}
int32_t csupport_flt_exponent_nan(const csupport_flt_semantics_t *s) {
  if (s->nonFiniteBehavior == CSUPPORT_FLT_NFB_NAN_ONLY) {
    if (s->nanEncoding == CSUPPORT_FLT_NAN_NEG_ZERO)
      return csupport_flt_exponent_zero(s);
    return s->maxExponent;
  }
  return s->maxExponent + 1;
}
unsigned csupport_flt_part_count_for_bits(unsigned bits) {
  return (bits + 63) / 64;
}
