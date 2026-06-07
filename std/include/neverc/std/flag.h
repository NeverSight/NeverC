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
void  neverc_flag_int64(const char *name, long long defval,
                        const char *usage, long long *ptr);
void  neverc_flag_uint64(const char *name, unsigned long long defval,
                         const char *usage, unsigned long long *ptr);
void  neverc_flag_bool(const char *name, int defval,
                       const char *usage, int *ptr);
void  neverc_flag_double(const char *name, double defval,
                         const char *usage, double *ptr);

int   neverc_flag_parse(int argc, char **argv);
int   neverc_flag_parsed(void);
int   neverc_flag_narg(void);
int   neverc_flag_nflag(void);
const char *neverc_flag_arg(int i);
int   neverc_flag_set(const char *name, const char *value);
int   neverc_flag_lookup(const char *name, const char **usage_out);
void  neverc_flag_print_defaults(void);
void  neverc_flag_reset(void);

typedef void (*neverc_flag_visit_fn)(const char *name, const char *usage, void *ctx);
void  neverc_flag_visit(neverc_flag_visit_fn fn, void *ctx);
void  neverc_flag_visit_all(neverc_flag_visit_fn fn, void *ctx);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_flag_t { char __tag; };
extern struct __neverc_std_flag_t __neverc_mod_flag;
extern struct __neverc_std_flag_t flag;
#endif

#endif /* NEVERC_FLAG_H */
