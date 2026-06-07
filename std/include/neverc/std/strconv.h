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

int neverc_strconv_parse_int(const char *s, int base, long long *result);
int neverc_strconv_parse_uint(const char *s, int base, unsigned long long *result);
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
