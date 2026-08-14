#include "neverc/std/archive/tar.h"
#include "neverc/std/io/fs.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int tar_path_is_safe(const char *name) {
    if (!name || !name[0]) return 0;
    size_t len = strlen(name);
    if (len >= sizeof(((neverc_tar_header_t *)0)->name)) return 0;
    char trimmed[sizeof(((neverc_tar_header_t *)0)->name)];
    memcpy(trimmed, name, len + 1);
    while (len > 0 && trimmed[len - 1] == '/')
        trimmed[--len] = '\0';
    if (len == 0) return 0;
    return neverc_fs_valid_path(trimmed);
}

static int parse_octal(const uint8_t *field, size_t width, uint64_t *value) {
    size_t i = 0;
    while (i < width && (field[i] == '\0' || field[i] == ' ')) i++;
    uint64_t result = 0;
    while (i < width && field[i] >= '0' && field[i] <= '7') {
        unsigned digit = (unsigned)(field[i] - '0');
        if (result > (UINT64_MAX - digit) / 8U) return -1;
        result = result * 8U + digit;
        i++;
    }
    while (i < width && (field[i] == '\0' || field[i] == ' ')) i++;
    if (i != width) return -1;
    *value = result;
    return 0;
}

static int write_octal(uint8_t *field, size_t width, uint64_t value) {
    if (width < 2U) return -1;
    size_t digits = width - 1U;
    uint64_t maximum = 0;
    for (size_t i = 0; i < digits; i++)
        maximum = maximum * 8U + 7U;
    if (value > maximum) return -1;
    memset(field, '0', digits);
    field[digits] = '\0';
    for (size_t i = digits; i > 0 && value != 0; i--) {
        field[i - 1U] = (uint8_t)('0' + (value & 7U));
        value >>= 3U;
    }
    return 0;
}

static unsigned int tar_checksum(const uint8_t *block) {
    unsigned int sum = 256;
    for (int i = 0; i < 148; i++) sum += block[i];
    for (int i = 156; i < 512; i++) sum += block[i];
    return sum;
}

static size_t tar_field_length(const uint8_t *field, size_t width) {
    size_t length = 0;
    while (length < width && field[length] != '\0') length++;
    return length;
}

static void copy_tar_field(char *destination, size_t capacity,
                           const uint8_t *field, size_t width) {
    size_t length = tar_field_length(field, width);
    if (length >= capacity) length = capacity - 1U;
    if (length > 0) memcpy(destination, field, length);
    destination[length] = '\0';
}

static int tar_padded_size(size_t size, size_t *padded) {
    if (size > SIZE_MAX - (NEVERC_TAR_BLOCK_SIZE - 1U)) return -1;
    *padded = (size + NEVERC_TAR_BLOCK_SIZE - 1U) &
              ~(size_t)(NEVERC_TAR_BLOCK_SIZE - 1U);
    return 0;
}

static int tar_size_fits(uint64_t value) {
#if SIZE_MAX < UINT64_MAX
    return value <= (uint64_t)SIZE_MAX;
#else
    (void)value;
    return 1;
#endif
}

void neverc_tar_reader_init(neverc_tar_reader_t *r, const uint8_t *data, size_t len) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->data = data;
    r->len = len;
}

static int reader_finish_entry(neverc_tar_reader_t *r) {
    if (!r->entry_active) return 0;
    if (r->entry_read > r->entry_size || r->pos > r->len) return -1;
    size_t padded = 0;
    if (tar_padded_size(r->entry_size, &padded) != 0) return -1;
    size_t unread = r->entry_size - r->entry_read;
    size_t padding = padded - r->entry_size;
    if (unread > r->len - r->pos) return -1;
    r->pos += unread;
    if (padding > r->len - r->pos) return -1;
    r->pos += padding;
    r->entry_read = r->entry_size;
    r->entry_active = 0;
    return 0;
}

int neverc_tar_reader_next(neverc_tar_reader_t *r, neverc_tar_header_t *hdr) {
    if (!r || !hdr)
        return -1;
    memset(hdr, 0, sizeof(*hdr));
    if ((!r->data && r->len != 0) || r->pos > r->len)
        return -1;
    if (r->ended) return 0;
    if (reader_finish_entry(r) != 0) return -1;
    if (r->pos == r->len) return -1;
    if (r->len - r->pos < NEVERC_TAR_BLOCK_SIZE) return -1;

    const uint8_t *block = r->data + r->pos;
    int all_zero = 1;
    for (size_t i = 0; i < NEVERC_TAR_BLOCK_SIZE; i++) {
        if (block[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    if (all_zero) {
        size_t remaining = r->len - r->pos;
        if (remaining < NEVERC_TAR_BLOCK_SIZE * 2U ||
            remaining % NEVERC_TAR_BLOCK_SIZE != 0)
            return -1;
        for (size_t i = NEVERC_TAR_BLOCK_SIZE; i < remaining; i++) {
            if (block[i] != 0)
                return -1;
        }
        r->pos = r->len;
        r->ended = 1;
        return 0;
    }

    uint64_t stored_checksum = 0;
    uint64_t mode = 0, uid = 0, gid = 0, size = 0, mtime = 0;
    if (parse_octal(block + 148, 8, &stored_checksum) != 0 ||
        stored_checksum != tar_checksum(block) ||
        parse_octal(block + 100, 8, &mode) != 0 ||
        parse_octal(block + 108, 8, &uid) != 0 ||
        parse_octal(block + 116, 8, &gid) != 0 ||
        parse_octal(block + 124, 12, &size) != 0 ||
        parse_octal(block + 136, 12, &mtime) != 0 ||
        mode > UINT32_MAX || size > INT64_MAX || !tar_size_fits(size) ||
        mtime > INT64_MAX)
        return -1;

    size_t padded = 0;
    if (tar_padded_size((size_t)size, &padded) != 0 ||
        padded > r->len - r->pos - NEVERC_TAR_BLOCK_SIZE)
        return -1;

    size_t name_length = tar_field_length(block, 100);
    size_t prefix_length = 0;
    if (memcmp(block + 257, "ustar", 5) == 0)
        prefix_length = tar_field_length(block + 345, 155);
    size_t full_length = name_length;
    if (prefix_length > 0) {
        if (prefix_length > SIZE_MAX - name_length - 1U) return -1;
        full_length = prefix_length + 1U + name_length;
    }
    if (name_length == 0 || full_length >= sizeof(hdr->name)) return -1;

    memset(hdr, 0, sizeof(*hdr));
    size_t offset = 0;
    if (prefix_length > 0) {
        memcpy(hdr->name, block + 345, prefix_length);
        offset = prefix_length;
        hdr->name[offset++] = '/';
    }
    memcpy(hdr->name + offset, block, name_length);
    hdr->name[full_length] = '\0';
    if (!tar_path_is_safe(hdr->name))
        return -1;
    hdr->mode = (uint32_t)mode;
    hdr->size = (int64_t)size;
    hdr->mtime = (int64_t)mtime;
    hdr->typeflag = block[156] ? block[156] : NEVERC_TAR_REG;
    copy_tar_field(hdr->linkname, sizeof(hdr->linkname),
                   block + 157, 100);
    if ((hdr->typeflag == NEVERC_TAR_SYM ||
         hdr->typeflag == NEVERC_TAR_LINK) &&
        hdr->linkname[0] != '\0' &&
        !tar_path_is_safe(hdr->linkname))
        return -1;
    copy_tar_field(hdr->uname, sizeof(hdr->uname), block + 265, 32);
    copy_tar_field(hdr->gname, sizeof(hdr->gname), block + 297, 32);

    r->pos += NEVERC_TAR_BLOCK_SIZE;
    r->entry_size = (size_t)size;
    r->entry_read = 0;
    r->entry_active = size > 0;
    return 1;
}

int neverc_tar_reader_read(neverc_tar_reader_t *r, const neverc_tar_header_t *hdr,
                           uint8_t *buf, size_t len, size_t *nread) {
    if (!nread) return -1;
    *nread = 0;
    if (!r || !hdr || hdr->size < 0 || (!buf && len != 0) ||
        !tar_size_fits((uint64_t)hdr->size) ||
        (!r->data && r->len != 0) || r->pos > r->len ||
        (size_t)hdr->size != r->entry_size ||
        r->entry_read > r->entry_size)
        return -1;
    size_t remaining = r->entry_size - r->entry_read;
    if (remaining == 0) return 0;
    size_t amount = remaining < len ? remaining : len;
    if (amount > r->len - r->pos) return -1;
    if (amount > 0) memcpy(buf, r->data + r->pos, amount);
    r->pos += amount;
    r->entry_read += amount;
    *nread = amount;
    if (r->entry_read == r->entry_size) {
        size_t padded = 0;
        if (tar_padded_size(r->entry_size, &padded) != 0) return -1;
        size_t padding = padded - r->entry_size;
        if (padding > r->len - r->pos) return -1;
        r->pos += padding;
        r->entry_active = 0;
    }
    return 0;
}

/* Writer */
void neverc_tar_writer_init(neverc_tar_writer_t *w) {
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->cap = 4096;
    w->data = (uint8_t *)malloc(w->cap);
    if (!w->data) w->cap = 0;
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

static int bounded_string_length(const char *string, size_t capacity,
                                 size_t *length) {
    for (size_t i = 0; i < capacity; i++) {
        if (string[i] == '\0') {
            *length = i;
            return 0;
        }
    }
    return -1;
}

static int split_ustar_name(const char *name, size_t length,
                            uint8_t *name_field, uint8_t *prefix_field) {
    if (length <= 100U) {
        memcpy(name_field, name, length);
        return 0;
    }
    for (size_t slash = length; slash > 0; slash--) {
        if (name[slash - 1U] != '/') continue;
        size_t prefix_length = slash - 1U;
        size_t suffix_length = length - slash;
        if (prefix_length > 0 && prefix_length <= 155U &&
            suffix_length > 0 && suffix_length <= 100U) {
            memcpy(prefix_field, name, prefix_length);
            memcpy(name_field, name + slash, suffix_length);
            return 0;
        }
    }
    return -1;
}

int neverc_tar_writer_write_header(neverc_tar_writer_t *w,
                                   const neverc_tar_header_t *hdr) {
    if (!w || !hdr || w->closed || w->failed || w->entry_open ||
        hdr->size < 0 || hdr->mtime < 0 ||
        !tar_size_fits((uint64_t)hdr->size) ||
        hdr->typeflag < 0 || hdr->typeflag > UCHAR_MAX)
        return -1;

    size_t name_length = 0, link_length = 0;
    size_t uname_length = 0, gname_length = 0;
    if (bounded_string_length(
            hdr->name, sizeof(hdr->name), &name_length) != 0 ||
        name_length == 0 ||
        bounded_string_length(
            hdr->linkname, sizeof(hdr->linkname), &link_length) != 0 ||
        bounded_string_length(
            hdr->uname, sizeof(hdr->uname), &uname_length) != 0 ||
        bounded_string_length(
            hdr->gname, sizeof(hdr->gname), &gname_length) != 0)
        return -1;
    if (!tar_path_is_safe(hdr->name))
        return -1;
    if ((hdr->typeflag == NEVERC_TAR_SYM || hdr->typeflag == NEVERC_TAR_LINK) &&
        (link_length == 0 || !tar_path_is_safe(hdr->linkname)))
        return -1;

    uint8_t block[NEVERC_TAR_BLOCK_SIZE] = {0};
    if (split_ustar_name(
            hdr->name, name_length, block, block + 345) != 0 ||
        write_octal(block + 100, 8, hdr->mode) != 0 ||
        write_octal(block + 108, 8, 0) != 0 ||
        write_octal(block + 116, 8, 0) != 0 ||
        write_octal(block + 124, 12, (uint64_t)hdr->size) != 0 ||
        write_octal(block + 136, 12, (uint64_t)hdr->mtime) != 0)
        return -1;

    block[156] = (uint8_t)(hdr->typeflag
        ? hdr->typeflag : NEVERC_TAR_REG);
    memcpy(block + 157, hdr->linkname, link_length);
    memcpy(block + 257, "ustar", 5);
    block[263] = '0';
    block[264] = '0';
    memcpy(block + 265, hdr->uname, uname_length);
    memcpy(block + 297, hdr->gname, gname_length);

    memset(block + 148, ' ', 8);
    unsigned int block_checksum = tar_checksum(block);
    if (write_octal(block + 148, 7, block_checksum) != 0) return -1;
    block[155] = ' ';

    if (!writer_grow(w, NEVERC_TAR_BLOCK_SIZE)) {
        w->failed = 1;
        return -1;
    }
    memcpy(w->data + w->len, block, sizeof(block));
    w->len += sizeof(block);
    w->current_size = (size_t)hdr->size;
    w->current_written = 0;
    w->entry_open = hdr->size > 0;
    return 0;
}

int neverc_tar_writer_write(neverc_tar_writer_t *w,
                            const uint8_t *data, size_t len) {
    if (!w || w->closed || w->failed || (!data && len != 0))
        return -1;
    if (!w->entry_open) return len == 0 ? 0 : -1;
    if (w->current_written > w->current_size ||
        len > w->current_size - w->current_written)
        return -1;
    int completes_entry =
        len == w->current_size - w->current_written;
    size_t padded = 0;
    if (tar_padded_size(w->current_size, &padded) != 0) return -1;
    size_t padding = completes_entry ? padded - w->current_size : 0;
    if (len > SIZE_MAX - padding || !writer_grow(w, len + padding)) {
        w->failed = 1;
        return -1;
    }
    if (len > 0) memcpy(w->data + w->len, data, len);
    if (padding > 0)
        memset(w->data + w->len + len, 0, padding);
    w->len += len + padding;
    w->current_written += len;
    if (completes_entry) w->entry_open = 0;
    return 0;
}

int neverc_tar_writer_close(neverc_tar_writer_t *w) {
    if (!w || w->failed || w->entry_open) return -1;
    if (w->closed) return 0;
    if (!writer_grow(w, NEVERC_TAR_BLOCK_SIZE * 2U)) {
        w->failed = 1;
        return -1;
    }
    memset(w->data + w->len, 0, NEVERC_TAR_BLOCK_SIZE * 2);
    w->len += NEVERC_TAR_BLOCK_SIZE * 2;
    w->closed = 1;
    return 0;
}

void neverc_tar_writer_free(neverc_tar_writer_t *w) {
    if (!w) return;
    free(w->data);
    memset(w, 0, sizeof(*w));
}
