#include "neverc/std/uuid.h"
#include "neverc/std/_platform.h"
#include <string.h>

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

static const char hex[] = "0123456789abcdef";

void neverc_uuid_to_string(neverc_uuid_t u, char out[37]) {
    int p = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[p++] = '-';
        out[p++] = hex[u.bytes[i] >> 4];
        out[p++] = hex[u.bytes[i] & 0x0F];
    }
    out[p] = '\0';
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int neverc_uuid_parse(const char *s, neverc_uuid_t *out) {
    if (strlen(s) != 36) return -1;
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-')
        return -1;

    int bi = 0;
    for (int i = 0; i < 36; i++) {
        if (s[i] == '-') continue;
        int hi = hex_val(s[i]);
        int lo = hex_val(s[i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out->bytes[bi++] = (uint8_t)((hi << 4) | lo);
        i++;
    }
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
