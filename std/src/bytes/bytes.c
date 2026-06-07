#include "neverc/bytes.h"
#include <stdlib.h>

/* --- Comparison --- */

int neverc_bytes_equal(const uint8_t *a, size_t alen,
                       const uint8_t *b, size_t blen) {
    if (alen != blen) return 0;
    for (size_t i = 0; i < alen; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

int neverc_bytes_compare(const uint8_t *a, size_t alen,
                         const uint8_t *b, size_t blen) {
    size_t n = alen < blen ? alen : blen;
    for (size_t i = 0; i < n; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    if (alen < blen) return -1;
    if (alen > blen) return 1;
    return 0;
}

static uint8_t to_lower_ascii(uint8_t c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int neverc_bytes_equal_fold(const uint8_t *s, size_t slen,
                            const uint8_t *t, size_t tlen) {
    if (slen != tlen) return 0;
    for (size_t i = 0; i < slen; i++)
        if (to_lower_ascii(s[i]) != to_lower_ascii(t[i])) return 0;
    return 1;
}

/* --- Search --- */

size_t neverc_bytes_index_byte(const uint8_t *b, size_t blen, uint8_t c) {
    for (size_t i = 0; i < blen; i++)
        if (b[i] == c) return i;
    return (size_t)-1;
}

size_t neverc_bytes_last_index_byte(const uint8_t *s, size_t slen, uint8_t c) {
    for (size_t i = slen; i > 0; i--)
        if (s[i - 1] == c) return i - 1;
    return (size_t)-1;
}

size_t neverc_bytes_index(const uint8_t *s, size_t slen,
                          const uint8_t *sep, size_t seplen) {
    if (seplen == 0) return 0;
    if (seplen > slen) return (size_t)-1;
    if (seplen == 1) return neverc_bytes_index_byte(s, slen, sep[0]);
    for (size_t i = 0; i <= slen - seplen; i++) {
        int match = 1;
        for (size_t j = 0; j < seplen; j++) {
            if (s[i + j] != sep[j]) { match = 0; break; }
        }
        if (match) return i;
    }
    return (size_t)-1;
}

size_t neverc_bytes_last_index(const uint8_t *s, size_t slen,
                               const uint8_t *sep, size_t seplen) {
    if (seplen == 0) return slen;
    if (seplen > slen) return (size_t)-1;
    for (size_t i = slen - seplen + 1; i > 0; i--) {
        int match = 1;
        for (size_t j = 0; j < seplen; j++) {
            if (s[i - 1 + j] != sep[j]) { match = 0; break; }
        }
        if (match) return i - 1;
    }
    return (size_t)-1;
}

size_t neverc_bytes_index_any(const uint8_t *s, size_t slen, const char *chars) {
    for (size_t i = 0; i < slen; i++) {
        for (const char *c = chars; *c; c++) {
            if (s[i] == (uint8_t)*c) return i;
        }
    }
    return (size_t)-1;
}

size_t neverc_bytes_last_index_any(const uint8_t *s, size_t slen,
                                   const char *chars) {
    for (size_t i = slen; i > 0; i--) {
        for (const char *c = chars; *c; c++) {
            if (s[i - 1] == (uint8_t)*c) return i - 1;
        }
    }
    return (size_t)-1;
}

int neverc_bytes_contains(const uint8_t *b, size_t blen,
                          const uint8_t *sub, size_t sublen) {
    return neverc_bytes_index(b, blen, sub, sublen) != (size_t)-1;
}

int neverc_bytes_contains_byte(const uint8_t *b, size_t blen, uint8_t c) {
    return neverc_bytes_index_byte(b, blen, c) != (size_t)-1;
}

int neverc_bytes_contains_any(const uint8_t *b, size_t blen, const char *chars) {
    return neverc_bytes_index_any(b, blen, chars) != (size_t)-1;
}

size_t neverc_bytes_count(const uint8_t *s, size_t slen,
                          const uint8_t *sep, size_t seplen) {
    if (seplen == 0) return slen + 1;
    size_t n = 0;
    size_t pos = 0;
    while (pos <= slen - seplen) {
        size_t idx = neverc_bytes_index(s + pos, slen - pos, sep, seplen);
        if (idx == (size_t)-1) break;
        n++;
        pos += idx + seplen;
    }
    return n;
}

/* --- Prefix / Suffix --- */

int neverc_bytes_has_prefix(const uint8_t *s, size_t slen,
                            const uint8_t *prefix, size_t plen) {
    if (plen > slen) return 0;
    return neverc_bytes_equal(s, plen, prefix, plen);
}

int neverc_bytes_has_suffix(const uint8_t *s, size_t slen,
                            const uint8_t *suffix, size_t sfxlen) {
    if (sfxlen > slen) return 0;
    return neverc_bytes_equal(s + slen - sfxlen, sfxlen, suffix, sfxlen);
}

/* --- Transform --- */

uint8_t *neverc_bytes_to_upper(const uint8_t *s, size_t slen, size_t *outlen) {
    uint8_t *r = (uint8_t *)malloc(slen);
    if (!r) { *outlen = 0; return NULL; }
    for (size_t i = 0; i < slen; i++)
        r[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
    *outlen = slen;
    return r;
}

uint8_t *neverc_bytes_to_lower(const uint8_t *s, size_t slen, size_t *outlen) {
    uint8_t *r = (uint8_t *)malloc(slen);
    if (!r) { *outlen = 0; return NULL; }
    for (size_t i = 0; i < slen; i++)
        r[i] = (s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i];
    *outlen = slen;
    return r;
}

uint8_t *neverc_bytes_to_title(const uint8_t *s, size_t slen, size_t *outlen) {
    uint8_t *r = (uint8_t *)malloc(slen);
    if (!r) { *outlen = 0; return NULL; }
    int prev_space = 1;
    for (size_t i = 0; i < slen; i++) {
        uint8_t c = s[i];
        if (prev_space && c >= 'a' && c <= 'z')
            r[i] = c - 32;
        else
            r[i] = c;
        prev_space = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
    }
    *outlen = slen;
    return r;
}

uint8_t *neverc_bytes_repeat(const uint8_t *b, size_t blen,
                             int count, size_t *outlen) {
    if (count <= 0 || blen == 0) {
        *outlen = 0;
        return (uint8_t *)malloc(1);
    }
    size_t total = blen * (size_t)count;
    uint8_t *r = (uint8_t *)malloc(total);
    if (!r) { *outlen = 0; return NULL; }
    for (int i = 0; i < count; i++)
        for (size_t j = 0; j < blen; j++)
            r[i * blen + j] = b[j];
    *outlen = total;
    return r;
}

uint8_t *neverc_bytes_replace(const uint8_t *s, size_t slen,
                              const uint8_t *old, size_t oldlen,
                              const uint8_t *new_, size_t newlen,
                              int n, size_t *outlen) {
    if (n == 0) {
        uint8_t *r = (uint8_t *)malloc(slen);
        for (size_t i = 0; i < slen; i++) r[i] = s[i];
        *outlen = slen;
        return r;
    }
    if (n < 0) n = (int)neverc_bytes_count(s, slen, old, oldlen);

    size_t result_len = slen + (size_t)n * (newlen - oldlen + (oldlen == 0 ? 0 : 0));
    if (newlen > oldlen)
        result_len = slen + (size_t)n * (newlen - oldlen);
    else
        result_len = slen - (size_t)n * (oldlen - newlen);

    uint8_t *r = (uint8_t *)malloc(result_len + 1);
    if (!r) { *outlen = 0; return NULL; }

    size_t wi = 0, ri = 0;
    int replaced = 0;
    while (ri < slen && replaced < n) {
        size_t idx = neverc_bytes_index(s + ri, slen - ri, old, oldlen);
        if (idx == (size_t)-1) break;
        for (size_t i = 0; i < idx; i++) r[wi++] = s[ri + i];
        ri += idx;
        for (size_t i = 0; i < newlen; i++) r[wi++] = new_[i];
        ri += oldlen;
        replaced++;
    }
    while (ri < slen) r[wi++] = s[ri++];
    *outlen = wi;
    return r;
}

uint8_t *neverc_bytes_replace_all(const uint8_t *s, size_t slen,
                                  const uint8_t *old, size_t oldlen,
                                  const uint8_t *new_, size_t newlen,
                                  size_t *outlen) {
    return neverc_bytes_replace(s, slen, old, oldlen, new_, newlen, -1, outlen);
}

/* --- Join --- */

uint8_t *neverc_bytes_join(const uint8_t **slices, const size_t *lens,
                           size_t count,
                           const uint8_t *sep, size_t seplen,
                           size_t *outlen) {
    if (count == 0) { *outlen = 0; return (uint8_t *)malloc(1); }
    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += lens[i];
    total += seplen * (count - 1);

    uint8_t *r = (uint8_t *)malloc(total);
    if (!r) { *outlen = 0; return NULL; }
    size_t wi = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            for (size_t j = 0; j < seplen; j++) r[wi++] = sep[j];
        }
        for (size_t j = 0; j < lens[i]; j++) r[wi++] = slices[i][j];
    }
    *outlen = wi;
    return r;
}

/* --- Trim helpers --- */

static int in_cutset(uint8_t c, const char *cutset) {
    for (const char *p = cutset; *p; p++)
        if ((uint8_t)*p == c) return 1;
    return 0;
}

static int is_space(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\v' || c == '\f';
}

uint8_t *neverc_bytes_trim_left(const uint8_t *s, size_t slen,
                                const char *cutset, size_t *outlen) {
    size_t start = 0;
    while (start < slen && in_cutset(s[start], cutset)) start++;
    *outlen = slen - start;
    uint8_t *r = (uint8_t *)malloc(*outlen + 1);
    for (size_t i = 0; i < *outlen; i++) r[i] = s[start + i];
    return r;
}

uint8_t *neverc_bytes_trim_right(const uint8_t *s, size_t slen,
                                 const char *cutset, size_t *outlen) {
    size_t end = slen;
    while (end > 0 && in_cutset(s[end - 1], cutset)) end--;
    *outlen = end;
    uint8_t *r = (uint8_t *)malloc(*outlen + 1);
    for (size_t i = 0; i < *outlen; i++) r[i] = s[i];
    return r;
}

uint8_t *neverc_bytes_trim(const uint8_t *s, size_t slen,
                           const char *cutset, size_t *outlen) {
    size_t start = 0, end = slen;
    while (start < end && in_cutset(s[start], cutset)) start++;
    while (end > start && in_cutset(s[end - 1], cutset)) end--;
    *outlen = end - start;
    uint8_t *r = (uint8_t *)malloc(*outlen + 1);
    for (size_t i = 0; i < *outlen; i++) r[i] = s[start + i];
    return r;
}

uint8_t *neverc_bytes_trim_space(const uint8_t *s, size_t slen, size_t *outlen) {
    size_t start = 0, end = slen;
    while (start < end && is_space(s[start])) start++;
    while (end > start && is_space(s[end - 1])) end--;
    *outlen = end - start;
    uint8_t *r = (uint8_t *)malloc(*outlen + 1);
    for (size_t i = 0; i < *outlen; i++) r[i] = s[start + i];
    return r;
}

uint8_t *neverc_bytes_trim_prefix(const uint8_t *s, size_t slen,
                                  const uint8_t *prefix, size_t plen,
                                  size_t *outlen) {
    if (neverc_bytes_has_prefix(s, slen, prefix, plen)) {
        *outlen = slen - plen;
        uint8_t *r = (uint8_t *)malloc(*outlen + 1);
        for (size_t i = 0; i < *outlen; i++) r[i] = s[plen + i];
        return r;
    }
    *outlen = slen;
    uint8_t *r = (uint8_t *)malloc(slen + 1);
    for (size_t i = 0; i < slen; i++) r[i] = s[i];
    return r;
}

uint8_t *neverc_bytes_trim_suffix(const uint8_t *s, size_t slen,
                                  const uint8_t *suffix, size_t sfxlen,
                                  size_t *outlen) {
    if (neverc_bytes_has_suffix(s, slen, suffix, sfxlen)) {
        *outlen = slen - sfxlen;
        uint8_t *r = (uint8_t *)malloc(*outlen + 1);
        for (size_t i = 0; i < *outlen; i++) r[i] = s[i];
        return r;
    }
    *outlen = slen;
    uint8_t *r = (uint8_t *)malloc(slen + 1);
    for (size_t i = 0; i < slen; i++) r[i] = s[i];
    return r;
}

/* --- Split --- */

neverc_bytes_slice_t *neverc_bytes_split_n(const uint8_t *s, size_t slen,
                                           const uint8_t *sep, size_t seplen,
                                           int n, size_t *count) {
    if (n == 0) { *count = 0; return NULL; }

    size_t cap = 16;
    neverc_bytes_slice_t *result = (neverc_bytes_slice_t *)malloc(
        cap * sizeof(neverc_bytes_slice_t));
    *count = 0;
    size_t pos = 0;

    while (pos <= slen) {
        if (n > 0 && (int)*count >= n - 1) {
            result[*count].data = s + pos;
            result[*count].len = slen - pos;
            (*count)++;
            break;
        }
        size_t idx;
        if (seplen == 0) {
            if (pos >= slen) break;
            idx = 0;
            result[*count].data = s + pos;
            result[*count].len = 1;
            (*count)++;
            pos += 1;
            if (*count >= cap) {
                cap *= 2;
                result = (neverc_bytes_slice_t *)realloc(
                    result, cap * sizeof(neverc_bytes_slice_t));
            }
            continue;
        }
        idx = neverc_bytes_index(s + pos, slen - pos, sep, seplen);
        if (idx == (size_t)-1) {
            result[*count].data = s + pos;
            result[*count].len = slen - pos;
            (*count)++;
            break;
        }
        result[*count].data = s + pos;
        result[*count].len = idx;
        (*count)++;
        pos += idx + seplen;
        if (*count >= cap) {
            cap *= 2;
            result = (neverc_bytes_slice_t *)realloc(
                result, cap * sizeof(neverc_bytes_slice_t));
        }
        if (pos > slen) break;
        if (pos == slen) {
            result[*count].data = s + pos;
            result[*count].len = 0;
            (*count)++;
            break;
        }
    }
    return result;
}

neverc_bytes_slice_t *neverc_bytes_split(const uint8_t *s, size_t slen,
                                         const uint8_t *sep, size_t seplen,
                                         size_t *count) {
    return neverc_bytes_split_n(s, slen, sep, seplen, -1, count);
}

neverc_bytes_slice_t *neverc_bytes_fields(const uint8_t *s, size_t slen,
                                          size_t *count) {
    size_t cap = 16;
    neverc_bytes_slice_t *result = (neverc_bytes_slice_t *)malloc(
        cap * sizeof(neverc_bytes_slice_t));
    *count = 0;
    size_t i = 0;
    while (i < slen) {
        while (i < slen && is_space(s[i])) i++;
        if (i >= slen) break;
        size_t start = i;
        while (i < slen && !is_space(s[i])) i++;
        if (*count >= cap) {
            cap *= 2;
            result = (neverc_bytes_slice_t *)realloc(
                result, cap * sizeof(neverc_bytes_slice_t));
        }
        result[*count].data = s + start;
        result[*count].len = i - start;
        (*count)++;
    }
    return result;
}

/* --- Cut --- */

int neverc_bytes_cut(const uint8_t *s, size_t slen,
                     const uint8_t *sep, size_t seplen,
                     const uint8_t **before, size_t *blen,
                     const uint8_t **after, size_t *alen) {
    size_t idx = neverc_bytes_index(s, slen, sep, seplen);
    if (idx == (size_t)-1) {
        *before = s; *blen = slen;
        *after = s + slen; *alen = 0;
        return 0;
    }
    *before = s; *blen = idx;
    *after = s + idx + seplen; *alen = slen - idx - seplen;
    return 1;
}

int neverc_bytes_cut_prefix(const uint8_t *s, size_t slen,
                            const uint8_t *prefix, size_t plen,
                            const uint8_t **after, size_t *alen) {
    if (neverc_bytes_has_prefix(s, slen, prefix, plen)) {
        *after = s + plen; *alen = slen - plen;
        return 1;
    }
    *after = s; *alen = slen;
    return 0;
}

int neverc_bytes_cut_suffix(const uint8_t *s, size_t slen,
                            const uint8_t *suffix, size_t sfxlen,
                            const uint8_t **before, size_t *blen) {
    if (neverc_bytes_has_suffix(s, slen, suffix, sfxlen)) {
        *before = s; *blen = slen - sfxlen;
        return 1;
    }
    *before = s; *blen = slen;
    return 0;
}

/* --- Func-based operations --- */

int neverc_bytes_contains_func(const uint8_t *b, size_t blen,
                               neverc_bytes_func_t f) {
    for (size_t i = 0; i < blen; i++)
        if (f(b[i])) return 1;
    return 0;
}

size_t neverc_bytes_index_func(const uint8_t *s, size_t slen,
                               neverc_bytes_func_t f) {
    for (size_t i = 0; i < slen; i++)
        if (f(s[i])) return i;
    return (size_t)-1;
}

size_t neverc_bytes_last_index_func(const uint8_t *s, size_t slen,
                                    neverc_bytes_func_t f) {
    for (size_t i = slen; i > 0; i--)
        if (f(s[i-1])) return i-1;
    return (size_t)-1;
}

uint8_t *neverc_bytes_trim_func(const uint8_t *s, size_t slen,
                                neverc_bytes_func_t f, size_t *outlen) {
    size_t start = 0, end = slen;
    while (start < end && f(s[start])) start++;
    while (end > start && f(s[end-1])) end--;
    *outlen = end - start;
    if (*outlen == 0) return NULL;
    uint8_t *r = (uint8_t *)malloc(*outlen);
    for (size_t i = 0; i < *outlen; i++) r[i] = s[start + i];
    return r;
}

uint8_t *neverc_bytes_trim_left_func(const uint8_t *s, size_t slen,
                                     neverc_bytes_func_t f, size_t *outlen) {
    size_t start = 0;
    while (start < slen && f(s[start])) start++;
    *outlen = slen - start;
    if (*outlen == 0) return NULL;
    uint8_t *r = (uint8_t *)malloc(*outlen);
    for (size_t i = 0; i < *outlen; i++) r[i] = s[start + i];
    return r;
}

uint8_t *neverc_bytes_trim_right_func(const uint8_t *s, size_t slen,
                                      neverc_bytes_func_t f, size_t *outlen) {
    size_t end = slen;
    while (end > 0 && f(s[end-1])) end--;
    *outlen = end;
    if (*outlen == 0) return NULL;
    uint8_t *r = (uint8_t *)malloc(*outlen);
    for (size_t i = 0; i < *outlen; i++) r[i] = s[i];
    return r;
}

neverc_bytes_slice_t *neverc_bytes_fields_func(const uint8_t *s, size_t slen,
                                               neverc_bytes_func_t f,
                                               size_t *count) {
    *count = 0;
    size_t cap = 8;
    neverc_bytes_slice_t *result = (neverc_bytes_slice_t *)malloc(cap * sizeof(*result));
    size_t i = 0;
    while (i < slen) {
        while (i < slen && f(s[i])) i++;
        if (i >= slen) break;
        size_t start = i;
        while (i < slen && !f(s[i])) i++;
        if (*count >= cap) {
            cap *= 2;
            result = (neverc_bytes_slice_t *)realloc(result, cap * sizeof(*result));
        }
        result[*count].data = s + start;
        result[*count].len = i - start;
        (*count)++;
    }
    return result;
}

/* --- Map --- */

uint8_t *neverc_bytes_map(uint8_t (*mapping)(uint8_t),
                          const uint8_t *s, size_t slen, size_t *outlen) {
    *outlen = slen;
    if (slen == 0) return NULL;
    uint8_t *r = (uint8_t *)malloc(slen);
    for (size_t i = 0; i < slen; i++) r[i] = mapping(s[i]);
    return r;
}

/* --- SplitAfter --- */

neverc_bytes_slice_t *neverc_bytes_split_after(const uint8_t *s, size_t slen,
                                               const uint8_t *sep, size_t seplen,
                                               size_t *count) {
    return neverc_bytes_split_after_n(s, slen, sep, seplen, -1, count);
}

neverc_bytes_slice_t *neverc_bytes_split_after_n(const uint8_t *s, size_t slen,
                                                 const uint8_t *sep, size_t seplen,
                                                 int n, size_t *count) {
    *count = 0;
    if (n == 0) return NULL;
    size_t cap = 8;
    neverc_bytes_slice_t *result = (neverc_bytes_slice_t *)malloc(cap * sizeof(*result));
    const uint8_t *p = s;
    size_t remaining = slen;

    while (remaining > 0) {
        if (n > 0 && (int)*count >= n - 1) {
            if (*count >= cap) { cap *= 2; result = (neverc_bytes_slice_t *)realloc(result, cap * sizeof(*result)); }
            result[*count].data = p;
            result[*count].len = remaining;
            (*count)++;
            break;
        }
        size_t idx = (size_t)-1;
        if (seplen > 0 && remaining >= seplen) {
            for (size_t i = 0; i <= remaining - seplen; i++) {
                int match = 1;
                for (size_t j = 0; j < seplen; j++) {
                    if (p[i+j] != sep[j]) { match = 0; break; }
                }
                if (match) { idx = i; break; }
            }
        }
        if (idx == (size_t)-1) {
            if (*count >= cap) { cap *= 2; result = (neverc_bytes_slice_t *)realloc(result, cap * sizeof(*result)); }
            result[*count].data = p;
            result[*count].len = remaining;
            (*count)++;
            break;
        }
        size_t chunk = idx + seplen;
        if (*count >= cap) { cap *= 2; result = (neverc_bytes_slice_t *)realloc(result, cap * sizeof(*result)); }
        result[*count].data = p;
        result[*count].len = chunk;
        (*count)++;
        p += chunk;
        remaining -= chunk;
    }
    return result;
}

/* --- Clone --- */

uint8_t *neverc_bytes_clone(const uint8_t *b, size_t blen) {
    if (blen == 0) return NULL;
    uint8_t *r = (uint8_t *)malloc(blen);
    if (!r) return NULL;
    for (size_t i = 0; i < blen; i++) r[i] = b[i];
    return r;
}

/* --- CutLast --- */

int neverc_bytes_cut_last(const uint8_t *s, size_t slen,
                          const uint8_t *sep, size_t seplen,
                          const uint8_t **before, size_t *blen,
                          const uint8_t **after, size_t *alen) {
    size_t idx = neverc_bytes_last_index(s, slen, sep, seplen);
    if (idx == (size_t)-1) {
        *before = s; *blen = slen;
        *after = s + slen; *alen = 0;
        return 0;
    }
    *before = s; *blen = idx;
    *after = s + idx + seplen; *alen = slen - idx - seplen;
    return 1;
}

/* --- UTF-8 helpers for rune operations --- */

static size_t utf8_decode(const uint8_t *p, size_t remaining, uint32_t *r) {
    if (remaining == 0) { *r = 0; return 0; }
    uint8_t b = p[0];
    if (b < 0x80) { *r = b; return 1; }
    if ((b & 0xE0) == 0xC0 && remaining >= 2 && (p[1] & 0xC0) == 0x80) {
        *r = ((uint32_t)(b & 0x1F) << 6) | (p[1] & 0x3F);
        return (*r >= 0x80) ? 2 : 0;
    }
    if ((b & 0xF0) == 0xE0 && remaining >= 3 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *r = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        if (*r >= 0x800 && (*r < 0xD800 || *r > 0xDFFF)) return 3;
        return 0;
    }
    if ((b & 0xF8) == 0xF0 && remaining >= 4 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        *r = ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) | ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        if (*r >= 0x10000 && *r <= 0x10FFFF) return 4;
        return 0;
    }
    *r = 0xFFFD; return 0;
}

static size_t utf8_encode(uint32_t r, uint8_t *buf) {
    if (r < 0x80) { buf[0] = (uint8_t)r; return 1; }
    if (r < 0x800) { buf[0] = 0xC0 | (r >> 6); buf[1] = 0x80 | (r & 0x3F); return 2; }
    if (r < 0x10000) { buf[0] = 0xE0 | (r >> 12); buf[1] = 0x80 | ((r >> 6) & 0x3F); buf[2] = 0x80 | (r & 0x3F); return 3; }
    buf[0] = 0xF0 | (r >> 18); buf[1] = 0x80 | ((r >> 12) & 0x3F); buf[2] = 0x80 | ((r >> 6) & 0x3F); buf[3] = 0x80 | (r & 0x3F); return 4;
}

/* --- IndexRune --- */

size_t neverc_bytes_index_rune(const uint8_t *s, size_t slen, uint32_t r) {
    if (r < 0x80) return neverc_bytes_index_byte(s, slen, (uint8_t)r);
    uint8_t enc[4];
    size_t elen = utf8_encode(r, enc);
    return neverc_bytes_index(s, slen, enc, elen);
}

/* --- Runes --- */

uint32_t *neverc_bytes_runes(const uint8_t *s, size_t slen, size_t *count) {
    *count = 0;
    size_t cap = slen > 0 ? slen : 1;
    uint32_t *result = (uint32_t *)malloc(cap * sizeof(uint32_t));
    if (!result) return NULL;
    size_t i = 0;
    while (i < slen) {
        uint32_t r;
        size_t n = utf8_decode(s + i, slen - i, &r);
        if (n == 0) { r = 0xFFFD; n = 1; }
        if (*count >= cap) { cap *= 2; result = (uint32_t *)realloc(result, cap * sizeof(uint32_t)); }
        result[(*count)++] = r;
        i += n;
    }
    return result;
}

/* --- ToValidUTF8 --- */

uint8_t *neverc_bytes_to_valid_utf8(const uint8_t *s, size_t slen,
                                     const uint8_t *replacement, size_t rlen,
                                     size_t *outlen) {
    size_t cap = slen + rlen * 4;
    uint8_t *result = (uint8_t *)malloc(cap);
    if (!result) { *outlen = 0; return NULL; }
    size_t out = 0;
    size_t i = 0;
    while (i < slen) {
        uint32_t r;
        size_t n = utf8_decode(s + i, slen - i, &r);
        if (n == 0) {
            while (out + rlen >= cap) { cap *= 2; result = (uint8_t *)realloc(result, cap); }
            for (size_t j = 0; j < rlen; j++) result[out++] = replacement[j];
            i++;
        } else {
            while (out + n >= cap) { cap *= 2; result = (uint8_t *)realloc(result, cap); }
            for (size_t j = 0; j < n; j++) result[out++] = s[i + j];
            i += n;
        }
    }
    *outlen = out;
    return result;
}
