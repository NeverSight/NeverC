#include "neverc/std/bytes.h"
#include "strsearch.h"
#include <stdlib.h>
#include <string.h>

static size_t utf8_decode(const uint8_t *p, size_t remaining, uint32_t *r);

static int bytes_span_valid(const void *data, size_t len) {
    return data != NULL || len == 0;
}

static const uint8_t *bytes_offset(const uint8_t *data, size_t offset) {
    return data ? data + offset : NULL;
}

static uint8_t *bytes_alloc(size_t len) {
    return (uint8_t *)malloc(len == 0 ? 1 : len);
}

static uint8_t *bytes_copy(const uint8_t *src, size_t len, size_t *outlen) {
    if (!outlen || !bytes_span_valid(src, len)) return NULL;
    *outlen = 0;
    uint8_t *result = bytes_alloc(len);
    if (!result) return NULL;
    if (len > 0) memcpy(result, src, len);
    *outlen = len;
    return result;
}

static int bytes_slices_grow(neverc_bytes_slice_t **slices, size_t *cap,
                             size_t needed) {
    if (needed <= *cap) return 1;
    size_t next = *cap == 0 ? 8 : *cap;
    while (next < needed) {
        if (next > SIZE_MAX / 2) {
            next = needed;
            break;
        }
        next *= 2;
    }
    if (next > SIZE_MAX / sizeof(**slices)) return 0;
    neverc_bytes_slice_t *grown = (neverc_bytes_slice_t *)realloc(
        *slices, next * sizeof(**slices));
    if (!grown) return 0;
    *slices = grown;
    *cap = next;
    return 1;
}

static int bytes_slices_append(neverc_bytes_slice_t **slices, size_t *cap,
                               size_t *count, const uint8_t *data,
                               size_t len) {
    if (*count == SIZE_MAX ||
        !bytes_slices_grow(slices, cap, *count + 1)) return 0;
    (*slices)[*count].data = data;
    (*slices)[*count].len = len;
    (*count)++;
    return 1;
}

static int bytes_buffer_reserve(uint8_t **buffer, size_t *cap,
                                size_t used, size_t additional) {
    if (additional > SIZE_MAX - used) return 0;
    size_t needed = used + additional;
    if (needed <= *cap) return 1;
    size_t next = *cap == 0 ? 1 : *cap;
    while (next < needed) {
        if (next > SIZE_MAX / 2) {
            next = needed;
            break;
        }
        next *= 2;
    }
    uint8_t *grown = (uint8_t *)realloc(*buffer, next);
    if (!grown) return 0;
    *buffer = grown;
    *cap = next;
    return 1;
}

/* --- Comparison --- */

int neverc_bytes_equal(const uint8_t *a, size_t alen,
                       const uint8_t *b, size_t blen) {
    if (alen != blen) return 0;
    if (!bytes_span_valid(a, alen) || !bytes_span_valid(b, blen)) return 0;
    return alen == 0 || memcmp(a, b, alen) == 0;
}

int neverc_bytes_compare(const uint8_t *a, size_t alen,
                         const uint8_t *b, size_t blen) {
    if (!bytes_span_valid(a, alen) || !bytes_span_valid(b, blen)) return 0;
    size_t n = alen < blen ? alen : blen;
    if (n > 0) {
        int r = memcmp(a, b, n);
        if (r != 0) return r < 0 ? -1 : 1;
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
    if (!bytes_span_valid(s, slen) || !bytes_span_valid(t, tlen)) return 0;
    size_t i = 0;
    /*
     * SWAR case-insensitive comparison:
     * 1. Check exact equality (fast path for already-matching words)
     * 2. OR 0x20 into both words (forces ASCII uppercase -> lowercase)
     * 3. Compare the lowered words — if still different, definite mismatch
     * 4. Verify that every byte where the originals differed is actually
     *    an ASCII letter (otherwise 'A'^0x20='a' but '@'^0x20='`', which
     *    would be a false positive).
     */
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
        if (to_lower_ascii(s[i]) != to_lower_ascii(t[i])) return 0;
    return 1;
}

/* --- Search --- */

size_t neverc_bytes_index_byte(const uint8_t *b, size_t blen, uint8_t c) {
    if (blen == 0 || !b) return (size_t)-1;
    const uint8_t *p = (const uint8_t *)memchr(b, c, blen);
    return p ? (size_t)(p - b) : (size_t)-1;
}

size_t neverc_bytes_last_index_byte(const uint8_t *s, size_t slen, uint8_t c) {
    if (slen == 0 || !s) return (size_t)-1;
    size_t i = slen;
    /* Word-at-a-time scan for the aligned tail */
    typedef unsigned long word_t;
    #define ONES ((word_t)-1 / 255)
    #define HIGHS (ONES * 128)
    word_t mask = ONES * c;
    while (i >= sizeof(word_t)) {
        i -= sizeof(word_t);
        word_t w;
        memcpy(&w, s + i, sizeof(word_t));
        w ^= mask;
        if ((w - ONES) & ~w & HIGHS) {
            for (size_t j = i + sizeof(word_t); j > i; j--)
                if (s[j - 1] == c) return j - 1;
        }
    }
    while (i > 0) {
        i--;
        if (s[i] == c) return i;
    }
    return (size_t)-1;
    #undef ONES
    #undef HIGHS
}

size_t neverc_bytes_index(const uint8_t *s, size_t slen,
                          const uint8_t *sep, size_t seplen) {
    if (!bytes_span_valid(s, slen) || !bytes_span_valid(sep, seplen))
        return (size_t)-1;
    if (seplen == 1) return neverc_bytes_index_byte(s, slen, sep[0]);
    return nci_ss_index(s, slen, sep, seplen);
}

size_t neverc_bytes_last_index(const uint8_t *s, size_t slen,
                               const uint8_t *sep, size_t seplen) {
    if (!bytes_span_valid(s, slen) || !bytes_span_valid(sep, seplen))
        return (size_t)-1;
    if (seplen == 1) return neverc_bytes_last_index_byte(s, slen, sep[0]);
    return nci_ss_last_index(s, slen, sep, seplen);
}

static void build_ascii_set(const char *chars, uint32_t set[8]) {
    memset(set, 0, 8 * sizeof(uint32_t));
    if (!chars) return;
    for (const char *c = chars; *c; c++)
        set[((uint8_t)*c) >> 5] |= 1u << (((uint8_t)*c) & 31);
}

#define ASCII_SET_HAS(set, c) ((set)[(c) >> 5] & (1u << ((c) & 31)))

size_t neverc_bytes_index_any(const uint8_t *s, size_t slen, const char *chars) {
    if (!bytes_span_valid(s, slen) || !chars) return (size_t)-1;
    if (!chars[0]) return (size_t)-1;
    if (!chars[1])   /* single-byte cutset: SIMD memchr beats the bitmap loop */
        return neverc_bytes_index_byte(s, slen, (uint8_t)chars[0]);
    uint32_t set[8];
    build_ascii_set(chars, set);
    for (size_t i = 0; i < slen; i++)
        if (ASCII_SET_HAS(set, s[i])) return i;
    return (size_t)-1;
}

size_t neverc_bytes_last_index_any(const uint8_t *s, size_t slen,
                                   const char *chars) {
    if (!bytes_span_valid(s, slen) || !chars) return (size_t)-1;
    if (!chars[0]) return (size_t)-1;
    if (!chars[1])   /* single-byte cutset: word-at-a-time reverse scan */
        return neverc_bytes_last_index_byte(s, slen, (uint8_t)chars[0]);
    uint32_t set[8];
    build_ascii_set(chars, set);
    for (size_t i = slen; i > 0; i--)
        if (ASCII_SET_HAS(set, s[i - 1])) return i - 1;
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
    if (!bytes_span_valid(s, slen) || !bytes_span_valid(sep, seplen)) return 0;
    if (seplen == 0) {
        size_t count = 1, pos = 0;
        while (pos < slen) {
            uint32_t r;
            size_t width = utf8_decode(s + pos, slen - pos, &r);
            pos += width == 0 ? 1 : width;
            if (count != SIZE_MAX) count++;
        }
        return count;
    }
    if (slen == 0) return 0;
    if (seplen > slen) return 0;
    if (seplen == 1) {
        size_t n = 0;
        const uint8_t *p = s;
        const uint8_t *end = s + slen;
        uint8_t c = sep[0];
        while (p < end) {
            const uint8_t *f = (const uint8_t *)memchr(p, c, (size_t)(end - p));
            if (!f) break;
            n++;
            p = f + 1;
        }
        return n;
    }

    /* Two-Way finder: preprocess once, count non-overlapping matches. */
    size_t n = 0, pos = 0;
    nci_ss_finder_t f;
    nci_ss_finder_init(&f, sep, seplen);
    for (;;) {
        size_t idx = nci_ss_finder_next(&f, s + pos, slen - pos);
        if (idx == (size_t)-1) break;
        n++;
        pos += idx + seplen;
        if (pos > slen) break;
    }
    return n;
}

/* --- Prefix / Suffix --- */

int neverc_bytes_has_prefix(const uint8_t *s, size_t slen,
                            const uint8_t *prefix, size_t plen) {
    if (!bytes_span_valid(s, slen) || !bytes_span_valid(prefix, plen)) return 0;
    if (plen > slen) return 0;
    return neverc_bytes_equal(s, plen, prefix, plen);
}

int neverc_bytes_has_suffix(const uint8_t *s, size_t slen,
                            const uint8_t *suffix, size_t sfxlen) {
    if (!bytes_span_valid(s, slen) || !bytes_span_valid(suffix, sfxlen)) return 0;
    if (sfxlen > slen) return 0;
    return neverc_bytes_equal(bytes_offset(s, slen - sfxlen), sfxlen,
                              suffix, sfxlen);
}

/* --- Transform --- */

uint8_t *neverc_bytes_to_upper(const uint8_t *s, size_t slen, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen)) return NULL;
    *outlen = 0;
    uint8_t *r = bytes_alloc(slen);
    if (!r) return NULL;
    for (size_t i = 0; i < slen; i++)
        r[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
    *outlen = slen;
    return r;
}

uint8_t *neverc_bytes_to_lower(const uint8_t *s, size_t slen, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen)) return NULL;
    *outlen = 0;
    uint8_t *r = bytes_alloc(slen);
    if (!r) return NULL;
    for (size_t i = 0; i < slen; i++)
        r[i] = (s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i];
    *outlen = slen;
    return r;
}

uint8_t *neverc_bytes_to_title(const uint8_t *s, size_t slen, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen)) return NULL;
    *outlen = 0;
    uint8_t *r = bytes_alloc(slen);
    if (!r) return NULL;
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
    if (!outlen || !bytes_span_valid(b, blen)) return NULL;
    *outlen = 0;
    if (count <= 0 || blen == 0) {
        return bytes_alloc(0);
    }
    if ((size_t)count > SIZE_MAX / blen) return NULL;
    size_t total = blen * (size_t)count;
    uint8_t *r = bytes_alloc(total);
    if (!r) return NULL;
    memcpy(r, b, blen);
    for (size_t copied = blen; copied < total; ) {
        size_t chunk = total - copied;
        if (chunk > copied) chunk = copied;
        memcpy(r + copied, r, chunk);
        copied += chunk;
    }
    *outlen = total;
    return r;
}

uint8_t *neverc_bytes_replace(const uint8_t *s, size_t slen,
                              const uint8_t *old, size_t oldlen,
                              const uint8_t *new_, size_t newlen,
                              int n, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen) ||
        !bytes_span_valid(old, oldlen) || !bytes_span_valid(new_, newlen))
        return NULL;
    *outlen = 0;
    if (n == 0) return bytes_copy(s, slen, outlen);

    size_t matches = neverc_bytes_count(s, slen, old, oldlen);
    size_t replacements = matches;
    if (n >= 0 && (size_t)n < replacements) replacements = (size_t)n;

    size_t result_len;
    if (oldlen == 0) {
        if (newlen != 0 && replacements > (SIZE_MAX - slen) / newlen)
            return NULL;
        result_len = slen + replacements * newlen;
    } else if (newlen > oldlen) {
        size_t growth = newlen - oldlen;
        if (replacements > (SIZE_MAX - slen) / growth) return NULL;
        result_len = slen + replacements * growth;
    } else {
        result_len = slen - replacements * (oldlen - newlen);
    }
    uint8_t *r = bytes_alloc(result_len);
    if (!r) return NULL;

    if (oldlen == 0) {
        size_t wi = 0, ri = 0, replaced = 0;
        if (replaced < replacements) {
            if (newlen > 0) memcpy(r + wi, new_, newlen);
            wi += newlen;
            replaced++;
        }
        while (ri < slen) {
            uint32_t rune;
            size_t width = utf8_decode(s + ri, slen - ri, &rune);
            if (width == 0) width = 1;
            memcpy(r + wi, s + ri, width);
            wi += width;
            ri += width;
            if (replaced < replacements) {
                if (newlen > 0) memcpy(r + wi, new_, newlen);
                wi += newlen;
                replaced++;
            }
        }
        *outlen = wi;
        return r;
    }

    /* Two-Way finder: preprocess `old` once, reuse across every replacement. */
    nci_ss_finder_t f;
    nci_ss_finder_init(&f, old, oldlen);

    size_t wi = 0, ri = 0;
    size_t replaced = 0;
    while (ri < slen && replaced < replacements) {
        size_t idx = nci_ss_finder_next(&f, s + ri, slen - ri);
        if (idx == (size_t)-1) break;
        if (idx > 0) { memcpy(r + wi, s + ri, idx); wi += idx; }
        ri += idx;
        if (newlen > 0) { memcpy(r + wi, new_, newlen); wi += newlen; }
        ri += oldlen;
        replaced++;
    }
    if (ri < slen) { memcpy(r + wi, s + ri, slen - ri); wi += slen - ri; }
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
    if (!outlen) return NULL;
    *outlen = 0;
    if (!bytes_span_valid(sep, seplen) ||
        (count > 0 && (!slices || !lens))) return NULL;
    if (count == 0) return bytes_alloc(0);
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        if (!bytes_span_valid(slices[i], lens[i]) || lens[i] > SIZE_MAX - total)
            return NULL;
        total += lens[i];
    }
    if (seplen != 0 && count - 1 > (SIZE_MAX - total) / seplen) return NULL;
    total += seplen * (count - 1);

    uint8_t *r = bytes_alloc(total);
    if (!r) return NULL;
    size_t wi = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0 && seplen > 0) {
            memcpy(r + wi, sep, seplen); wi += seplen;
        }
        if (lens[i] > 0) {
            memcpy(r + wi, slices[i], lens[i]); wi += lens[i];
        }
    }
    *outlen = wi;
    return r;
}

/* --- Trim helpers --- */

static int in_cutset(uint8_t c, const uint32_t set[8]) {
    return (set[c >> 5] & (1u << (c & 31))) != 0;
}

static int is_space(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\v' || c == '\f';
}

uint8_t *neverc_bytes_trim_left(const uint8_t *s, size_t slen,
                                const char *cutset, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen) || !cutset) return NULL;
    *outlen = 0;
    uint32_t set[8];
    build_ascii_set(cutset, set);
    size_t start = 0;
    while (start < slen && in_cutset(s[start], set)) start++;
    return bytes_copy(bytes_offset(s, start), slen - start, outlen);
}

uint8_t *neverc_bytes_trim_right(const uint8_t *s, size_t slen,
                                 const char *cutset, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen) || !cutset) return NULL;
    *outlen = 0;
    uint32_t set[8];
    build_ascii_set(cutset, set);
    size_t end = slen;
    while (end > 0 && in_cutset(s[end - 1], set)) end--;
    return bytes_copy(s, end, outlen);
}

uint8_t *neverc_bytes_trim(const uint8_t *s, size_t slen,
                           const char *cutset, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen) || !cutset) return NULL;
    *outlen = 0;
    uint32_t set[8];
    build_ascii_set(cutset, set);
    size_t start = 0, end = slen;
    while (start < end && in_cutset(s[start], set)) start++;
    while (end > start && in_cutset(s[end - 1], set)) end--;
    return bytes_copy(bytes_offset(s, start), end - start, outlen);
}

uint8_t *neverc_bytes_trim_space(const uint8_t *s, size_t slen, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen)) return NULL;
    *outlen = 0;
    size_t start = 0, end = slen;
    while (start < end && is_space(s[start])) start++;
    while (end > start && is_space(s[end - 1])) end--;
    return bytes_copy(bytes_offset(s, start), end - start, outlen);
}

uint8_t *neverc_bytes_trim_prefix(const uint8_t *s, size_t slen,
                                  const uint8_t *prefix, size_t plen,
                                  size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen) ||
        !bytes_span_valid(prefix, plen)) return NULL;
    *outlen = 0;
    if (neverc_bytes_has_prefix(s, slen, prefix, plen)) {
        return bytes_copy(bytes_offset(s, plen), slen - plen, outlen);
    }
    return bytes_copy(s, slen, outlen);
}

uint8_t *neverc_bytes_trim_suffix(const uint8_t *s, size_t slen,
                                  const uint8_t *suffix, size_t sfxlen,
                                  size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen) ||
        !bytes_span_valid(suffix, sfxlen)) return NULL;
    *outlen = 0;
    if (neverc_bytes_has_suffix(s, slen, suffix, sfxlen)) {
        return bytes_copy(s, slen - sfxlen, outlen);
    }
    return bytes_copy(s, slen, outlen);
}

/* --- Split --- */

neverc_bytes_slice_t *neverc_bytes_split_n(const uint8_t *s, size_t slen,
                                           const uint8_t *sep, size_t seplen,
                                           int n, size_t *count) {
    if (!count) return NULL;
    *count = 0;
    if (n == 0 || !bytes_span_valid(s, slen) ||
        !bytes_span_valid(sep, seplen)) return NULL;

    size_t cap = 16;
    neverc_bytes_slice_t *result = (neverc_bytes_slice_t *)malloc(
        cap * sizeof(neverc_bytes_slice_t));
    if (!result) return NULL;
    size_t pos = 0;

    nci_ss_finder_t f;
    nci_ss_finder_init(&f, sep, seplen);

    while (pos <= slen) {
        const uint8_t *current = s ? s + pos : NULL;
        if (n > 0 && *count >= (size_t)(n - 1)) {
            if (!bytes_slices_append(&result, &cap, count, current,
                                     slen - pos)) goto fail;
            break;
        }
        size_t idx;
        if (seplen == 0) {
            if (pos >= slen) break;
            uint32_t rune;
            size_t width = utf8_decode(current, slen - pos, &rune);
            if (width == 0) width = 1;
            if (!bytes_slices_append(&result, &cap, count, current, width))
                goto fail;
            pos += width;
            continue;
        }
        idx = nci_ss_finder_next(&f, current, slen - pos);
        if (idx == (size_t)-1) {
            if (!bytes_slices_append(&result, &cap, count, current,
                                     slen - pos)) goto fail;
            break;
        }
        if (!bytes_slices_append(&result, &cap, count, current, idx))
            goto fail;
        pos += idx + seplen;
        if (pos > slen) break;
        if (pos == slen) {
            if (!bytes_slices_append(&result, &cap, count, s + pos, 0))
                goto fail;
            break;
        }
    }
    return result;

fail:
    free(result);
    *count = 0;
    return NULL;
}

neverc_bytes_slice_t *neverc_bytes_split(const uint8_t *s, size_t slen,
                                         const uint8_t *sep, size_t seplen,
                                         size_t *count) {
    return neverc_bytes_split_n(s, slen, sep, seplen, -1, count);
}

neverc_bytes_slice_t *neverc_bytes_fields(const uint8_t *s, size_t slen,
                                          size_t *count) {
    if (!count) return NULL;
    *count = 0;
    if (!bytes_span_valid(s, slen)) return NULL;
    size_t cap = 16;
    neverc_bytes_slice_t *result = (neverc_bytes_slice_t *)malloc(
        cap * sizeof(neverc_bytes_slice_t));
    if (!result) return NULL;
    size_t i = 0;
    while (i < slen) {
        while (i < slen && is_space(s[i])) i++;
        if (i >= slen) break;
        size_t start = i;
        while (i < slen && !is_space(s[i])) i++;
        if (!bytes_slices_append(&result, &cap, count, s + start,
                                 i - start)) {
            free(result);
            *count = 0;
            return NULL;
        }
    }
    return result;
}

/* --- Cut --- */

int neverc_bytes_cut(const uint8_t *s, size_t slen,
                     const uint8_t *sep, size_t seplen,
                     const uint8_t **before, size_t *blen,
                     const uint8_t **after, size_t *alen) {
    if (!before || !blen || !after || !alen ||
        !bytes_span_valid(s, slen) || !bytes_span_valid(sep, seplen)) return 0;
    size_t idx = neverc_bytes_index(s, slen, sep, seplen);
    if (idx == (size_t)-1) {
        *before = s; *blen = slen;
        *after = bytes_offset(s, slen); *alen = 0;
        return 0;
    }
    *before = s; *blen = idx;
    *after = bytes_offset(s, idx + seplen); *alen = slen - idx - seplen;
    return 1;
}

int neverc_bytes_cut_prefix(const uint8_t *s, size_t slen,
                            const uint8_t *prefix, size_t plen,
                            const uint8_t **after, size_t *alen) {
    if (!after || !alen || !bytes_span_valid(s, slen) ||
        !bytes_span_valid(prefix, plen)) return 0;
    if (neverc_bytes_has_prefix(s, slen, prefix, plen)) {
        *after = bytes_offset(s, plen); *alen = slen - plen;
        return 1;
    }
    *after = s; *alen = slen;
    return 0;
}

int neverc_bytes_cut_suffix(const uint8_t *s, size_t slen,
                            const uint8_t *suffix, size_t sfxlen,
                            const uint8_t **before, size_t *blen) {
    if (!before || !blen || !bytes_span_valid(s, slen) ||
        !bytes_span_valid(suffix, sfxlen)) return 0;
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
    if (!bytes_span_valid(b, blen) || !f) return 0;
    for (size_t i = 0; i < blen; i++)
        if (f(b[i])) return 1;
    return 0;
}

size_t neverc_bytes_index_func(const uint8_t *s, size_t slen,
                               neverc_bytes_func_t f) {
    if (!bytes_span_valid(s, slen) || !f) return (size_t)-1;
    for (size_t i = 0; i < slen; i++)
        if (f(s[i])) return i;
    return (size_t)-1;
}

size_t neverc_bytes_last_index_func(const uint8_t *s, size_t slen,
                                    neverc_bytes_func_t f) {
    if (!bytes_span_valid(s, slen) || !f) return (size_t)-1;
    for (size_t i = slen; i > 0; i--)
        if (f(s[i-1])) return i-1;
    return (size_t)-1;
}

uint8_t *neverc_bytes_trim_func(const uint8_t *s, size_t slen,
                                neverc_bytes_func_t f, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen) || !f) return NULL;
    *outlen = 0;
    size_t start = 0, end = slen;
    while (start < end && f(s[start])) start++;
    while (end > start && f(s[end-1])) end--;
    return bytes_copy(bytes_offset(s, start), end - start, outlen);
}

uint8_t *neverc_bytes_trim_left_func(const uint8_t *s, size_t slen,
                                     neverc_bytes_func_t f, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen) || !f) return NULL;
    *outlen = 0;
    size_t start = 0;
    while (start < slen && f(s[start])) start++;
    return bytes_copy(bytes_offset(s, start), slen - start, outlen);
}

uint8_t *neverc_bytes_trim_right_func(const uint8_t *s, size_t slen,
                                      neverc_bytes_func_t f, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen) || !f) return NULL;
    *outlen = 0;
    size_t end = slen;
    while (end > 0 && f(s[end-1])) end--;
    return bytes_copy(s, end, outlen);
}

neverc_bytes_slice_t *neverc_bytes_fields_func(const uint8_t *s, size_t slen,
                                               neverc_bytes_func_t f,
                                               size_t *count) {
    if (!count) return NULL;
    *count = 0;
    if (!bytes_span_valid(s, slen) || !f) return NULL;
    size_t cap = 8;
    neverc_bytes_slice_t *result = (neverc_bytes_slice_t *)malloc(cap * sizeof(*result));
    if (!result) return NULL;
    size_t i = 0;
    while (i < slen) {
        while (i < slen && f(s[i])) i++;
        if (i >= slen) break;
        size_t start = i;
        while (i < slen && !f(s[i])) i++;
        if (!bytes_slices_append(&result, &cap, count, s + start,
                                 i - start)) {
            free(result);
            *count = 0;
            return NULL;
        }
    }
    return result;
}

/* --- Map --- */

uint8_t *neverc_bytes_map(uint8_t (*mapping)(uint8_t),
                          const uint8_t *s, size_t slen, size_t *outlen) {
    if (!outlen || !bytes_span_valid(s, slen) || !mapping) return NULL;
    *outlen = 0;
    uint8_t *r = bytes_alloc(slen);
    if (!r) return NULL;
    for (size_t i = 0; i < slen; i++) r[i] = mapping(s[i]);
    *outlen = slen;
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
    if (!count) return NULL;
    *count = 0;
    if (n == 0 || !bytes_span_valid(s, slen) ||
        !bytes_span_valid(sep, seplen)) return NULL;
    size_t cap = 8;
    neverc_bytes_slice_t *result = (neverc_bytes_slice_t *)malloc(cap * sizeof(*result));
    if (!result) return NULL;
    const uint8_t *p = s;
    size_t remaining = slen;

    nci_ss_finder_t f;
    nci_ss_finder_init(&f, sep, seplen);

    while (remaining > 0) {
        if (n > 0 && *count >= (size_t)(n - 1)) {
            if (!bytes_slices_append(&result, &cap, count, p, remaining))
                goto split_after_fail;
            break;
        }
        if (seplen == 0) {
            uint32_t rune;
            size_t width = utf8_decode(p, remaining, &rune);
            if (width == 0) width = 1;
            if (!bytes_slices_append(&result, &cap, count, p, width))
                goto split_after_fail;
            p += width;
            remaining -= width;
            continue;
        }
        size_t idx;
        if (remaining < seplen)
            idx = (size_t)-1;
        else
            idx = nci_ss_finder_next(&f, p, remaining);
        if (idx == (size_t)-1) {
            if (!bytes_slices_append(&result, &cap, count, p, remaining))
                goto split_after_fail;
            break;
        }
        size_t chunk = idx + seplen;
        if (!bytes_slices_append(&result, &cap, count, p, chunk))
            goto split_after_fail;
        p += chunk;
        remaining -= chunk;
    }
    if (seplen > 0 && remaining == 0 &&
        !bytes_slices_append(&result, &cap, count, p, 0))
        goto split_after_fail;
    return result;

split_after_fail:
    free(result);
    *count = 0;
    return NULL;
}

/* --- Clone --- */

uint8_t *neverc_bytes_clone(const uint8_t *b, size_t blen) {
    if (!bytes_span_valid(b, blen) || blen == 0) return NULL;
    uint8_t *r = bytes_alloc(blen);
    if (!r) return NULL;
    memcpy(r, b, blen);
    return r;
}

/* --- CutLast --- */

int neverc_bytes_cut_last(const uint8_t *s, size_t slen,
                          const uint8_t *sep, size_t seplen,
                          const uint8_t **before, size_t *blen,
                          const uint8_t **after, size_t *alen) {
    if (!before || !blen || !after || !alen ||
        !bytes_span_valid(s, slen) || !bytes_span_valid(sep, seplen)) return 0;
    size_t idx = neverc_bytes_last_index(s, slen, sep, seplen);
    if (idx == (size_t)-1) {
        *before = s; *blen = slen;
        *after = bytes_offset(s, slen); *alen = 0;
        return 0;
    }
    *before = s; *blen = idx;
    *after = bytes_offset(s, idx + seplen); *alen = slen - idx - seplen;
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
    if (!bytes_span_valid(s, slen) || r > 0x10FFFF ||
        (r >= 0xD800 && r <= 0xDFFF)) return (size_t)-1;
    if (r < 0x80) return neverc_bytes_index_byte(s, slen, (uint8_t)r);
    if (r == 0xFFFD) {
        size_t pos = 0;
        while (pos < slen) {
            uint32_t decoded;
            size_t width = utf8_decode(s + pos, slen - pos, &decoded);
            if (width == 0 || decoded == r) return pos;
            pos += width;
        }
        return (size_t)-1;
    }
    uint8_t enc[4];
    size_t elen = utf8_encode(r, enc);
    return neverc_bytes_index(s, slen, enc, elen);
}

/* --- Runes --- */

uint32_t *neverc_bytes_runes(const uint8_t *s, size_t slen, size_t *count) {
    if (!count) return NULL;
    *count = 0;
    if (!bytes_span_valid(s, slen) || slen > SIZE_MAX / sizeof(uint32_t))
        return NULL;
    size_t cap = slen > 0 ? slen : 1;
    uint32_t *result = (uint32_t *)malloc(cap * sizeof(uint32_t));
    if (!result) return NULL;
    size_t i = 0;
    while (i < slen) {
        uint32_t r;
        size_t n = utf8_decode(s + i, slen - i, &r);
        if (n == 0) { r = 0xFFFD; n = 1; }
        result[(*count)++] = r;
        i += n;
    }
    return result;
}

/* --- ToValidUTF8 --- */

uint8_t *neverc_bytes_to_valid_utf8(const uint8_t *s, size_t slen,
                                     const uint8_t *replacement, size_t rlen,
                                     size_t *outlen) {
    if (!outlen) return NULL;
    *outlen = 0;
    if (!bytes_span_valid(s, slen) || !bytes_span_valid(replacement, rlen))
        return NULL;
    size_t cap = slen > 0 ? slen : 1;
    uint8_t *result = (uint8_t *)malloc(cap);
    if (!result) return NULL;
    size_t out = 0;
    size_t i = 0;
    int invalid = 0;
    while (i < slen) {
        uint32_t r;
        size_t n = utf8_decode(s + i, slen - i, &r);
        if (n == 0) {
            if (!invalid) {
                if (!bytes_buffer_reserve(&result, &cap, out, rlen))
                    goto utf8_fail;
                if (rlen > 0) {
                    memcpy(result + out, replacement, rlen);
                    out += rlen;
                }
                invalid = 1;
            }
            i++;
        } else {
            invalid = 0;
            if (!bytes_buffer_reserve(&result, &cap, out, n)) goto utf8_fail;
            memcpy(result + out, s + i, n); out += n;
            i += n;
        }
    }
    *outlen = out;
    return result;

utf8_fail:
    free(result);
    return NULL;
}

int neverc_bytes_contains_rune(const uint8_t *s, size_t slen, uint32_t r) {
    return neverc_bytes_index_rune(s, slen, r) != (size_t)-1;
}
