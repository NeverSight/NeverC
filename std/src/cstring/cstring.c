#include "neverc/std/cstring.h"
#include "../bytes/strsearch.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

/* ======================================================================
 * Internal helpers
 * ====================================================================== */

static int nc_size_add(size_t left, size_t right, size_t *result) {
    if (right > SIZE_MAX - left) return 0;
    *result = left + right;
    return 1;
}

static int nc_size_mul(size_t left, size_t right, size_t *result) {
    if (left != 0 && right > SIZE_MAX / left) return 0;
    *result = left * right;
    return 1;
}

static char *nc_alloc_string(size_t len) {
    if (len == SIZE_MAX) return NULL;
    return (char *)malloc(len + 1);
}

static char *nc_strdup(const char *s, size_t len) {
    if (!s && len != 0) return NULL;
    char *r = nc_alloc_string(len);
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

static size_t find_substring(const char *s, size_t slen,
                             const char *substr, size_t sublen,
                             int last) {
    if (sublen == 0) return last ? slen : 0;
    if (sublen > slen) return SIZE_MAX;
    if (!last)
        return nci_ss_index((const uint8_t *)s, slen,
                            (const uint8_t *)substr, sublen);
    return nci_ss_last_index((const uint8_t *)s, slen,
                             (const uint8_t *)substr, sublen);
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
    while (slen - i >= 8) {
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
    if (c == '\0') return -1;
    const char *p = strchr(s, c);
    if (!p) return -1;
    size_t index = (size_t)(p - s);
    return index > INT_MAX ? INT_MAX : (int)index;
}

int neverc_cstring_last_index_byte(const char *s, char c) {
    if (c == '\0') return -1;
    const char *p = strrchr(s, c);
    if (!p) return -1;
    size_t index = (size_t)(p - s);
    return index > INT_MAX ? INT_MAX : (int)index;
}

int neverc_cstring_index(const char *s, const char *substr) {
    size_t slen = strlen(s);
    size_t sublen = strlen(substr);
    size_t r = find_substring(s, slen, substr, sublen, 0);
    return r == SIZE_MAX ? -1 : r > INT_MAX ? INT_MAX : (int)r;
}

int neverc_cstring_last_index(const char *s, const char *substr) {
    size_t slen = strlen(s);
    size_t sublen = strlen(substr);
    size_t r = find_substring(s, slen, substr, sublen, 1);
    return r == SIZE_MAX ? -1 : r > INT_MAX ? INT_MAX : (int)r;
}

int neverc_cstring_index_any(const char *s, const char *chars) {
    if (!chars[0]) return -1;
    uint32_t set[8];
    build_ascii_set(chars, set);
    for (size_t i = 0; s[i]; i++)
        if (ASCII_SET_HAS(set, s[i]))
            return i > INT_MAX ? INT_MAX : (int)i;
    return -1;
}

int neverc_cstring_last_index_any(const char *s, const char *chars) {
    if (!chars[0]) return -1;
    uint32_t set[8];
    build_ascii_set(chars, set);
    size_t len = strlen(s);
    for (size_t i = len; i > 0; i--)
        if (ASCII_SET_HAS(set, s[i - 1]))
            return i - 1 > INT_MAX ? INT_MAX : (int)(i - 1);
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
        return len >= INT_MAX ? INT_MAX : (int)len + 1;
    }
    size_t slen = strlen(s);
    if (sublen > slen) return 0;
    if (sublen == 1) {
        int n = 0;
        const char *p = s;
        char c = substr[0];
        while ((p = strchr(p, c)) != NULL) {
            if (n == INT_MAX) return INT_MAX;
            n++;
            p++;
        }
        return n;
    }

    /* Two-Way finder: preprocess once, count non-overlapping matches. */
    int n = 0;
    size_t pos = 0;
    nci_ss_finder_t f;
    nci_ss_finder_init(&f, (const uint8_t *)substr, sublen);
    for (;;) {
        size_t idx = nci_ss_finder_next(&f, (const uint8_t *)s + pos, slen - pos);
        if (idx == (size_t)-1) break;
        if (n == INT_MAX) return INT_MAX;
        n++;
        pos += idx + sublen;
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
    char *r = nc_alloc_string(len);
    if (!r) return NULL;
    for (size_t i = 0; i < len; i++) r[i] = to_upper_ch(s[i]);
    r[len] = '\0';
    return r;
}

char *neverc_cstring_to_lower(const char *s) {
    size_t len = strlen(s);
    char *r = nc_alloc_string(len);
    if (!r) return NULL;
    for (size_t i = 0; i < len; i++) r[i] = to_lower_ch(s[i]);
    r[len] = '\0';
    return r;
}

char *neverc_cstring_to_title(const char *s) {
    size_t len = strlen(s);
    char *r = nc_alloc_string(len);
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
    size_t total;
    if (!nc_size_mul(len, (size_t)count, &total)) return NULL;
    char *r = nc_alloc_string(total);
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

char *neverc_cstring_replace(const char *s, const char *old_s,
                              const char *new_s, int n) {
    if (n == 0) return nc_strdup(s, strlen(s));

    size_t slen = strlen(s);
    size_t oldlen = strlen(old_s);
    size_t newlen = strlen(new_s);

    if (oldlen == 0) {
        size_t max_reps;
        if (!nc_size_add(slen, 1, &max_reps)) return NULL;
        size_t reps = n < 0 || (size_t)n > max_reps ? max_reps : (size_t)n;
        size_t inserted;
        size_t total;
        if (!nc_size_mul(reps, newlen, &inserted) ||
            !nc_size_add(slen, inserted, &total))
            return NULL;
        char *r = nc_alloc_string(total);
        if (!r) return NULL;
        size_t w = 0;
        size_t done = 0;
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

    /* Two-Way finder: preprocess `old_s` once, reuse for counting and building. */
    nci_ss_finder_t f;
    nci_ss_finder_init(&f, (const uint8_t *)old_s, oldlen);

    size_t cnt = 0;
    {
        size_t pos = 0;
        while (oldlen <= slen - pos) {
            size_t idx = nci_ss_finder_next(&f, (const uint8_t *)s + pos,
                                            slen - pos);
            if (idx == (size_t)-1) break;
            cnt++;
            pos += idx + oldlen;
        }
    }
    if (n >= 0 && cnt > (size_t)n) cnt = (size_t)n;

    size_t removed;
    size_t inserted;
    size_t total;
    if (!nc_size_mul(cnt, oldlen, &removed) || removed > slen ||
        !nc_size_mul(cnt, newlen, &inserted) ||
        !nc_size_add(slen - removed, inserted, &total))
        return NULL;

    char *r = nc_alloc_string(total);
    if (!r) return NULL;
    size_t w = 0;
    size_t done = 0;
    const char *p = s;
    size_t remaining = slen;
    while (remaining > 0) {
        if (done < cnt) {
            size_t idx = nci_ss_finder_next(&f, (const uint8_t *)p, remaining);
            if (idx != (size_t)-1) {
                memcpy(r + w, p, idx);
                w += idx;
                memcpy(r + w, new_s, newlen);
                w += newlen;
                p += idx + oldlen;
                remaining -= idx + oldlen;
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
    if (!mapping) return NULL;
    size_t len = strlen(s);
    char *r = nc_alloc_string(len);
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
    if (!strs || !sep) return NULL;
    size_t seplen = strlen(sep);

    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        if (!strs[i] || !nc_size_add(total, strlen(strs[i]), &total) ||
            (i > 0 && !nc_size_add(total, seplen, &total)))
            return NULL;
    }

    char *r = nc_alloc_string(total);
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

static void free_string_array(char **arr, size_t count) {
    if (!arr) return;
    for (size_t i = 0; i < count; i++) free(arr[i]);
    free(arr);
}

static char **alloc_string_array(size_t count) {
    size_t bytes;
    if (!nc_size_mul(count, sizeof(char *), &bytes)) return NULL;
    return (char **)malloc(bytes);
}

static char **gen_split(const char *s, const char *sep,
                         size_t sep_save, int n, size_t *out_count) {
    if (!out_count) return NULL;
    *out_count = 0;
    if (!s || !sep || n == 0) return NULL;
    size_t slen = strlen(s);
    size_t seplen = strlen(sep);

    if (seplen == 0) {
        size_t cnt = slen;
        if (n > 0 && cnt > (size_t)n) cnt = (size_t)n;
        if (cnt == 0) cnt = 1;
        char **arr = alloc_string_array(cnt);
        if (!arr) return NULL;
        size_t made = 0;
        for (; made + 1 < cnt; made++) {
            arr[made] = nc_strdup(s + made, 1);
            if (!arr[made]) {
                free_string_array(arr, made);
                return NULL;
            }
        }
        arr[made] = nc_strdup(s + made, slen - made);
        if (!arr[made]) {
            free_string_array(arr, made);
            return NULL;
        }
        *out_count = cnt;
        return arr;
    }

    size_t cap;
    if (n < 0) {
        cap = 1;
        size_t pos = 0;
        nci_ss_finder_t counter;
        nci_ss_finder_init(&counter, (const uint8_t *)sep, seplen);
        while (seplen <= slen - pos) {
            size_t match = nci_ss_finder_next(
                &counter, (const uint8_t *)s + pos, slen - pos);
            if (match == (size_t)-1) break;
            if (cap == SIZE_MAX) return NULL;
            cap++;
            pos += match + seplen;
        }
    } else {
        cap = (size_t)n;
    }
    size_t max_cap;
    if (!nc_size_add(slen, 1, &max_cap)) return NULL;
    if (cap > max_cap) cap = max_cap;
    char **arr = alloc_string_array(cap);
    if (!arr) return NULL;

    nci_ss_finder_t f;
    nci_ss_finder_init(&f, (const uint8_t *)sep, seplen);

    size_t cnt = 0;
    const char *p = s;
    size_t remaining = slen;
    while (cnt + 1 < cap) {
        size_t idx = nci_ss_finder_next(&f, (const uint8_t *)p, remaining);
        if (idx == (size_t)-1) break;
        arr[cnt] = nc_strdup(p, idx + sep_save);
        if (!arr[cnt]) {
            free_string_array(arr, cnt);
            return NULL;
        }
        cnt++;
        p += idx + seplen;
        remaining -= idx + seplen;
    }
    arr[cnt] = nc_strdup(p, remaining);
    if (!arr[cnt]) {
        free_string_array(arr, cnt);
        return NULL;
    }
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
    if (!sep) { if (count) *count = 0; return NULL; }
    return gen_split(s, sep, strlen(sep), -1, count);
}

char **neverc_cstring_split_after_n(const char *s, const char *sep,
                                     int n, size_t *count) {
    if (!sep) { if (count) *count = 0; return NULL; }
    return gen_split(s, sep, strlen(sep), n, count);
}

char **neverc_cstring_fields(const char *s, size_t *count) {
    if (!count) return NULL;
    *count = 0;
    if (!s) return NULL;
    /* Count fields first */
    size_t n = 0;
    int in_field = 0;
    for (const char *p = s; *p; p++) {
        if (is_space(*p)) {
            in_field = 0;
        } else if (!in_field) {
            in_field = 1;
            if (n == SIZE_MAX) return NULL;
            n++;
        }
    }
    if (n == 0) return NULL;

    char **arr = alloc_string_array(n);
    if (!arr) return NULL;

    size_t idx = 0;
    const char *p = s;
    while (*p) {
        while (*p && is_space(*p)) p++;
        if (!*p) break;
        const char *start = p;
        while (*p && !is_space(*p)) p++;
        arr[idx] = nc_strdup(start, (size_t)(p - start));
        if (!arr[idx]) {
            free_string_array(arr, idx);
            return NULL;
        }
        idx++;
    }
    *count = idx;
    return arr;
}

void neverc_cstring_free_split(char **arr, size_t count) {
    free_string_array(arr, count);
}

/* ======================================================================
 * Cut
 * ====================================================================== */

static int make_cut_pair(const char *left, size_t left_len,
                         const char *right, size_t right_len,
                         char **left_out, char **right_out, int found) {
    char *left_copy = nc_strdup(left, left_len);
    if (!left_copy) return -1;
    char *right_copy = nc_strdup(right, right_len);
    if (!right_copy) {
        free(left_copy);
        return -1;
    }
    *left_out = left_copy;
    *right_out = right_copy;
    return found;
}

int neverc_cstring_cut(const char *s, const char *sep,
                        char **before, char **after) {
    if (before) *before = NULL;
    if (after) *after = NULL;
    if (!s || !sep || !before || !after || before == after) return -1;
    size_t slen = strlen(s);
    size_t seplen = strlen(sep);
    size_t index = find_substring(s, slen, sep, seplen, 0);
    if (index == SIZE_MAX)
        return make_cut_pair(s, slen, "", 0, before, after, 0);
    return make_cut_pair(s, index, s + index + seplen,
                         slen - index - seplen, before, after, 1);
}

int neverc_cstring_cut_prefix(const char *s, const char *prefix,
                               char **after) {
    if (after) *after = NULL;
    if (!s || !prefix || !after) return -1;
    if (neverc_cstring_has_prefix(s, prefix)) {
        size_t plen = strlen(prefix);
        *after = nc_strdup(s + plen, strlen(s) - plen);
        return *after ? 1 : -1;
    }
    *after = nc_strdup(s, strlen(s));
    return *after ? 0 : -1;
}

int neverc_cstring_cut_suffix(const char *s, const char *suffix,
                               char **before) {
    if (before) *before = NULL;
    if (!s || !suffix || !before) return -1;
    size_t slen = strlen(s);
    size_t sfxlen = strlen(suffix);
    if (sfxlen <= slen && memcmp(s + slen - sfxlen, suffix, sfxlen) == 0) {
        *before = nc_strdup(s, slen - sfxlen);
        return *before ? 1 : -1;
    }
    *before = nc_strdup(s, slen);
    return *before ? 0 : -1;
}

int neverc_cstring_cut_last(const char *s, const char *sep,
                             char **before, char **after) {
    if (before) *before = NULL;
    if (after) *after = NULL;
    if (!s || !sep || !before || !after || before == after) return -1;
    size_t slen = strlen(s);
    size_t seplen = strlen(sep);
    size_t index = find_substring(s, slen, sep, seplen, 1);
    if (index == SIZE_MAX)
        return make_cut_pair(s, slen, "", 0, before, after, 0);
    return make_cut_pair(s, index, s + index + seplen,
                         slen - index - seplen, before, after, 1);
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
