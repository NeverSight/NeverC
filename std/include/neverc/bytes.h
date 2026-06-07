#ifndef NEVERC_BYTES_H
#define NEVERC_BYTES_H

/*
 * NeverC bytes — byte slice manipulation (mirrors Go bytes package).
 *
 * All functions operate on (const uint8_t *data, size_t len) pairs.
 * Functions that produce new byte slices allocate with malloc; caller frees.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Comparison --- */
int  neverc_bytes_equal(const uint8_t *a, size_t alen,
                        const uint8_t *b, size_t blen);
int  neverc_bytes_compare(const uint8_t *a, size_t alen,
                          const uint8_t *b, size_t blen);
int  neverc_bytes_equal_fold(const uint8_t *s, size_t slen,
                             const uint8_t *t, size_t tlen);

/* --- Search --- */
int    neverc_bytes_contains(const uint8_t *b, size_t blen,
                             const uint8_t *sub, size_t sublen);
int    neverc_bytes_contains_byte(const uint8_t *b, size_t blen, uint8_t c);
int    neverc_bytes_contains_any(const uint8_t *b, size_t blen,
                                const char *chars);
size_t neverc_bytes_count(const uint8_t *s, size_t slen,
                          const uint8_t *sep, size_t seplen);

/* Returns index or (size_t)-1 if not found */
size_t neverc_bytes_index(const uint8_t *s, size_t slen,
                          const uint8_t *sep, size_t seplen);
size_t neverc_bytes_index_byte(const uint8_t *b, size_t blen, uint8_t c);
size_t neverc_bytes_index_any(const uint8_t *s, size_t slen, const char *chars);
size_t neverc_bytes_last_index(const uint8_t *s, size_t slen,
                               const uint8_t *sep, size_t seplen);
size_t neverc_bytes_last_index_byte(const uint8_t *s, size_t slen, uint8_t c);
size_t neverc_bytes_last_index_any(const uint8_t *s, size_t slen,
                                   const char *chars);

/* --- Prefix / Suffix --- */
int  neverc_bytes_has_prefix(const uint8_t *s, size_t slen,
                             const uint8_t *prefix, size_t plen);
int  neverc_bytes_has_suffix(const uint8_t *s, size_t slen,
                             const uint8_t *suffix, size_t sfxlen);

/* --- Transform (allocate result, caller frees) --- */
uint8_t *neverc_bytes_to_upper(const uint8_t *s, size_t slen, size_t *outlen);
uint8_t *neverc_bytes_to_lower(const uint8_t *s, size_t slen, size_t *outlen);
uint8_t *neverc_bytes_to_title(const uint8_t *s, size_t slen, size_t *outlen);
uint8_t *neverc_bytes_repeat(const uint8_t *b, size_t blen,
                             int count, size_t *outlen);
uint8_t *neverc_bytes_replace(const uint8_t *s, size_t slen,
                              const uint8_t *old, size_t oldlen,
                              const uint8_t *new_, size_t newlen,
                              int n, size_t *outlen);
uint8_t *neverc_bytes_replace_all(const uint8_t *s, size_t slen,
                                  const uint8_t *old, size_t oldlen,
                                  const uint8_t *new_, size_t newlen,
                                  size_t *outlen);

/* --- Join --- */
uint8_t *neverc_bytes_join(const uint8_t **slices, const size_t *lens,
                           size_t count,
                           const uint8_t *sep, size_t seplen,
                           size_t *outlen);

/* --- Trim --- */
uint8_t *neverc_bytes_trim(const uint8_t *s, size_t slen,
                           const char *cutset, size_t *outlen);
uint8_t *neverc_bytes_trim_left(const uint8_t *s, size_t slen,
                                const char *cutset, size_t *outlen);
uint8_t *neverc_bytes_trim_right(const uint8_t *s, size_t slen,
                                 const char *cutset, size_t *outlen);
uint8_t *neverc_bytes_trim_space(const uint8_t *s, size_t slen, size_t *outlen);
uint8_t *neverc_bytes_trim_prefix(const uint8_t *s, size_t slen,
                                  const uint8_t *prefix, size_t plen,
                                  size_t *outlen);
uint8_t *neverc_bytes_trim_suffix(const uint8_t *s, size_t slen,
                                  const uint8_t *suffix, size_t sfxlen,
                                  size_t *outlen);

/* --- Split (returns malloc'd array of slices; caller frees result array) --- */
typedef struct {
    const uint8_t *data;
    size_t         len;
} neverc_bytes_slice_t;

neverc_bytes_slice_t *neverc_bytes_split(const uint8_t *s, size_t slen,
                                         const uint8_t *sep, size_t seplen,
                                         size_t *count);
neverc_bytes_slice_t *neverc_bytes_split_n(const uint8_t *s, size_t slen,
                                           const uint8_t *sep, size_t seplen,
                                           int n, size_t *count);
neverc_bytes_slice_t *neverc_bytes_fields(const uint8_t *s, size_t slen,
                                          size_t *count);

/* --- Cut --- */
int neverc_bytes_cut(const uint8_t *s, size_t slen,
                     const uint8_t *sep, size_t seplen,
                     const uint8_t **before, size_t *blen,
                     const uint8_t **after, size_t *alen);
int neverc_bytes_cut_prefix(const uint8_t *s, size_t slen,
                            const uint8_t *prefix, size_t plen,
                            const uint8_t **after, size_t *alen);
int neverc_bytes_cut_suffix(const uint8_t *s, size_t slen,
                            const uint8_t *suffix, size_t sfxlen,
                            const uint8_t **before, size_t *blen);

/* --- Func-based operations --- */
typedef int (*neverc_bytes_func_t)(uint8_t);

int    neverc_bytes_contains_func(const uint8_t *b, size_t blen,
                                  neverc_bytes_func_t f);
size_t neverc_bytes_index_func(const uint8_t *s, size_t slen,
                               neverc_bytes_func_t f);
size_t neverc_bytes_last_index_func(const uint8_t *s, size_t slen,
                                    neverc_bytes_func_t f);
uint8_t *neverc_bytes_trim_func(const uint8_t *s, size_t slen,
                                neverc_bytes_func_t f, size_t *outlen);
uint8_t *neverc_bytes_trim_left_func(const uint8_t *s, size_t slen,
                                     neverc_bytes_func_t f, size_t *outlen);
uint8_t *neverc_bytes_trim_right_func(const uint8_t *s, size_t slen,
                                      neverc_bytes_func_t f, size_t *outlen);
neverc_bytes_slice_t *neverc_bytes_fields_func(const uint8_t *s, size_t slen,
                                               neverc_bytes_func_t f,
                                               size_t *count);

/* --- Map --- */
uint8_t *neverc_bytes_map(uint8_t (*mapping)(uint8_t),
                          const uint8_t *s, size_t slen, size_t *outlen);

/* --- SplitAfter --- */
neverc_bytes_slice_t *neverc_bytes_split_after(const uint8_t *s, size_t slen,
                                               const uint8_t *sep, size_t seplen,
                                               size_t *count);
neverc_bytes_slice_t *neverc_bytes_split_after_n(const uint8_t *s, size_t slen,
                                                 const uint8_t *sep, size_t seplen,
                                                 int n, size_t *count);

/* --- Clone --- */
uint8_t *neverc_bytes_clone(const uint8_t *b, size_t blen);

/* --- CutLast (Go 1.24+) --- */
int neverc_bytes_cut_last(const uint8_t *s, size_t slen,
                          const uint8_t *sep, size_t seplen,
                          const uint8_t **before, size_t *blen,
                          const uint8_t **after, size_t *alen);

/* --- IndexRune --- */
size_t neverc_bytes_index_rune(const uint8_t *s, size_t slen, uint32_t r);

/* --- Runes: decode byte slice to array of Unicode codepoints --- */
uint32_t *neverc_bytes_runes(const uint8_t *s, size_t slen, size_t *count);

/* --- ToValidUTF8: replace invalid UTF-8 with replacement bytes --- */
uint8_t *neverc_bytes_to_valid_utf8(const uint8_t *s, size_t slen,
                                     const uint8_t *replacement, size_t rlen,
                                     size_t *outlen);

#ifdef __cplusplus
}
#endif

/* ===== Std Module Dot-Syntax Support ===== */
#ifdef __neverc__
struct __neverc_std_bytes_t { char __tag; };
extern struct __neverc_std_bytes_t bytes;
#endif

#endif /* NEVERC_BYTES_H */
