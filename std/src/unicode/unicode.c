#include "neverc/unicode.h"

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

int neverc_unicode_is_control(uint32_t r) {
    if (r <= 0x1F) return 1;
    if (r >= 0x7F && r <= 0x9F) return 1;
    return 0;
}

int neverc_unicode_is_digit(uint32_t r) {
    if (r >= '0' && r <= '9') return 1;
    /* Common full-width digits */
    if (r >= 0xFF10 && r <= 0xFF19) return 1;
    /* Arabic-Indic digits */
    if (r >= 0x0660 && r <= 0x0669) return 1;
    if (r >= 0x06F0 && r <= 0x06F9) return 1;
    /* Devanagari digits */
    if (r >= 0x0966 && r <= 0x096F) return 1;
    return 0;
}

int neverc_unicode_is_upper(uint32_t r) {
    if (r >= 'A' && r <= 'Z') return 1;
    /* Latin-1 uppercase: À-Ö (C0-D6), Ø-Þ (D8-DE) */
    if (r >= 0xC0 && r <= 0xD6) return 1;
    if (r >= 0xD8 && r <= 0xDE) return 1;
    /* Latin Extended-A pairs (every other, starting uppercase) */
    if (r >= 0x100 && r <= 0x17E && !(r & 1)) return 1;
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
    /* Latin Extended-A pairs (every other, starting lowercase) */
    if (r >= 0x101 && r <= 0x17F && (r & 1)) return 1;
    /* Greek lowercase */
    if (r >= 0x3B1 && r <= 0x3C9) return 1;
    /* Cyrillic lowercase */
    if (r >= 0x430 && r <= 0x44F) return 1;
    /* Special: ß (0xDF), µ (0xB5) */
    if (r == 0xDF || r == 0xB5) return 1;
    return 0;
}

int neverc_unicode_is_letter(uint32_t r) {
    if (neverc_unicode_is_upper(r) || neverc_unicode_is_lower(r)) return 1;
    if (r == '_') return 0;
    /* CJK Unified Ideographs */
    if (r >= 0x4E00 && r <= 0x9FFF) return 1;
    /* CJK Extension A */
    if (r >= 0x3400 && r <= 0x4DBF) return 1;
    /* Hiragana */
    if (r >= 0x3040 && r <= 0x309F) return 1;
    /* Katakana */
    if (r >= 0x30A0 && r <= 0x30FF) return 1;
    /* Hangul Syllables */
    if (r >= 0xAC00 && r <= 0xD7AF) return 1;
    /* Latin Extended Additional */
    if (r >= 0x1E00 && r <= 0x1EFF) return 1;
    /* Arabic letters */
    if (r >= 0x0621 && r <= 0x064A) return 1;
    /* Hebrew letters */
    if (r >= 0x05D0 && r <= 0x05EA) return 1;
    /* Thai */
    if (r >= 0x0E01 && r <= 0x0E3A) return 1;
    /* Devanagari */
    if (r >= 0x0900 && r <= 0x0963) return 1;
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
    case 0xFEFF:   /* BOM / Zero Width No-Break Space */
        return 1;
    default:
        /* EN/EM spaces (U+2000..U+200A) */
        if (r >= 0x2000 && r <= 0x200A) return 1;
        return 0;
    }
}

int neverc_unicode_is_punct(uint32_t r) {
    /* ASCII punctuation */
    if (r >= 0x21 && r <= 0x2F) return 1;
    if (r >= 0x3A && r <= 0x40) return 1;
    if (r >= 0x5B && r <= 0x60) return 1;
    if (r >= 0x7B && r <= 0x7E) return 1;
    /* Latin-1 supplement punctuation */
    if (r == 0xA1 || r == 0xA7 || r == 0xAB || r == 0xB6 || r == 0xB7 ||
        r == 0xBB || r == 0xBF) return 1;
    /* General punctuation block */
    if (r >= 0x2010 && r <= 0x2027) return 1;
    /* CJK punctuation */
    if (r >= 0x3001 && r <= 0x3003) return 1;
    if (r == 0x3008 || r == 0x3009 || r == 0x300A || r == 0x300B) return 1;
    if (r >= 0xFF01 && r <= 0xFF0F) return 1;
    return 0;
}

int neverc_unicode_is_graphic(uint32_t r) {
    if (neverc_unicode_is_letter(r)) return 1;
    if (neverc_unicode_is_digit(r)) return 1;
    if (neverc_unicode_is_punct(r)) return 1;
    if (neverc_unicode_is_space(r) && r != ' ' && r != '\t' && r != '\n' &&
        r != '\r' && r != '\v' && r != '\f') return 0;
    /* Symbols: currency, math, etc. */
    if (r >= 0x24 && r <= 0x24) return 1;
    if (r == 0xA2 || r == 0xA3 || r == 0xA4 || r == 0xA5) return 1;
    if (r >= 0x2200 && r <= 0x22FF) return 1;
    return neverc_unicode_is_print(r) && !neverc_unicode_is_control(r);
}

int neverc_unicode_is_print(uint32_t r) {
    if (neverc_unicode_is_control(r)) return 0;
    if (r == 0x7F) return 0;
    if (r > NEVERC_UNICODE_MAX_RUNE) return 0;
    /* Surrogate pairs are not printable */
    if (r >= 0xD800 && r <= 0xDFFF) return 0;
    /* Noncharacters */
    if (r >= 0xFDD0 && r <= 0xFDEF) return 0;
    if ((r & 0xFFFE) == 0xFFFE) return 0;
    if (r >= 0x20) return 1;
    return 0;
}

uint32_t neverc_unicode_to_upper(uint32_t r) {
    if (r >= 'a' && r <= 'z') return r - 32;
    if (r >= 0xE0 && r <= 0xF6) return r - 32;
    if (r >= 0xF8 && r <= 0xFE) return r - 32;
    /* Greek lowercase → uppercase */
    if (r >= 0x3B1 && r <= 0x3C9) return r - 32;
    /* Cyrillic lowercase → uppercase */
    if (r >= 0x430 && r <= 0x44F) return r - 32;
    /* Latin Extended-A: odd → even */
    if (r >= 0x101 && r <= 0x17F && (r & 1)) return r - 1;
    return r;
}

uint32_t neverc_unicode_to_lower(uint32_t r) {
    if (r >= 'A' && r <= 'Z') return r + 32;
    if (r >= 0xC0 && r <= 0xD6) return r + 32;
    if (r >= 0xD8 && r <= 0xDE) return r + 32;
    /* Greek uppercase → lowercase */
    if (r >= 0x391 && r <= 0x3A9 && r != 0x3A2) return r + 32;
    /* Cyrillic uppercase → lowercase */
    if (r >= 0x410 && r <= 0x42F) return r + 32;
    /* Latin Extended-A: even → odd */
    if (r >= 0x100 && r <= 0x17E && !(r & 1)) return r + 1;
    return r;
}
