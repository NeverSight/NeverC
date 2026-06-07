#ifndef NEVERC_TEXT_SCANNER_H
#define NEVERC_TEXT_SCANNER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_SCANNER_EOF       (-1)
#define NEVERC_SCANNER_IDENT     (-2)
#define NEVERC_SCANNER_INT       (-3)
#define NEVERC_SCANNER_FLOAT     (-4)
#define NEVERC_SCANNER_CHAR      (-5)
#define NEVERC_SCANNER_STRING    (-6)
#define NEVERC_SCANNER_RAWSTRING (-7)
#define NEVERC_SCANNER_COMMENT   (-8)

#define NEVERC_SCAN_IDENTS     (1 << 2)
#define NEVERC_SCAN_INTS       (1 << 3)
#define NEVERC_SCAN_FLOATS     (1 << 4)
#define NEVERC_SCAN_CHARS      (1 << 5)
#define NEVERC_SCAN_STRINGS    (1 << 6)
#define NEVERC_SCAN_RAWSTRINGS (1 << 7)
#define NEVERC_SCAN_COMMENTS   (1 << 8)
#define NEVERC_SCAN_SKIP_COMMENTS (1 << 9)

#define NEVERC_SCAN_GO_TOKENS \
    (NEVERC_SCAN_IDENTS | NEVERC_SCAN_FLOATS | NEVERC_SCAN_CHARS | \
     NEVERC_SCAN_STRINGS | NEVERC_SCAN_RAWSTRINGS | \
     NEVERC_SCAN_COMMENTS | NEVERC_SCAN_SKIP_COMMENTS)

#define NEVERC_SCAN_C_TOKENS \
    (NEVERC_SCAN_IDENTS | NEVERC_SCAN_FLOATS | NEVERC_SCAN_CHARS | \
     NEVERC_SCAN_STRINGS | NEVERC_SCAN_COMMENTS | NEVERC_SCAN_SKIP_COMMENTS)

typedef struct {
    int    line;
    int    column;
    int    offset;
} neverc_scanner_pos_t;

typedef struct {
    const char *src;
    size_t      src_len;
    size_t      pos;
    unsigned    mode;

    neverc_scanner_pos_t tok_pos;
    int                  tok_type;
    char                 tok_buf[4096];
    size_t               tok_len;

    int line;
    int col;
} neverc_scanner_t;

void neverc_scanner_init(neverc_scanner_t *s, const char *src, size_t len);
void neverc_scanner_set_mode(neverc_scanner_t *s, unsigned mode);
int  neverc_scanner_scan(neverc_scanner_t *s);
const char *neverc_scanner_token_text(const neverc_scanner_t *s, size_t *len);
neverc_scanner_pos_t neverc_scanner_position(const neverc_scanner_t *s);
int  neverc_scanner_peek(neverc_scanner_t *s);
const char *neverc_scanner_token_name(int tok);

#ifdef __cplusplus
}
#endif

#endif
