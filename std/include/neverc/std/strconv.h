#ifndef NEVERC_STRCONV_H
#define NEVERC_STRCONV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Return codes for parse functions: 0 = success, -1 = syntax error, -2 = range error */
#define NEVERC_STRCONV_OK          0
#define NEVERC_STRCONV_ERR_SYNTAX  (-1)
#define NEVERC_STRCONV_ERR_RANGE   (-2)
#define NEVERC_STRCONV_ERR_BASE    (-3)

/* ===== Parse: string → value ===== */

int neverc_strconv_atoi(const char *s, int *result);
int neverc_strconv_atol(const char *s, long long *result);

/* Base 0 accepts Go-style prefixes (0x/0b/0o/leading 0) and digit-separating
 * underscores. Explicit bases 2/8/16 also accept 0b/0o/0x prefixes but
 * reject underscores. */
int neverc_strconv_parse_int(const char *s, int base, long long *result);
int neverc_strconv_parse_uint(const char *s, int base, unsigned long long *result);
/* Decimal, Inf/NaN, and Go hex floats (0x1p0). Surrounding whitespace is
 * a syntax error, matching ParseInt/ParseBool. */
int neverc_strconv_parse_float(const char *s, double *result);
int neverc_strconv_parse_bool(const char *s, int *result);

/* ===== Format: value → string ===== */
/* All format functions return the number of chars written (excluding '\0'),
   or -1 if the buffer is too small. */

int neverc_strconv_itoa(int n, char *buf, size_t bufsize);
int neverc_strconv_ltoa(long long n, char *buf, size_t bufsize);

int neverc_strconv_format_int(long long n, int base, char *buf, size_t bufsize);
int neverc_strconv_format_uint(unsigned long long n, int base, char *buf, size_t bufsize);
int neverc_strconv_format_float(double f, char fmt, int prec, char *buf, size_t bufsize);
int neverc_strconv_format_bool(int b, char *buf, size_t bufsize);

/* ===== Quote / Unquote ===== */

/* Returns malloc'd quoted string (double-quoted, with escape sequences). Caller frees. */
char *neverc_strconv_quote(const char *s);
char *neverc_strconv_quote_to_ascii(const char *s);
char *neverc_strconv_quote_to_graphic(const char *s);

/* Returns malloc'd single-quoted rune literal. Caller frees. */
char *neverc_strconv_quote_rune(uint32_t r);
char *neverc_strconv_quote_rune_to_ascii(uint32_t r);
char *neverc_strconv_quote_rune_to_graphic(uint32_t r);

/* Unquote: interprets a Go-style quoted string literal.
   Writes unquoted result into buf, returns length, or -1 on error. */
int neverc_strconv_unquote(const char *s, char *buf, size_t bufsize);

/* UnquoteChar: decode first char/escape from s.
   Returns rune in *r, bytes consumed in return value, or -1 on error. */
int neverc_strconv_unquote_char(const char *s, size_t slen, char quote,
                                uint32_t *r, int *multibyte);

int neverc_strconv_can_backquote(const char *s);
int neverc_strconv_is_print(uint32_t r);
int neverc_strconv_is_graphic(uint32_t r);

/* Append variants: format into the start of a caller-provided buffer.
   Returns bytes written (excluding NUL), or -1 if too small. */
int neverc_strconv_append_bool(char *buf, size_t cap, int b);
int neverc_strconv_append_int(char *buf, size_t cap, long long n, int base);
int neverc_strconv_append_uint(char *buf, size_t cap, unsigned long long n, int base);
int neverc_strconv_append_float(char *buf, size_t cap, double f, char fmt, int prec);

int neverc_strconv_append_quote(char *buf, size_t cap, const char *s);
int neverc_strconv_append_quote_to_ascii(char *buf, size_t cap, const char *s);
int neverc_strconv_append_quote_to_graphic(char *buf, size_t cap, const char *s);
int neverc_strconv_append_quote_rune(char *buf, size_t cap, uint32_t r);
int neverc_strconv_append_quote_rune_to_ascii(char *buf, size_t cap, uint32_t r);
int neverc_strconv_append_quote_rune_to_graphic(char *buf, size_t cap, uint32_t r);

/* QuotedPrefix: returns length of the quoted string prefix, or -1 on error. */
int neverc_strconv_quoted_prefix(const char *s, size_t *prefix_len);

/* Complex number formatting/parsing (a+bi form). */
int neverc_strconv_format_complex(double re, double im, char fmt, int prec,
                                   char *buf, size_t bufsize);
int neverc_strconv_parse_complex(const char *s, double *re, double *im);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
struct __neverc_std_strconv_t { char __tag; };
extern struct __neverc_std_strconv_t __neverc_mod_strconv;
extern struct __neverc_std_strconv_t strconv;
#endif

#endif /* NEVERC_STRCONV_H */
