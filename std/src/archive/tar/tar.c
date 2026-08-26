#include "neverc/std/archive/tar.h"
#include "neverc/std/io/fs.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int tar_path_is_safe(const char *name, size_t capacity) {
    if (!name || !name[0]) return 0;
    size_t len = 0;
    while (len < capacity && name[len] != '\0') len++;
    if (len == capacity || capacity > sizeof(((neverc_tar_header_v2_t *)0)->name))
        return 0;
    char trimmed[sizeof(((neverc_tar_header_v2_t *)0)->name)];
    memcpy(trimmed, name, len + 1);
    while (len > 0 && trimmed[len - 1] == '/')
        trimmed[--len] = '\0';
    if (len == 0 || strcmp(trimmed, ".") == 0) return 0;
    if (strchr(trimmed, ':') != NULL) return 0;
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

/* POSIX sums unsigned bytes; historical Sun tar summed signed bytes.
 * Accept either so a valid header is not rejected, and still reject a
 * stored value that matches neither (the CRC-mismatch case for tar). */
static int tar_checksum_matches(const uint8_t *block, uint64_t stored) {
    unsigned int unsigned_sum = 256;
    int signed_sum = 256;
    for (int i = 0; i < 148; i++) {
        int byte = (int)block[i];
        unsigned_sum += (unsigned int)byte;
        signed_sum += byte < 128 ? byte : byte - 256;
    }
    for (int i = 156; i < 512; i++) {
        int byte = (int)block[i];
        unsigned_sum += (unsigned int)byte;
        signed_sum += byte < 128 ? byte : byte - 256;
    }
    if (stored == unsigned_sum) return 1;
    return signed_sum >= 0 && stored == (uint64_t)signed_sum;
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

static int tar_type_supported(int typeflag) {
    return typeflag == NEVERC_TAR_REG ||
           typeflag == NEVERC_TAR_LINK ||
           typeflag == NEVERC_TAR_SYM ||
           typeflag == NEVERC_TAR_DIR;
}

/* Symlinks and directories carry no file body even when size is set. POSIX
 * pax linkdata permits typeflag 1 hard links to carry a real data section. */
static int tar_type_header_only(int typeflag) {
    return typeflag == NEVERC_TAR_SYM ||
           typeflag == NEVERC_TAR_DIR;
}

static int tar_name_has_slash_suffix(const char *name) {
    size_t length = strlen(name);
    return length > 0 && name[length - 1U] == '/';
}

/* TypeRegA (NUL) is REG, or DIR when the final name ends in '/'. POSIX also
 * treats REGTYPE + trailing slash as a directory; both must be header-only. */
static int tar_resolve_typeflag(int typeflag, const char *name) {
    if (typeflag != 0 && typeflag != NEVERC_TAR_REG)
        return typeflag;
    return tar_name_has_slash_suffix(name) ? NEVERC_TAR_DIR : NEVERC_TAR_REG;
}

void neverc_tar_reader_init(neverc_tar_reader_t *r, const uint8_t *data, size_t len) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->data = data;
    r->len = len;
}

/* The released reader has only data/len/pos. The archive is immutable by
 * contract, so entry state can be reconstructed from its beginning without a
 * side table, allocation, hidden pointer, or public-layout change. */
typedef struct {
    size_t data_pos;
    size_t entry_size;
    size_t padded_size;
    size_t entry_read;
    size_t previous_size;
    int entry_active;
    int has_previous;
    int ended;
} tar_reader_state_t;

/* 1 = entry, 0 = two-block terminator, -1 = malformed. */
static int tar_parse_header_at(const neverc_tar_reader_t *r, size_t position,
                               neverc_tar_header_v2_t *hdr,
                               size_t *payload_out, size_t *padded_out) {
    if (!r || !hdr || !payload_out || !padded_out || position > r->len ||
        r->len - position < NEVERC_TAR_BLOCK_SIZE)
        return -1;
    memset(hdr, 0, sizeof(*hdr));
    const uint8_t *block = r->data + position;
    int all_zero = 1;
    for (size_t i = 0; i < NEVERC_TAR_BLOCK_SIZE; i++) {
        if (block[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    if (all_zero) {
        size_t remaining = r->len - position;
        if (remaining < NEVERC_TAR_BLOCK_SIZE * 2U)
            return -1;
        for (size_t i = NEVERC_TAR_BLOCK_SIZE;
             i < NEVERC_TAR_BLOCK_SIZE * 2U; i++) {
            if (block[i] != 0)
                return -1;
        }
        return 0;
    }

    uint64_t stored_checksum = 0;
    uint64_t mode = 0, uid = 0, gid = 0, size = 0, mtime = 0;
    if (parse_octal(block + 148, 8, &stored_checksum) != 0 ||
        !tar_checksum_matches(block, stored_checksum) ||
        parse_octal(block + 100, 8, &mode) != 0 ||
        parse_octal(block + 108, 8, &uid) != 0 ||
        parse_octal(block + 116, 8, &gid) != 0 ||
        parse_octal(block + 124, 12, &size) != 0 ||
        parse_octal(block + 136, 12, &mtime) != 0 ||
        mode > UINT32_MAX || size > INT64_MAX || !tar_size_fits(size) ||
        mtime > INT64_MAX)
        return -1;

    size_t name_length = tar_field_length(block, 100);
    size_t prefix_length = 0;
    /* POSIX ustar is "ustar\0"; GNU is "ustar " and offset 345 is not prefix. */
    if (memcmp(block + 257, "ustar", 5) == 0 && block[262] == '\0')
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
    if (!tar_path_is_safe(hdr->name, sizeof(hdr->name)))
        return -1;

    int typeflag = tar_resolve_typeflag((int)block[156], hdr->name);
    if (!tar_type_supported(typeflag))
        return -1;
    uint64_t payload = tar_type_header_only(typeflag) ? 0 : size;
    size_t padded = 0;
    if (tar_padded_size((size_t)payload, &padded) != 0 ||
        padded > r->len - position - NEVERC_TAR_BLOCK_SIZE)
        return -1;

    hdr->mode = (uint32_t)mode;
    hdr->size = (int64_t)payload;
    hdr->mtime = (int64_t)mtime;
    hdr->typeflag = typeflag;
    copy_tar_field(hdr->linkname, sizeof(hdr->linkname),
                   block + 157, 100);
    if ((hdr->typeflag == NEVERC_TAR_SYM ||
         hdr->typeflag == NEVERC_TAR_LINK) &&
        (hdr->linkname[0] == '\0' ||
         !tar_path_is_safe(hdr->linkname, sizeof(hdr->linkname))))
        return -1;
    copy_tar_field(hdr->uname, sizeof(hdr->uname), block + 265, 32);
    copy_tar_field(hdr->gname, sizeof(hdr->gname), block + 297, 32);

    *payload_out = (size_t)payload;
    *padded_out = padded;
    return 1;
}

static int tar_reader_replay(const neverc_tar_reader_t *r,
                             tar_reader_state_t *state) {
    if (!r || !state || (!r->data && r->len != 0) || r->pos > r->len)
        return -1;
    memset(state, 0, sizeof(*state));
    size_t cursor = 0;
    for (;;) {
        if (cursor == r->pos) return 0;
        neverc_tar_header_v2_t ignored;
        size_t payload = 0, padded = 0;
        int parsed = tar_parse_header_at(r, cursor, &ignored,
                                         &payload, &padded);
        if (parsed < 0) return -1;
        if (parsed == 0) {
            if (r->pos != r->len) return -1;
            state->ended = 1;
            return 0;
        }
        size_t data_pos = cursor + NEVERC_TAR_BLOCK_SIZE;
        size_t padded_end = data_pos + padded;
        if (r->pos >= data_pos && r->pos < data_pos + payload) {
            state->data_pos = data_pos;
            state->entry_size = payload;
            state->padded_size = padded;
            state->entry_read = r->pos - data_pos;
            state->entry_active = 1;
            return 0;
        }
        if (r->pos > data_pos && r->pos < padded_end) return -1;
        if (r->pos < data_pos) return -1;
        state->previous_size = payload;
        state->has_previous = 1;
        cursor = padded_end;
        if (cursor > r->pos) return -1;
    }
}

static int tar_reader_prepare_next(const neverc_tar_reader_t *r,
                                   neverc_tar_header_v2_t *hdr,
                                   size_t *next_position) {
    tar_reader_state_t state;
    if (!r || !hdr || !next_position || tar_reader_replay(r, &state) != 0)
        return -1;
    if (state.ended) {
        *next_position = r->len;
        return 0;
    }
    size_t position = state.entry_active
        ? state.data_pos + state.padded_size : r->pos;
    size_t payload = 0, padded = 0;
    int parsed = tar_parse_header_at(r, position, hdr, &payload, &padded);
    if (parsed < 0) return -1;
    *next_position = parsed == 0
        ? r->len : position + NEVERC_TAR_BLOCK_SIZE;
    return parsed;
}

static int tar_header_v2_to_legacy(const neverc_tar_header_v2_t *source,
                                   neverc_tar_header_t *destination) {
    size_t name_length = strlen(source->name);
    size_t link_length = strlen(source->linkname);
    size_t uname_length = strlen(source->uname);
    size_t gname_length = strlen(source->gname);
    if (name_length >= sizeof(destination->name) ||
        link_length >= sizeof(destination->linkname) ||
        uname_length >= sizeof(destination->uname) ||
        gname_length >= sizeof(destination->gname))
        return -1;
    memset(destination, 0, sizeof(*destination));
    memcpy(destination->name, source->name, name_length + 1U);
    memcpy(destination->linkname, source->linkname, link_length + 1U);
    memcpy(destination->uname, source->uname, uname_length + 1U);
    memcpy(destination->gname, source->gname, gname_length + 1U);
    destination->size = source->size;
    destination->mode = source->mode;
    destination->mtime = source->mtime;
    destination->typeflag = source->typeflag;
    return 0;
}

int neverc_tar_reader_next_v2(neverc_tar_reader_t *r,
                              neverc_tar_header_v2_t *hdr) {
    if (!hdr) return -1;
    memset(hdr, 0, sizeof(*hdr));
    size_t next_position = 0;
    int result = tar_reader_prepare_next(r, hdr, &next_position);
    if (result >= 0) r->pos = next_position;
    return result;
}

int neverc_tar_reader_next(neverc_tar_reader_t *r, neverc_tar_header_t *hdr) {
    if (!hdr) return -1;
    memset(hdr, 0, sizeof(*hdr));
    neverc_tar_header_v2_t parsed;
    size_t next_position = 0;
    int result = tar_reader_prepare_next(r, &parsed, &next_position);
    if (result <= 0) {
        if (result == 0) r->pos = next_position;
        return result;
    }
    if (tar_header_v2_to_legacy(&parsed, hdr) != 0) return -1;
    r->pos = next_position;
    return 1;
}

static int tar_reader_read_size(neverc_tar_reader_t *r, int64_t header_size,
                                uint8_t *buf, size_t len, size_t *nread) {
    if (!nread) return -1;
    *nread = 0;
    if (!r || header_size < 0 || (!buf && len != 0) ||
        !tar_size_fits((uint64_t)header_size))
        return -1;
    tar_reader_state_t state;
    if (tar_reader_replay(r, &state) != 0) return -1;
    if (!state.entry_active) {
        if (header_size == 0 ||
            (state.has_previous &&
             (size_t)header_size == state.previous_size))
            return 0;
        return -1;
    }
    if ((size_t)header_size != state.entry_size ||
        state.entry_read > state.entry_size)
        return -1;
    size_t remaining = state.entry_size - state.entry_read;
    if (remaining == 0) return 0;
    size_t amount = remaining < len ? remaining : len;
    if (amount > r->len - r->pos) return -1;
    if (amount > 0) memcpy(buf, r->data + r->pos, amount);
    r->pos += amount;
    *nread = amount;
    if (state.entry_read + amount == state.entry_size)
        r->pos = state.data_pos + state.padded_size;
    return 0;
}

int neverc_tar_reader_read(neverc_tar_reader_t *r,
                           const neverc_tar_header_t *hdr,
                           uint8_t *buf, size_t len, size_t *nread) {
    if (!hdr) {
        if (nread) *nread = 0;
        return -1;
    }
    return tar_reader_read_size(r, hdr->size, buf, len, nread);
}

int neverc_tar_reader_read_v2(neverc_tar_reader_t *r,
                              const neverc_tar_header_v2_t *hdr,
                              uint8_t *buf, size_t len, size_t *nread) {
    if (!hdr) {
        if (nread) *nread = 0;
        return -1;
    }
    return tar_reader_read_size(r, hdr->size, buf, len, nread);
}

/* Writer */
#define NCI_TAR_WRITER_META_MAGIC UINT32_C(0x54415257)

typedef struct {
    size_t current_size;
    size_t current_written;
    uint32_t magic;
    uint8_t entry_open;
    uint8_t closed;
    uint8_t failed;
} tar_writer_meta_t;

static tar_writer_meta_t tar_writer_meta_default(void) {
    tar_writer_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.magic = NCI_TAR_WRITER_META_MAGIC;
    return meta;
}

/* data+cap need not satisfy tar_writer_meta_t alignment. Keep the allocation
 * trailer as bytes and copy through aligned local objects. */
static int tar_writer_meta_load(const neverc_tar_writer_t *w,
                                tar_writer_meta_t *meta) {
    if (!w || !meta || !w->data ||
        w->cap > SIZE_MAX - sizeof(*meta))
        return 0;
    memcpy(meta, w->data + w->cap, sizeof(*meta));
    return meta->magic == NCI_TAR_WRITER_META_MAGIC;
}

static int tar_writer_meta_store(neverc_tar_writer_t *w,
                                 const tar_writer_meta_t *meta) {
    if (!w || !meta || !w->data ||
        w->cap > SIZE_MAX - sizeof(*meta))
        return 0;
    memcpy(w->data + w->cap, meta, sizeof(*meta));
    return 1;
}

void neverc_tar_writer_init(neverc_tar_writer_t *w) {
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->cap = 4096;
    w->data = w->cap <= SIZE_MAX - sizeof(tar_writer_meta_t)
        ? (uint8_t *)malloc(w->cap + sizeof(tar_writer_meta_t)) : NULL;
    if (!w->data) w->cap = 0;
    else {
        tar_writer_meta_t meta = tar_writer_meta_default();
        (void)tar_writer_meta_store(w, &meta);
    }
}

static int writer_grow(neverc_tar_writer_t *w, size_t need) {
    tar_writer_meta_t meta;
    if (!w || need > SIZE_MAX - w->len ||
        !tar_writer_meta_load(w, &meta))
        return 0;
    size_t required = w->len + need;
    if (required <= w->cap) return 1;
    size_t next = w->cap < 4096 ? 4096 : w->cap;
    while (next < required) {
        if (next > SIZE_MAX / 2) {
            next = required;
            break;
        }
        next *= 2;
    }
    if (next > SIZE_MAX - sizeof(meta)) return 0;
    size_t old_cap = w->cap;
    uint8_t *grown = (uint8_t *)realloc(
        w->data, next + sizeof(meta));
    if (!grown) return 0;
    w->data = grown;
    w->cap = next;
    size_t clear = sizeof(meta);
    if (clear > next - old_cap) clear = next - old_cap;
    memset(grown + old_cap, 0, clear);
    return tar_writer_meta_store(w, &meta);
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

static int tar_writer_write_header_common(neverc_tar_writer_t *w,
                                          const neverc_tar_header_v2_t *hdr) {
    tar_writer_meta_t meta;
    if (!w || !hdr || !tar_writer_meta_load(w, &meta) ||
        meta.closed || meta.failed || meta.entry_open ||
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
    if (!tar_path_is_safe(hdr->name, sizeof(hdr->name)))
        return -1;
    int typeflag = tar_resolve_typeflag(hdr->typeflag, hdr->name);
    if (!tar_type_supported(typeflag) ||
        (tar_type_header_only(typeflag) && hdr->size != 0))
        return -1;
    if ((typeflag == NEVERC_TAR_SYM || typeflag == NEVERC_TAR_LINK) &&
        (link_length == 0 ||
         !tar_path_is_safe(hdr->linkname, sizeof(hdr->linkname))))
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

    block[156] = (uint8_t)typeflag;
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

    /* hdr may alias the writer's allocation. Snapshot every value that is
     * still needed before writer_grow can move that allocation. */
    size_t current_size = (size_t)hdr->size;
    if (!writer_grow(w, NEVERC_TAR_BLOCK_SIZE)) {
        meta.failed = 1;
        (void)tar_writer_meta_store(w, &meta);
        return -1;
    }
    memcpy(w->data + w->len, block, sizeof(block));
    w->len += sizeof(block);
    meta.current_size = current_size;
    meta.current_written = 0;
    meta.entry_open = current_size > 0;
    return tar_writer_meta_store(w, &meta) ? 0 : -1;
}

int neverc_tar_writer_write_header_v2(neverc_tar_writer_t *w,
                                      const neverc_tar_header_v2_t *hdr) {
    return tar_writer_write_header_common(w, hdr);
}

int neverc_tar_writer_write_header(neverc_tar_writer_t *w,
                                   const neverc_tar_header_t *hdr) {
    if (!hdr) return -1;
    neverc_tar_header_v2_t converted;
    memset(&converted, 0, sizeof(converted));
    size_t name_length = 0, link_length = 0;
    size_t uname_length = 0, gname_length = 0;
    if (bounded_string_length(
            hdr->name, sizeof(hdr->name), &name_length) != 0 ||
        bounded_string_length(
            hdr->linkname, sizeof(hdr->linkname), &link_length) != 0 ||
        bounded_string_length(
            hdr->uname, sizeof(hdr->uname), &uname_length) != 0 ||
        bounded_string_length(
            hdr->gname, sizeof(hdr->gname), &gname_length) != 0)
        return -1;
    memcpy(converted.name, hdr->name, name_length + 1U);
    memcpy(converted.linkname, hdr->linkname, link_length + 1U);
    memcpy(converted.uname, hdr->uname, uname_length + 1U);
    memcpy(converted.gname, hdr->gname, gname_length + 1U);
    converted.size = hdr->size;
    converted.mode = hdr->mode;
    converted.mtime = hdr->mtime;
    converted.typeflag = hdr->typeflag;
    return tar_writer_write_header_common(w, &converted);
}

int neverc_tar_writer_write(neverc_tar_writer_t *w,
                            const uint8_t *data, size_t len) {
    tar_writer_meta_t meta;
    if (!w || !tar_writer_meta_load(w, &meta) ||
        meta.closed || meta.failed || (!data && len != 0))
        return -1;
    if (!meta.entry_open) return len == 0 ? 0 : -1;
    if (meta.current_written > meta.current_size ||
        len > meta.current_size - meta.current_written)
        return -1;
    int completes_entry =
        len == meta.current_size - meta.current_written;
    size_t padded = 0;
    if (tar_padded_size(meta.current_size, &padded) != 0) return -1;
    size_t padding = completes_entry ? padded - meta.current_size : 0;
    if (len > SIZE_MAX - padding || !writer_grow(w, len + padding)) {
        meta.failed = 1;
        (void)tar_writer_meta_store(w, &meta);
        return -1;
    }
    if (len > 0) memcpy(w->data + w->len, data, len);
    if (padding > 0)
        memset(w->data + w->len + len, 0, padding);
    w->len += len + padding;
    meta.current_written += len;
    if (completes_entry) meta.entry_open = 0;
    return tar_writer_meta_store(w, &meta) ? 0 : -1;
}

int neverc_tar_writer_close(neverc_tar_writer_t *w) {
    tar_writer_meta_t meta;
    if (!w || !tar_writer_meta_load(w, &meta) ||
        meta.failed || meta.entry_open)
        return -1;
    if (meta.closed) return 0;
    if (!writer_grow(w, NEVERC_TAR_BLOCK_SIZE * 2U)) {
        meta.failed = 1;
        (void)tar_writer_meta_store(w, &meta);
        return -1;
    }
    memset(w->data + w->len, 0, NEVERC_TAR_BLOCK_SIZE * 2);
    w->len += NEVERC_TAR_BLOCK_SIZE * 2;
    meta.closed = 1;
    return tar_writer_meta_store(w, &meta) ? 0 : -1;
}

void neverc_tar_writer_free(neverc_tar_writer_t *w) {
    if (!w) return;
    free(w->data);
    memset(w, 0, sizeof(*w));
}
