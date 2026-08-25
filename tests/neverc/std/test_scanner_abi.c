#include "neverc/std/text/scanner.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *src;
    size_t src_len;
    size_t pos;
    unsigned mode;
    neverc_scanner_pos_t tok_pos;
    int tok_type;
    char tok_buf[4096];
    size_t tok_len;
    int line;
    int col;
} v3389_scanner_t;

#define ABI_FIELD(field)                                                   \
    _Static_assert(offsetof(neverc_scanner_t, field) ==                    \
                       offsetof(v3389_scanner_t, field),                   \
                   "neverc_scanner_t." #field " v3389 offset changed")

_Static_assert(sizeof(neverc_scanner_t) == sizeof(v3389_scanner_t),
               "neverc_scanner_t v3389 size changed");
_Static_assert(_Alignof(neverc_scanner_t) == _Alignof(v3389_scanner_t),
               "neverc_scanner_t v3389 alignment changed");
ABI_FIELD(src);
ABI_FIELD(src_len);
ABI_FIELD(pos);
ABI_FIELD(mode);
ABI_FIELD(tok_pos);
ABI_FIELD(tok_type);
ABI_FIELD(tok_buf);
ABI_FIELD(tok_len);
ABI_FIELD(line);
ABI_FIELD(col);

#undef ABI_FIELD

static int canary_ok(const unsigned char *canary, size_t size) {
    for (size_t i = 0; i < size; i++)
        if (canary[i] != 0xa5) return 0;
    return 1;
}

int main(void) {
    struct {
        neverc_scanner_t scanner;
        unsigned char canary[32];
    } wrapped;
    char long_token[5000];
    memset(&wrapped, 0, sizeof(wrapped));
    memset(wrapped.canary, 0xa5, sizeof(wrapped.canary));
    memset(long_token, 'a', sizeof(long_token));

    neverc_scanner_init(&wrapped.scanner, long_token, sizeof(long_token));
    if (neverc_scanner_scan(&wrapped.scanner) != NEVERC_SCANNER_IDENT ||
        neverc_scanner_error_count(&wrapped.scanner) != 1 ||
        !canary_ok(wrapped.canary, sizeof(wrapped.canary)))
        return 1;

    const char invalid[] = {(char)0xff};
    neverc_scanner_init(&wrapped.scanner, invalid, sizeof(invalid));
    neverc_scanner_set_mode(&wrapped.scanner, 0);
    if (neverc_scanner_scan(&wrapped.scanner) != 0xfffd ||
        neverc_scanner_error_count(&wrapped.scanner) != 1 ||
        !canary_ok(wrapped.canary, sizeof(wrapped.canary)))
        return 1;

    puts("passed");
    return 0;
}
