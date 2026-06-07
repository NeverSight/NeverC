#include "neverc/std/encoding/base32.h"

/*
 * Base32 encoding/decoding per RFC 4648.
 *
 * Standard alphabet: A-Z 2-7 (values 0-31)
 * Hex alphabet:      0-9 A-V (values 0-31)
 *
 * Encoding: every 5 input bytes → 8 output characters
 * Padding with '=' to make output length a multiple of 8.
 */

static const char std_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
static const char hex_table[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

static const uint8_t decode_std[256] = {
    ['A']= 0, ['B']= 1, ['C']= 2, ['D']= 3, ['E']= 4, ['F']= 5, ['G']= 6, ['H']= 7,
    ['I']= 8, ['J']= 9, ['K']=10, ['L']=11, ['M']=12, ['N']=13, ['O']=14, ['P']=15,
    ['Q']=16, ['R']=17, ['S']=18, ['T']=19, ['U']=20, ['V']=21, ['W']=22, ['X']=23,
    ['Y']=24, ['Z']=25,
    ['2']=26, ['3']=27, ['4']=28, ['5']=29, ['6']=30, ['7']=31,
    ['a']= 0, ['b']= 1, ['c']= 2, ['d']= 3, ['e']= 4, ['f']= 5, ['g']= 6, ['h']= 7,
    ['i']= 8, ['j']= 9, ['k']=10, ['l']=11, ['m']=12, ['n']=13, ['o']=14, ['p']=15,
    ['q']=16, ['r']=17, ['s']=18, ['t']=19, ['u']=20, ['v']=21, ['w']=22, ['x']=23,
    ['y']=24, ['z']=25,
};

static const uint8_t decode_hex[256] = {
    ['0']= 0, ['1']= 1, ['2']= 2, ['3']= 3, ['4']= 4, ['5']= 5, ['6']= 6, ['7']= 7,
    ['8']= 8, ['9']= 9,
    ['A']=10, ['B']=11, ['C']=12, ['D']=13, ['E']=14, ['F']=15, ['G']=16, ['H']=17,
    ['I']=18, ['J']=19, ['K']=20, ['L']=21, ['M']=22, ['N']=23, ['O']=24, ['P']=25,
    ['Q']=26, ['R']=27, ['S']=28, ['T']=29, ['U']=30, ['V']=31,
    ['a']=10, ['b']=11, ['c']=12, ['d']=13, ['e']=14, ['f']=15, ['g']=16, ['h']=17,
    ['i']=18, ['j']=19, ['k']=20, ['l']=21, ['m']=22, ['n']=23, ['o']=24, ['p']=25,
    ['q']=26, ['r']=27, ['s']=28, ['t']=29, ['u']=30, ['v']=31,
};

static const uint8_t valid_std[256] = {
    ['A']=1,['B']=1,['C']=1,['D']=1,['E']=1,['F']=1,['G']=1,['H']=1,
    ['I']=1,['J']=1,['K']=1,['L']=1,['M']=1,['N']=1,['O']=1,['P']=1,
    ['Q']=1,['R']=1,['S']=1,['T']=1,['U']=1,['V']=1,['W']=1,['X']=1,
    ['Y']=1,['Z']=1, ['2']=1,['3']=1,['4']=1,['5']=1,['6']=1,['7']=1,
    ['a']=1,['b']=1,['c']=1,['d']=1,['e']=1,['f']=1,['g']=1,['h']=1,
    ['i']=1,['j']=1,['k']=1,['l']=1,['m']=1,['n']=1,['o']=1,['p']=1,
    ['q']=1,['r']=1,['s']=1,['t']=1,['u']=1,['v']=1,['w']=1,['x']=1,
    ['y']=1,['z']=1, ['=']=1,
};

static const uint8_t valid_hex[256] = {
    ['0']=1,['1']=1,['2']=1,['3']=1,['4']=1,['5']=1,['6']=1,['7']=1,
    ['8']=1,['9']=1,
    ['A']=1,['B']=1,['C']=1,['D']=1,['E']=1,['F']=1,['G']=1,['H']=1,
    ['I']=1,['J']=1,['K']=1,['L']=1,['M']=1,['N']=1,['O']=1,['P']=1,
    ['Q']=1,['R']=1,['S']=1,['T']=1,['U']=1,['V']=1,
    ['a']=1,['b']=1,['c']=1,['d']=1,['e']=1,['f']=1,['g']=1,['h']=1,
    ['i']=1,['j']=1,['k']=1,['l']=1,['m']=1,['n']=1,['o']=1,['p']=1,
    ['q']=1,['r']=1,['s']=1,['t']=1,['u']=1,['v']=1,
    ['=']=1,
};

size_t neverc_base32_encoded_len(size_t n) {
    return ((n + 4) / 5) * 8;
}

size_t neverc_base32_decoded_len(size_t n) {
    return (n / 8) * 5;
}

static size_t encode_with_table(char *dst, const uint8_t *src, size_t src_len,
                                const char *table) {
    size_t di = 0;
    size_t si = 0;

    size_t n = (src_len / 5) * 5;
    while (si < n) {
        uint64_t val = ((uint64_t)src[si] << 32) |
                       ((uint64_t)src[si+1] << 24) |
                       ((uint64_t)src[si+2] << 16) |
                       ((uint64_t)src[si+3] << 8) |
                       (uint64_t)src[si+4];
        dst[di]   = table[(val >> 35) & 0x1f];
        dst[di+1] = table[(val >> 30) & 0x1f];
        dst[di+2] = table[(val >> 25) & 0x1f];
        dst[di+3] = table[(val >> 20) & 0x1f];
        dst[di+4] = table[(val >> 15) & 0x1f];
        dst[di+5] = table[(val >> 10) & 0x1f];
        dst[di+6] = table[(val >> 5)  & 0x1f];
        dst[di+7] = table[val         & 0x1f];
        si += 5;
        di += 8;
    }

    size_t remain = src_len - si;
    if (remain > 0) {
        uint64_t val = 0;
        for (size_t k = 0; k < remain; k++)
            val |= (uint64_t)src[si + k] << (32 - k * 8);

        dst[di]   = table[(val >> 35) & 0x1f];
        dst[di+1] = table[(val >> 30) & 0x1f];
        int out_chars;
        switch (remain) {
        case 1: out_chars = 2; break;
        case 2: out_chars = 4; break;
        case 3: out_chars = 5; break;
        case 4: out_chars = 7; break;
        default: out_chars = 0; break;
        }
        if (out_chars > 2) dst[di+2] = table[(val >> 25) & 0x1f];
        if (out_chars > 3) dst[di+3] = table[(val >> 20) & 0x1f];
        if (out_chars > 4) dst[di+4] = table[(val >> 15) & 0x1f];
        if (out_chars > 5) dst[di+5] = table[(val >> 10) & 0x1f];
        if (out_chars > 6) dst[di+6] = table[(val >> 5)  & 0x1f];
        for (int k = out_chars; k < 8; k++)
            dst[di + k] = '=';
        di += 8;
    }

    dst[di] = '\0';
    return di;
}

static int decode_impl(uint8_t *dst, const char *src, size_t src_len,
                       const uint8_t *dec_table, const uint8_t *val_table) {
    while (src_len > 0 && src[src_len - 1] == '=')
        src_len--;

    size_t di = 0;
    size_t si = 0;

    size_t n = (src_len / 8) * 8;
    while (si < n) {
        for (int k = 0; k < 8; k++)
            if (!val_table[(uint8_t)src[si + k]]) return -1;

        uint64_t val = ((uint64_t)dec_table[(uint8_t)src[si]]   << 35) |
                       ((uint64_t)dec_table[(uint8_t)src[si+1]] << 30) |
                       ((uint64_t)dec_table[(uint8_t)src[si+2]] << 25) |
                       ((uint64_t)dec_table[(uint8_t)src[si+3]] << 20) |
                       ((uint64_t)dec_table[(uint8_t)src[si+4]] << 15) |
                       ((uint64_t)dec_table[(uint8_t)src[si+5]] << 10) |
                       ((uint64_t)dec_table[(uint8_t)src[si+6]] << 5)  |
                       (uint64_t)dec_table[(uint8_t)src[si+7]];
        dst[di]   = (uint8_t)(val >> 32);
        dst[di+1] = (uint8_t)(val >> 24);
        dst[di+2] = (uint8_t)(val >> 16);
        dst[di+3] = (uint8_t)(val >> 8);
        dst[di+4] = (uint8_t)val;
        si += 8;
        di += 5;
    }

    size_t remain = src_len - si;
    if (remain > 0) {
        for (size_t k = 0; k < remain; k++)
            if (!val_table[(uint8_t)src[si + k]]) return -1;

        uint64_t val = 0;
        for (size_t k = 0; k < remain; k++)
            val |= (uint64_t)dec_table[(uint8_t)src[si + k]] << (35 - k * 5);

        int out_bytes;
        switch (remain) {
        case 2: out_bytes = 1; break;
        case 4: out_bytes = 2; break;
        case 5: out_bytes = 3; break;
        case 7: out_bytes = 4; break;
        default: return -1;
        }
        if (out_bytes > 0) dst[di++] = (uint8_t)(val >> 32);
        if (out_bytes > 1) dst[di++] = (uint8_t)(val >> 24);
        if (out_bytes > 2) dst[di++] = (uint8_t)(val >> 16);
        if (out_bytes > 3) dst[di++] = (uint8_t)(val >> 8);
    }

    return (int)di;
}

size_t neverc_base32_encode(char *dst, const uint8_t *src, size_t src_len) {
    return encode_with_table(dst, src, src_len, std_table);
}

int neverc_base32_decode(uint8_t *dst, const char *src, size_t src_len) {
    return decode_impl(dst, src, src_len, decode_std, valid_std);
}

size_t neverc_base32_hex_encode(char *dst, const uint8_t *src, size_t src_len) {
    return encode_with_table(dst, src, src_len, hex_table);
}

int neverc_base32_hex_decode(uint8_t *dst, const char *src, size_t src_len) {
    return decode_impl(dst, src, src_len, decode_hex, valid_hex);
}
