#include "neverc/encoding/pem.h"
#include "neverc/encoding/base64.h"
#include <string.h>

/*
 * PEM encoding/decoding — simplified C port of Go encoding/pem.
 * Uses neverc_base64_encode/decode for the payload.
 */

#define PEM_LINE_LEN 64

static const char *BEGIN_PREFIX = "-----BEGIN ";
static const char *END_PREFIX   = "-----END ";
static const char *DASHES       = "-----";

int neverc_pem_encode(char *out, size_t out_cap,
                      const char *type_str,
                      const uint8_t *data, size_t data_len)
{
    size_t type_len = strlen(type_str);
    size_t b64_len = neverc_base64_encoded_len(data_len);

    size_t line_breaks = (b64_len > 0) ? ((b64_len - 1) / PEM_LINE_LEN) : 0;

    size_t needed = 11 + type_len + 6    /* -----BEGIN <type>-----\n */
                  + b64_len + line_breaks + 1 /* base64 + newlines */
                  + 9 + type_len + 6     /* -----END <type>-----\n */
                  + 1;                   /* NUL */

    if (out_cap < needed) return -1;

    char *p = out;
    memcpy(p, BEGIN_PREFIX, 11); p += 11;
    memcpy(p, type_str, type_len); p += type_len;
    memcpy(p, DASHES, 5); p += 5;
    *p++ = '\n';

    size_t raw_b64_len = b64_len;
    char *temp = (char *)(out + out_cap - raw_b64_len - 1);
    neverc_base64_encode(temp, (const uint8_t *)data, data_len);

    for (size_t i = 0; i < raw_b64_len; i += PEM_LINE_LEN) {
        size_t chunk = raw_b64_len - i;
        if (chunk > PEM_LINE_LEN) chunk = PEM_LINE_LEN;
        memcpy(p, temp + i, chunk);
        p += chunk;
        *p++ = '\n';
    }

    memcpy(p, END_PREFIX, 9); p += 9;
    memcpy(p, type_str, type_len); p += type_len;
    memcpy(p, DASHES, 5); p += 5;
    *p++ = '\n';
    *p = '\0';

    return (int)(p - out);
}

int neverc_pem_decode(const char *pem_data, size_t pem_len,
                      char *type_buf, size_t type_cap,
                      uint8_t *out_buf, size_t out_cap,
                      size_t *bytes_written,
                      size_t *rest_offset)
{
    if (bytes_written) *bytes_written = 0;
    if (rest_offset) *rest_offset = pem_len;

    const char *begin = NULL;
    for (size_t i = 0; i + 11 <= pem_len; i++) {
        if (memcmp(pem_data + i, BEGIN_PREFIX, 11) == 0) {
            if (i == 0 || pem_data[i - 1] == '\n' || pem_data[i - 1] == '\r') {
                begin = pem_data + i;
                break;
            }
        }
    }
    if (!begin) return -1;

    const char *type_start = begin + 11;
    const char *dash_end = strstr(type_start, DASHES);
    if (!dash_end) return -1;

    size_t type_len = (size_t)(dash_end - type_start);
    if (type_len >= type_cap) return -1;
    memcpy(type_buf, type_start, type_len);
    type_buf[type_len] = '\0';

    const char *body_start = dash_end + 5;
    while (body_start < pem_data + pem_len && (*body_start == '\n' || *body_start == '\r'))
        body_start++;

    char end_marker[256];
    size_t em_len = 0;
    size_t ep_len = strlen(END_PREFIX);
    size_t ds_len = strlen(DASHES);
    if (ep_len + type_len + ds_len >= sizeof(end_marker)) return -1;
    memcpy(end_marker + em_len, END_PREFIX, ep_len); em_len += ep_len;
    memcpy(end_marker + em_len, type_buf, type_len); em_len += type_len;
    memcpy(end_marker + em_len, DASHES, ds_len); em_len += ds_len;
    end_marker[em_len] = '\0';

    const char *end = NULL;
    for (const char *s = body_start; s + em_len <= pem_data + pem_len; s++) {
        if (memcmp(s, end_marker, (size_t)em_len) == 0) {
            end = s;
            break;
        }
    }
    if (!end) return -1;

    size_t b64_raw_len = (size_t)(end - body_start);

    /* Count non-whitespace base64 characters */
    size_t clean_len = 0;
    for (size_t i = 0; i < b64_raw_len; i++) {
        char c = body_start[i];
        if (c != '\n' && c != '\r' && c != ' ' && c != '\t')
            clean_len++;
    }

    size_t decoded_max = neverc_base64_decoded_len(clean_len);
    if (decoded_max > out_cap) return -1;

    /* Decode base64 inline, skipping whitespace. Since decoded output
       is always shorter than encoded input, we can safely use out_buf. */
    static const int8_t b64tab[256] = {
        ['A']=0, ['B']=1, ['C']=2, ['D']=3, ['E']=4, ['F']=5, ['G']=6, ['H']=7,
        ['I']=8, ['J']=9, ['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
        ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
        ['Y']=24,['Z']=25,
        ['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,['g']=32,['h']=33,
        ['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,['o']=40,['p']=41,
        ['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,['w']=48,['x']=49,
        ['y']=50,['z']=51,
        ['0']=52,['1']=53,['2']=54,['3']=55,['4']=56,['5']=57,['6']=58,['7']=59,
        ['8']=60,['9']=61,['+']=62,['/']=63,
    };

    uint8_t quad[4];
    int qi = 0;
    size_t out_i = 0;
    int pad = 0;
    for (size_t i = 0; i < b64_raw_len; i++) {
        unsigned char c = (unsigned char)body_start[i];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        if (c == '=') { quad[qi++] = 0; pad++; }
        else quad[qi++] = (uint8_t)b64tab[c];
        if (qi == 4) {
            if (out_i < out_cap) out_buf[out_i++] = (quad[0]<<2) | (quad[1]>>4);
            if (pad < 2 && out_i < out_cap) out_buf[out_i++] = (quad[1]<<4) | (quad[2]>>2);
            if (pad < 1 && out_i < out_cap) out_buf[out_i++] = (quad[2]<<6) | quad[3];
            qi = 0; pad = 0;
        }
    }
    int decoded = (int)out_i;
    if (decoded < 0) return -1;

    if (bytes_written) *bytes_written = (size_t)decoded;

    const char *after_end = end + em_len;
    while (after_end < pem_data + pem_len && (*after_end == '-' || *after_end == '\n' || *after_end == '\r'))
        after_end++;
    if (rest_offset) *rest_offset = (size_t)(after_end - pem_data);

    return 0;
}
