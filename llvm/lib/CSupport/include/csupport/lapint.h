#ifndef CSUPPORT_LAPINT_H
#define CSUPPORT_LAPINT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void csupport_apint_shift_left(uint64_t *dst, const uint64_t *src,
                               unsigned words, unsigned shift);
void csupport_apint_shift_right_logical(uint64_t *dst, const uint64_t *src,
                                        unsigned words, unsigned shift);

void csupport_apint_tc_set(uint64_t *dst, uint64_t part, unsigned parts);
void csupport_apint_tc_assign(uint64_t *dst, const uint64_t *src,
                              unsigned parts);
int csupport_apint_tc_is_zero(const uint64_t *src, unsigned parts);
int csupport_apint_tc_compare(const uint64_t *lhs, const uint64_t *rhs,
                              unsigned parts);

uint64_t csupport_apint_tc_add(uint64_t *dst, const uint64_t *rhs,
                               uint64_t carry, unsigned parts);
uint64_t csupport_apint_tc_add_part(uint64_t *dst, uint64_t src,
                                    unsigned parts);
uint64_t csupport_apint_tc_subtract(uint64_t *dst, const uint64_t *rhs,
                                    uint64_t borrow, unsigned parts);
uint64_t csupport_apint_tc_subtract_part(uint64_t *dst, uint64_t src,
                                         unsigned parts);

void csupport_apint_tc_complement(uint64_t *dst, unsigned parts);
void csupport_apint_tc_negate(uint64_t *dst, unsigned parts);

int csupport_apint_tc_extract_bit(const uint64_t *parts, unsigned bit);
void csupport_apint_tc_set_bit(uint64_t *parts, unsigned bit);
void csupport_apint_tc_clear_bit(uint64_t *parts, unsigned bit);

unsigned csupport_apint_tc_lsb(const uint64_t *parts, unsigned n);
unsigned csupport_apint_tc_msb(const uint64_t *parts, unsigned n);

int csupport_apint_tc_multiply_part(uint64_t *dst, const uint64_t *src,
                                    uint64_t multiplier, uint64_t carry,
                                    unsigned src_parts, unsigned dst_parts,
                                    int add);
int csupport_apint_tc_multiply(uint64_t *dst, const uint64_t *lhs,
                               const uint64_t *rhs, unsigned parts);
void csupport_apint_tc_full_multiply(uint64_t *dst, const uint64_t *lhs,
                                     const uint64_t *rhs, unsigned lhs_parts,
                                     unsigned rhs_parts);
int csupport_apint_tc_divide(uint64_t *lhs, const uint64_t *rhs,
                             uint64_t *remainder, uint64_t *scratch,
                             unsigned parts);

void csupport_knuth_div(uint32_t *u, uint32_t *v, uint32_t *q, uint32_t *r,
                        unsigned m, unsigned n);

void csupport_apint_divide(const uint64_t *lhs, unsigned lhs_words,
                           const uint64_t *rhs, unsigned rhs_words,
                           uint64_t *quotient, uint64_t *remainder);

void csupport_apint_byte_swap(uint64_t *dst, const uint64_t *src,
                              unsigned bit_width);
void csupport_apint_reverse_bits(uint64_t *dst, const uint64_t *src,
                                 unsigned bit_width);

unsigned csupport_apint_get_digit(char c, unsigned radix);

void csupport_apint_to_string(const uint64_t *data, unsigned bit_width,
                              int is_signed, unsigned radix, char *buf,
                              size_t buflen, size_t *out_len);
int csupport_apint_from_string(uint64_t *dst, unsigned bit_width,
                               const char *str, size_t str_len, unsigned radix);

unsigned csupport_apint_count_leading_zeros(const uint64_t *data,
                                            unsigned words, unsigned bit_width);
unsigned csupport_apint_count_leading_ones(const uint64_t *data, unsigned words,
                                           unsigned bit_width);
unsigned csupport_apint_count_trailing_zeros(const uint64_t *data,
                                             unsigned words,
                                             unsigned bit_width);
unsigned csupport_apint_count_trailing_ones(const uint64_t *data,
                                            unsigned words);
unsigned csupport_apint_popcount(const uint64_t *data, unsigned words);

void csupport_apint_and_assign(uint64_t *dst, const uint64_t *rhs,
                               unsigned words);
void csupport_apint_or_assign(uint64_t *dst, const uint64_t *rhs,
                              unsigned words);
void csupport_apint_xor_assign(uint64_t *dst, const uint64_t *rhs,
                               unsigned words);
void csupport_apint_flip_all(uint64_t *dst, unsigned words);
int csupport_apint_intersects(const uint64_t *lhs, const uint64_t *rhs,
                              unsigned words);
int csupport_apint_is_subset_of(const uint64_t *lhs, const uint64_t *rhs,
                                unsigned words);
void csupport_apint_set_bits(uint64_t *dst, unsigned lo_bit, unsigned hi_bit);
unsigned csupport_apint_sufficient_bits(const char *str, unsigned len,
                                        unsigned radix);

void csupport_apint_ashr(uint64_t *dst, unsigned num_words, unsigned bit_width,
                         unsigned shift_amt);

void csupport_apint_insert_bits(uint64_t *dst, unsigned dst_words,
                                const uint64_t *src, unsigned src_words,
                                unsigned src_bit_width, unsigned bit_position);
void csupport_apint_insert_bits64(uint64_t *dst, unsigned dst_words,
                                  uint64_t val, unsigned bit_position,
                                  unsigned num_bits);
void csupport_apint_extract_bits(const uint64_t *src, unsigned src_words,
                                 uint64_t *dst, unsigned dst_words,
                                 unsigned num_bits, unsigned bit_position);
uint64_t csupport_apint_extract_bits_zext(const uint64_t *src,
                                          unsigned src_words, unsigned num_bits,
                                          unsigned bit_position);
void csupport_apint_sext_words(uint64_t *dst, unsigned dst_words,
                               const uint64_t *src, unsigned src_words,
                               unsigned src_bit_width);
void csupport_apint_trunc_words(uint64_t *dst, const uint64_t *src,
                                unsigned dst_words, unsigned width);

void csupport_apint_negate_slow(uint64_t *dst, unsigned num_words,
                                unsigned bit_width);
void csupport_apint_shl_slow(uint64_t *dst, const uint64_t *src,
                             unsigned num_words, unsigned shift_amt,
                             unsigned bit_width);
void csupport_apint_lshr_slow(uint64_t *dst, const uint64_t *src,
                              unsigned num_words, unsigned shift_amt,
                              unsigned bit_width);
void csupport_apint_gcd(uint64_t *result, const uint64_t *a, const uint64_t *b,
                        unsigned num_words, unsigned bit_width);
void csupport_apint_rotl(uint64_t *result, const uint64_t *src,
                         unsigned num_words, unsigned bit_width,
                         unsigned rotate_amt);
void csupport_apint_rotr(uint64_t *result, const uint64_t *src,
                         unsigned num_words, unsigned bit_width,
                         unsigned rotate_amt);
void csupport_apint_concat(uint64_t *result, unsigned result_words,
                           const uint64_t *hi, unsigned hi_bits,
                           const uint64_t *lo, unsigned lo_bits);

void csupport_apint_tc_extract(uint64_t *dst, unsigned dst_count,
                               const uint64_t *src, unsigned src_bits,
                               unsigned src_lsb);

uint64_t csupport_apint_tc_increment(uint64_t *dst, unsigned parts);

void csupport_apint_equal_slow(const uint64_t *lhs, const uint64_t *rhs,
                               unsigned words, int *result);

double csupport_apint_round_to_double(const uint64_t *words, unsigned num_words,
                                      unsigned bit_width, int is_signed);

#ifdef __cplusplus
}
#endif
#endif
