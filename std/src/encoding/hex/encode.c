#include "neverc/std/encoding/hex.h"

static const char hextable[] = "0123456789abcdef";

size_t neverc_hex_encoded_len(size_t n) {
    return n * 2;
}

size_t neverc_hex_encode(char *dst, const uint8_t *src, size_t src_len) {
    size_t j = 0;
    for (size_t i = 0; i < src_len; i++) {
        dst[j]     = hextable[src[i] >> 4];
        dst[j + 1] = hextable[src[i] & 0x0f];
        j += 2;
    }
    dst[j] = '\0';
    return src_len * 2;
}
