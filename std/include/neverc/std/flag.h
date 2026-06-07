#ifndef NEVERC_FLAG_H
#define NEVERC_FLAG_H

/*
 * NeverC flag — command-line flag parsing (mirrors Go flag package).
 *
 * Register flags with neverc_flag_string/int/bool/double, then call
 * neverc_flag_parse(argc, argv). After parsing, flag variables are set.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_FLAG_MAX 64

void  neverc_flag_string(const char *name, const char *defval,
                         const char *usage, const char **ptr);
void  neverc_flag_int(const char *name, int defval,
                      const char *usage, int *ptr);
void  neverc_flag_bool(const char *name, int defval,
                       const char *usage, int *ptr);
void  neverc_flag_double(const char *name, double defval,
                         const char *usage, double *ptr);

int   neverc_flag_parse(int argc, char **argv);
int   neverc_flag_narg(void);
const char *neverc_flag_arg(int i);
void  neverc_flag_print_defaults(void);
void  neverc_flag_reset(void);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_flag_t { char __tag; };
extern struct __neverc_std_flag_t flag;
#endif

#endif /* NEVERC_FLAG_H */
