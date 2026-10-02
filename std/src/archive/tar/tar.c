#include "neverc/std/archive/tar.h"
#include "neverc/std/io/fs.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* No name or link target longer than the v3 field is read or written. */
#define TAR_PATH_LIMIT ((size_t)NEVERC_TAR_V3_NAME_SIZE)

/* name holds len bytes followed by a NUL. */
static int tar_path_is_safe(const char *name, size_t len,
                            int allow_root_directory) {
    if (!name || len == 0 || len >= TAR_PATH_LIMIT) return 0;
    if (allow_root_directory && len == 2U &&
        name[0] == '.' && name[1] == '/')
        return 1;
    while (len > 0 && name[len - 1U] == '/') len--;
    if (len == 0 || memchr(name, ':', len) != NULL) return 0;

    /* Dot components are lexical no-ops and cannot escape an extraction
     * root. Validate a dot-free spelling while preserving the archive's
     * original name bytes for interoperability. */
    char normalized[TAR_PATH_LIMIT];
    size_t input = 0;
    size_t output = 0;
    while (input < len) {
        size_t start = input;
        while (input < len && name[input] != '/') input++;
        size_t component_len = input - start;
        if (component_len == 0) return 0;
        if (!(component_len == 1U && name[start] == '.')) {
            if (output > 0) normalized[output++] = '/';
            memcpy(normalized + output, name + start, component_len);
            output += component_len;
        }
        if (input < len) input++;
    }
    if (output == 0) return 0;
    normalized[output] = '\0';
    return neverc_fs_valid_path(normalized);
}

/* Trim leading and trailing spaces/NULs, then parse the digits before the
 * first remaining NUL; bytes after that NUL are ignored, as in Go. */
static int parse_octal(const uint8_t *field, size_t width, uint64_t *value) {
    size_t start = 0, end = width;
    while (start < end && (field[start] == '\0' || field[start] == ' '))
        start++;
    while (end > start && (field[end - 1U] == '\0' || field[end - 1U] == ' '))
        end--;
    for (size_t i = start; i < end; i++) {
        if (field[i] == '\0') {
            end = i;
            break;
        }
    }
    uint64_t result = 0;
    for (size_t i = start; i < end; i++) {
        if (field[i] < '0' || field[i] > '7') return -1;
        unsigned digit = (unsigned)(field[i] - '0');
        if (result > (UINT64_MAX - digit) / 8U) return -1;
        result = result * 8U + digit;
    }
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

static int tar_padded_size(size_t size, size_t *padded) {
    if (size > SIZE_MAX - (NEVERC_TAR_BLOCK_SIZE - 1U)) return -1;
    if (padded)
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

/* Header layouts are told apart by magic: v7 has none, POSIX ustar uses
 * "ustar\0" (star adds a "tar\0" trailer and a 131-byte prefix followed by
 * atime/ctime), and GNU uses "ustar " with version " \0". Only the ustar,
 * star, and GNU layouts carry owner names. */
enum {
    TAR_FORMAT_V7,
    TAR_FORMAT_USTAR,
    TAR_FORMAT_STAR,
    TAR_FORMAT_GNU
};

static int tar_header_format(const uint8_t *block) {
    if (memcmp(block + 257, "ustar\0", 6) == 0)
        return memcmp(block + 508, "tar\0", 4) == 0 ? TAR_FORMAT_STAR
                                                    : TAR_FORMAT_USTAR;
    if (memcmp(block + 257, "ustar ", 6) == 0 &&
        memcmp(block + 263, " \0", 2) == 0)
        return TAR_FORMAT_GNU;
    return TAR_FORMAT_V7;
}

void neverc_tar_reader_init(neverc_tar_reader_t *r, const uint8_t *data, size_t len) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->data = data;
    r->len = len;
}

/* The released reader has only data/len/pos, so it is kept as a cursor that
 * never revisits earlier headers: data/len cover the unconsumed archive and
 * pos counts the payload bytes of the current entry that remain at data.
 * Every accept/reject decision is made in whole blocks, so len is trimmed to
 * whole blocks at a header boundary. From then on data + len is a block
 * boundary, which makes len % 512 the distance from data to the next one and
 * yields the padding that follows a payload without the entry's size. */

/* A string made of archive bytes: head, then '/' and tail when joined (a
 * ustar prefix and name). The bytes never contain NUL. */
typedef struct {
    const uint8_t *head;
    size_t head_len;
    const uint8_t *tail;
    size_t tail_len;
    int joined;
} tar_text_t;

static void tar_text_set(tar_text_t *text, const uint8_t *bytes,
                         size_t length) {
    text->head = bytes;
    text->head_len = length;
    text->tail = NULL;
    text->tail_len = 0;
    text->joined = 0;
}

static size_t tar_text_length(const tar_text_t *text) {
    return text->head_len + (text->joined ? 1U + text->tail_len : 0U);
}

static int tar_text_has_slash_suffix(const tar_text_t *text) {
    if (text->joined)
        return text->tail_len == 0 || text->tail[text->tail_len - 1U] == '/';
    return text->head_len > 0 && text->head[text->head_len - 1U] == '/';
}

/* Copies text and its NUL terminator into destination[capacity]. */
static int tar_text_copy(char *destination, size_t capacity,
                         const tar_text_t *text, size_t *length_out) {
    size_t length = tar_text_length(text);
    if (length >= capacity) return -1;
    size_t offset = 0;
    if (text->head_len > 0) {
        memcpy(destination, text->head, text->head_len);
        offset = text->head_len;
    }
    if (text->joined) {
        destination[offset++] = '/';
        if (text->tail_len > 0) {
            memcpy(destination + offset, text->tail, text->tail_len);
            offset += text->tail_len;
        }
    }
    destination[offset] = '\0';
    if (length_out) *length_out = length;
    return 0;
}

static int tar_text_equals(const uint8_t *bytes, size_t length,
                           const char *literal) {
    size_t literal_length = strlen(literal);
    return length == literal_length &&
           (length == 0 || memcmp(bytes, literal, length) == 0);
}

/* Mirrors Go's strconv.ParseInt(s, 10, 64): an optional sign, then one or
 * more decimal digits, within int64 range. */
static int tar_parse_decimal(const uint8_t *text, size_t length,
                             int64_t *value) {
    size_t i = 0;
    int negative = 0;
    if (length > 0 && (text[0] == '+' || text[0] == '-')) {
        negative = text[0] == '-';
        i = 1;
    }
    if (i == length) return -1;
    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1U : (uint64_t)INT64_MAX;
    uint64_t result = 0;
    for (; i < length; i++) {
        if (text[i] < '0' || text[i] > '9') return -1;
        unsigned digit = (unsigned)(text[i] - '0');
        if (result > (limit - digit) / 10U) return -1;
        result = result * 10U + digit;
    }
    if (!negative)
        *value = (int64_t)result;
    else if (result == (uint64_t)INT64_MAX + 1U)
        *value = INT64_MIN;
    else
        *value = -(int64_t)result;
    return 0;
}

/* Pax times are "%d" or "%d.%d" with any number of fraction digits, of which
 * the first nine are nanoseconds. A negative time subtracts its fraction,
 * which is normalized to floor seconds plus nanoseconds in [0, 1e9). */
static int tar_parse_pax_time(const uint8_t *text, size_t length,
                              int64_t *seconds, int32_t *nanoseconds) {
    size_t dot = 0;
    while (dot < length && text[dot] != '.') dot++;
    int64_t whole = 0;
    if (tar_parse_decimal(text, dot, &whole) != 0) return -1;
    int32_t fraction = 0;
    size_t digits = 0;
    for (size_t i = dot + 1U; i < length; i++, digits++) {
        if (text[i] < '0' || text[i] > '9') return -1;
        if (digits < 9U) fraction = fraction * 10 + (int32_t)(text[i] - '0');
    }
    for (; digits < 9U; digits++) fraction *= 10;
    if (text[0] == '-' && fraction != 0) {
        /* Floor of INT64_MIN minus a fraction is not representable. */
        if (whole == INT64_MIN) return -1;
        whole -= 1;
        fraction = 1000000000 - fraction;
    }
    *seconds = whole;
    *nanoseconds = fraction;
    return 0;
}

/* Numeric header fields are octal, or GNU base-256 when the first byte has
 * its high bit set: two's complement big-endian with bit 6 of the first byte
 * as the sign. Values must fit in int64, as in Go. */
static int tar_parse_numeric(const uint8_t *field, size_t width,
                             int64_t *value) {
    if (width > 0 && (field[0] & 0x80U) != 0) {
        uint8_t invert = (field[0] & 0x40U) != 0 ? 0xFFU : 0x00U;
        uint64_t result = 0;
        for (size_t i = 0; i < width; i++) {
            uint8_t byte = (uint8_t)(field[i] ^ invert);
            if (i == 0) byte &= 0x7FU;
            if ((result >> 56) != 0) return -1;
            result = (result << 8) | byte;
        }
        if ((result >> 63) != 0) return -1;
        *value = invert ? -(int64_t)result - 1 : (int64_t)result;
        return 0;
    }
    uint64_t octal = 0;
    if (parse_octal(field, width, &octal) != 0 || octal > INT64_MAX)
        return -1;
    *value = (int64_t)octal;
    return 0;
}

static int tar_block_is_zero(const uint8_t *block) {
    for (size_t i = 0; i < NEVERC_TAR_BLOCK_SIZE; i++)
        if (block[i] != 0) return 0;
    return 1;
}

/* One header block's fields, before pax or GNU metadata is applied. */
typedef struct {
    int typeflag;
    int64_t size, mode, uid, gid, mtime, atime, ctime;
    tar_text_t name, linkname, uname, gname;
} tar_block_t;

static int tar_parse_block(const uint8_t *block, tar_block_t *parsed) {
    memset(parsed, 0, sizeof(*parsed));
    uint64_t stored_checksum = 0;
    if (parse_octal(block + 148, 8, &stored_checksum) != 0 ||
        !tar_checksum_matches(block, stored_checksum))
        return -1;
    int format = tar_header_format(block);
    if (tar_parse_numeric(block + 100, 8, &parsed->mode) != 0 ||
        tar_parse_numeric(block + 108, 8, &parsed->uid) != 0 ||
        tar_parse_numeric(block + 116, 8, &parsed->gid) != 0 ||
        tar_parse_numeric(block + 124, 12, &parsed->size) != 0 ||
        tar_parse_numeric(block + 136, 12, &parsed->mtime) != 0)
        return -1;
    parsed->typeflag = (int)block[156];
    tar_text_set(&parsed->name, block, tar_field_length(block, 100));
    tar_text_set(&parsed->linkname, block + 157,
                 tar_field_length(block + 157, 100));
    if (format == TAR_FORMAT_V7) return 0;

    tar_text_set(&parsed->uname, block + 265,
                 tar_field_length(block + 265, 32));
    tar_text_set(&parsed->gname, block + 297,
                 tar_field_length(block + 297, 32));
    int64_t device = 0;
    if (tar_parse_numeric(block + 329, 8, &device) != 0 ||
        tar_parse_numeric(block + 337, 8, &device) != 0)
        return -1;
    size_t prefix_length = 0;
    if (format == TAR_FORMAT_STAR) {
        if (tar_parse_numeric(block + 476, 12, &parsed->atime) != 0 ||
            tar_parse_numeric(block + 488, 12, &parsed->ctime) != 0)
            return -1;
        prefix_length = tar_field_length(block + 345, 131);
    } else if (format == TAR_FORMAT_USTAR) {
        prefix_length = tar_field_length(block + 345, 155);
    } else {
        /* GNU atime/ctime are optional; an unparsable pair is dropped. */
        int64_t atime = 0, ctime = 0;
        if ((block[345] == 0 ||
             tar_parse_numeric(block + 345, 12, &atime) == 0) &&
            (block[357] == 0 ||
             tar_parse_numeric(block + 357, 12, &ctime) == 0)) {
            parsed->atime = atime;
            parsed->ctime = ctime;
        }
    }
    if (prefix_length > 0) {
        parsed->name.tail = parsed->name.head;
        parsed->name.tail_len = parsed->name.head_len;
        parsed->name.head = block + 345;
        parsed->name.head_len = prefix_length;
        parsed->name.joined = 1;
    }
    return 0;
}

typedef struct {
    const uint8_t *key;
    size_t key_len;
    const uint8_t *value;
    size_t value_len;
} tar_pax_record_t;

static int tar_pax_key_is(const tar_pax_record_t *record, const char *key) {
    return tar_text_equals(record->key, record->key_len, key);
}

/* Splits one "%d %s=%s\n" record off the front of *records, where the
 * decimal length counts the whole record. Keys must be non-empty and free of
 * NUL; path, linkpath, uname, and gname values must be free of NUL. */
static int tar_pax_next_record(const uint8_t **records, size_t *remaining,
                               tar_pax_record_t *record) {
    const uint8_t *text = *records;
    size_t available = *remaining;
    const uint8_t *space = (const uint8_t *)memchr(text, ' ', available);
    if (!space) return -1;
    size_t digits = (size_t)(space - text);
    int64_t declared = 0;
    if (tar_parse_decimal(text, digits, &declared) != 0 || declared < 5 ||
        (uint64_t)declared > (uint64_t)available ||
        (size_t)declared <= digits + 1U)
        return -1;
    size_t length = (size_t)declared;
    const uint8_t *body = space + 1;
    size_t body_len = length - digits - 1U;
    if (body[body_len - 1U] != '\n') return -1;
    const uint8_t *equals =
        (const uint8_t *)memchr(body, '=', body_len - 1U);
    if (!equals) return -1;
    record->key = body;
    record->key_len = (size_t)(equals - body);
    record->value = equals + 1;
    record->value_len = body_len - 1U - record->key_len - 1U;
    if (record->key_len == 0) return -1;
    if (tar_pax_key_is(record, "path") || tar_pax_key_is(record, "linkpath") ||
        tar_pax_key_is(record, "uname") || tar_pax_key_is(record, "gname")) {
        if (record->value_len > 0 &&
            memchr(record->value, '\0', record->value_len) != NULL)
            return -1;
    } else if (memchr(record->key, '\0', record->key_len) != NULL) {
        return -1;
    }
    *records = text + length;
    *remaining = available - length;
    return 0;
}

/* The last value of each pax key that affects an entry; a later record
 * replaces an earlier one, and an absent key reads as an empty value. */
typedef struct {
    tar_pax_record_t path, linkpath, uname, gname, uid, gid, size;
    tar_pax_record_t mtime, atime, ctime;
    tar_pax_record_t sparse_major, sparse_minor, sparse_map;
    size_t sparse_values;
    int sparse_first_empty;
} tar_pax_view_t;

/* Validates a pax payload ('x' or 'g'), collecting its keys into view when
 * one is supplied. GNU sparse 0.0 offset/numbytes records must alternate,
 * starting with an offset, and contain no comma. */
static int tar_pax_scan(const uint8_t *records, size_t length,
                        tar_pax_view_t *view) {
    tar_pax_view_t local;
    if (!view) view = &local;
    memset(view, 0, sizeof(*view));
    while (length > 0) {
        tar_pax_record_t record;
        if (tar_pax_next_record(&records, &length, &record) != 0) return -1;
        int offset = tar_pax_key_is(&record, "GNU.sparse.offset");
        if (offset || tar_pax_key_is(&record, "GNU.sparse.numbytes")) {
            if ((view->sparse_values % 2U == 0U) != offset ||
                (record.value_len > 0 &&
                 memchr(record.value, ',', record.value_len) != NULL))
                return -1;
            if (view->sparse_values == 0)
                view->sparse_first_empty = record.value_len == 0;
            view->sparse_values++;
        }
        else if (tar_pax_key_is(&record, "path")) view->path = record;
        else if (tar_pax_key_is(&record, "linkpath")) view->linkpath = record;
        else if (tar_pax_key_is(&record, "uname")) view->uname = record;
        else if (tar_pax_key_is(&record, "gname")) view->gname = record;
        else if (tar_pax_key_is(&record, "uid")) view->uid = record;
        else if (tar_pax_key_is(&record, "gid")) view->gid = record;
        else if (tar_pax_key_is(&record, "size")) view->size = record;
        else if (tar_pax_key_is(&record, "mtime")) view->mtime = record;
        else if (tar_pax_key_is(&record, "atime")) view->atime = record;
        else if (tar_pax_key_is(&record, "ctime")) view->ctime = record;
        else if (tar_pax_key_is(&record, "GNU.sparse.major"))
            view->sparse_major = record;
        else if (tar_pax_key_is(&record, "GNU.sparse.minor"))
            view->sparse_minor = record;
        else if (tar_pax_key_is(&record, "GNU.sparse.map"))
            view->sparse_map = record;
    }
    return 0;
}

/* Matches Go's detection of the GNU pax sparse formats 0.0, 0.1, and 1.0;
 * an unknown version is an ordinary file. Offset/numbytes records replace
 * any GNU.sparse.map record. */
static int tar_pax_is_sparse(const tar_pax_view_t *view) {
    const tar_pax_record_t *major = &view->sparse_major;
    const tar_pax_record_t *minor = &view->sparse_minor;
    if (tar_text_equals(major->value, major->value_len, "0") &&
        (tar_text_equals(minor->value, minor->value_len, "0") ||
         tar_text_equals(minor->value, minor->value_len, "1")))
        return 1;
    if (tar_text_equals(major->value, major->value_len, "1") &&
        tar_text_equals(minor->value, minor->value_len, "0"))
        return 1;
    if (major->value_len > 0 || minor->value_len > 0) return 0;
    if (view->sparse_values > 0)
        return view->sparse_values > 1U || !view->sparse_first_empty;
    return view->sparse_map.value_len > 0;
}

/* An entry after its metadata blocks have been applied. */
typedef struct {
    tar_text_t name, linkname, uname, gname;
    int typeflag;
    int64_t size;
    uint32_t mode;
    int64_t uid, gid;
    int64_t mtime, atime, ctime;
    int32_t mtime_nsec, atime_nsec, ctime_nsec;
} tar_entry_t;

static int tar_entry_apply_pax(tar_entry_t *entry, int64_t *size,
                               const uint8_t *records, size_t length) {
    tar_pax_view_t view;
    if (tar_pax_scan(records, length, &view) != 0) return -1;
    if (view.path.value_len > 0)
        tar_text_set(&entry->name, view.path.value, view.path.value_len);
    if (view.linkpath.value_len > 0)
        tar_text_set(&entry->linkname, view.linkpath.value,
                     view.linkpath.value_len);
    if (view.uname.value_len > 0)
        tar_text_set(&entry->uname, view.uname.value, view.uname.value_len);
    if (view.gname.value_len > 0)
        tar_text_set(&entry->gname, view.gname.value, view.gname.value_len);
    if ((view.uid.value_len > 0 &&
         tar_parse_decimal(view.uid.value, view.uid.value_len,
                           &entry->uid) != 0) ||
        (view.gid.value_len > 0 &&
         tar_parse_decimal(view.gid.value, view.gid.value_len,
                           &entry->gid) != 0) ||
        (view.size.value_len > 0 &&
         tar_parse_decimal(view.size.value, view.size.value_len, size) != 0) ||
        (view.mtime.value_len > 0 &&
         tar_parse_pax_time(view.mtime.value, view.mtime.value_len,
                            &entry->mtime, &entry->mtime_nsec) != 0) ||
        (view.atime.value_len > 0 &&
         tar_parse_pax_time(view.atime.value, view.atime.value_len,
                            &entry->atime, &entry->atime_nsec) != 0) ||
        (view.ctime.value_len > 0 &&
         tar_parse_pax_time(view.ctime.value, view.ctime.value_len,
                            &entry->ctime, &entry->ctime_nsec) != 0))
        return -1;
    return tar_pax_is_sparse(&view) ? -1 : 0;
}

/* Go treats these typeflags as having no data even when size is set. */
static int tar_typeflag_has_no_data(int typeflag) {
    return typeflag >= '1' && typeflag <= '6';
}

/* Reads one entry starting at a header boundary: any pax ('x'/'g') or GNU
 * ('L'/'K') metadata blocks, then the file header. Returns 1 with *offset at
 * the entry's payload, 0 with *offset at the two-block terminator, or -1. */
static int tar_read_entry(const uint8_t *data, size_t avail,
                          tar_entry_t *entry, size_t *offset) {
    size_t position = 0;
    const uint8_t *pax = NULL;
    size_t pax_len = 0;
    tar_text_t long_name, long_link;
    tar_text_set(&long_name, NULL, 0);
    tar_text_set(&long_link, NULL, 0);
    for (;;) {
        if (avail - position < NEVERC_TAR_BLOCK_SIZE) return -1;
        const uint8_t *block = data + position;
        if (tar_block_is_zero(block)) {
            if (avail - position < NEVERC_TAR_BLOCK_SIZE * 2U ||
                !tar_block_is_zero(block + NEVERC_TAR_BLOCK_SIZE))
                return -1;
            *offset = position;
            return 0;
        }
        tar_block_t parsed;
        if (tar_parse_block(block, &parsed) != 0 ||
            (parsed.size < 0 && !tar_typeflag_has_no_data(parsed.typeflag)))
            return -1;
        position += NEVERC_TAR_BLOCK_SIZE;

        int typeflag = parsed.typeflag;
        if (typeflag == 'x' || typeflag == 'g' ||
            typeflag == 'L' || typeflag == 'K') {
            if ((uint64_t)parsed.size > (uint64_t)NEVERC_TAR_SPECIAL_MAX)
                return -1;
            size_t length = (size_t)parsed.size, padded = 0;
            if (tar_padded_size(length, &padded) != 0 ||
                padded > avail - position)
                return -1;
            const uint8_t *payload = data + position;
            position += padded;
            if (typeflag == 'x' || typeflag == 'g') {
                if (tar_pax_scan(payload, length, NULL) != 0) return -1;
                if (typeflag == 'x') {
                    pax = payload;
                    pax_len = length;
                } else {
                    /* Go returns a global header as its own entry, so
                     * metadata pending for the next file is dropped. */
                    pax = NULL;
                    pax_len = 0;
                    tar_text_set(&long_name, NULL, 0);
                    tar_text_set(&long_link, NULL, 0);
                }
            } else {
                const uint8_t *nul = length > 0
                    ? (const uint8_t *)memchr(payload, '\0', length) : NULL;
                tar_text_set(typeflag == 'L' ? &long_name : &long_link,
                             payload,
                             nul ? (size_t)(nul - payload) : length);
            }
            continue;
        }

        memset(entry, 0, sizeof(*entry));
        entry->name = parsed.name;
        entry->linkname = parsed.linkname;
        entry->uname = parsed.uname;
        entry->gname = parsed.gname;
        entry->uid = parsed.uid;
        entry->gid = parsed.gid;
        entry->mtime = parsed.mtime;
        entry->atime = parsed.atime;
        entry->ctime = parsed.ctime;
        int64_t size = parsed.size;
        if (pax && tar_entry_apply_pax(entry, &size, pax, pax_len) != 0)
            return -1;
        if (long_name.head_len > 0) entry->name = long_name;
        if (long_link.head_len > 0) entry->linkname = long_link;
        /* As in Go, a negative size is invalid unless the type carries no
         * data. Go makes only NUL (not '0') plus a trailing slash a
         * directory, so '0' with a slash still rejects a negative size. */
        int slash = tar_text_has_slash_suffix(&entry->name);
        int go_typeflag = typeflag == 0
            ? (slash ? NEVERC_TAR_DIR : NEVERC_TAR_REG) : typeflag;
        if (size < 0 && !tar_typeflag_has_no_data(go_typeflag)) return -1;
        if (typeflag == 0 || typeflag == NEVERC_TAR_REG)
            typeflag = slash ? NEVERC_TAR_DIR : NEVERC_TAR_REG;
        if (!tar_type_supported(typeflag) ||
            parsed.mode < 0 || parsed.mode > (int64_t)UINT32_MAX)
            return -1;
        if (tar_type_header_only(typeflag)) size = 0;
        size_t padded = 0;
        if (size < 0 || !tar_size_fits((uint64_t)size) ||
            tar_padded_size((size_t)size, &padded) != 0 ||
            padded > avail - position)
            return -1;
        entry->typeflag = typeflag;
        entry->size = size;
        entry->mode = (uint32_t)parsed.mode;
        *offset = position;
        return 1;
    }
}

/* Copies an entry's strings into fields of the given capacities and applies
 * the path policy to the copied name and link target. */
static int tar_entry_strings(const tar_entry_t *entry,
                             char *name, size_t name_capacity,
                             char *linkname, size_t link_capacity,
                             char *uname, size_t uname_capacity,
                             char *gname, size_t gname_capacity) {
    size_t name_length = 0, link_length = 0;
    if (tar_text_copy(name, name_capacity, &entry->name, &name_length) != 0 ||
        tar_text_copy(linkname, link_capacity, &entry->linkname,
                      &link_length) != 0 ||
        tar_text_copy(uname, uname_capacity, &entry->uname, NULL) != 0 ||
        tar_text_copy(gname, gname_capacity, &entry->gname, NULL) != 0 ||
        !tar_path_is_safe(name, name_length,
                          entry->typeflag == NEVERC_TAR_DIR))
        return -1;
    if ((entry->typeflag == NEVERC_TAR_SYM ||
         entry->typeflag == NEVERC_TAR_LINK) &&
        !tar_path_is_safe(linkname, link_length, 0))
        return -1;
    return 0;
}

static int tar_entry_to_legacy(const tar_entry_t *entry,
                               neverc_tar_header_t *hdr) {
    if (tar_entry_strings(entry, hdr->name, sizeof(hdr->name),
                          hdr->linkname, sizeof(hdr->linkname),
                          hdr->uname, sizeof(hdr->uname),
                          hdr->gname, sizeof(hdr->gname)) != 0)
        return -1;
    hdr->size = entry->size;
    hdr->mode = entry->mode;
    hdr->mtime = entry->mtime;
    hdr->typeflag = entry->typeflag;
    return 0;
}

static int tar_entry_to_v2(const tar_entry_t *entry,
                           neverc_tar_header_v2_t *hdr) {
    if (tar_entry_strings(entry, hdr->name, sizeof(hdr->name),
                          hdr->linkname, sizeof(hdr->linkname),
                          hdr->uname, sizeof(hdr->uname),
                          hdr->gname, sizeof(hdr->gname)) != 0)
        return -1;
    hdr->size = entry->size;
    hdr->mode = entry->mode;
    hdr->mtime = entry->mtime;
    hdr->typeflag = entry->typeflag;
    return 0;
}

static int tar_entry_to_v3(const tar_entry_t *entry,
                           neverc_tar_header_v3_t *hdr) {
    if (tar_entry_strings(entry, hdr->name, sizeof(hdr->name),
                          hdr->linkname, sizeof(hdr->linkname),
                          hdr->uname, sizeof(hdr->uname),
                          hdr->gname, sizeof(hdr->gname)) != 0)
        return -1;
    hdr->size = entry->size;
    hdr->mode = entry->mode;
    hdr->mtime = entry->mtime;
    hdr->typeflag = entry->typeflag;
    hdr->uid = entry->uid;
    hdr->gid = entry->gid;
    hdr->mtime_nsec = entry->mtime_nsec;
    hdr->atime = entry->atime;
    hdr->atime_nsec = entry->atime_nsec;
    hdr->ctime = entry->ctime;
    hdr->ctime_nsec = entry->ctime_nsec;
    return 0;
}

/* Positions a reader at its next header boundary without changing it:
 * skips the unread payload of the current entry plus its padding, or trims a
 * partial trailing record when already at a boundary. */
static int tar_reader_boundary(const neverc_tar_reader_t *r,
                               const uint8_t **block, size_t *avail) {
    if (!r || (!r->data && r->len != 0) || r->pos > r->len)
        return -1;
    const uint8_t *cursor = r->data;
    size_t remaining = r->len;
    if (r->pos > 0) {
        size_t skip = r->pos +
            (remaining - r->pos) % NEVERC_TAR_BLOCK_SIZE;
        cursor += skip;
        remaining -= skip;
    } else {
        remaining -= remaining % NEVERC_TAR_BLOCK_SIZE;
    }
    *block = cursor;
    *avail = remaining;
    return 0;
}

/* Parses the next entry and reports the cursor that follows it. Nothing is
 * committed here, so a caller that fails afterwards leaves r unchanged. */
static int tar_reader_prepare_next(const neverc_tar_reader_t *r,
                                   tar_entry_t *entry,
                                   neverc_tar_reader_t *next) {
    const uint8_t *block = NULL;
    size_t avail = 0, offset = 0;
    if (!r || !entry || !next || tar_reader_boundary(r, &block, &avail) != 0)
        return -1;
    int parsed = tar_read_entry(block, avail, entry, &offset);
    if (parsed < 0) return -1;
    /* At the end, stay on the terminator so every later next() reports it. */
    *next = *r;
    next->data = block + offset;
    next->len = avail - offset;
    next->pos = parsed == 1 ? (size_t)entry->size : 0U;
    return parsed;
}

int neverc_tar_reader_next(neverc_tar_reader_t *r, neverc_tar_header_t *hdr) {
    if (!hdr) return -1;
    memset(hdr, 0, sizeof(*hdr));
    tar_entry_t entry;
    neverc_tar_reader_t next;
    int result = tar_reader_prepare_next(r, &entry, &next);
    if (result == 1 && tar_entry_to_legacy(&entry, hdr) != 0) {
        memset(hdr, 0, sizeof(*hdr));
        return -1;
    }
    if (result >= 0) *r = next;
    return result;
}

int neverc_tar_reader_next_v2(neverc_tar_reader_t *r,
                              neverc_tar_header_v2_t *hdr) {
    if (!hdr) return -1;
    memset(hdr, 0, sizeof(*hdr));
    tar_entry_t entry;
    neverc_tar_reader_t next;
    int result = tar_reader_prepare_next(r, &entry, &next);
    if (result == 1 && tar_entry_to_v2(&entry, hdr) != 0) {
        memset(hdr, 0, sizeof(*hdr));
        return -1;
    }
    if (result >= 0) *r = next;
    return result;
}

int neverc_tar_reader_next_v3(neverc_tar_reader_t *r,
                              neverc_tar_header_v3_t *hdr) {
    if (!hdr) return -1;
    memset(hdr, 0, sizeof(*hdr));
    tar_entry_t entry;
    neverc_tar_reader_t next;
    int result = tar_reader_prepare_next(r, &entry, &next);
    if (result == 1 && tar_entry_to_v3(&entry, hdr) != 0) {
        memset(hdr, 0, sizeof(*hdr));
        return -1;
    }
    if (result >= 0) *r = next;
    return result;
}

/* hdr must describe the current entry; the cursor cannot recover its full
 * size, so only a header too small for the unread payload is rejected. */
static int tar_reader_read_size(neverc_tar_reader_t *r, int64_t header_size,
                                uint8_t *buf, size_t len, size_t *nread) {
    if (!nread) return -1;
    *nread = 0;
    if (!r || header_size < 0 || (!buf && len != 0) ||
        !tar_size_fits((uint64_t)header_size) ||
        (!r->data && r->len != 0) || r->pos > r->len)
        return -1;
    if (r->pos == 0) return 0;
    if ((uint64_t)header_size < (uint64_t)r->pos) return -1;
    size_t amount = r->pos < len ? r->pos : len;
    if (amount > 0) memcpy(buf, r->data, amount);
    r->data += amount;
    r->len -= amount;
    r->pos -= amount;
    if (r->pos == 0) {
        size_t padding = r->len % NEVERC_TAR_BLOCK_SIZE;
        r->data += padding;
        r->len -= padding;
    }
    *nread = amount;
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

int neverc_tar_reader_read_v3(neverc_tar_reader_t *r,
                              const neverc_tar_header_v3_t *hdr,
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

static int tar_writer_data_offset(
    const neverc_tar_writer_t *w, const uint8_t *data, size_t len,
    size_t *offset) {
    if (!w || !w->data || !data || !offset) return 0;
    uintptr_t base = (uintptr_t)(const void *)w->data;
    uintptr_t source = (uintptr_t)(const void *)data;
    if (source < base) return 0;
    uintptr_t distance = source - base;
    if (distance > (uintptr_t)w->cap) return 0;
    size_t source_offset = (size_t)distance;
    if (len > w->cap - source_offset) return 0;
    *offset = source_offset;
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

static int tar_writer_write_header_common(neverc_tar_writer_t *w,
                                          const neverc_tar_header_v2_t *hdr) {
    tar_writer_meta_t meta;
    if (!w || !hdr || !tar_writer_meta_load(w, &meta) ||
        meta.closed || meta.failed || meta.entry_open ||
        hdr->size < 0 || hdr->mtime < 0 ||
        !tar_size_fits((uint64_t)hdr->size) ||
        hdr->typeflag < 0 || hdr->typeflag > UCHAR_MAX)
        return -1;
    size_t current_size = (size_t)hdr->size;
    if (tar_padded_size(current_size, NULL) != 0)
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
    int typeflag = tar_resolve_typeflag(hdr->typeflag, hdr->name);
    if (!tar_type_supported(typeflag) ||
        !tar_path_is_safe(hdr->name, name_length,
                          typeflag == NEVERC_TAR_DIR) ||
        (tar_type_header_only(typeflag) && hdr->size != 0))
        return -1;
    if ((typeflag == NEVERC_TAR_SYM || typeflag == NEVERC_TAR_LINK) &&
        (link_length == 0 ||
         !tar_path_is_safe(hdr->linkname, link_length, 0)))
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
    size_t data_offset = 0;
    int data_aliases_output =
        tar_writer_data_offset(w, data, len, &data_offset);
    if (len > SIZE_MAX - padding || !writer_grow(w, len + padding)) {
        meta.failed = 1;
        (void)tar_writer_meta_store(w, &meta);
        return -1;
    }
    if (data_aliases_output) data = w->data + data_offset;
    if (len > 0) memmove(w->data + w->len, data, len);
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
