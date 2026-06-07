#ifndef NEVERC_CSTRING_H
#define NEVERC_CSTRING_H

/*
 * NeverC cstring — C-string manipulation (mirrors Go strings package).
 *
 * All functions operate on null-terminated const char * strings.
 * Functions that produce new strings allocate with malloc; caller frees.
 * Index functions return -1 when not found.
 *
 * This module is separate from the built-in neverc_string_* (which is a
 * managed string type). cstring operates on raw C strings for lightweight
 * use-cases and pure-C interop.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Comparison --- */
int  neverc_cstring_compare(const char *a, const char *b);
int  neverc_cstring_equal_fold(const char *s, const char *t);

/* --- Search --- */
int  neverc_cstring_contains(const char *s, const char *substr);
int  neverc_cstring_contains_any(const char *s, const char *chars);
int  neverc_cstring_contains_char(const char *s, char c);
int  neverc_cstring_count(const char *s, const char *substr);

/* Returns index or -1 if not found */
int  neverc_cstring_index(const char *s, const char *substr);
int  neverc_cstring_index_byte(const char *s, char c);
int  neverc_cstring_index_any(const char *s, const char *chars);
int  neverc_cstring_last_index(const char *s, const char *substr);
int  neverc_cstring_last_index_byte(const char *s, char c);
int  neverc_cstring_last_index_any(const char *s, const char *chars);

/* --- Prefix / Suffix --- */
int  neverc_cstring_has_prefix(const char *s, const char *prefix);
int  neverc_cstring_has_suffix(const char *s, const char *suffix);

/* --- Transform (allocate result, caller frees) --- */
char *neverc_cstring_to_upper(const char *s);
char *neverc_cstring_to_lower(const char *s);
char *neverc_cstring_to_title(const char *s);
char *neverc_cstring_repeat(const char *s, int count);
char *neverc_cstring_replace(const char *s, const char *old_s,
                              const char *new_s, int n);
char *neverc_cstring_replace_all(const char *s, const char *old_s,
                                  const char *new_s);
char *neverc_cstring_map(char (*mapping)(char), const char *s);

/* --- Join --- */
char *neverc_cstring_join(const char **strs, size_t count, const char *sep);

/* --- Trim (allocate result, caller frees) --- */
char *neverc_cstring_trim(const char *s, const char *cutset);
char *neverc_cstring_trim_left(const char *s, const char *cutset);
char *neverc_cstring_trim_right(const char *s, const char *cutset);
char *neverc_cstring_trim_space(const char *s);
char *neverc_cstring_trim_prefix(const char *s, const char *prefix);
char *neverc_cstring_trim_suffix(const char *s, const char *suffix);

/* --- Split (returns malloc'd array of malloc'd strings) --- */
char **neverc_cstring_split(const char *s, const char *sep, size_t *count);
char **neverc_cstring_split_n(const char *s, const char *sep,
                               int n, size_t *count);
char **neverc_cstring_split_after(const char *s, const char *sep,
                                   size_t *count);
char **neverc_cstring_split_after_n(const char *s, const char *sep,
                                     int n, size_t *count);
char **neverc_cstring_fields(const char *s, size_t *count);

/* Free an array returned by split/fields */
void   neverc_cstring_free_split(char **arr, size_t count);

/* --- Cut --- */
int  neverc_cstring_cut(const char *s, const char *sep,
                         char **before, char **after);
int  neverc_cstring_cut_prefix(const char *s, const char *prefix,
                                char **after);
int  neverc_cstring_cut_suffix(const char *s, const char *suffix,
                                char **before);
int  neverc_cstring_cut_last(const char *s, const char *sep,
                              char **before, char **after);

/* --- Clone --- */
char *neverc_cstring_clone(const char *s);

/* --- Utility --- */
size_t neverc_cstring_len(const char *s);

#ifdef __cplusplus
}
#endif

/* ===== Std Module Dot-Syntax Support ===== */
#ifdef __neverc__
struct __neverc_std_cstring_t { char __tag; };
extern struct __neverc_std_cstring_t __neverc_mod_cstring;
extern struct __neverc_std_cstring_t cstring;
#endif

#endif /* NEVERC_CSTRING_H */
