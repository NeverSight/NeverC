#ifndef CSUPPORT_LA_LP_LFLOAT_H
#define CSUPPORT_LA_LP_LFLOAT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_float_classify(double v);
int csupport_float_is_nan(double v);
int csupport_float_is_inf(double v);
int csupport_float_is_zero(double v);
int csupport_float_is_negative(double v);
double csupport_float_round_to_integral(double v, int round_mode);

enum {
  CSUPPORT_LF_EXACTLY_ZERO = 0,
  CSUPPORT_LF_LESS_THAN_HALF = 1,
  CSUPPORT_LF_EXACTLY_HALF = 2,
  CSUPPORT_LF_MORE_THAN_HALF = 3
};

typedef struct {
  const char *first_sig_digit;
  const char *last_sig_digit;
  int exponent;
  int normalized_exponent;
} csupport_decimal_info_t;

#define CSUPPORT_APFLOAT_ERR_NONE 0
#define CSUPPORT_APFLOAT_ERR_EXPONENT_NO_DIGITS 1
#define CSUPPORT_APFLOAT_ERR_INVALID_EXPONENT 2
#define CSUPPORT_APFLOAT_ERR_SIG_NO_DIGITS 3
#define CSUPPORT_APFLOAT_ERR_MULTIPLE_DOTS 4
#define CSUPPORT_APFLOAT_ERR_INVALID_SIG 5

int csupport_apfloat_read_exponent(const char *begin, const char *end,
                                   int *result);
int csupport_apfloat_total_exponent(const char *begin, const char *end,
                                    int exponent_adjustment, int *result);
int csupport_apfloat_skip_leading_zeros(const char *begin, const char *end,
                                        const char **dot, const char **result);
int csupport_apfloat_interpret_decimal(const char *begin, const char *end,
                                       csupport_decimal_info_t *D);

int csupport_apfloat_combine_lost_fractions(int more_significant,
                                            int less_significant);
unsigned csupport_apfloat_huerr_bound(int inexact_multiply, unsigned huerr1,
                                      unsigned huerr2);
int csupport_apfloat_lost_fraction_truncation(const uint64_t *parts,
                                              unsigned part_count,
                                              unsigned bits);
uint64_t csupport_apfloat_ulps_from_boundary(const uint64_t *parts,
                                             unsigned bits, int is_nearest);
unsigned csupport_apfloat_power_of5(uint64_t *dst, unsigned power);
unsigned csupport_apfloat_part_as_hex(char *dst, uint64_t part, unsigned count,
                                      const char *hex_digits);
char *csupport_apfloat_write_unsigned_decimal(char *dst, unsigned n);
char *csupport_apfloat_write_signed_decimal(char *dst, int value);
int csupport_apfloat_trailing_hex_fraction(const char *p, const char *end,
                                           unsigned digit_value, int *result);
int csupport_apfloat_is_significand_all_ones(const uint64_t *parts,
                                             unsigned precision);
int csupport_apfloat_is_significand_all_ones_except_lsb(const uint64_t *parts,
                                                        unsigned precision);
int csupport_apfloat_is_significand_all_zeros(const uint64_t *parts,
                                              unsigned precision);
int csupport_apfloat_is_significand_all_zeros_except_msb(const uint64_t *parts,
                                                         unsigned precision);
void csupport_apfloat_tc_set_least_significant_bits(uint64_t *dst,
                                                    unsigned parts,
                                                    unsigned bits);

int csupport_apfloat_round_away_from_zero(int rounding_mode, int lost_fraction,
                                          int category, int sign_bit,
                                          const uint64_t *parts, unsigned bit);
int csupport_apfloat_convert_normal_to_hex(
    char *dst, const uint64_t *significand, unsigned parts_count,
    unsigned precision, int exponent_val, int sign_bit, unsigned sig_lsb,
    unsigned hex_digits, int upper_case, int rounding_mode, int category,
    unsigned *out_len);

const char *csupport_apfloat_error_msg(int err);

int csupport_apfloat_divide_significand(uint64_t *lhs_sig,
                                        const uint64_t *rhs_sig,
                                        unsigned parts_count,
                                        unsigned precision, int *exponent_out);

int csupport_apfloat_multiply_significand_simple(uint64_t *lhs_sig,
                                                 const uint64_t *rhs_sig,
                                                 unsigned parts_count,
                                                 unsigned precision,
                                                 int *exponent_out);

int csupport_apfloat_add_or_subtract_significand(
    uint64_t *lhs_sig, unsigned parts_count, int *lhs_exp, int *lhs_sign,
    const uint64_t *rhs_sig, int rhs_exp, int rhs_sign, int subtract);

int csupport_apfloat_handle_overflow(uint64_t *significand,
                                     unsigned parts_count, unsigned precision,
                                     int rounding_mode, int *sign,
                                     int *category_out, int *exponent_out,
                                     int max_exponent, int min_exponent,
                                     int non_finite_behavior, int nan_encoding);

void csupport_apfloat_make_largest(uint64_t *significand, unsigned part_count,
                                   unsigned precision, int non_finite_behavior,
                                   int nan_encoding);
void csupport_apfloat_make_smallest(uint64_t *significand, unsigned part_count);
void csupport_apfloat_make_smallest_normalized(uint64_t *significand,
                                               unsigned part_count,
                                               unsigned precision);
void csupport_apfloat_make_zero(uint64_t *significand, unsigned part_count);
void csupport_apfloat_make_inf(uint64_t *significand, unsigned part_count);
int csupport_apfloat_compare_absolute(const uint64_t *lhs_sig,
                                      const uint64_t *rhs_sig,
                                      unsigned part_count);
void csupport_apfloat_adjust_precision_buffer(char *buffer, unsigned *size,
                                              int *exp,
                                              unsigned format_precision);

int csupport_apfloat_normalize(uint64_t *significand, unsigned parts_count,
                               int *exponent, int *category, int *sign,
                               int rounding_mode, int lost_fraction,
                               unsigned precision, int max_exponent,
                               int min_exponent, int non_finite_behavior,
                               int nan_encoding);

int csupport_apfloat_parse_special_string(
    const char *str, size_t len, int *out_category, int *out_is_negative,
    int *out_is_signaling, unsigned *out_radix, const char **payload_begin,
    size_t *payload_len);

void csupport_apfloat_format_to_string(const char *buffer, unsigned n_digits,
                                       int exp, unsigned format_precision,
                                       unsigned format_max_padding,
                                       int truncate_zero, int is_negative,
                                       char *out, unsigned *out_len);

int csupport_apfloat_round_away_from_zero_simple(int rounding_mode,
                                                 int lost_fraction,
                                                 int bit_at_boundary);

enum {
  CSUPPORT_APFLOAT_CAT_ZERO = 0,
  CSUPPORT_APFLOAT_CAT_INF = 1,
  CSUPPORT_APFLOAT_CAT_NAN = 2,
  CSUPPORT_APFLOAT_CAT_NORMAL = 3,
  CSUPPORT_APFLOAT_CAT_DENORM = 4
};

int csupport_apfloat_unpack_ieee(const uint64_t *words, unsigned num_words,
                                 unsigned precision, unsigned size_in_bits,
                                 int min_exponent, int non_finite_behavior,
                                 int nan_encoding, int *out_sign,
                                 int *out_exponent, uint64_t *out_significand,
                                 unsigned max_sig_parts);

void csupport_apfloat_make_nan(uint64_t *significand, unsigned num_parts,
                               unsigned precision, int snan, int *sign,
                               const uint64_t *fill, unsigned fill_words,
                               int non_finite_behavior, int nan_encoding,
                               int is_x87);

void csupport_apfloat_convert_ieee_to_words(
    int category, int is_finite_nonzero, int exponent_val, int sign_bit,
    const uint64_t *significand_parts, unsigned precision,
    unsigned size_in_bits, int min_exponent, int non_finite_behavior,
    int nan_encoding, uint64_t *words, unsigned num_words);

int csupport_apfloat_divide_specials(int lhs_cat, int rhs_cat,
                                     int *out_category);
int csupport_apfloat_remainder_specials(int lhs_cat, int rhs_cat,
                                        int *out_category);
int csupport_apfloat_mod_specials(int lhs_cat, int rhs_cat, int *out_category);
int csupport_apfloat_add_or_subtract_specials(int lhs_cat, int rhs_cat,
                                              int subtract, int lhs_sign,
                                              int rhs_sign, int *out_category,
                                              int *out_sign);

int csupport_apfloat_compare_categories(int lhs_cat, int rhs_cat, int lhs_sign,
                                        int rhs_sign);

int csupport_apfloat_multiply_specials(int lhs_cat, int rhs_cat,
                                       int *out_category);

void csupport_apfloat_convert_f80_to_words(int category, int is_finite_nonzero,
                                           int exponent_val, int sign_bit,
                                           const uint64_t *significand_parts,
                                           uint64_t *words);

int csupport_apfloat_convert_from_hex_core(
    const char *begin, const char *end, uint64_t *significand,
    unsigned parts_count, unsigned integer_part_width, unsigned precision,
    int *exponent_out, int *category_out, int *lost_fraction_out);

int csupport_apfloat_convert_from_decimal_core(const char *first_sig,
                                               const char *last_sig,
                                               int normalized_exp, int exponent,
                                               uint64_t **dec_significand_out,
                                               unsigned *part_count_out);

int csupport_apfloat_convert_to_sign_ext_int(
    const uint64_t *src_sig, unsigned src_part_count, unsigned precision,
    int exponent_val, int category, int sign_bit, int is_signed,
    int rounding_mode, uint64_t *dst, unsigned dst_part_count, unsigned width,
    int *is_exact);

int csupport_apfloat_get_exact_log2_abs(const uint64_t *significand,
                                        unsigned part_count, unsigned precision,
                                        int exponent_val, int min_exponent,
                                        int is_finite, int is_zero);

int csupport_apfloat_unpack_f80(const uint64_t *raw_words, int *out_sign,
                                int *out_exponent, uint64_t *out_significand);

int csupport_apfloat_is_integer_significand(const uint64_t *parts,
                                            unsigned part_count, int exponent,
                                            unsigned precision);
int csupport_apfloat_classify(int category, int sign, int is_denormal);
size_t csupport_apfloat_format_hex(const uint64_t *significand,
                                   unsigned part_count, unsigned precision,
                                   int exponent, int sign, int uppercase,
                                   char *buf, size_t buflen);

enum { CSUPPORT_FLT_NFB_IEEE754 = 0, CSUPPORT_FLT_NFB_NAN_ONLY = 1 };
enum {
  CSUPPORT_FLT_NAN_IEEE = 0,
  CSUPPORT_FLT_NAN_ALL_ONES = 1,
  CSUPPORT_FLT_NAN_NEG_ZERO = 2
};

typedef struct {
  int32_t maxExponent;
  int32_t minExponent;
  unsigned int precision;
  unsigned int sizeInBits;
  int nonFiniteBehavior;
  int nanEncoding;
} csupport_flt_semantics_t;

extern const csupport_flt_semantics_t csupport_sem_ieee_half;
extern const csupport_flt_semantics_t csupport_sem_bfloat;
extern const csupport_flt_semantics_t csupport_sem_ieee_single;
extern const csupport_flt_semantics_t csupport_sem_ieee_double;
extern const csupport_flt_semantics_t csupport_sem_ieee_quad;
extern const csupport_flt_semantics_t csupport_sem_float8_e5m2;
extern const csupport_flt_semantics_t csupport_sem_float8_e5m2fnuz;
extern const csupport_flt_semantics_t csupport_sem_float8_e4m3fn;
extern const csupport_flt_semantics_t csupport_sem_float8_e4m3fnuz;
extern const csupport_flt_semantics_t csupport_sem_float8_e4m3b11fnuz;
extern const csupport_flt_semantics_t csupport_sem_float_tf32;
extern const csupport_flt_semantics_t csupport_sem_x87_double_extended;
extern const csupport_flt_semantics_t csupport_sem_bogus;
extern const csupport_flt_semantics_t csupport_sem_ppc_double_double;
extern const csupport_flt_semantics_t csupport_sem_ppc_double_double_legacy;

unsigned csupport_flt_semantics_precision(const csupport_flt_semantics_t *s);
int32_t csupport_flt_semantics_max_exponent(const csupport_flt_semantics_t *s);
int32_t csupport_flt_semantics_min_exponent(const csupport_flt_semantics_t *s);
unsigned csupport_flt_semantics_size_in_bits(const csupport_flt_semantics_t *s);
unsigned
csupport_flt_semantics_int_size_in_bits(const csupport_flt_semantics_t *s,
                                        int is_signed);
int csupport_flt_semantics_is_representable_as_normal_in(
    const csupport_flt_semantics_t *src, const csupport_flt_semantics_t *dst);

int32_t csupport_flt_exponent_zero(const csupport_flt_semantics_t *s);
int32_t csupport_flt_exponent_inf(const csupport_flt_semantics_t *s);
int32_t csupport_flt_exponent_nan(const csupport_flt_semantics_t *s);
unsigned csupport_flt_part_count_for_bits(unsigned bits);

/* IEEEFloat::convert — semantics change + significand storage (union) updates.
 */
int csupport_apfloat_convert_semantics(
    const csupport_flt_semantics_t *from_sem,
    const csupport_flt_semantics_t *to_sem, int from_is_x87_ext,
    int to_is_x87_ext, uint64_t *inline_part, uint64_t **heap_parts,
    unsigned old_part_count, int *exponent, int *category, int *sign,
    int rounding_mode, int is_signaling, int finite_nonzero, int *loses_info);

/* IEEEFloat::multiplySignificand(rhs, addend) — FMA extended path + simple mul.
 */
int csupport_apfloat_multiply_significand_fma(
    const csupport_flt_semantics_t *sem, uint64_t *lhs_sig, unsigned lhs_pc,
    int *lhs_exp, int *lhs_sign, const uint64_t *rhs_sig, int rhs_exp,
    const uint64_t *add_sig, unsigned add_pc, int add_exp, int add_cat,
    int add_sign, int addend_nonzero);

#ifdef __cplusplus
}
#endif
#endif
