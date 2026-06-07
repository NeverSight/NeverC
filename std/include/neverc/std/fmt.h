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

/* Scan functions — parse formatted input (mirrors Go fmt.Scan/Sscan/Fscan) */
int neverc_fmt_sscanf(const char *str, const char *format, ...);
int neverc_fmt_sscan(const char *str, ...);
int neverc_fmt_scanf(const char *format, ...);
int neverc_fmt_scan(int *out_int);
int neverc_fmt_fscanf(FILE *f, const char *format, ...);

/* Append functions — format into caller-provided buffer */
int neverc_fmt_appendf(char *buf, size_t cap, const char *format, ...);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_fmt_t { char __tag; };
extern struct __neverc_std_fmt_t fmt;
#endif

#endif /* NEVERC_FMT_H */
