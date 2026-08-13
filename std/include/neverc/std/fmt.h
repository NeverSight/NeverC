#ifndef NEVERC_FMT_H
#define NEVERC_FMT_H

/*
 * NeverC fmt — formatted I/O (mirrors Go fmt package).
 *
 * Self-implemented formatting engine — does NOT call libc printf/sprintf.
 * Supported verbs: %d %u %x %X %o %b %s %c %f %e %g %p %%
 * Width/precision: %10d %-10s %.5f %*d
 * Flags: - + 0 # (space)
 *
 * neverc_fmt_sprintf returns malloc'd string; caller frees.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

char *neverc_fmt_sprintf(const char *format, ...);
char *neverc_fmt_vsprintf(const char *format, va_list args);
int   neverc_fmt_fprintf(FILE *f, const char *format, ...);
int   neverc_fmt_printf(const char *format, ...);
int   neverc_fmt_println(const char *format, ...);

char *neverc_fmt_sprint(const char *s);
char *neverc_fmt_sprintln(const char *s);
int   neverc_fmt_fprint(FILE *f, const char *s);
int   neverc_fmt_fprintln(FILE *f, const char *s);
char *neverc_fmt_errorf(const char *format, ...);

/* Scan functions — parse formatted input (mirrors Go fmt.Scan/Sscan/Fscan).
 * String conversions must include a positive maximum width (for example,
 * "%31s" for a 32-byte destination) so the terminating NUL always fits.
 * neverc_fmt_sscan reads only its first integer destination; trailing
 * arguments are accepted for source compatibility but intentionally ignored.
 * Use neverc_fmt_sscan_ints for an explicitly bounded output array. */
int neverc_fmt_sscanf(const char *str, const char *format, ...);
int neverc_fmt_sscan(const char *str, ...);
int neverc_fmt_sscan_ints(const char *str, int *outputs,
                          size_t output_count);
int neverc_fmt_scanf(const char *format, ...);
int neverc_fmt_scan(int *out_int);
int neverc_fmt_fscanf(FILE *f, const char *format, ...);

/* Append functions — format into caller-provided buffer */
int neverc_fmt_appendf(char *buf, size_t cap, const char *format, ...);
int neverc_fmt_append(char *buf, size_t cap, const char *s);
int neverc_fmt_appendln(char *buf, size_t cap, const char *s);

/* Line-oriented scan functions (read until newline) */
int neverc_fmt_scanln(const char *format, ...);
int neverc_fmt_sscanln(const char *str, const char *format, ...);
int neverc_fmt_fscan(FILE *f, int *out_int);
int neverc_fmt_fscanln(FILE *f, const char *format, ...);

/* Print to string (variadic) */
char *neverc_fmt_sprintfln(const char *format, ...);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_fmt_t { char __tag; };
extern struct __neverc_std_fmt_t __neverc_mod_fmt;
extern struct __neverc_std_fmt_t fmt;
#endif

#endif /* NEVERC_FMT_H */
