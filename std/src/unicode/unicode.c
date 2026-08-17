#include "neverc/std/unicode.h"
#include "unicode_tables.h"

/*
 * Unicode classification and case mapping — tables and lookup rules match
 * Go's unicode package (Unicode 15.0.0 / Go 1.23). Latin-1 uses the same
 * property bitmap fast path; everything else is a range-table search.
 */

#define NCI_LINEAR_MAX 18

static int nci_is16(const nci_ur16 *ranges, int n, uint16_t r) {
    if (n <= 0) return 0;
    if (n <= NCI_LINEAR_MAX || r <= NEVERC_UNICODE_MAX_LATIN1) {
        for (int i = 0; i < n; i++) {
            if (r < ranges[i].lo) return 0;
            if (r <= ranges[i].hi)
                return ranges[i].stride == 1 ||
                       ((r - ranges[i].lo) % ranges[i].stride) == 0;
        }
        return 0;
    }
    int lo = 0, hi = n;
    while (lo < hi) {
        int m = (int)((unsigned)(lo + hi) >> 1);
        if (ranges[m].lo <= r && r <= ranges[m].hi)
            return ranges[m].stride == 1 ||
                   ((r - ranges[m].lo) % ranges[m].stride) == 0;
        if (r < ranges[m].lo) hi = m;
        else lo = m + 1;
    }
    return 0;
}

static int nci_is32(const nci_ur32 *ranges, int n, uint32_t r) {
    if (n <= 0) return 0;
    if (n <= NCI_LINEAR_MAX) {
        for (int i = 0; i < n; i++) {
            if (r < ranges[i].lo) return 0;
            if (r <= ranges[i].hi)
                return ranges[i].stride == 1 ||
                       ((r - ranges[i].lo) % ranges[i].stride) == 0;
        }
        return 0;
    }
    int lo = 0, hi = n;
    while (lo < hi) {
        int m = (int)((unsigned)(lo + hi) >> 1);
        if (ranges[m].lo <= r && r <= ranges[m].hi)
            return ranges[m].stride == 1 ||
                   ((r - ranges[m].lo) % ranges[m].stride) == 0;
        if (r < ranges[m].lo) hi = m;
        else lo = m + 1;
    }
    return 0;
}

static int nci_in_table(const nci_utable *t, uint32_t r) {
    if (t->n16 > 0 && r <= t->r16[t->n16 - 1].hi)
        return nci_is16(t->r16, t->n16, (uint16_t)r);
    if (t->n32 > 0 && r >= t->r32[0].lo)
        return nci_is32(t->r32, t->n32, r);
    return 0;
}

static int nci_in_table_ex_latin(const nci_utable *t, uint32_t r) {
    int off = t->latin_offset;
    if (t->n16 > off && r <= t->r16[t->n16 - 1].hi)
        return nci_is16(t->r16 + off, t->n16 - off, (uint16_t)r);
    if (t->n32 > 0 && r >= t->r32[0].lo)
        return nci_is32(t->r32, t->n32, r);
    return 0;
}

static const nci_ucase *nci_find_case(uint32_t r) {
    int lo = 0, hi = NCI_CASE_MAP_N;
    while (lo < hi) {
        int m = (int)((unsigned)(lo + hi) >> 1);
        if (nci_case_map[m].r < r) lo = m + 1;
        else hi = m;
    }
    if (lo < NCI_CASE_MAP_N && nci_case_map[lo].r == r)
        return &nci_case_map[lo];
    return 0;
}

int neverc_unicode_is_control(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1)
        return (nci_uprops[r] & NCI_PC) != 0;
    return 0;
}

int neverc_unicode_is_digit(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1)
        return r >= '0' && r <= '9';
    return nci_in_table_ex_latin(&nci_tab_digit, r);
}

int neverc_unicode_is_upper(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1)
        return (nci_uprops[r] & (NCI_PLU | NCI_PLL)) == NCI_PLU;
    return nci_in_table_ex_latin(&nci_tab_upper, r);
}

int neverc_unicode_is_lower(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1)
        return (nci_uprops[r] & (NCI_PLU | NCI_PLL)) == NCI_PLL;
    return nci_in_table_ex_latin(&nci_tab_lower, r);
}

int neverc_unicode_is_letter(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1)
        return (nci_uprops[r] & (NCI_PLU | NCI_PLL)) != 0;
    return nci_in_table_ex_latin(&nci_tab_letter, r);
}

int neverc_unicode_is_space(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1) {
        switch (r) {
        case '\t': case '\n': case '\v': case '\f': case '\r': case ' ':
        case 0x85: case 0xA0:
            return 1;
        default:
            return 0;
        }
    }
    return nci_in_table_ex_latin(&nci_tab_space, r);
}

int neverc_unicode_is_punct(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1)
        return (nci_uprops[r] & NCI_PP) != 0;
    return nci_in_table(&nci_tab_punct, r);
}

int neverc_unicode_is_number(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1)
        return (nci_uprops[r] & NCI_PN) != 0;
    return nci_in_table_ex_latin(&nci_tab_number, r);
}

int neverc_unicode_is_symbol(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1)
        return (nci_uprops[r] & NCI_PS) != 0;
    return nci_in_table_ex_latin(&nci_tab_symbol, r);
}

int neverc_unicode_is_title(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1) return 0;
    return nci_in_table_ex_latin(&nci_tab_title, r);
}

int neverc_unicode_is_mark(uint32_t r) {
    return nci_in_table_ex_latin(&nci_tab_mark, r);
}

int neverc_unicode_is_print(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1)
        return (nci_uprops[r] & NCI_PPR) != 0;
    return nci_in_table_ex_latin(&nci_tab_letter, r) ||
           nci_in_table_ex_latin(&nci_tab_mark, r) ||
           nci_in_table_ex_latin(&nci_tab_number, r) ||
           nci_in_table(&nci_tab_punct, r) ||
           nci_in_table_ex_latin(&nci_tab_symbol, r);
}

int neverc_unicode_is_graphic(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_LATIN1)
        return (nci_uprops[r] & (NCI_PPR | NCI_PZ)) != 0;
    return neverc_unicode_is_print(r) || nci_in_table_ex_latin(&nci_tab_zs, r);
}

uint32_t neverc_unicode_to_upper(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_ASCII) {
        if (r >= 'a' && r <= 'z') return r - 32;
        return r;
    }
    const nci_ucase *c = nci_find_case(r);
    return c ? c->u : r;
}

uint32_t neverc_unicode_to_lower(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_ASCII) {
        if (r >= 'A' && r <= 'Z') return r + 32;
        return r;
    }
    const nci_ucase *c = nci_find_case(r);
    return c ? c->l : r;
}

uint32_t neverc_unicode_to_title(uint32_t r) {
    if (r <= NEVERC_UNICODE_MAX_ASCII) {
        if (r >= 'a' && r <= 'z') return r - 32;
        return r;
    }
    const nci_ucase *c = nci_find_case(r);
    return c ? c->t : r;
}

uint32_t neverc_unicode_simple_fold(uint32_t r) {
    if (r > NEVERC_UNICODE_MAX_RUNE) return r;
    if (r <= NEVERC_UNICODE_MAX_ASCII) return nci_ascii_fold[r];

    int lo = 0, hi = NCI_CASE_ORBIT_N;
    while (lo < hi) {
        int m = (int)((unsigned)(lo + hi) >> 1);
        if (nci_case_orbit[m].from < r) lo = m + 1;
        else hi = m;
    }
    if (lo < NCI_CASE_ORBIT_N && nci_case_orbit[lo].from == r)
        return nci_case_orbit[lo].to;

    uint32_t l = neverc_unicode_to_lower(r);
    if (l != r) return l;
    return neverc_unicode_to_upper(r);
}
