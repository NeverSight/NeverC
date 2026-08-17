#include "neverc/std/encoding/xml.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void neverc_xml_decoder_init(neverc_xml_decoder_t *d, const char *data, size_t len) {
    if (!d) return;
    d->src = data;
    d->len = data || len == 0 ? len : 0;
    d->pos = d->len >= 3 &&
             (unsigned char)d->src[0] == 0xef &&
             (unsigned char)d->src[1] == 0xbb &&
             (unsigned char)d->src[2] == 0xbf ? 3 : 0;
}

static char *dup_range(const char *s, size_t n) {
    if ((!s && n != 0) || n == SIZE_MAX) return NULL;
    char *r = (char *)malloc(n + 1);
    if (!r) return NULL;
    if (n > 0) memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

static int xml_char_is_valid(uint32_t codepoint) {
    if (codepoint >= 0xfdd0 && codepoint <= 0xfdef) return 0;
    if ((codepoint & 0xfffeU) == 0xfffeU) return 0;
    return codepoint == 0x09 || codepoint == 0x0a ||
           codepoint == 0x0d ||
           (codepoint >= 0x20 && codepoint <= 0xd7ff) ||
           (codepoint >= 0xe000 && codepoint <= 0xfffd) ||
           (codepoint >= 0x10000 && codepoint <= 0x10ffff);
}

static int xml_decode_utf8(const char *data, size_t length,
                           size_t *consumed, uint32_t *codepoint) {
    if (!data || length == 0 || !consumed || !codepoint)
        return -1;
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t value;
    size_t count;
    if (bytes[0] < 0x80) {
        value = bytes[0];
        count = 1;
    } else if (bytes[0] >= 0xc2 && bytes[0] <= 0xdf) {
        value = bytes[0] & 0x1fU;
        count = 2;
    } else if (bytes[0] >= 0xe0 && bytes[0] <= 0xef) {
        value = bytes[0] & 0x0fU;
        count = 3;
    } else if (bytes[0] >= 0xf0 && bytes[0] <= 0xf4) {
        value = bytes[0] & 0x07U;
        count = 4;
    } else {
        return -1;
    }
    if (count > length)
        return -1;
    for (size_t i = 1; i < count; i++) {
        if ((bytes[i] & 0xc0U) != 0x80U)
            return -1;
        value = (value << 6U) | (bytes[i] & 0x3fU);
    }
    if ((count == 3 && value < 0x800) ||
        (count == 4 && value < 0x10000) ||
        !xml_char_is_valid(value))
        return -1;
    *consumed = count;
    *codepoint = value;
    return 0;
}

static size_t xml_encode_utf8(uint32_t codepoint, char output[4]) {
    if (codepoint <= 0x7f) {
        output[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7ff) {
        output[0] = (char)(0xc0U | (codepoint >> 6U));
        output[1] = (char)(0x80U | (codepoint & 0x3fU));
        return 2;
    }
    if (codepoint <= 0xffff) {
        output[0] = (char)(0xe0U | (codepoint >> 12U));
        output[1] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        output[2] = (char)(0x80U | (codepoint & 0x3fU));
        return 3;
    }
    output[0] = (char)(0xf0U | (codepoint >> 18U));
    output[1] = (char)(0x80U | ((codepoint >> 12U) & 0x3fU));
    output[2] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
    output[3] = (char)(0x80U | (codepoint & 0x3fU));
    return 4;
}

static int xml_name_start(uint32_t codepoint) {
    return codepoint == ':' || codepoint == '_' ||
           (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= 'a' && codepoint <= 'z') ||
           (codepoint >= 0x00c0 && codepoint <= 0x00d6) ||
           (codepoint >= 0x00d8 && codepoint <= 0x00f6) ||
           (codepoint >= 0x00f8 && codepoint <= 0x02ff) ||
           (codepoint >= 0x0370 && codepoint <= 0x037d) ||
           (codepoint >= 0x037f && codepoint <= 0x1fff) ||
           (codepoint >= 0x200c && codepoint <= 0x200d) ||
           (codepoint >= 0x2070 && codepoint <= 0x218f) ||
           (codepoint >= 0x2c00 && codepoint <= 0x2fef) ||
           (codepoint >= 0x3001 && codepoint <= 0xd7ff) ||
           (codepoint >= 0xf900 && codepoint <= 0xfdcf) ||
           (codepoint >= 0xfdf0 && codepoint <= 0xfffd) ||
           (codepoint >= 0x10000 && codepoint <= 0xeffff);
}

static int xml_name_continue(uint32_t codepoint) {
    return xml_name_start(codepoint) || codepoint == '-' ||
           codepoint == '.' ||
           (codepoint >= '0' && codepoint <= '9') ||
           codepoint == 0x00b7 ||
           (codepoint >= 0x0300 && codepoint <= 0x036f) ||
           (codepoint >= 0x203f && codepoint <= 0x2040);
}

static int xml_parse_name(neverc_xml_decoder_t *decoder,
                          size_t *start, size_t *length) {
    if (!decoder || !start || !length || decoder->pos >= decoder->len)
        return -1;
    *start = decoder->pos;
    int first = 1;
    while (decoder->pos < decoder->len) {
        size_t consumed = 0;
        uint32_t codepoint = 0;
        if (xml_decode_utf8(
                decoder->src + decoder->pos,
                decoder->len - decoder->pos,
                &consumed, &codepoint) != 0)
            return -1;
        if ((first && !xml_name_start(codepoint)) ||
            (!first && !xml_name_continue(codepoint)))
            break;
        decoder->pos += consumed;
        first = 0;
    }
    *length = decoder->pos - *start;
    return first ? -1 : 0;
}

static char *xml_decode_text(const char *data, size_t length,
                             int attribute, size_t *decoded_length) {
    if (!decoded_length || (!data && length != 0) || length == SIZE_MAX)
        return NULL;
    *decoded_length = 0;
    char *decoded = (char *)malloc(length + 1);
    if (!decoded)
        return NULL;
    size_t input = 0;
    size_t output = 0;
    while (input < length) {
        if (data[input] == '&') {
            size_t end = input + 1;
            while (end < length && data[end] != ';')
                end++;
            if (end >= length || end == input + 1)
                goto invalid;
            const char *entity = data + input + 1;
            size_t entity_length = end - input - 1;
            uint32_t codepoint = 0;
            if (entity_length == 2 && memcmp(entity, "lt", 2) == 0)
                codepoint = '<';
            else if (entity_length == 2 && memcmp(entity, "gt", 2) == 0)
                codepoint = '>';
            else if (entity_length == 3 && memcmp(entity, "amp", 3) == 0)
                codepoint = '&';
            else if (entity_length == 4 &&
                     memcmp(entity, "apos", 4) == 0)
                codepoint = '\'';
            else if (entity_length == 4 &&
                     memcmp(entity, "quot", 4) == 0)
                codepoint = '"';
            else if (entity[0] == '#') {
                size_t digit = 1;
                unsigned base = 10;
                if (digit < entity_length &&
                    entity[digit] == 'x') {
                    base = 16;
                    digit++;
                }
                if (digit == entity_length)
                    goto invalid;
                for (; digit < entity_length; digit++) {
                    unsigned value;
                    unsigned char c = (unsigned char)entity[digit];
                    if (c >= '0' && c <= '9')
                        value = c - '0';
                    else if (base == 16 && c >= 'a' && c <= 'f')
                        value = c - 'a' + 10U;
                    else if (base == 16 && c >= 'A' && c <= 'F')
                        value = c - 'A' + 10U;
                    else
                        goto invalid;
                    if (codepoint > (UINT32_MAX - value) / base)
                        goto invalid;
                    codepoint = codepoint * base + value;
                }
            } else {
                goto invalid;
            }
            if (!xml_char_is_valid(codepoint))
                goto invalid;
            /* Character references keep the referenced character (XML 1.0 §3.3.3). */
            char encoded[4];
            size_t encoded_length = xml_encode_utf8(codepoint, encoded);
            memcpy(decoded + output, encoded, encoded_length);
            output += encoded_length;
            input = end + 1;
            continue;
        }

        size_t consumed = 0;
        uint32_t codepoint = 0;
        if (xml_decode_utf8(data + input, length - input,
                            &consumed, &codepoint) != 0 ||
            (attribute && codepoint == '<'))
            goto invalid;
        if (codepoint == '\r') {
            input += consumed;
            if (input < length && data[input] == '\n')
                input++;
            decoded[output++] = attribute ? ' ' : '\n';
            continue;
        }
        if (attribute && (codepoint == '\n' || codepoint == '\t')) {
            decoded[output++] = ' ';
            input += consumed;
            continue;
        }
        memcpy(decoded + output, data + input, consumed);
        output += consumed;
        input += consumed;
    }
    decoded[output] = '\0';
    *decoded_length = output;
    return decoded;

invalid:
    free(decoded);
    return NULL;
}

static char *xml_copy_valid_text(const char *data, size_t length,
                                 size_t *copied_length) {
    if (!copied_length || (!data && length != 0) || length == SIZE_MAX)
        return NULL;
    *copied_length = 0;
    char *copy = (char *)malloc(length + 1);
    if (!copy)
        return NULL;
    size_t position = 0;
    size_t output = 0;
    while (position < length) {
        size_t consumed = 0;
        uint32_t codepoint = 0;
        if (xml_decode_utf8(data + position, length - position,
                            &consumed, &codepoint) != 0) {
            free(copy);
            return NULL;
        }
        if (codepoint == '\r') {
            position += consumed;
            if (position < length && data[position] == '\n')
                position++;
            copy[output++] = '\n';
            continue;
        }
        memcpy(copy + output, data + position, consumed);
        output += consumed;
        position += consumed;
    }
    copy[output] = '\0';
    *copied_length = output;
    return copy;
}

static int xml_ascii_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static unsigned char xml_ascii_lower(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
}

static int xml_memieq(const char *s, size_t n, const char *lit) {
    size_t i;
    for (i = 0; lit[i]; i++) {
        if (i >= n || xml_ascii_lower((unsigned char)s[i]) !=
                          xml_ascii_lower((unsigned char)lit[i]))
            return 0;
    }
    return i == n;
}

static int xml_decl_keyword(const char *data, size_t len, size_t i,
                            const char *key) {
    size_t key_len = strlen(key);
    /* Subtraction form: `i + key_len` wraps when i is near SIZE_MAX and
     * would treat an out-of-range memcmp as in-bounds. */
    if (i > len || key_len > len - i || memcmp(data + i, key, key_len) != 0)
        return 0;
    if (key_len < len - i &&
        !xml_ascii_ws((unsigned char)data[i + key_len]) &&
        data[i + key_len] != '=')
        return 0;
    return 1;
}

static int xml_decl_quoted(const char *data, size_t len, size_t *i,
                           const char **val, size_t *vlen) {
    while (*i < len && xml_ascii_ws((unsigned char)data[*i]))
        (*i)++;
    if (*i >= len || data[*i] != '=')
        return 0;
    (*i)++;
    while (*i < len && xml_ascii_ws((unsigned char)data[*i]))
        (*i)++;
    if (*i >= len || (data[*i] != '"' && data[*i] != '\''))
        return 0;
    char q = data[(*i)++];
    size_t vs = *i;
    while (*i < len && data[*i] != q)
        (*i)++;
    if (*i >= len)
        return 0;
    *val = data + vs;
    *vlen = *i - vs;
    (*i)++;
    return 1;
}

/* XML 1.0 XMLDecl / Go encoding/xml without CharsetReader: version is
 * required and must be 1.0; encoding, if present, must be UTF-8. Attributes
 * are parsed in order so a missing version, a cramped `encoding=` jammed
 * against the previous quote, or a second non-UTF-8 encoding cannot slip
 * through the earlier whitespace-delimited search. */
static int xml_decl_ok(const char *data, size_t len) {
    size_t i = 0;
    while (i < len && !xml_ascii_ws((unsigned char)data[i]))
        i++;

    const char *val = NULL;
    size_t vlen = 0;
    int saw_version = 0, saw_encoding = 0, saw_standalone = 0;
    int need_ws = 0;

    while (i < len) {
        size_t before = i;
        while (i < len && xml_ascii_ws((unsigned char)data[i]))
            i++;
        if (i >= len)
            break;
        if (need_ws && i == before)
            return 0;

        if (xml_decl_keyword(data, len, i, "version")) {
            if (saw_version)
                return 0;
            i += 7;
            if (!xml_decl_quoted(data, len, &i, &val, &vlen) ||
                vlen != 3 || memcmp(val, "1.0", 3) != 0)
                return 0;
            saw_version = 1;
            need_ws = 1;
            continue;
        }
        if (xml_decl_keyword(data, len, i, "encoding")) {
            if (!saw_version || saw_encoding || saw_standalone)
                return 0;
            i += 8;
            if (!xml_decl_quoted(data, len, &i, &val, &vlen) ||
                vlen == 0 || !xml_memieq(val, vlen, "utf-8"))
                return 0;
            saw_encoding = 1;
            need_ws = 1;
            continue;
        }
        if (xml_decl_keyword(data, len, i, "standalone")) {
            if (!saw_version || saw_standalone)
                return 0;
            i += 10;
            if (!xml_decl_quoted(data, len, &i, &val, &vlen) ||
                !((vlen == 3 && memcmp(val, "yes", 3) == 0) ||
                  (vlen == 2 && memcmp(val, "no", 2) == 0)))
                return 0;
            saw_standalone = 1;
            need_ws = 1;
            continue;
        }
        return 0;
    }
    return saw_version;
}

static void skip_ws(neverc_xml_decoder_t *d) {
    while (d->pos < d->len) {
        char c = d->src[d->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') d->pos++;
        else break;
    }
}

static void free_attrs(neverc_xml_attr_t *attrs, int count) {
    if (!attrs) return;
    for (int i = 0; i < count; i++) {
        free(attrs[i].name);
        free(attrs[i].value);
    }
    free(attrs);
}

static neverc_xml_attr_t *parse_attrs(neverc_xml_decoder_t *d, int *count) {
    /* Allocate lazily: attribute-less tags (the common case) return NULL with
     * count 0 and pay no malloc/free. All consumers already guard on a NULL
     * attrs pointer (token_free, node_free, node_attr, the DOM builder). */
    int cap = 0;
    neverc_xml_attr_t *attrs = NULL;
    *count = 0;

    while (d->pos < d->len) {
        size_t before_whitespace = d->pos;
        skip_ws(d);
        if (d->pos >= d->len || d->src[d->pos] == '>' ||
            d->src[d->pos] == '/' || d->src[d->pos] == '?')
            break;
        if (d->pos == before_whitespace)
            goto error;

        size_t ns = 0, name_length = 0;
        if (xml_parse_name(d, &ns, &name_length) != 0)
            goto error;
        char *name = dup_range(d->src + ns, name_length);
        if (!name) goto error;
        for (int i = 0; i < *count; i++) {
            if (strcmp(attrs[i].name, name) == 0) {
                free(name);
                goto error;
            }
        }

        skip_ws(d);
        if (d->pos >= d->len || d->src[d->pos] != '=') {
            free(name);
            goto error;
        }
        d->pos++;
        skip_ws(d);
        if (d->pos >= d->len ||
            (d->src[d->pos] != '"' && d->src[d->pos] != '\'')) {
            free(name);
            goto error;
        }
        char q = d->src[d->pos++];
        size_t vs = d->pos;
        while (d->pos < d->len && d->src[d->pos] != q) d->pos++;
        if (d->pos >= d->len) {
            free(name);
            goto error;
        }
        size_t value_length = 0;
        char *value = xml_decode_text(
            d->src + vs, d->pos - vs, 1, &value_length);
        d->pos++;
        if (!value) {
            free(name);
            goto error;
        }

        if (*count >= cap) {
            if (cap > INT32_MAX / 2) {
                free(name);
                free(value);
                goto error;
            }
            int next_cap = cap ? cap * 2 : 4;
            if ((size_t)next_cap > SIZE_MAX / sizeof(*attrs)) {
                free(name);
                free(value);
                goto error;
            }
            neverc_xml_attr_t *grown = (neverc_xml_attr_t *)realloc(
                attrs, (size_t)next_cap * sizeof(*attrs));
            if (!grown) {
                free(name);
                free(value);
                goto error;
            }
            attrs = grown;
            cap = next_cap;
        }
        attrs[*count].name = name;
        attrs[*count].value = value;
        (*count)++;
    }
    return attrs;

error:
    free_attrs(attrs, *count);
    *count = -1;
    return NULL;
}

int neverc_xml_decode_token(neverc_xml_decoder_t *d, neverc_xml_token_t *tok) {
    if (!tok) return -1;
    memset(tok, 0, sizeof(*tok));
    if (!d || (!d->src && d->len != 0) || d->pos > d->len) {
        tok->type = NEVERC_XML_ERROR;
        return -1;
    }

    if (d->pos >= d->len) { tok->type = NEVERC_XML_EOF; return 0; }

    if (d->src[d->pos] != '<') {
        size_t start = d->pos;
        /* Character data runs until the next '<'; memchr scans it in bulk
         * instead of byte-by-byte (text content dominates most documents). */
        const char *lt = (const char *)memchr(d->src + d->pos, '<', d->len - d->pos);
        d->pos = lt ? (size_t)(lt - d->src) : d->len;
        for (size_t i = start; d->pos - i >= 3; i++) {
            if (d->src[i] == ']' && d->src[i + 1] == ']' &&
                d->src[i + 2] == '>')
                goto error;
        }
        tok->type = NEVERC_XML_CHAR_DATA;
        tok->data = xml_decode_text(
            d->src + start, d->pos - start, 0, &tok->data_len);
        if (!tok->data) goto error;
        return 1;
    }

    d->pos++;
    if (d->pos >= d->len) goto error;

    if (d->src[d->pos] == '/') {
        d->pos++;
        size_t ns = 0, name_length = 0;
        if (xml_parse_name(d, &ns, &name_length) != 0)
            goto error;
        skip_ws(d);
        if (d->pos >= d->len || d->src[d->pos] != '>')
            goto error;
        tok->type = NEVERC_XML_END_ELEMENT;
        tok->name = dup_range(d->src + ns, name_length);
        if (!tok->name) goto error;
        d->pos++;
        return 1;
    }

    if (d->src[d->pos] == '!' && d->len - d->pos >= 8 &&
        memcmp(d->src + d->pos, "![CDATA[", 8) == 0) {
        d->pos += 8;
        size_t start = d->pos;
        while (d->len - d->pos >= 3 &&
               !(d->src[d->pos] == ']' &&
                 d->src[d->pos + 1] == ']' &&
                 d->src[d->pos + 2] == '>'))
            d->pos++;
        if (d->len - d->pos < 3)
            goto error;
        tok->type = NEVERC_XML_CHAR_DATA;
        tok->data = xml_copy_valid_text(
            d->src + start, d->pos - start, &tok->data_len);
        if (!tok->data)
            goto error;
        d->pos += 3;
        return 1;
    }

    if (d->src[d->pos] == '!' && d->len - d->pos >= 3 &&
        d->src[d->pos + 1] == '-' && d->src[d->pos + 2] == '-') {
        d->pos += 3;
        size_t cs = d->pos;
        while (d->len - d->pos >= 3 &&
               !(d->src[d->pos] == '-' &&
                 d->src[d->pos + 1] == '-' &&
                 d->src[d->pos + 2] == '>')) {
            if (d->src[d->pos] == '-' &&
                d->src[d->pos + 1] == '-')
                goto error;
            d->pos++;
        }
        if (d->len - d->pos < 3) goto error;
        tok->type = NEVERC_XML_COMMENT;
        tok->data = xml_copy_valid_text(
            d->src + cs, d->pos - cs, &tok->data_len);
        if (!tok->data) goto error;
        d->pos += 3;
        return 1;
    }

    /* DTD/entity declarations are intentionally unsupported: treating them
     * as elements is incorrect and resolving them would enable XXE. */
    if (d->src[d->pos] == '!')
        goto error;

    if (d->src[d->pos] == '?') {
        d->pos++;
        size_t ps = d->pos;
        size_t target_start = 0, target_length = 0;
        if (xml_parse_name(
                d, &target_start, &target_length) != 0)
            goto error;
        if (d->pos < d->len && d->src[d->pos] != '?' &&
            d->src[d->pos] != ' ' && d->src[d->pos] != '\t' &&
            d->src[d->pos] != '\n' && d->src[d->pos] != '\r')
            goto error;
        while (d->len - d->pos >= 2 &&
               !(d->src[d->pos] == '?' && d->src[d->pos+1] == '>'))
            d->pos++;
        if (d->len - d->pos < 2) goto error;
        tok->type = NEVERC_XML_PROC_INST;
        tok->data = xml_copy_valid_text(
            d->src + ps, d->pos - ps, &tok->data_len);
        if (!tok->data) goto error;
        d->pos += 2;
        if (target_length == 3 &&
            xml_memieq(d->src + target_start, 3, "xml") &&
            !xml_decl_ok(tok->data, tok->data_len))
            goto error;
        return 1;
    }

    size_t ns = 0, name_length = 0;
    if (xml_parse_name(d, &ns, &name_length) != 0)
        goto error;
    tok->type = NEVERC_XML_START_ELEMENT;
    tok->name = dup_range(d->src + ns, name_length);
    if (!tok->name) goto error;
    tok->attrs = parse_attrs(d, &tok->nattrs);
    if (tok->nattrs < 0) goto error;

    if (d->pos < d->len && d->src[d->pos] == '/') {
        d->pos++;
        tok->self_closing = 1;
    }
    if (d->pos >= d->len || d->src[d->pos] != '>') goto error;
    d->pos++;

    return 1;

error:
    neverc_xml_token_free(tok);
    memset(tok, 0, sizeof(*tok));
    tok->type = NEVERC_XML_ERROR;
    return -1;
}

void neverc_xml_token_free(neverc_xml_token_t *tok) {
    if (!tok) return;
    free(tok->name);
    free(tok->data);
    if (tok->attrs) {
        for (int i = 0; i < tok->nattrs; i++) {
            free(tok->attrs[i].name);
            free(tok->attrs[i].value);
        }
        free(tok->attrs);
    }
    memset(tok, 0, sizeof(*tok));
}

/* DOM parser */
static neverc_xml_node_t *new_node(const char *tag) {
    neverc_xml_node_t *n = (neverc_xml_node_t *)calloc(1, sizeof(neverc_xml_node_t));
    if (!n) return NULL;
    n->tag = tag ? dup_range(tag, strlen(tag)) : NULL;
    if (tag && !n->tag) {
        free(n);
        return NULL;
    }
    n->cap_children = 4;
    n->children = (neverc_xml_node_t **)malloc(n->cap_children * sizeof(void *));
    if (!n->children) {
        free(n->tag);
        free(n);
        return NULL;
    }
    return n;
}

static int add_child(neverc_xml_node_t *parent, neverc_xml_node_t *child) {
    if (!parent || !child || parent->nchildren < 0 ||
        parent->cap_children < 0 || parent->nchildren == INT32_MAX) return 0;
    if (parent->nchildren >= parent->cap_children) {
        if (parent->cap_children > INT32_MAX / 2) return 0;
        int next_cap = parent->cap_children < 4 ? 4
                                                : parent->cap_children * 2;
        if ((size_t)next_cap > SIZE_MAX / sizeof(*parent->children))
            return 0;
        neverc_xml_node_t **grown = (neverc_xml_node_t **)realloc(
            parent->children, (size_t)next_cap * sizeof(*grown));
        if (!grown) return 0;
        parent->children = grown;
        parent->cap_children = next_cap;
    }
    parent->children[parent->nchildren++] = child;
    return 1;
}

static int grow_parse_stacks(neverc_xml_node_t ***stack, size_t **tlen,
                             size_t **tcap, int *cap, int used) {
    if (*cap > INT32_MAX / 2 || used < 0) return 0;
    int next_cap = *cap * 2;
    if ((size_t)next_cap > SIZE_MAX / sizeof(**stack) ||
        (size_t)next_cap > SIZE_MAX / sizeof(**tlen))
        return 0;
    neverc_xml_node_t **new_stack = (neverc_xml_node_t **)malloc(
        (size_t)next_cap * sizeof(*new_stack));
    size_t *new_tlen = (size_t *)malloc((size_t)next_cap * sizeof(*new_tlen));
    size_t *new_tcap = (size_t *)malloc((size_t)next_cap * sizeof(*new_tcap));
    if (!new_stack || !new_tlen || !new_tcap) {
        free(new_stack);
        free(new_tlen);
        free(new_tcap);
        return 0;
    }
    memcpy(new_stack, *stack, (size_t)used * sizeof(*new_stack));
    memcpy(new_tlen, *tlen, (size_t)used * sizeof(*new_tlen));
    memcpy(new_tcap, *tcap, (size_t)used * sizeof(*new_tcap));
    free(*stack);
    free(*tlen);
    free(*tcap);
    *stack = new_stack;
    *tlen = new_tlen;
    *tcap = new_tcap;
    *cap = next_cap;
    return 1;
}

/* Cap nesting depth. Parse is iterative (heap stack) and node_free is too, but
 * unbounded depth is still a memory/DoS hazard. 1000 is well beyond real
 * documents. */
#define NCI_XML_MAX_DEPTH 1000

neverc_xml_node_t *neverc_xml_parse(const char *data, size_t len) {
    if (!data && len != 0) return NULL;
    neverc_xml_decoder_t d;
    neverc_xml_decoder_init(&d, data, len);

    neverc_xml_node_t *root = new_node("__root__");
    if (!root) return NULL;

    int stack_cap = 32;
    neverc_xml_node_t **stack = (neverc_xml_node_t **)malloc(stack_cap * sizeof(void *));
    /* Parallel per-depth text accumulators. Tracking each open element's text
     * length and buffer capacity lets repeated character-data runs (mixed
     * content like <p>a<b/>c<b/>d...</p>, where each child splits the text) grow
     * the buffer geometrically instead of strlen + realloc-to-exact on every
     * run, which was O(n^2) in the number of runs per element. */
    size_t *tlen = (size_t *)malloc(stack_cap * sizeof(size_t));
    size_t *tcap = (size_t *)malloc(stack_cap * sizeof(size_t));
    if (!stack || !tlen || !tcap) goto parse_fail;
    int stack_top = 0;
    stack[stack_top] = root;
    tlen[stack_top] = 0;
    tcap[stack_top] = 0;
    stack_top++;
    int document_elements = 0;

    neverc_xml_token_t tok;
    int decode_result;
    while ((decode_result = neverc_xml_decode_token(&d, &tok)) > 0) {
        int failed = 0;
        switch (tok.type) {
        case NEVERC_XML_START_ELEMENT: {
            if (!tok.name) {
                failed = 1;
                break;
            }
            if (stack_top == 1 && ++document_elements != 1) {
                failed = 1;
                break;
            }
            if (!tok.self_closing && stack_top > NCI_XML_MAX_DEPTH) {
                failed = 1;
                break;
            }
            if (!tok.self_closing && stack_top >= stack_cap &&
                !grow_parse_stacks(&stack, &tlen, &tcap, &stack_cap,
                                   stack_top)) {
                failed = 1;
                break;
            }
            neverc_xml_node_t *child = new_node(tok.name);
            if (!child) {
                failed = 1;
                break;
            }
            child->attrs = tok.attrs;
            child->nattrs = tok.nattrs;
            tok.attrs = NULL; tok.nattrs = 0;
            if (!add_child(stack[stack_top - 1], child)) {
                neverc_xml_node_free(child);
                failed = 1;
                break;
            }
            if (!tok.self_closing) {
                stack[stack_top] = child;
                tlen[stack_top] = 0;
                tcap[stack_top] = 0;
                stack_top++;
            }
            break;
        }
        case NEVERC_XML_END_ELEMENT:
            if (stack_top <= 1 || !tok.name ||
                !stack[stack_top - 1]->tag ||
                strcmp(stack[stack_top - 1]->tag, tok.name) != 0) {
                failed = 1;
            } else {
                stack_top--;
            }
            break;
        case NEVERC_XML_CHAR_DATA: {
            int si = stack_top - 1;
            if (si == 0) {
                for (size_t i = 0; i < tok.data_len; i++) {
                    char c = tok.data[i];
                    if (c != ' ' && c != '\t' &&
                        c != '\n' && c != '\r') {
                        failed = 1;
                        break;
                    }
                }
                break;
            }
            neverc_xml_node_t *cur = stack[si];
            size_t nlen = tok.data_len;
            if (!cur->text) {
                /* First run for this element: steal the token's buffer
                 * (dup_range already allocated exactly data_len + 1). */
                cur->text = tok.data;
                tok.data = NULL;
                tlen[si] = nlen;
                tcap[si] = nlen + 1;
            } else if (nlen > 0) {
                if (tlen[si] == SIZE_MAX ||
                    nlen > SIZE_MAX - tlen[si] - 1) {
                    failed = 1;
                    break;
                }
                size_t need = tlen[si] + nlen + 1;
                if (need > tcap[si]) {
                    size_t nc = tcap[si] * 2;
                    if (nc < tcap[si] || nc < need) nc = need;
                    char *grown = (char *)realloc(cur->text, nc);
                    if (!grown) {
                        failed = 1;
                        break;
                    }
                    cur->text = grown;
                    tcap[si] = nc;
                }
                memcpy(cur->text + tlen[si], tok.data, nlen);
                tlen[si] += nlen;
                cur->text[tlen[si]] = '\0';
            }
            break;
        }
        default:
            break;
        }
        neverc_xml_token_free(&tok);
        if (failed) goto parse_fail;
    }

    if (decode_result < 0 || stack_top != 1 || document_elements != 1)
        goto parse_fail;

    free(stack);
    free(tlen);
    free(tcap);
    return root;

parse_fail:
    free(stack);
    free(tlen);
    free(tcap);
    neverc_xml_node_free(root);
    return NULL;
}

static void xml_node_release_self(neverc_xml_node_t *node) {
    free(node->tag);
    free(node->text);
    free_attrs(node->attrs, node->nattrs);
    free(node->children);
    free(node);
}

static void xml_node_free_recursive(neverc_xml_node_t *node) {
    int i;
    if (!node) return;
    for (i = 0; i < node->nchildren; i++)
        xml_node_free_recursive(node->children[i]);
    xml_node_release_self(node);
}

/* Iterative free: a 1000-deep tree (the parse cap) plus sanitizer frames can
 * overflow small thread stacks if this recurses per child. */
void neverc_xml_node_free(neverc_xml_node_t *root) {
    neverc_xml_node_t **stack;
    int cap, top;
    if (!root) return;
    cap = 8;
    stack = (neverc_xml_node_t **)malloc((size_t)cap * sizeof(*stack));
    if (!stack) {
        xml_node_free_recursive(root);
        return;
    }
    top = 0;
    stack[top++] = root;
    while (top > 0) {
        neverc_xml_node_t *node = stack[--top];
        int i;
        for (i = 0; i < node->nchildren; i++) {
            neverc_xml_node_t *child = node->children[i];
            if (!child) continue;
            if (top >= cap) {
                neverc_xml_node_t **grown;
                int next_cap;
                if (cap > INT32_MAX / 2 ||
                    (size_t)cap * 2U > SIZE_MAX / sizeof(*stack)) {
                    xml_node_free_recursive(child);
                    continue;
                }
                next_cap = cap * 2;
                grown = (neverc_xml_node_t **)realloc(
                    stack, (size_t)next_cap * sizeof(*stack));
                if (!grown) {
                    xml_node_free_recursive(child);
                    continue;
                }
                stack = grown;
                cap = next_cap;
            }
            stack[top++] = child;
        }
        node->nchildren = 0;
        xml_node_release_self(node);
    }
    free(stack);
}

const char *neverc_xml_node_attr(const neverc_xml_node_t *node, const char *name) {
    if (!node || !name || node->nattrs < 0) return NULL;
    for (int i = 0; i < node->nattrs; i++)
        if (strcmp(node->attrs[i].name, name) == 0)
            return node->attrs[i].value;
    return NULL;
}

neverc_xml_node_t *neverc_xml_node_child(const neverc_xml_node_t *node,
                                          const char *tag) {
    if (!node || !tag || node->nchildren < 0) return NULL;
    for (int i = 0; i < node->nchildren; i++)
        if (node->children[i]->tag && strcmp(node->children[i]->tag, tag) == 0)
            return node->children[i];
    return NULL;
}

/*
 * Per-byte expansion table: xml_esc_extra[c] is the extra bytes c's escape adds
 * beyond the original byte (0 for self-representing bytes), doubling as the
 * "is special" predicate.  & -> &amp; (4),  < > -> 4-char (3),  " ' -> 6-char (5).
 */
static const uint8_t xml_esc_extra[256] = {
    ['&'] = 4, ['<'] = 3, ['>'] = 3, ['"'] = 5, ['\''] = 5,
};

char *neverc_xml_escape(const char *s, size_t *outlen) {
    if (!outlen) return NULL;
    *outlen = 0;
    if (!s) return NULL;
    size_t slen = strlen(s);

    for (size_t position = 0; position < slen;) {
        size_t consumed = 0;
        uint32_t codepoint = 0;
        if (xml_decode_utf8(
                s + position, slen - position,
                &consumed, &codepoint) != 0)
            return NULL;
        position += consumed;
    }

    /* Branchless pass to size the output exactly (no realloc, no slack). */
    size_t extra = 0;
    for (size_t i = 0; i < slen; i++) {
        if (xml_esc_extra[(unsigned char)s[i]] > SIZE_MAX - extra) return NULL;
        extra += xml_esc_extra[(unsigned char)s[i]];
    }

    if (extra > SIZE_MAX - slen - 1) return NULL;
    char *r = (char *)malloc(slen + extra + 1);
    if (!r) return NULL;

    /* Fast path: nothing needs escaping, copy the whole string in one go. */
    if (extra == 0) {
        memcpy(r, s, slen);
        r[slen] = '\0';
        *outlen = slen;
        return r;
    }

    /* Single read: store self-representing bytes directly (buffer is exact, no
     * bounds check); specials expand via constant-size memcpy the compiler
     * inlines. */
    size_t wi = 0;
    for (size_t i = 0; i < slen; i++) {
        unsigned char c = (unsigned char)s[i];
        if (xml_esc_extra[c] == 0) { r[wi++] = (char)c; continue; }
        switch (c) {
            case '&':  memcpy(r + wi, "&amp;",  5); wi += 5; break;
            case '<':  memcpy(r + wi, "&lt;",   4); wi += 4; break;
            case '>':  memcpy(r + wi, "&gt;",   4); wi += 4; break;
            case '"':  memcpy(r + wi, "&quot;", 6); wi += 6; break;
            case '\'': memcpy(r + wi, "&apos;", 6); wi += 6; break;
        }
    }
    r[wi] = '\0';
    *outlen = wi;
    return r;
}

char *neverc_xml_unescape(const char *s, size_t len, size_t *outlen) {
    if (!outlen)
        return NULL;
    *outlen = 0;
    if (!s)
        return NULL;
    return xml_decode_text(s, len, 0, outlen);
}
