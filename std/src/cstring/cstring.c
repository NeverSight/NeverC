#include "neverc/std/cstring.h"
#include <stdlib.h>
#include <string.h>

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

static int in_cutset(char c, const char *cutset) {
    for (const char *p = cutset; *p; p++)
        if (*p == c) return 1;
    return 0;
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
    while (*a && *b) {
        if ((unsigned char)*a < (unsigned char)*b) return -1;
        if ((unsigned char)*a > (unsigned char)*b) return 1;
        a++;
        b++;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

int neverc_cstring_equal_fold(const char *s, const char *t) {
    while (*s && *t) {
        if (to_lower_ch(*s) != to_lower_ch(*t)) return 0;
        s++;
        t++;
    }
    return *s == '\0' && *t == '\0';
}

/* ======================================================================
 * Search / Index
 * ====================================================================== */

int neverc_cstring_index_byte(const char *s, char c) {
    for (int i = 0; s[i]; i++)
        if (s[i] == c) return i;
    return -1;
}

int neverc_cstring_last_index_byte(const char *s, char c) {
    int last = -1;
    for (int i = 0; s[i]; i++)
        if (s[i] == c) last = i;
    return last;
}

#define NCI_RK_PRIME 16777619U

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

    uint32_t h_pat = 0, h_win = 0, pw = 1;
    for (size_t i = 0; i < sublen; i++) {
        h_pat = h_pat * NCI_RK_PRIME + (uint32_t)up[i];
        h_win = h_win * NCI_RK_PRIME + (uint32_t)us[i];
        pw *= NCI_RK_PRIME;
    }
    if (h_win == h_pat && memcmp(s, substr, sublen) == 0) return 0;
    for (size_t i = sublen; i < slen; i++) {
        h_win = h_win * NCI_RK_PRIME + (uint32_t)us[i]
              - pw * (uint32_t)us[i - sublen];
        if (h_win == h_pat) {
            size_t pos = i - sublen + 1;
            if (memcmp(s + pos, substr, sublen) == 0) return (int)pos;
        }
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

    unsigned char clast = (unsigned char)substr[sublen - 1];
    const unsigned char *us = (const unsigned char *)s;
    for (size_t i = slen; i >= sublen; i--)
        if (us[i - 1] == clast && memcmp(s + i - sublen, substr, sublen) == 0)
            return (int)(i - sublen);
    return -1;
}

int neverc_cstring_index_any(const char *s, const char *chars) {
    if (!chars[0]) return -1;
    for (int i = 0; s[i]; i++)
        if (in_cutset(s[i], chars)) return i;
    return -1;
}

int neverc_cstring_last_index_any(const char *s, const char *chars) {
    if (!chars[0]) return -1;
    int last = -1;
    for (int i = 0; s[i]; i++)
        if (in_cutset(s[i], chars)) last = i;
    return last;
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
        /* Go: Count("", "") == 1, Count("abc", "") == 4 */
        int n = 0;
        while (s[n]) n++;
        return n + 1;
    }
    int n = 0;
    int pos = 0;
    while (s[pos]) {
        int idx = neverc_cstring_index(s + pos, substr);
        if (idx < 0) break;
        n++;
        pos += idx + (int)sublen;
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

char *neverc_cstring_to_upper(const char *s) {
    size_t len = strlen(s);
    char *r = (char *)malloc(len + 1);
    if (!r) return NULL;
    for (size_t i = 0; i < len; i++) r[i] = to_upper_ch(s[i]);
    r[len] = '\0';
    return r;
}

char *neverc_cstring_to_lower(const char *s) {
    size_t len = strlen(s);
    char *r = (char *)malloc(len + 1);
    if (!r) return NULL;
    for (size_t i = 0; i < len; i++) r[i] = to_lower_ch(s[i]);
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
    for (int i = 0; i < count; i++)
        memcpy(r + (size_t)i * len, s, len);
    r[total] = '\0';
    return r;
}

char *neverc_cstring_replace(const char *s, const char *old_s,
                              const char *new_s, int n) {
    if (n == 0) return nc_strdup(s, strlen(s));

    size_t slen = strlen(s);
    size_t oldlen = strlen(old_s);
    size_t newlen = strlen(new_s);

    if (oldlen == 0) {
        /* Go semantics: empty old matches before each char + at end */
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

    /* Count replacements */
    int cnt = 0;
    {
        const char *p = s;
        while (*p) {
            int idx = neverc_cstring_index(p, old_s);
            if (idx < 0) break;
            cnt++;
            p += idx + (int)oldlen;
        }
    }
    if (n >= 0 && cnt > n) cnt = n;

    size_t total = slen + (size_t)cnt * (newlen > oldlen ? newlen - oldlen : 0)
                        - (size_t)cnt * (oldlen > newlen ? oldlen - newlen : 0);
    /* Safer calculation */
    total = slen - (size_t)cnt * oldlen + (size_t)cnt * newlen;

    char *r = (char *)malloc(total + 1);
    if (!r) return NULL;
    size_t w = 0;
    int done = 0;
    const char *p = s;
    while (*p) {
        if (done < cnt) {
            int idx = neverc_cstring_index(p, old_s);
            if (idx >= 0) {
                memcpy(r + w, p, (size_t)idx);
                w += (size_t)idx;
                memcpy(r + w, new_s, newlen);
                w += newlen;
                p += idx + oldlen;
                done++;
                continue;
            }
        }
        /* Copy rest */
        size_t rest = strlen(p);
        memcpy(r + w, p, rest);
        w += rest;
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
    while (*s && in_cutset(*s, cutset)) s++;
    return nc_strdup(s, strlen(s));
}

char *neverc_cstring_trim_right(const char *s, const char *cutset) {
    size_t len = strlen(s);
    while (len > 0 && in_cutset(s[len - 1], cutset)) len--;
    return nc_strdup(s, len);
}

char *neverc_cstring_trim(const char *s, const char *cutset) {
    while (*s && in_cutset(*s, cutset)) s++;
    size_t len = strlen(s);
    while (len > 0 && in_cutset(s[len - 1], cutset)) len--;
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
        /* Split into individual characters */
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

    size_t cnt = 0;
    const char *p = s;
    int limit = n - 1;
    while (cnt < (size_t)limit) {
        int idx = neverc_cstring_index(p, sep);
        if (idx < 0) break;
        arr[cnt] = nc_strdup(p, (size_t)idx + (size_t)sep_save);
        cnt++;
        p += idx + (int)seplen;
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
