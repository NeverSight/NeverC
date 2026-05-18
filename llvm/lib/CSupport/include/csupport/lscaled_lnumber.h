#ifndef CSUPPORT_LSCALED_LNUMBER_H
#define CSUPPORT_LSCALED_LNUMBER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void csupport_multiply_u64(uint64_t lhs, uint64_t rhs, uint64_t *hi,
                           uint64_t *lo);
uint64_t csupport_divide_u128_by_u64(uint64_t dividend_hi, uint64_t dividend_lo,
                                     uint64_t divisor, uint64_t *remainder);

typedef struct {
  uint64_t value;
  int16_t exponent;
} csupport_scaled_pair_t;

csupport_scaled_pair_t csupport_scaled_multiply64(uint64_t lhs, uint64_t rhs);
csupport_scaled_pair_t csupport_scaled_divide64(uint64_t dividend,
                                                uint64_t divisor);
csupport_scaled_pair_t csupport_scaled_divide32(uint32_t dividend,
                                                uint32_t divisor);
int csupport_scaled_compare(uint64_t l, uint64_t r, int scale_diff);

int csupport_count_leading_zeros_64(uint64_t v);
int csupport_count_trailing_zeros_64(uint64_t v);

csupport_scaled_pair_t
csupport_scaled_get_rounded64(uint64_t digits, int16_t scale, int round_up);
csupport_scaled_pair_t csupport_scaled_get_adjusted64(uint64_t digits,
                                                      int16_t scale);

size_t csupport_scaled_strip_trailing_zeros(const char *str, size_t len,
                                            char *out, size_t out_cap);
size_t csupport_scaled_format_digits(uint64_t above, uint64_t below,
                                     uint64_t extra, int extra_shift,
                                     unsigned precision, int width, char *out,
                                     size_t out_cap);

#ifdef __cplusplus
}
#endif
#endif
