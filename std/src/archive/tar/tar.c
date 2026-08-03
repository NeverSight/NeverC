#include "neverc/std/archive/tar.h"
#include <stdlib.h>
#include <string.h>

static int64_t octal_to_int(const char *s, int n) {
    int64_t val = 0;
    for (int i = 0; i < n && s[i] >= '0' && s[i] <= '7'; i++)
        val = val * 8 + (s[i] - '0');
    return val;
}

static void int_to_octal(char *buf, int n, int64_t val) {
    buf[n - 1] = '\0';
    for (int i = n - 2; i >= 0; i--) {
        buf[i] = '0' + (val & 7);
        val >>= 3;
    }
}

static unsigned int checksum(const uint8_t *block) {
    unsigned int sum = 256;
    for (int i = 0; i < 148; i++) sum += block[i];
    for (int i = 156; i < 512; i++) sum += block[i];
    return sum;
}

void neverc_tar_reader_init(neverc_tar_reader_t *r, const uint8_t *data, size_t len) {
    if (!r) return;
    r->data = data;
    r->len = data || len == 0 ? len : 0;
    r->pos = 0;
}

int neverc_tar_reader_next(neverc_tar_reader_t *r, neverc_tar_header_t *hdr) {
    if (!r || !hdr || (!r->data && r->len != 0) || r->pos > r->len) return 0;
    while (r->len - r->pos >= NEVERC_TAR_BLOCK_SIZE) {
        const uint8_t *block = r->data + r->pos;

        int all_zero = 1;
        for (int i = 0; i < NEVERC_TAR_BLOCK_SIZE; i++)
            if (block[i]) { all_zero = 0; break; }
        if (all_zero) { r->pos += NEVERC_TAR_BLOCK_SIZE; continue; }

        memset(hdr, 0, sizeof(*hdr));

        size_t name_len = 0;
        while (name_len < 100 && block[name_len]) name_len++;
        memcpy(hdr->name, block, name_len);
        hdr->name[name_len] = '\0';

        hdr->mode = (uint32_t)octal_to_int((const char *)block + 100, 8);
        hdr->size = octal_to_int((const char *)block + 124, 12);
        hdr->mtime = octal_to_int((const char *)block + 136, 12);
        hdr->typeflag = block[156] ? block[156] : '0';

        size_t link_len = 0;
        while (link_len < 100 && block[157 + link_len]) link_len++;
        memcpy(hdr->linkname, block + 157, link_len);

        /* ustar prefix: full path is prefix + '/' + name. POSIX allows
         * prefix[155] + name[100] = up to 256 chars, which together with the
         * NUL does not fit hdr->name[256]; the previous code also sized `full`
         * at exactly 256 and wrote full[256] / memcpy'd 257 bytes — a stack
         * (and hdr->name) overflow on long-but-valid ustar headers. Bound every
         * write to the buffer and truncate to fit. */
        if (memcmp(block + 257, "ustar", 5) == 0) {
            size_t plen = 0;
            while (plen < 155 && block[345 + plen]) plen++;
            if (plen > 0) {
                char full[256];
                size_t fi = 0;
                for (size_t i = 0; i < plen && fi < sizeof(full) - 1; i++)
                    full[fi++] = (char)block[345 + i];
                if (fi < sizeof(full) - 1) full[fi++] = '/';
                for (size_t i = 0; i < name_len && fi < sizeof(full) - 1; i++)
                    full[fi++] = hdr->name[i];
                full[fi] = '\0';
                memcpy(hdr->name, full, fi + 1);
            }
        }

        r->pos += NEVERC_TAR_BLOCK_SIZE;
        return 1;
    }
    return 0;
}

int neverc_tar_reader_read(neverc_tar_reader_t *r, const neverc_tar_header_t *hdr,
                           uint8_t *buf, size_t len, size_t *nread) {
    if (!nread) return -1;
    *nread = 0;
    if (!r || !hdr || hdr->size < 0 || (!buf && len != 0) ||
        (!r->data && r->len != 0) || r->pos > r->len) return -1;
    /* Guard pos > len up front: otherwise `r->len - r->pos` below underflows to
     * a huge size_t and memcpy runs off the buffer. pos can sit past len after a
     * prior read consumed an oversized entry, so this also hardens callers that
     * don't gate every read on reader_next()'s return. */
    if (r->pos >= r->len) return 0;
    size_t avail = (size_t)hdr->size;
    if (avail > len) avail = len;
    if (r->pos + avail > r->len) avail = r->len - r->pos;
    if (avail > 0) memcpy(buf, r->data + r->pos, avail);
    *nread = avail;

    size_t size = (size_t)hdr->size;
    if (size > SIZE_MAX - (NEVERC_TAR_BLOCK_SIZE - 1)) return -1;
    size_t blocks = (size + NEVERC_TAR_BLOCK_SIZE - 1) / NEVERC_TAR_BLOCK_SIZE;
    if (blocks > SIZE_MAX / NEVERC_TAR_BLOCK_SIZE) return -1;
    size_t advance = blocks * NEVERC_TAR_BLOCK_SIZE;
    r->pos = advance > r->len - r->pos ? r->len : r->pos + advance;
    return 0;
}

/* Writer */
void neverc_tar_writer_init(neverc_tar_writer_t *w) {
    if (!w) return;
    w->cap = 4096;
    w->data = (uint8_t *)malloc(w->cap);
    if (!w->data) w->cap = 0;
    w->len = 0;
}

static int writer_grow(neverc_tar_writer_t *w, size_t need) {
    if (!w || need > SIZE_MAX - w->len) return 0;
    size_t required = w->len + need;
    if (w->data && required <= w->cap) return 1;
    size_t next = w->cap < 4096 ? 4096 : w->cap;
    while (next < required) {
        if (next > SIZE_MAX / 2) {
            next = required;
            break;
        }
        next *= 2;
    }
    uint8_t *grown = (uint8_t *)realloc(w->data, next);
    if (!grown) return 0;
    w->data = grown;
    w->cap = next;
    return 1;
}

int neverc_tar_writer_write_header(neverc_tar_writer_t *w,
                                   const neverc_tar_header_t *hdr) {
    if (!w || !hdr || hdr->size < 0 ||
        !writer_grow(w, NEVERC_TAR_BLOCK_SIZE)) return -1;
    uint8_t *block = w->data + w->len;
    memset(block, 0, NEVERC_TAR_BLOCK_SIZE);

    size_t name_len = strlen(hdr->name);
    if (name_len > 100) name_len = 100;
    memcpy(block, hdr->name, name_len);

    int_to_octal((char *)block + 100, 8, hdr->mode);
    int_to_octal((char *)block + 108, 8, 0);
    int_to_octal((char *)block + 116, 8, 0);
    int_to_octal((char *)block + 124, 12, hdr->size);
    int_to_octal((char *)block + 136, 12, hdr->mtime);

    block[156] = (uint8_t)hdr->typeflag;

    memcpy(block + 257, "ustar", 5);
    block[263] = '0'; block[264] = '0';

    memset(block + 148, ' ', 8);
    unsigned int cksum = checksum(block);
    int_to_octal((char *)block + 148, 7, cksum);
    block[155] = ' ';

    w->len += NEVERC_TAR_BLOCK_SIZE;
    return 0;
}

int neverc_tar_writer_write(neverc_tar_writer_t *w,
                            const uint8_t *data, size_t len) {
    if (!w || (!data && len != 0) ||
        len > SIZE_MAX - (NEVERC_TAR_BLOCK_SIZE - 1)) return -1;
    size_t blocks = (len + NEVERC_TAR_BLOCK_SIZE - 1) / NEVERC_TAR_BLOCK_SIZE;
    if (blocks > SIZE_MAX / NEVERC_TAR_BLOCK_SIZE) return -1;
    size_t padded = blocks * NEVERC_TAR_BLOCK_SIZE;
    if (!writer_grow(w, padded)) return -1;
    if (len > 0) memcpy(w->data + w->len, data, len);
    if (padded > len)
        memset(w->data + w->len + len, 0, padded - len);
    w->len += padded;
    return 0;
}

int neverc_tar_writer_close(neverc_tar_writer_t *w) {
    if (!w || !writer_grow(w, NEVERC_TAR_BLOCK_SIZE * 2)) return -1;
    memset(w->data + w->len, 0, NEVERC_TAR_BLOCK_SIZE * 2);
    w->len += NEVERC_TAR_BLOCK_SIZE * 2;
    return 0;
}

void neverc_tar_writer_free(neverc_tar_writer_t *w) {
    if (!w) return;
    free(w->data);
    w->data = NULL;
    w->len = w->cap = 0;
}
