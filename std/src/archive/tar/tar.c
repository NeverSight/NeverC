#include "neverc/archive/tar.h"
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
    r->data = data;
    r->len = len;
    r->pos = 0;
}

int neverc_tar_reader_next(neverc_tar_reader_t *r, neverc_tar_header_t *hdr) {
    while (r->pos + NEVERC_TAR_BLOCK_SIZE <= r->len) {
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

        /* ustar prefix */
        if (memcmp(block + 257, "ustar", 5) == 0) {
            char prefix[156];
            size_t plen = 0;
            while (plen < 155 && block[345 + plen]) plen++;
            if (plen > 0) {
                memcpy(prefix, block + 345, plen);
                prefix[plen] = '\0';
                char full[256];
                size_t fi = 0;
                for (size_t i = 0; i < plen; i++) full[fi++] = prefix[i];
                full[fi++] = '/';
                for (size_t i = 0; i < name_len; i++) full[fi++] = hdr->name[i];
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
    size_t avail = (size_t)hdr->size;
    if (avail > len) avail = len;
    if (r->pos + avail > r->len) avail = r->len - r->pos;
    memcpy(buf, r->data + r->pos, avail);
    *nread = avail;

    size_t blocks = ((size_t)hdr->size + NEVERC_TAR_BLOCK_SIZE - 1) / NEVERC_TAR_BLOCK_SIZE;
    r->pos += blocks * NEVERC_TAR_BLOCK_SIZE;
    return 0;
}

/* Writer */
void neverc_tar_writer_init(neverc_tar_writer_t *w) {
    w->cap = 4096;
    w->data = (uint8_t *)malloc(w->cap);
    w->len = 0;
}

static void writer_grow(neverc_tar_writer_t *w, size_t need) {
    while (w->len + need > w->cap) {
        w->cap *= 2;
        w->data = (uint8_t *)realloc(w->data, w->cap);
    }
}

int neverc_tar_writer_write_header(neverc_tar_writer_t *w,
                                   const neverc_tar_header_t *hdr) {
    writer_grow(w, NEVERC_TAR_BLOCK_SIZE);
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
    size_t blocks = (len + NEVERC_TAR_BLOCK_SIZE - 1) / NEVERC_TAR_BLOCK_SIZE;
    size_t padded = blocks * NEVERC_TAR_BLOCK_SIZE;
    writer_grow(w, padded);
    memcpy(w->data + w->len, data, len);
    if (padded > len)
        memset(w->data + w->len + len, 0, padded - len);
    w->len += padded;
    return 0;
}

int neverc_tar_writer_close(neverc_tar_writer_t *w) {
    writer_grow(w, NEVERC_TAR_BLOCK_SIZE * 2);
    memset(w->data + w->len, 0, NEVERC_TAR_BLOCK_SIZE * 2);
    w->len += NEVERC_TAR_BLOCK_SIZE * 2;
    return 0;
}

void neverc_tar_writer_free(neverc_tar_writer_t *w) {
    free(w->data);
    w->data = NULL;
    w->len = w->cap = 0;
}
