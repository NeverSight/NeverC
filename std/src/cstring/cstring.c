#include "neverc/std/cstring.h"
#include "neverc/std/bytes.h"
#include "neverc/std/unicode.h"
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

static const char *nc_s(const char *s) {
    return s ? s : "";
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

static int is_separator(char c) {
    if (c >= '0' && c <= '9') return 0;
    if (c >= 'a' && c <= 'z') return 0;
    if (c >= 'A' && c <= 'Z') return 0;
    if (c == '_') return 0;
    return 1;
}

/* Go strings.Title isSeparator: ASCII alnum/_ stay words; for r > 0x7F,
 * letters and digits stay words and only unicode.IsSpace is a separator. */
static int is_separator_rune(uint32_t r) {
    if (r <= 0x7F) return is_separator((char)r);
    if (neverc_unicode_is_letter(r) || neverc_unicode_is_digit(r))
        return 0;
    return neverc_unicode_is_space(r);
}

static size_t utf8_decode(const uint8_t *p, size_t remaining, uint32_t *r) {
    if (remaining == 0) { *r = 0; return 0; }
    uint8_t b = p[0];
    if (b < 0x80) { *r = b; return 1; }
    if ((b & 0xE0) == 0xC0 && remaining >= 2 && (p[1] & 0xC0) == 0x80) {
        *r = ((uint32_t)(b & 0x1F) << 6) | (p[1] & 0x3F);
        return (*r >= 0x80) ? 2 : 0;
    }
    if ((b & 0xF0) == 0xE0 && remaining >= 3 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80) {
        *r = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) |
             (p[2] & 0x3F);
        if (*r >= 0x800 && (*r < 0xD800 || *r > 0xDFFF)) return 3;
        return 0;
    }
    if ((b & 0xF8) == 0xF0 && remaining >= 4 && (p[1] & 0xC0) == 0x80 &&
        (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        *r = ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
             ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        if (*r >= 0x10000 && *r <= 0x10FFFF) return 4;
        return 0;
    }
    *r = 0xFFFD;
    return 0;
}

static size_t cstring_utf8_encode(uint32_t r, uint8_t *buf) {
    if (r < 0x80) { buf[0] = (uint8_t)r; return 1; }
    if (r < 0x800) {
        buf[0] = (uint8_t)(0xC0 | (r >> 6));
        buf[1] = (uint8_t)(0x80 | (r & 0x3F));
        return 2;
    }
    if (r < 0x10000) {
        buf[0] = (uint8_t)(0xE0 | (r >> 12));
        buf[1] = (uint8_t)(0x80 | ((r >> 6) & 0x3F));
        buf[2] = (uint8_t)(0x80 | (r & 0x3F));
        return 3;
    }
    buf[0] = (uint8_t)(0xF0 | (r >> 18));
    buf[1] = (uint8_t)(0x80 | ((r >> 12) & 0x3F));
    buf[2] = (uint8_t)(0x80 | ((r >> 6) & 0x3F));
    buf[3] = (uint8_t)(0x80 | (r & 0x3F));
    return 4;
}

static size_t utf8_rune_width(const char *s, size_t remaining) {
    uint32_t rune;
    size_t width = utf8_decode((const uint8_t *)s, remaining, &rune);
    return width == 0 ? 1 : width;
}

static size_t utf8_rune_count(const char *s, size_t slen) {
    size_t count = 0, pos = 0;
    while (pos < slen) {
        pos += utf8_rune_width(s + pos, slen - pos);
        if (count != SIZE_MAX) count++;
    }
    return count;
}

static char *cstr_from_bytes(uint8_t *r, size_t outlen) {
    if (!r) return NULL;
    char *out = nc_strdup((const char *)r, outlen);
    free(r);
    return out;
}

static int index_to_int(size_t r) {
    return r == (size_t)-1 ? -1 : r > INT_MAX ? INT_MAX : (int)r;
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
    int r = strcmp(nc_s(a), nc_s(b));
    return r < 0 ? -1 : r > 0 ? 1 : 0;
}

int neverc_cstring_equal_fold(const char *s, const char *t) {
    s = nc_s(s);
    t = nc_s(t);
    return neverc_bytes_equal_fold((const uint8_t *)s, strlen(s),
                                   (const uint8_t *)t, strlen(t));
}

/* ======================================================================
 * Search / Index
 * ====================================================================== */

int neverc_cstring_index_byte(const char *s, char c) {
    if (c == '\0') return -1;
    s = nc_s(s);
    const char *p = strchr(s, c);
    if (!p) return -1;
    size_t index = (size_t)(p - s);
    return index > INT_MAX ? INT_MAX : (int)index;
}

int neverc_cstring_last_index_byte(const char *s, char c) {
    if (c == '\0') return -1;
    s = nc_s(s);
    const char *p = strrchr(s, c);
    if (!p) return -1;
    size_t index = (size_t)(p - s);
    return index > INT_MAX ? INT_MAX : (int)index;
}

int neverc_cstring_index(const char *s, const char *substr) {
    s = nc_s(s);
    substr = nc_s(substr);
    size_t slen = strlen(s);
    size_t sublen = strlen(substr);
    size_t r = find_substring(s, slen, substr, sublen, 0);
    return r == SIZE_MAX ? -1 : r > INT_MAX ? INT_MAX : (int)r;
}

int neverc_cstring_last_index(const char *s, const char *substr) {
    s = nc_s(s);
    substr = nc_s(substr);
    size_t slen = strlen(s);
    size_t sublen = strlen(substr);
    size_t r = find_substring(s, slen, substr, sublen, 1);
    return r == SIZE_MAX ? -1 : r > INT_MAX ? INT_MAX : (int)r;
}

int neverc_cstring_index_any(const char *s, const char *chars) {
    s = nc_s(s);
    return index_to_int(neverc_bytes_index_any(
        (const uint8_t *)s, strlen(s), nc_s(chars)));
}

int neverc_cstring_last_index_any(const char *s, const char *chars) {
    s = nc_s(s);
    return index_to_int(neverc_bytes_last_index_any(
        (const uint8_t *)s, strlen(s), nc_s(chars)));
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
    s = nc_s(s);
    substr = nc_s(substr);
    size_t sublen = strlen(substr);
    if (sublen == 0) {
        size_t runes = utf8_rune_count(s, strlen(s));
        return runes >= INT_MAX ? INT_MAX : (int)runes + 1;
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
    s = nc_s(s);
    prefix = nc_s(prefix);
    size_t plen = strlen(prefix);
    if (plen == 0) return 1;
    size_t slen = strlen(s);
    if (plen > slen) return 0;
    return memcmp(s, prefix, plen) == 0;
}

int neverc_cstring_has_suffix(const char *s, const char *suffix) {
    s = nc_s(s);
    suffix = nc_s(suffix);
    size_t sfxlen = strlen(suffix);
    if (sfxlen == 0) return 1;
    size_t slen = strlen(s);
    if (sfxlen > slen) return 0;
    return memcmp(s + slen - sfxlen, suffix, sfxlen) == 0;
}

/* ======================================================================
 * Transform
 * ====================================================================== */

/* Go strings.ToUpper/ToLower: ASCII stays in place; otherwise Map each
 * rune through unicode.ToUpper/ToLower. Invalid UTF-8 becomes U+FFFD. */
static char *cstring_map_case(const char *s, uint32_t (*map_fn)(uint32_t)) {
    s = nc_s(s);
    size_t len = strlen(s);
    int ascii = 1;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)s[i] >= 0x80) {
            ascii = 0;
            break;
        }
    }
    if (ascii) {
        char *r = nc_alloc_string(len);
        if (!r) return NULL;
        for (size_t i = 0; i < len; i++)
            r[i] = (char)map_fn((unsigned char)s[i]);
        r[len] = '\0';
        return r;
    }
    size_t need = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t rune = 0;
        size_t n = utf8_decode((const uint8_t *)s + i, len - i, &rune);
        if (n == 0) {
            rune = 0xFFFD;
            n = 1;
        }
        uint8_t tmp[4];
        size_t wn = cstring_utf8_encode(map_fn(rune), tmp);
        if (!nc_size_add(need, wn, &need)) return NULL;
        i += n;
    }
    char *r = nc_alloc_string(need);
    if (!r) return NULL;
    size_t o = 0;
    i = 0;
    while (i < len) {
        uint32_t rune = 0;
        size_t n = utf8_decode((const uint8_t *)s + i, len - i, &rune);
        if (n == 0) {
            rune = 0xFFFD;
            n = 1;
        }
        o += cstring_utf8_encode(map_fn(rune), (uint8_t *)r + o);
        i += n;
    }
    r[need] = '\0';
    return r;
}

char *neverc_cstring_to_upper(const char *s) {
    return cstring_map_case(s, neverc_unicode_to_upper);
}

char *neverc_cstring_to_lower(const char *s) {
    return cstring_map_case(s, neverc_unicode_to_lower);
}

char *neverc_cstring_to_title(const char *s) {
    s = nc_s(s);
    size_t len = strlen(s);
    size_t need = 0;
    size_t i = 0;
    int prev_sep = 1;
    while (i < len) {
        uint32_t rune = 0;
        size_t n = utf8_decode((const uint8_t *)s + i, len - i, &rune);
        size_t wn;
        if (n == 0) {
            /* Go strings.Map: invalid UTF-8 becomes U+FFFD, which is not
             * a separator, so the next letter is not titled. */
            rune = 0xFFFD;
            n = 1;
        }
        {
            uint32_t titled = prev_sep ? neverc_unicode_to_title(rune) : rune;
            uint8_t tmp[4];
            wn = cstring_utf8_encode(titled, tmp);
            prev_sep = is_separator_rune(rune);
        }
        if (!nc_size_add(need, wn, &need)) return NULL;
        i += n;
    }
    char *r = nc_alloc_string(need);
    if (!r) return NULL;
    size_t o = 0;
    i = 0;
    prev_sep = 1;
    while (i < len) {
        uint32_t rune = 0;
        size_t n = utf8_decode((const uint8_t *)s + i, len - i, &rune);
        if (n == 0) {
            rune = 0xFFFD;
            n = 1;
        }
        {
            uint32_t titled = prev_sep ? neverc_unicode_to_title(rune) : rune;
            o += cstring_utf8_encode(titled, (uint8_t *)r + o);
            prev_sep = is_separator_rune(rune);
            i += n;
        }
    }
    r[need] = '\0';
    return r;
}

char *neverc_cstring_repeat(const char *s, int count) {
    if (count < 0) return NULL;
    s = nc_s(s);
    if (count == 0) return nc_strdup("", 0);
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
    s = nc_s(s);
    old_s = nc_s(old_s);
    new_s = nc_s(new_s);
    if (n == 0) return nc_strdup(s, strlen(s));

    size_t slen = strlen(s);
    size_t oldlen = strlen(old_s);
    size_t newlen = strlen(new_s);

    if (oldlen == 0) {
        size_t max_reps;
        if (!nc_size_add(utf8_rune_count(s, slen), 1, &max_reps)) return NULL;
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
        size_t i = 0;
        while (i < slen) {
            if (done < reps) {
                memcpy(r + w, new_s, newlen);
                w += newlen;
                done++;
            }
            size_t width = utf8_rune_width(s + i, slen - i);
            memcpy(r + w, s + i, width);
            w += width;
            i += width;
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
    s = nc_s(s);
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
        if (!nc_size_add(total, strlen(nc_s(strs[i])), &total) ||
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
        const char *part = nc_s(strs[i]);
        size_t elen = strlen(part);
        memcpy(r + w, part, elen);
        w += elen;
    }
    r[w] = '\0';
    return r;
}

/* ======================================================================
 * Trim
 * ====================================================================== */

char *neverc_cstring_trim_left(const char *s, const char *cutset) {
    size_t outlen = 0;
    uint8_t *trimmed;
    s = nc_s(s);
    cutset = nc_s(cutset);
    /* Keep the helper call in a separate full expression. C leaves argument
     * evaluation order unspecified, so combining this call with the outlen
     * read in cstr_from_bytes makes the returned length order-dependent. */
    trimmed = neverc_bytes_trim_left((const uint8_t *)s, strlen(s), cutset,
                                     &outlen);
    return cstr_from_bytes(trimmed, outlen);
}

char *neverc_cstring_trim_right(const char *s, const char *cutset) {
    size_t outlen = 0;
    uint8_t *trimmed;
    s = nc_s(s);
    cutset = nc_s(cutset);
    trimmed = neverc_bytes_trim_right((const uint8_t *)s, strlen(s), cutset,
                                      &outlen);
    return cstr_from_bytes(trimmed, outlen);
}

char *neverc_cstring_trim(const char *s, const char *cutset) {
    size_t outlen = 0;
    uint8_t *trimmed;
    s = nc_s(s);
    cutset = nc_s(cutset);
    trimmed = neverc_bytes_trim((const uint8_t *)s, strlen(s), cutset,
                                &outlen);
    return cstr_from_bytes(trimmed, outlen);
}

char *neverc_cstring_trim_space(const char *s) {
    size_t outlen = 0;
    uint8_t *trimmed;
    s = nc_s(s);
    trimmed = neverc_bytes_trim_space((const uint8_t *)s, strlen(s), &outlen);
    return cstr_from_bytes(trimmed, outlen);
}

char *neverc_cstring_trim_prefix(const char *s, const char *prefix) {
    s = nc_s(s);
    prefix = nc_s(prefix);
    if (neverc_cstring_has_prefix(s, prefix)) {
        size_t plen = strlen(prefix);
        return nc_strdup(s + plen, strlen(s) - plen);
    }
    return nc_strdup(s, strlen(s));
}

char *neverc_cstring_trim_suffix(const char *s, const char *suffix) {
    s = nc_s(s);
    suffix = nc_s(suffix);
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
        /* Go explode: clamp n to the rune count. SplitN("", "", 1) is empty. */
        size_t cnt = utf8_rune_count(s, slen);
        if (n > 0 && cnt > (size_t)n) cnt = (size_t)n;
        if (cnt == 0) return NULL;
        char **arr = alloc_string_array(cnt);
        if (!arr) return NULL;
        size_t made = 0, pos = 0;
        for (; made + 1 < cnt; made++) {
            size_t width = utf8_rune_width(s + pos, slen - pos);
            arr[made] = nc_strdup(s + pos, width);
            if (!arr[made]) {
                free_string_array(arr, made);
                return NULL;
            }
            pos += width;
        }
        arr[made] = nc_strdup(s + pos, slen - pos);
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
    size_t n = 0;
    neverc_bytes_slice_t *parts = neverc_bytes_fields(
        (const uint8_t *)s, strlen(s), &n);
    if (!parts) return NULL;
    if (n == 0) {
        free(parts);
        return NULL;
    }
    char **arr = alloc_string_array(n);
    if (!arr) {
        free(parts);
        return NULL;
    }
    for (size_t i = 0; i < n; i++) {
        arr[i] = nc_strdup((const char *)parts[i].data, parts[i].len);
        if (!arr[i]) {
            free_string_array(arr, i);
            free(parts);
            return NULL;
        }
    }
    free(parts);
    *count = n;
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
    s = nc_s(s);
    return nc_strdup(s, strlen(s));
}

size_t neverc_cstring_len(const char *s) {
    return strlen(nc_s(s));
}
