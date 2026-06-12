#include "neverc/std/cstring.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ======================================================================
 * Internal helpers
 * ====================================================================== */

static char *nc_strdup(const char *s, size_t len) {
    char *r = (char *)malloc(len + 1);
    if (!r) return NULL;
    if (len > 0) memcpy(r, s, len);
    r[len] = '\0';
    return r;
}

static char to_lower_ch(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static char to_upper_ch(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - ('a' - 'A')) : c;
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\f' || c == '\v';
}

static void build_ascii_set(const char *chars, uint32_t set[8]) {
    memset(set, 0, 8 * sizeof(uint32_t));
    for (const char *c = chars; *c; c++)
        set[((unsigned char)*c) >> 5] |= 1u << (((unsigned char)*c) & 31);
}

#define ASCII_SET_HAS(set, c) ((set)[((unsigned char)(c)) >> 5] & (1u << (((unsigned char)(c)) & 31)))

static int in_cutset(char c, const uint32_t set[8]) {
    return ASCII_SET_HAS(set, c) != 0;
}

static int is_separator(char c) {
    if (c >= '0' && c <= '9') return 0;
    if (c >= 'a' && c <= 'z') return 0;
    if (c >= 'A' && c <= 'Z') return 0;
    if (c == '_') return 0;
    return 1;
}

/* ======================================================================
 * Comparison
 * ====================================================================== */

int neverc_cstring_compare(const char *a, const char *b) {
    int r = strcmp(a, b);
    return r < 0 ? -1 : r > 0 ? 1 : 0;
}

int neverc_cstring_equal_fold(const char *s, const char *t) {
    size_t slen = strlen(s);
    size_t tlen = strlen(t);
    if (slen != tlen) return 0;
    size_t i = 0;
    #define NCI_FOLD_ONES  ((uint64_t)0x0101010101010101ULL)
    #define NCI_FOLD_CASE  (NCI_FOLD_ONES * 0x20)
    #define NCI_FOLD_HIGHS (NCI_FOLD_ONES * 0x80)
    while (i + 8 <= slen) {
        uint64_t ws, wt;
        memcpy(&ws, s + i, 8);
        memcpy(&wt, t + i, 8);
        if (ws == wt) { i += 8; continue; }
        uint64_t ls = ws | NCI_FOLD_CASE;
        uint64_t lt = wt | NCI_FOLD_CASE;
        if (ls != lt) return 0;
        uint64_t diff = ws ^ wt;
        uint64_t lowered = ls;
        uint64_t cleared = lowered & ~NCI_FOLD_HIGHS;
        uint64_t below_a = cleared + NCI_FOLD_ONES * (0x80u - 'a');
        uint64_t above_z = cleared + NCI_FOLD_ONES * (0x7fu - 'z');
        uint64_t is_letter = ((below_a ^ above_z) & ~lowered) & NCI_FOLD_HIGHS;
        if (diff & ~(is_letter >> 2)) return 0;
        i += 8;
    }
    #undef NCI_FOLD_ONES
    #undef NCI_FOLD_CASE
    #undef NCI_FOLD_HIGHS
    for (; i < slen; i++)
        if (to_lower_ch(s[i]) != to_lower_ch(t[i])) return 0;
    return 1;
}

/* ======================================================================
 * Search / Index
 * ====================================================================== */

int neverc_cstring_index_byte(const char *s, char c) {
    const char *p = strchr(s, c);
    return p ? (int)(p - s) : -1;
}

int neverc_cstring_last_index_byte(const char *s, char c) {
    const char *p = strrchr(s, c);
    return p ? (int)(p - s) : -1;
}

int neverc_cstring_index(const char *s, const char *substr) {
    size_t slen = strlen(s);
    size_t sublen = strlen(substr);
    if (sublen == 0) return 0;
    if (sublen > slen) return -1;
    if (sublen == 1) return neverc_cstring_index_byte(s, substr[0]);
    if (sublen == slen) return memcmp(s, substr, slen) == 0 ? 0 : -1;

    const unsigned char *us = (const unsigned char *)s;
    const unsigned char *up = (const unsigned char *)substr;

    if (sublen <= 8 || slen <= 64) {
        unsigned char c0 = up[0];
        for (size_t i = 0; i <= slen - sublen; i++)
            if (us[i] == c0 && memcmp(us + i + 1, up + 1, sublen - 1) == 0)
                return (int)i;
        return -1;
    }

    /* Boyer-Moore-Horspool */
    size_t skip[256];
    for (int c = 0; c < 256; c++) skip[c] = sublen;
    for (size_t i = 0; i < sublen - 1; i++) skip[up[i]] = sublen - 1 - i;

    unsigned char last = up[sublen - 1];
    size_t pos = 0;
    while (pos <= slen - sublen) {
        unsigned char c = us[pos + sublen - 1];
        if (c == last && memcmp(us + pos, up, sublen - 1) == 0)
            return (int)pos;
        pos += skip[c];
    }
    return -1;
}

int neverc_cstring_last_index(const char *s, const char *substr) {
    size_t slen = strlen(s);
    size_t sublen = strlen(substr);
    if (sublen == 0) return (int)slen;
    if (sublen > slen) return -1;
    if (sublen == 1) return neverc_cstring_last_index_byte(s, substr[0]);
    if (sublen == slen) return memcmp(s, substr, slen) == 0 ? 0 : -1;

    const unsigned char *us = (const unsigned char *)s;
    const unsigned char *up = (const unsigned char *)substr;

    if (sublen <= 8 || slen <= 64) {
        unsigned char clast = up[sublen - 1];
        for (size_t i = slen; i >= sublen; i--)
            if (us[i - 1] == clast && memcmp(s + i - sublen, substr, sublen) == 0)
                return (int)(i - sublen);
        return -1;
    }

    /* Reverse Boyer-Moore-Horspool */
    size_t skip[256];
    for (int c = 0; c < 256; c++) skip[c] = sublen;
    for (size_t i = sublen - 1; i > 0; i--) skip[up[i]] = i;

    unsigned char first = up[0];
    size_t pos = slen - sublen;
    for (;;) {
        unsigned char c = us[pos];
        if (c == first && memcmp(us + pos + 1, up + 1, sublen - 1) == 0)
            return (int)pos;
        size_t shift = skip[c];
        if (pos < shift) break;
        pos -= shift;
    }
    return -1;
}

int neverc_cstring_index_any(const char *s, const char *chars) {
    if (!chars[0]) return -1;
    uint32_t set[8];
    build_ascii_set(chars, set);
    for (int i = 0; s[i]; i++)
        if (ASCII_SET_HAS(set, s[i])) return i;
    return -1;
}

int neverc_cstring_last_index_any(const char *s, const char *chars) {
    if (!chars[0]) return -1;
    uint32_t set[8];
    build_ascii_set(chars, set);
    size_t len = strlen(s);
    for (size_t i = len; i > 0; i--)
        if (ASCII_SET_HAS(set, s[i - 1])) return (int)(i - 1);
    return -1;
}

int neverc_cstring_contains(const char *s, const char *substr) {
    return neverc_cstring_index(s, substr) >= 0;
}

int neverc_cstring_contains_any(const char *s, const char *chars) {
    return neverc_cstring_index_any(s, chars) >= 0;
}

int neverc_cstring_contains_char(const char *s, char c) {
    return neverc_cstring_index_byte(s, c) >= 0;
}

int neverc_cstring_count(const char *s, const char *substr) {
    size_t sublen = strlen(substr);
    if (sublen == 0) {
        size_t len = strlen(s);
        return (int)len + 1;
    }
    size_t slen = strlen(s);
    if (sublen > slen) return 0;
    if (sublen == 1) {
        int n = 0;
        const char *p = s;
        char c = substr[0];
        while ((p = strchr(p, c)) != NULL) {
            n++;
            p++;
        }
        return n;
    }

    const unsigned char *us = (const unsigned char *)s;
    const unsigned char *up = (const unsigned char *)substr;
    int n = 0;

    if (sublen <= 8 || slen <= 64) {
        unsigned char c0 = up[0];
        size_t pos = 0;
        while (pos <= slen - sublen) {
            if (us[pos] == c0 && memcmp(us + pos + 1, up + 1, sublen - 1) == 0) {
                n++;
                pos += sublen;
            } else {
                pos++;
            }
        }
        return n;
    }

    /* BMH with skip table built once */
    size_t skip[256];
    for (int c = 0; c < 256; c++) skip[c] = sublen;
    for (size_t i = 0; i < sublen - 1; i++) skip[up[i]] = sublen - 1 - i;

    unsigned char last = up[sublen - 1];
    size_t pos = 0;
    while (pos <= slen - sublen) {
        unsigned char c = us[pos + sublen - 1];
        if (c == last && memcmp(us + pos, up, sublen - 1) == 0) {
            n++;
            pos += sublen;
        } else {
            pos += skip[c];
        }
    }
    return n;
}

/* ======================================================================
 * Prefix / Suffix
 * ====================================================================== */

int neverc_cstring_has_prefix(const char *s, const char *prefix) {
    size_t plen = strlen(prefix);
    if (plen == 0) return 1;
    size_t slen = strlen(s);
    if (plen > slen) return 0;
    return memcmp(s, prefix, plen) == 0;
}

int neverc_cstring_has_suffix(const char *s, const char *suffix) {
    size_t sfxlen = strlen(suffix);
    if (sfxlen == 0) return 1;
    size_t slen = strlen(s);
    if (sfxlen > slen) return 0;
    return memcmp(s + slen - sfxlen, suffix, sfxlen) == 0;
}

/* ======================================================================
 * Transform
 * ====================================================================== */

/* SWAR 8-byte parallel ASCII case conversion */
#define NCI_SWAR_HIGHS 0x8080808080808080ULL

static inline uint64_t nci_swar_range(uint64_t w, uint8_t lo, uint8_t hi) {
    uint64_t cleared = w & ~NCI_SWAR_HIGHS;
    uint64_t t_hi = cleared + (uint64_t)(0x7fu - hi) * 0x0101010101010101ULL;
    uint64_t t_lo = cleared + (uint64_t)(0x80u - lo) * 0x0101010101010101ULL;
    return (t_lo ^ t_hi) & NCI_SWAR_HIGHS;
}

char *neverc_cstring_to_upper(const char *s) {
    size_t len = strlen(s);
    char *r = (char *)malloc(len + 1);
    if (!r) return NULL;
    size_t i = 0;
    for (; i + 8 <= len; i += 8) {
        uint64_t w;
        memcpy(&w, s + i, 8);
        w ^= nci_swar_range(w, 'a', 'z') >> 2;
        memcpy(r + i, &w, 8);
    }
    for (; i < len; i++) r[i] = to_upper_ch(s[i]);
    r[len] = '\0';
    return r;
}

char *neverc_cstring_to_lower(const char *s) {
    size_t len = strlen(s);
    char *r = (char *)malloc(len + 1);
    if (!r) return NULL;
    size_t i = 0;
    for (; i + 8 <= len; i += 8) {
        uint64_t w;
        memcpy(&w, s + i, 8);
        w ^= nci_swar_range(w, 'A', 'Z') >> 2;
        memcpy(r + i, &w, 8);
    }
    for (; i < len; i++) r[i] = to_lower_ch(s[i]);
    r[len] = '\0';
    return r;
}

char *neverc_cstring_to_title(const char *s) {
    size_t len = strlen(s);
    char *r = (char *)malloc(len + 1);
    if (!r) return NULL;
    int prev_sep = 1;
    for (size_t i = 0; i < len; i++) {
        if (prev_sep)
            r[i] = to_upper_ch(s[i]);
        else
            r[i] = s[i];
        prev_sep = is_separator(s[i]);
    }
    r[len] = '\0';
    return r;
}

char *neverc_cstring_repeat(const char *s, int count) {
    if (count <= 0) return nc_strdup("", 0);
    size_t len = strlen(s);
    if (len == 0) return nc_strdup("", 0);
    size_t total = len * (size_t)count;
    char *r = (char *)malloc(total + 1);
    if (!r) return NULL;
    memcpy(r, s, len);
    for (size_t copied = len; copied < total; ) {
        size_t chunk = total - copied;
        if (chunk > copied) chunk = copied;
        memcpy(r + copied, r, chunk);
        copied += chunk;
    }
    r[total] = '\0';
    return r;
}

/*
 * Internal BMH search with pre-built skip table for cstring.
 * Returns index or -1. Avoids rebuilding the table per call in replace loops.
 */
static int nci_cstring_index_with_skip(const char *s, size_t slen,
                                       const unsigned char *pat, size_t patlen,
                                       const size_t skip[256]) {
    if (slen < patlen) return -1;
    unsigned char last = pat[patlen - 1];
    size_t pos = 0;
    while (pos <= slen - patlen) {
        unsigned char c = (unsigned char)s[pos + patlen - 1];
        if (c == last && memcmp(s + pos, pat, patlen - 1) == 0)
            return (int)pos;
        pos += skip[c];
    }
    return -1;
}

char *neverc_cstring_replace(const char *s, const char *old_s,
                              const char *new_s, int n) {
    if (n == 0) return nc_strdup(s, strlen(s));

    size_t slen = strlen(s);
    size_t oldlen = strlen(old_s);
    size_t newlen = strlen(new_s);

    if (oldlen == 0) {
        int reps = (n < 0) ? (int)slen + 1 : (n < (int)slen + 1 ? n : (int)slen + 1);
        size_t total = slen + (size_t)reps * newlen;
        char *r = (char *)malloc(total + 1);
        if (!r) return NULL;
        size_t w = 0;
        int done = 0;
        for (size_t i = 0; i < slen; i++) {
            if (done < reps) {
                memcpy(r + w, new_s, newlen);
                w += newlen;
                done++;
            }
            r[w++] = s[i];
        }
        if (done < reps) {
            memcpy(r + w, new_s, newlen);
            w += newlen;
        }
        r[w] = '\0';
        return r;
    }

    int use_bmh = (oldlen > 8 && slen > 64);
    size_t skip[256];
    const unsigned char *up = (const unsigned char *)old_s;
    if (use_bmh) {
        for (int c = 0; c < 256; c++) skip[c] = oldlen;
        for (size_t i = 0; i < oldlen - 1; i++) skip[up[i]] = oldlen - 1 - i;
    }

    int cnt = 0;
    {
        const char *p = s;
        size_t remaining = slen;
        while (remaining >= oldlen) {
            int idx;
            if (use_bmh)
                idx = nci_cstring_index_with_skip(p, remaining, up, oldlen, skip);
            else
                idx = neverc_cstring_index(p, old_s);
            if (idx < 0) break;
            cnt++;
            p += idx + oldlen;
            remaining -= (size_t)idx + oldlen;
        }
    }
    if (n >= 0 && cnt > n) cnt = n;

    size_t total = slen - (size_t)cnt * oldlen + (size_t)cnt * newlen;

    char *r = (char *)malloc(total + 1);
    if (!r) return NULL;
    size_t w = 0;
    int done = 0;
    const char *p = s;
    size_t remaining = slen;
    while (remaining > 0) {
        if (done < cnt) {
            int idx;
            if (use_bmh)
                idx = nci_cstring_index_with_skip(p, remaining, up, oldlen, skip);
            else
                idx = neverc_cstring_index(p, old_s);
            if (idx >= 0) {
                memcpy(r + w, p, (size_t)idx);
                w += (size_t)idx;
                memcpy(r + w, new_s, newlen);
                w += newlen;
                p += idx + oldlen;
                remaining -= (size_t)idx + oldlen;
                done++;
                continue;
            }
        }
        memcpy(r + w, p, remaining);
        w += remaining;
        break;
    }
    r[w] = '\0';
    return r;
}

char *neverc_cstring_replace_all(const char *s, const char *old_s,
                                  const char *new_s) {
    return neverc_cstring_replace(s, old_s, new_s, -1);
}

char *neverc_cstring_map(char (*mapping)(char), const char *s) {
    size_t len = strlen(s);
    char *r = (char *)malloc(len + 1);
    if (!r) return NULL;
    for (size_t i = 0; i < len; i++) r[i] = mapping(s[i]);
    r[len] = '\0';
    return r;
}

/* ======================================================================
 * Join
 * ====================================================================== */

char *neverc_cstring_join(const char **strs, size_t count, const char *sep) {
    if (count == 0) return nc_strdup("", 0);
    size_t seplen = strlen(sep);

    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        total += strlen(strs[i]);
        if (i > 0) total += seplen;
    }

    char *r = (char *)malloc(total + 1);
    if (!r) return NULL;
    size_t w = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            memcpy(r + w, sep, seplen);
            w += seplen;
        }
        size_t elen = strlen(strs[i]);
        memcpy(r + w, strs[i], elen);
        w += elen;
    }
    r[w] = '\0';
    return r;
}

/* ======================================================================
 * Trim
 * ====================================================================== */

char *neverc_cstring_trim_left(const char *s, const char *cutset) {
    uint32_t set[8];
    build_ascii_set(cutset, set);
    while (*s && in_cutset(*s, set)) s++;
    return nc_strdup(s, strlen(s));
}

char *neverc_cstring_trim_right(const char *s, const char *cutset) {
    uint32_t set[8];
    build_ascii_set(cutset, set);
    size_t len = strlen(s);
    while (len > 0 && in_cutset(s[len - 1], set)) len--;
    return nc_strdup(s, len);
}

char *neverc_cstring_trim(const char *s, const char *cutset) {
    uint32_t set[8];
    build_ascii_set(cutset, set);
    while (*s && in_cutset(*s, set)) s++;
    size_t len = strlen(s);
    while (len > 0 && in_cutset(s[len - 1], set)) len--;
    return nc_strdup(s, len);
}

char *neverc_cstring_trim_space(const char *s) {
    while (*s && is_space(*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && is_space(s[len - 1])) len--;
    return nc_strdup(s, len);
}

char *neverc_cstring_trim_prefix(const char *s, const char *prefix) {
    if (neverc_cstring_has_prefix(s, prefix)) {
        size_t plen = strlen(prefix);
        return nc_strdup(s + plen, strlen(s) - plen);
    }
    return nc_strdup(s, strlen(s));
}

char *neverc_cstring_trim_suffix(const char *s, const char *suffix) {
    size_t slen = strlen(s);
    size_t sfxlen = strlen(suffix);
    if (sfxlen <= slen && memcmp(s + slen - sfxlen, suffix, sfxlen) == 0)
        return nc_strdup(s, slen - sfxlen);
    return nc_strdup(s, slen);
}

/* ======================================================================
 * Split
 * ====================================================================== */

static char **gen_split(const char *s, const char *sep,
                         int sep_save, int n, size_t *out_count) {
    if (n == 0) { *out_count = 0; return NULL; }
    size_t slen = strlen(s);
    size_t seplen = strlen(sep);

    if (seplen == 0) {
        int cnt = (int)slen;
        if (n > 0 && cnt > n) cnt = n;
        if (cnt == 0) { *out_count = 0; return NULL; }
        char **arr = (char **)malloc((size_t)cnt * sizeof(char *));
        if (!arr) { *out_count = 0; return NULL; }
        for (int i = 0; i < cnt - 1; i++)
            arr[i] = nc_strdup(s + i, 1);
        arr[cnt - 1] = nc_strdup(s + cnt - 1, slen - (size_t)(cnt - 1));
        *out_count = (size_t)cnt;
        return arr;
    }

    if (n < 0) {
        n = neverc_cstring_count(s, sep) + 1;
    }

    size_t cap = (size_t)n;
    if (cap > slen + 1) cap = slen + 1;
    char **arr = (char **)malloc(cap * sizeof(char *));
    if (!arr) { *out_count = 0; return NULL; }

    int use_bmh = (seplen > 8 && slen > 64);
    size_t skip[256];
    const unsigned char *up = (const unsigned char *)sep;
    if (use_bmh) {
        for (int c = 0; c < 256; c++) skip[c] = seplen;
        for (size_t i = 0; i < seplen - 1; i++) skip[up[i]] = seplen - 1 - i;
    }

    size_t cnt = 0;
    const char *p = s;
    size_t remaining = slen;
    int limit = n - 1;
    while (cnt < (size_t)limit) {
        int idx;
        if (use_bmh)
            idx = nci_cstring_index_with_skip(p, remaining, up, seplen, skip);
        else
            idx = neverc_cstring_index(p, sep);
        if (idx < 0) break;
        arr[cnt] = nc_strdup(p, (size_t)idx + (size_t)sep_save);
        cnt++;
        p += idx + (int)seplen;
        remaining -= (size_t)idx + seplen;
    }
    arr[cnt] = nc_strdup(p, strlen(p));
    cnt++;
    *out_count = cnt;
    return arr;
}

char **neverc_cstring_split(const char *s, const char *sep, size_t *count) {
    return gen_split(s, sep, 0, -1, count);
}

char **neverc_cstring_split_n(const char *s, const char *sep,
                               int n, size_t *count) {
    return gen_split(s, sep, 0, n, count);
}

char **neverc_cstring_split_after(const char *s, const char *sep,
                                   size_t *count) {
    return gen_split(s, sep, (int)strlen(sep), -1, count);
}

char **neverc_cstring_split_after_n(const char *s, const char *sep,
                                     int n, size_t *count) {
    return gen_split(s, sep, (int)strlen(sep), n, count);
}

char **neverc_cstring_fields(const char *s, size_t *count) {
    /* Count fields first */
    size_t n = 0;
    int in_field = 0;
    for (const char *p = s; *p; p++) {
        if (is_space(*p)) {
            in_field = 0;
        } else if (!in_field) {
            in_field = 1;
            n++;
        }
    }
    if (n == 0) { *count = 0; return NULL; }

    char **arr = (char **)malloc(n * sizeof(char *));
    if (!arr) { *count = 0; return NULL; }

    size_t idx = 0;
    const char *p = s;
    while (*p) {
        while (*p && is_space(*p)) p++;
        if (!*p) break;
        const char *start = p;
        while (*p && !is_space(*p)) p++;
        arr[idx++] = nc_strdup(start, (size_t)(p - start));
    }
    *count = idx;
    return arr;
}

void neverc_cstring_free_split(char **arr, size_t count) {
    if (!arr) return;
    for (size_t i = 0; i < count; i++) free(arr[i]);
    free(arr);
}

/* ======================================================================
 * Cut
 * ====================================================================== */

int neverc_cstring_cut(const char *s, const char *sep,
                        char **before, char **after) {
    int idx = neverc_cstring_index(s, sep);
    if (idx < 0) {
        *before = nc_strdup(s, strlen(s));
        *after = nc_strdup("", 0);
        return 0;
    }
    size_t seplen = strlen(sep);
    *before = nc_strdup(s, (size_t)idx);
    *after = nc_strdup(s + idx + seplen, strlen(s + idx + seplen));
    return 1;
}

int neverc_cstring_cut_prefix(const char *s, const char *prefix,
                               char **after) {
    if (neverc_cstring_has_prefix(s, prefix)) {
        size_t plen = strlen(prefix);
        *after = nc_strdup(s + plen, strlen(s) - plen);
        return 1;
    }
    *after = nc_strdup(s, strlen(s));
    return 0;
}

int neverc_cstring_cut_suffix(const char *s, const char *suffix,
                               char **before) {
    size_t slen = strlen(s);
    size_t sfxlen = strlen(suffix);
    if (sfxlen <= slen && memcmp(s + slen - sfxlen, suffix, sfxlen) == 0) {
        *before = nc_strdup(s, slen - sfxlen);
        return 1;
    }
    *before = nc_strdup(s, slen);
    return 0;
}

int neverc_cstring_cut_last(const char *s, const char *sep,
                             char **before, char **after) {
    int idx = neverc_cstring_last_index(s, sep);
    if (idx < 0) {
        *before = nc_strdup(s, strlen(s));
        *after = nc_strdup("", 0);
        return 0;
    }
    size_t seplen = strlen(sep);
    *before = nc_strdup(s, (size_t)idx);
    *after = nc_strdup(s + idx + seplen, strlen(s + idx + seplen));
    return 1;
}

/* ======================================================================
 * Clone / Utility
 * ====================================================================== */

char *neverc_cstring_clone(const char *s) {
    return nc_strdup(s, strlen(s));
}

size_t neverc_cstring_len(const char *s) {
    return strlen(s);
}
