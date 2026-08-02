#include "neverc/std/uuid.h"
#include "neverc/std/_platform.h"
#include <string.h>

#if defined(__has_attribute)
#  if __has_attribute(nonstring)
#    define NEVERC_HEX_PAIR_ATTR __attribute__((nonstring))
#  else
#    define NEVERC_HEX_PAIR_ATTR
#  endif
#else
#  define NEVERC_HEX_PAIR_ATTR
#endif

static void fill_random(uint8_t *buf, size_t len) {
    neverc_platform_random(buf, len);
}

neverc_uuid_t neverc_uuid_new(void) {
    neverc_uuid_t u;
    fill_random(u.bytes, 16);
    u.bytes[6] = (u.bytes[6] & 0x0F) | 0x40;
    u.bytes[8] = (u.bytes[8] & 0x3F) | 0x80;
    return u;
}

/*
 * Lowercase hex-pair table: hex_pair[b] holds the two hex characters for byte
 * b. One 16-bit store per byte replaces two nibble lookups plus two byte
 * stores, and the four dash slots are written directly instead of being
 * branched on once per byte.
 */
static const char hex_pair[256][2] NEVERC_HEX_PAIR_ATTR = {
    "00","01","02","03","04","05","06","07","08","09","0a","0b","0c","0d","0e","0f",
    "10","11","12","13","14","15","16","17","18","19","1a","1b","1c","1d","1e","1f",
    "20","21","22","23","24","25","26","27","28","29","2a","2b","2c","2d","2e","2f",
    "30","31","32","33","34","35","36","37","38","39","3a","3b","3c","3d","3e","3f",
    "40","41","42","43","44","45","46","47","48","49","4a","4b","4c","4d","4e","4f",
    "50","51","52","53","54","55","56","57","58","59","5a","5b","5c","5d","5e","5f",
    "60","61","62","63","64","65","66","67","68","69","6a","6b","6c","6d","6e","6f",
    "70","71","72","73","74","75","76","77","78","79","7a","7b","7c","7d","7e","7f",
    "80","81","82","83","84","85","86","87","88","89","8a","8b","8c","8d","8e","8f",
    "90","91","92","93","94","95","96","97","98","99","9a","9b","9c","9d","9e","9f",
    "a0","a1","a2","a3","a4","a5","a6","a7","a8","a9","aa","ab","ac","ad","ae","af",
    "b0","b1","b2","b3","b4","b5","b6","b7","b8","b9","ba","bb","bc","bd","be","bf",
    "c0","c1","c2","c3","c4","c5","c6","c7","c8","c9","ca","cb","cc","cd","ce","cf",
    "d0","d1","d2","d3","d4","d5","d6","d7","d8","d9","da","db","dc","dd","de","df",
    "e0","e1","e2","e3","e4","e5","e6","e7","e8","e9","ea","eb","ec","ed","ee","ef",
    "f0","f1","f2","f3","f4","f5","f6","f7","f8","f9","fa","fb","fc","fd","fe","ff",
};

/*
 * Reverse table: a hex digit maps to its 0-15 value, every other byte maps to
 * 0xff. ORing the looked-up nibbles lets a single (acc & 0xf0) test reject any
 * invalid character at the end of the loop instead of branching per digit.
 */
static const uint8_t reverse_hex[256] = {
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07, 0x08,0x09,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
};

/*
 * Offset of each of the 16 bytes within the 36-char canonical form; the dashes
 * occupy the fixed slots 8/13/18/23 between groups. The same offsets drive both
 * formatting (where to write a pair) and parsing (where to read one).
 */
static const uint8_t byte_off[16] = {
    0, 2, 4, 6, 9, 11, 14, 16, 19, 21, 24, 26, 28, 30, 32, 34,
};

void neverc_uuid_to_string(neverc_uuid_t u, char out[37]) {
    out[8] = out[13] = out[18] = out[23] = '-';
    for (int i = 0; i < 16; i++)
        memcpy(out + byte_off[i], hex_pair[u.bytes[i]], 2);
    out[36] = '\0';
}

int neverc_uuid_parse(const char *s, neverc_uuid_t *out) {
    if (strlen(s) != 36) return -1;
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
        return -1;

    const uint8_t *p = (const uint8_t *)s;
    uint8_t bad = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t hi = reverse_hex[p[byte_off[i]]];
        uint8_t lo = reverse_hex[p[byte_off[i] + 1]];
        bad |= (uint8_t)(hi | lo);
        out->bytes[i] = (uint8_t)((hi << 4) | lo);
    }
    if (bad & 0xf0) return -1;
    return 0;
}

int neverc_uuid_equal(neverc_uuid_t a, neverc_uuid_t b) {
    return memcmp(a.bytes, b.bytes, 16) == 0;
}

int neverc_uuid_is_nil(neverc_uuid_t u) {
    for (int i = 0; i < 16; i++)
        if (u.bytes[i] != 0) return 0;
    return 1;
}

int neverc_uuid_version(neverc_uuid_t u) {
    return (u.bytes[6] >> 4) & 0x0F;
}

int neverc_uuid_variant(neverc_uuid_t u) {
    uint8_t b = u.bytes[8];
    if ((b & 0x80) == 0) return 0;
    if ((b & 0xC0) == 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    return 3;
}

neverc_uuid_t neverc_uuid_nil(void) {
    neverc_uuid_t u;
    memset(u.bytes, 0, 16);
    return u;
}
