#include "neverc/encoding/base64.h"

static const char std_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char url_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static const uint8_t decode_std[256] = {
    ['A']=0,  ['B']=1,  ['C']=2,  ['D']=3,  ['E']=4,  ['F']=5,  ['G']=6,  ['H']=7,
    ['I']=8,  ['J']=9,  ['K']=10, ['L']=11, ['M']=12, ['N']=13, ['O']=14, ['P']=15,
    ['Q']=16, ['R']=17, ['S']=18, ['T']=19, ['U']=20, ['V']=21, ['W']=22, ['X']=23,
    ['Y']=24, ['Z']=25,
    ['a']=26, ['b']=27, ['c']=28, ['d']=29, ['e']=30, ['f']=31, ['g']=32, ['h']=33,
    ['i']=34, ['j']=35, ['k']=36, ['l']=37, ['m']=38, ['n']=39, ['o']=40, ['p']=41,
    ['q']=42, ['r']=43, ['s']=44, ['t']=45, ['u']=46, ['v']=47, ['w']=48, ['x']=49,
    ['y']=50, ['z']=51,
    ['0']=52, ['1']=53, ['2']=54, ['3']=55, ['4']=56, ['5']=57, ['6']=58, ['7']=59,
    ['8']=60, ['9']=61,
    ['+']=62, ['/']=63,
    ['-']=62, ['_']=63,
};

static const uint8_t valid_std[256] = {
    ['A']=1,['B']=1,['C']=1,['D']=1,['E']=1,['F']=1,['G']=1,['H']=1,
    ['I']=1,['J']=1,['K']=1,['L']=1,['M']=1,['N']=1,['O']=1,['P']=1,
    ['Q']=1,['R']=1,['S']=1,['T']=1,['U']=1,['V']=1,['W']=1,['X']=1,
    ['Y']=1,['Z']=1,
    ['a']=1,['b']=1,['c']=1,['d']=1,['e']=1,['f']=1,['g']=1,['h']=1,
    ['i']=1,['j']=1,['k']=1,['l']=1,['m']=1,['n']=1,['o']=1,['p']=1,
    ['q']=1,['r']=1,['s']=1,['t']=1,['u']=1,['v']=1,['w']=1,['x']=1,
    ['y']=1,['z']=1,
    ['0']=1,['1']=1,['2']=1,['3']=1,['4']=1,['5']=1,['6']=1,['7']=1,
    ['8']=1,['9']=1,
    ['+']=1,['/']=1,
    ['-']=1,['_']=1,
    ['=']=1,
};

size_t neverc_base64_encoded_len(size_t n) {
    return ((n + 2) / 3) * 4;
}

size_t neverc_base64_decoded_len(size_t n) {
    return (n / 4) * 3;
}

static size_t encode_with_table(char *dst, const uint8_t *src, size_t src_len,
                                const char *table) {
    size_t di = 0;
    size_t si = 0;

    size_t n = (src_len / 3) * 3;
    while (si < n) {
        uint32_t val = ((uint32_t)src[si] << 16) |
                       ((uint32_t)src[si+1] << 8) |
                       (uint32_t)src[si+2];
        dst[di]   = table[(val >> 18) & 0x3f];
        dst[di+1] = table[(val >> 12) & 0x3f];
        dst[di+2] = table[(val >> 6)  & 0x3f];
        dst[di+3] = table[val         & 0x3f];
        si += 3;
        di += 4;
    }

    size_t remain = src_len - si;
    if (remain == 1) {
        uint32_t val = (uint32_t)src[si] << 16;
        dst[di]   = table[(val >> 18) & 0x3f];
        dst[di+1] = table[(val >> 12) & 0x3f];
        dst[di+2] = '=';
        dst[di+3] = '=';
        di += 4;
    } else if (remain == 2) {
        uint32_t val = ((uint32_t)src[si] << 16) |
                       ((uint32_t)src[si+1] << 8);
        dst[di]   = table[(val >> 18) & 0x3f];
        dst[di+1] = table[(val >> 12) & 0x3f];
        dst[di+2] = table[(val >> 6)  & 0x3f];
        dst[di+3] = '=';
        di += 4;
    }

    dst[di] = '\0';
    return di;
}

static int decode_impl(uint8_t *dst, const char *src, size_t src_len) {
    while (src_len > 0 && src[src_len - 1] == '=')
        src_len--;

    size_t di = 0;
    size_t si = 0;

    size_t n = (src_len / 4) * 4;
    while (si < n) {
        if (!valid_std[(uint8_t)src[si]] || !valid_std[(uint8_t)src[si+1]] ||
            !valid_std[(uint8_t)src[si+2]] || !valid_std[(uint8_t)src[si+3]])
            return -1;
        uint32_t val = ((uint32_t)decode_std[(uint8_t)src[si]] << 18) |
                       ((uint32_t)decode_std[(uint8_t)src[si+1]] << 12) |
                       ((uint32_t)decode_std[(uint8_t)src[si+2]] << 6) |
                       (uint32_t)decode_std[(uint8_t)src[si+3]];
        dst[di]   = (uint8_t)(val >> 16);
        dst[di+1] = (uint8_t)(val >> 8);
        dst[di+2] = (uint8_t)val;
        si += 4;
        di += 3;
    }

    size_t remain = src_len - si;
    if (remain == 2) {
        if (!valid_std[(uint8_t)src[si]] || !valid_std[(uint8_t)src[si+1]])
            return -1;
        uint32_t val = ((uint32_t)decode_std[(uint8_t)src[si]] << 18) |
                       ((uint32_t)decode_std[(uint8_t)src[si+1]] << 12);
        dst[di] = (uint8_t)(val >> 16);
        di += 1;
    } else if (remain == 3) {
        if (!valid_std[(uint8_t)src[si]] || !valid_std[(uint8_t)src[si+1]] ||
            !valid_std[(uint8_t)src[si+2]])
            return -1;
        uint32_t val = ((uint32_t)decode_std[(uint8_t)src[si]] << 18) |
                       ((uint32_t)decode_std[(uint8_t)src[si+1]] << 12) |
                       ((uint32_t)decode_std[(uint8_t)src[si+2]] << 6);
        dst[di]   = (uint8_t)(val >> 16);
        dst[di+1] = (uint8_t)(val >> 8);
        di += 2;
    } else if (remain == 1) {
        return -1;
    }

    return (int)di;
}

size_t neverc_base64_encode(char *dst, const uint8_t *src, size_t src_len) {
    return encode_with_table(dst, src, src_len, std_table);
}

int neverc_base64_decode(uint8_t *dst, const char *src, size_t src_len) {
    return decode_impl(dst, src, src_len);
}

size_t neverc_base64_url_encode(char *dst, const uint8_t *src, size_t src_len) {
    return encode_with_table(dst, src, src_len, url_table);
}

int neverc_base64_url_decode(uint8_t *dst, const char *src, size_t src_len) {
    return decode_impl(dst, src, src_len);
}
