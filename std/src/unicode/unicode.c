#include "neverc/std/unicode.h"

/*
 * Unicode character classification — mirrors subset of Go unicode package.
 * Covers ASCII + Latin-1 precisely; for runes > 0xFF uses range tables
 * covering the most common Unicode blocks.
 *
 * Full Unicode tables would add ~50KB. This implementation covers:
 * - ASCII (U+0000..U+007F): exact
 * - Latin-1 Supplement (U+0080..U+00FF): exact
 * - Common scripts (U+0100..U+10FFFF): heuristic based on Unicode blocks
 */

static int unicode_is_latin_extended_upper(uint32_t r) {
    if (r >= 0x0100 && r <= 0x0136) return (r & 1U) == 0;
    if (r >= 0x0139 && r <= 0x0147) return (r & 1U) != 0;
    if (r >= 0x014A && r <= 0x0176) return (r & 1U) == 0;
    if (r == 0x0178) return 1;
    if (r >= 0x0179 && r <= 0x017D) return (r & 1U) != 0;
    return 0;
}

static int unicode_is_latin_extended_lower(uint32_t r) {
    if (r >= 0x0101 && r <= 0x0137) return (r & 1U) != 0;
    if (r == 0x0138 || r == 0x0149 || r == 0x017F) return 1;
    if (r >= 0x013A && r <= 0x0148) return (r & 1U) == 0;
    if (r >= 0x014B && r <= 0x0177) return (r & 1U) != 0;
    if (r >= 0x017A && r <= 0x017E) return (r & 1U) == 0;
    return 0;
}

static int unicode_is_zs(uint32_t r) {
    return r == 0x20 || r == 0xA0 || r == 0x1680 ||
           (r >= 0x2000 && r <= 0x200A) || r == 0x202F ||
           r == 0x205F || r == 0x3000;
}

int neverc_unicode_is_control(uint32_t r) {
    if (r <= 0x1F) return 1;
    if (r >= 0x7F && r <= 0x9F) return 1;
    return 0;
}

int neverc_unicode_is_digit(uint32_t r) {
    if (r >= '0' && r <= '9') return 1;
    if (r >= 0x0660 && r <= 0x0669) return 1;
    if (r >= 0x06F0 && r <= 0x06F9) return 1;
    if (r >= 0x07C0 && r <= 0x07C9) return 1;
    if (r >= 0x0966 && r <= 0x096F) return 1;
    if (r >= 0x09E6 && r <= 0x09EF) return 1;
    if (r >= 0x0A66 && r <= 0x0A6F) return 1;
    if (r >= 0x0AE6 && r <= 0x0AEF) return 1;
    if (r >= 0x0B66 && r <= 0x0B6F) return 1;
    if (r >= 0x0BE6 && r <= 0x0BEF) return 1;
    if (r >= 0x0C66 && r <= 0x0C6F) return 1;
    if (r >= 0x0CE6 && r <= 0x0CEF) return 1;
    if (r >= 0x0D66 && r <= 0x0D6F) return 1;
    if (r >= 0x0E50 && r <= 0x0E59) return 1;
    if (r >= 0x0ED0 && r <= 0x0ED9) return 1;
    if (r >= 0x0F20 && r <= 0x0F29) return 1;
    if (r >= 0xFF10 && r <= 0xFF19) return 1;
    return 0;
}

int neverc_unicode_is_upper(uint32_t r) {
    if (r >= 'A' && r <= 'Z') return 1;
    /* Latin-1 uppercase: À-Ö (C0-D6), Ø-Þ (D8-DE) */
    if (r >= 0xC0 && r <= 0xD6) return 1;
    if (r >= 0xD8 && r <= 0xDE) return 1;
    if (unicode_is_latin_extended_upper(r)) return 1;
    /* Greek uppercase */
    if (r >= 0x391 && r <= 0x3A9 && r != 0x3A2) return 1;
    /* Cyrillic uppercase */
    if (r >= 0x410 && r <= 0x42F) return 1;
    return 0;
}

int neverc_unicode_is_lower(uint32_t r) {
    if (r >= 'a' && r <= 'z') return 1;
    /* Latin-1 lowercase: à-ö (E0-F6), ø-ÿ (F8-FF) */
    if (r >= 0xE0 && r <= 0xF6) return 1;
    if (r >= 0xF8 && r <= 0xFF) return 1;
    if (unicode_is_latin_extended_lower(r)) return 1;
    /* Greek lowercase */
    if (r >= 0x3B1 && r <= 0x3C9) return 1;
    /* Cyrillic lowercase */
    if (r >= 0x430 && r <= 0x44F) return 1;
    /* Special: ß (0xDF), µ (0xB5) */
    if (r == 0xDF || r == 0xB5) return 1;
    return 0;
}

int neverc_unicode_is_letter(uint32_t r) {
    /* ASCII fast path: a letter is exactly A-Z or a-z, so skip the Latin-1,
     * Greek, Cyrillic and CJK range checks for the common case. */
    if (r < 0x80)
        return (r >= 'A' && r <= 'Z') || (r >= 'a' && r <= 'z');
    if (neverc_unicode_is_upper(r) || neverc_unicode_is_lower(r)) return 1;
    if (neverc_unicode_is_title(r)) return 1;
    if (r == 0x00AA || r == 0x00BA) return 1;
    if ((r >= 0x01C4 && r <= 0x01CC) ||
        (r >= 0x01F1 && r <= 0x01F3)) return 1;
    if (r == '_') return 0;
    /* CJK Unified Ideographs */
    if (r >= 0x4E00 && r <= 0x9FFF) return 1;
    /* CJK Extension A */
    if (r >= 0x3400 && r <= 0x4DBF) return 1;
    /* Hiragana and Katakana letters */
    if ((r >= 0x3041 && r <= 0x3096) ||
        (r >= 0x309D && r <= 0x309F)) return 1;
    if ((r >= 0x30A1 && r <= 0x30FA) ||
        (r >= 0x30FC && r <= 0x30FF)) return 1;
    /* Hangul Syllables */
    if (r >= 0xAC00 && r <= 0xD7AF) return 1;
    /* Latin Extended Additional */
    if (r >= 0x1E00 && r <= 0x1EFF) return 1;
    /* Arabic letters */
    if (r >= 0x0621 && r <= 0x064A) return 1;
    /* Hebrew letters */
    if (r >= 0x05D0 && r <= 0x05EA) return 1;
    /* Thai letters */
    if ((r >= 0x0E01 && r <= 0x0E30) ||
        (r >= 0x0E32 && r <= 0x0E33) ||
        (r >= 0x0E40 && r <= 0x0E46)) return 1;
    /* Devanagari letters */
    if ((r >= 0x0904 && r <= 0x0939) || r == 0x093D ||
        r == 0x0950 || (r >= 0x0958 && r <= 0x0961)) return 1;
    return 0;
}

int neverc_unicode_is_space(uint32_t r) {
    switch (r) {
    case '\t': case '\n': case '\v': case '\f': case '\r': case ' ':
        return 1;
    case 0x85:     /* NEL */
    case 0xA0:     /* NBSP */
    case 0x1680:   /* Ogham Space Mark */
    case 0x2028:   /* Line Separator */
    case 0x2029:   /* Paragraph Separator */
    case 0x202F:   /* Narrow No-Break Space */
    case 0x205F:   /* Medium Mathematical Space */
    case 0x3000:   /* Ideographic Space */
        return 1;
    default:
        /* EN/EM spaces (U+2000..U+200A) */
        if (r >= 0x2000 && r <= 0x200A) return 1;
        return 0;
    }
}

int neverc_unicode_is_punct(uint32_t r) {
    /* ASCII punctuation */
    if ((r >= 0x21 && r <= 0x23) ||
        (r >= 0x25 && r <= 0x2A) ||
        (r >= 0x2C && r <= 0x2F) ||
        (r >= 0x3A && r <= 0x3B) ||
        (r >= 0x3F && r <= 0x40) ||
        (r >= 0x5B && r <= 0x5D) ||
        r == 0x5F || r == 0x7B || r == 0x7D) return 1;
    /* Latin-1 supplement punctuation */
    if (r == 0xA1 || r == 0xA7 || r == 0xAB || r == 0xB6 || r == 0xB7 ||
        r == 0xBB || r == 0xBF) return 1;
    /* General punctuation block */
    if (r >= 0x2010 && r <= 0x2027) return 1;
    /* CJK punctuation */
    if (r >= 0x3001 && r <= 0x3003) return 1;
    if (r == 0x3008 || r == 0x3009 || r == 0x300A || r == 0x300B) return 1;
    if (r == 0x30A0 || r == 0x30FB) return 1;
    if (r >= 0xFF01 && r <= 0xFF0F) return 1;
    return 0;
}

int neverc_unicode_is_graphic(uint32_t r) {
    return neverc_unicode_is_print(r) || unicode_is_zs(r);
}

int neverc_unicode_is_print(uint32_t r) {
    if (r > NEVERC_UNICODE_MAX_RUNE ||
        (r >= 0xD800 && r <= 0xDFFF))
        return 0;
    return r == 0x20 || neverc_unicode_is_letter(r) ||
           neverc_unicode_is_mark(r) || neverc_unicode_is_number(r) ||
           neverc_unicode_is_punct(r) || neverc_unicode_is_symbol(r);
}

uint32_t neverc_unicode_to_upper(uint32_t r) {
    if (r == 0x00B5) return 0x039C;
    if (r == 0x00FF) return 0x0178;
    if (r == 0x0131) return 0x0049;
    if (r == 0x03C2) return 0x03A3;
    if (r == 0x017F) return 0x0053;
    if (r >= 'a' && r <= 'z') return r - 32;
    if (r >= 0xE0 && r <= 0xF6) return r - 32;
    if (r >= 0xF8 && r <= 0xFE) return r - 32;
    /* Greek lowercase → uppercase */
    if (r >= 0x3B1 && r <= 0x3C9) return r - 32;
    /* Cyrillic lowercase → uppercase */
    if (r >= 0x430 && r <= 0x44F) return r - 32;
    if (r >= 0x0101 && r <= 0x0137 && (r & 1U)) return r - 1;
    if (r >= 0x013A && r <= 0x0148 && !(r & 1U)) return r - 1;
    if (r >= 0x014B && r <= 0x0177 && (r & 1U)) return r - 1;
    if (r >= 0x017A && r <= 0x017E && !(r & 1U)) return r - 1;
    return r;
}

uint32_t neverc_unicode_to_lower(uint32_t r) {
    if (r == 0x0130) return 0x0069;
    if (r == 0x0178) return 0x00FF;
    if (r >= 'A' && r <= 'Z') return r + 32;
    if (r >= 0xC0 && r <= 0xD6) return r + 32;
    if (r >= 0xD8 && r <= 0xDE) return r + 32;
    /* Greek uppercase → lowercase */
    if (r >= 0x391 && r <= 0x3A9 && r != 0x3A2) return r + 32;
    /* Cyrillic uppercase → lowercase */
    if (r >= 0x410 && r <= 0x42F) return r + 32;
    if (r >= 0x0100 && r <= 0x0136 && !(r & 1U)) return r + 1;
    if (r >= 0x0139 && r <= 0x0147 && (r & 1U)) return r + 1;
    if (r >= 0x014A && r <= 0x0176 && !(r & 1U)) return r + 1;
    if (r >= 0x0179 && r <= 0x017D && (r & 1U)) return r + 1;
    return r;
}

uint32_t neverc_unicode_to_title(uint32_t r) {
    if (r >= 0x01C4 && r <= 0x01C6) return 0x01C5;
    if (r >= 0x01C7 && r <= 0x01C9) return 0x01C8;
    if (r >= 0x01CA && r <= 0x01CC) return 0x01CB;
    if (r >= 0x01F1 && r <= 0x01F3) return 0x01F2;
    return neverc_unicode_to_upper(r);
}

int neverc_unicode_is_number(uint32_t r) {
    if (r >= '0' && r <= '9') return 1;
    if (r >= 0x0660 && r <= 0x0669) return 1;
    if (r >= 0x06F0 && r <= 0x06F9) return 1;
    if (r >= 0x07C0 && r <= 0x07C9) return 1;
    if (r >= 0x0966 && r <= 0x096F) return 1;
    if (r >= 0x09E6 && r <= 0x09EF) return 1;
    if (r >= 0x0A66 && r <= 0x0A6F) return 1;
    if (r >= 0x0AE6 && r <= 0x0AEF) return 1;
    if (r >= 0x0B66 && r <= 0x0B6F) return 1;
    if (r >= 0x0BE6 && r <= 0x0BEF) return 1;
    if (r >= 0x0C66 && r <= 0x0C6F) return 1;
    if (r >= 0x0CE6 && r <= 0x0CEF) return 1;
    if (r >= 0x0D66 && r <= 0x0D6F) return 1;
    if (r >= 0x0E50 && r <= 0x0E59) return 1;
    if (r >= 0x0ED0 && r <= 0x0ED9) return 1;
    if (r >= 0x0F20 && r <= 0x0F29) return 1;
    if (r >= 0xFF10 && r <= 0xFF19) return 1;
    if (r == 0x2070 || (r >= 0x2074 && r <= 0x2079)) return 1;
    if (r >= 0x2080 && r <= 0x2089) return 1;
    if (r >= 0x00B2 && r <= 0x00B3) return 1;
    if (r == 0x00B9 || r == 0x00BC || r == 0x00BD || r == 0x00BE) return 1;
    return 0;
}

int neverc_unicode_is_symbol(uint32_t r) {
    if (r == '$' || r == '+' || r == '<' || r == '=' || r == '>' ||
        r == '^' || r == '`' || r == '|' || r == '~') return 1;
    if ((r >= 0x00A2 && r <= 0x00A6) ||
        (r >= 0x00A8 && r <= 0x00A9)) return 1;
    if (r == 0x00AC || r == 0x00AE || r == 0x00AF) return 1;
    if (r == 0x00B0 || r == 0x00B1) return 1;
    if (r == 0x00B4 || r == 0x00B8) return 1;
    if (r == 0x00D7 || r == 0x00F7) return 1;
    if (r >= 0x2190 && r <= 0x21FF) return 1;
    if (r >= 0x2200 && r <= 0x22FF) return 1;
    if (r >= 0x2300 && r <= 0x23FF) return 1;
    if (r >= 0x2600 && r <= 0x26FF) return 1;
    if (r >= 0x2700 && r <= 0x27BF) return 1;
    if (r >= 0x20A0 && r <= 0x20CF) return 1;
    if (r >= 0x309B && r <= 0x309C) return 1;
    return 0;
}

int neverc_unicode_is_title(uint32_t r) {
    if (r == 0x01C5) return 1;
    if (r == 0x01C8) return 1;
    if (r == 0x01CB) return 1;
    if (r == 0x01F2) return 1;
    if (r >= 0x1F88 && r <= 0x1F8F) return 1;
    if (r >= 0x1F98 && r <= 0x1F9F) return 1;
    if (r >= 0x1FA8 && r <= 0x1FAF) return 1;
    if (r == 0x1FBC || r == 0x1FCC || r == 0x1FFC) return 1;
    return 0;
}

int neverc_unicode_is_mark(uint32_t r) {
    if (r >= 0x0300 && r <= 0x036F) return 1;
    if (r >= 0x0483 && r <= 0x0489) return 1;
    if (r >= 0x0591 && r <= 0x05BD) return 1;
    if (r == 0x05BF || r == 0x05C1 || r == 0x05C2 || r == 0x05C4 || r == 0x05C5 || r == 0x05C7) return 1;
    if (r >= 0x0610 && r <= 0x061A) return 1;
    if (r >= 0x064B && r <= 0x065F) return 1;
    if (r >= 0x0900 && r <= 0x0903) return 1;
    if ((r >= 0x093A && r <= 0x093C) ||
        (r >= 0x093E && r <= 0x094F)) return 1;
    if (r >= 0x0951 && r <= 0x0957) return 1;
    if (r >= 0x0962 && r <= 0x0963) return 1;
    if (r == 0x0E31 || (r >= 0x0E34 && r <= 0x0E3A) ||
        (r >= 0x0E47 && r <= 0x0E4E)) return 1;
    if (r >= 0x3099 && r <= 0x309A) return 1;
    if (r >= 0xFE20 && r <= 0xFE2F) return 1;
    if (r >= 0x20D0 && r <= 0x20F0) return 1;
    return 0;
}

uint32_t neverc_unicode_simple_fold(uint32_t r) {
    if (r == 0x00B5) return 0x039C;
    if (r == 0x039C) return 0x03BC;
    if (r == 0x03BC) return 0x00B5;
    if (r == 0x03A3) return 0x03C2;
    if (r == 0x03C2) return 0x03C3;
    if (r == 0x03C3) return 0x03A3;
    if (r == 0x0130 || r == 0x0131) return r;
    if (r == 'K') return 'k';
    if (r == 'k') return 0x212A;
    if (r == 0x212A) return 'K';
    if (r == 'S') return 's';
    if (r == 's') return 0x017F;
    if (r == 0x017F) return 'S';
    if (r >= 'A' && r <= 'Z') return r + 32;
    if (r >= 'a' && r <= 'z') return r - 32;
    uint32_t lo = neverc_unicode_to_lower(r);
    if (lo != r) return lo;
    uint32_t up = neverc_unicode_to_upper(r);
    if (up != r) return up;
    return r;
}
