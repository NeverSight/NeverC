#include "neverc/std/crypto/des.h"
#include "neverc/std/_platform.h"
#include <string.h>

/*
 * DES / Triple DES, ported from Go crypto/des.
 * Originally FIPS 46-3; bit manipulation tricks from Go's block.go.
 */

static const uint8_t sBoxes[8][4][16] = {
    {{14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7},{0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8},{4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0},{15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13}},
    {{15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10},{3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5},{0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15},{13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9}},
    {{10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8},{13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1},{13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7},{1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12}},
    {{7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15},{13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9},{10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4},{3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14}},
    {{2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9},{14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6},{4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14},{11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3}},
    {{12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11},{10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8},{9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6},{4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13}},
    {{4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1},{13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6},{1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2},{6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12}},
    {{13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7},{1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2},{7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8},{2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11}},
};

static const uint8_t permutationFunction[32] = {
    16,25,12,11,3,20,4,15,31,17,9,6,27,14,1,22,
    30,24,8,18,0,5,29,23,13,19,2,26,10,21,28,7,
};

static const uint8_t permutedChoice1[56] = {
    7,15,23,31,39,47,55,63,6,14,22,30,38,46,54,62,
    5,13,21,29,37,45,53,61,4,12,20,28,1,9,17,25,
    33,41,49,57,2,10,18,26,34,42,50,58,3,11,19,27,
    35,43,51,59,36,44,52,60,
};

static const uint8_t permutedChoice2[48] = {
    42,39,45,32,55,51,53,28,41,50,35,46,33,37,44,52,
    30,48,40,49,29,36,43,54,15,4,25,19,9,1,26,16,
    5,11,23,8,12,7,17,0,22,3,10,14,6,20,27,24,
};

static const uint8_t ksRotations[16] = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};

static uint32_t feistelBox[8][64];
static int feistelBoxInit = 0; /* 0 = uninit, 1 = initializing, 2 = ready */

/* FIPS 46-3 weak (4) and semi-weak (12) keys. Parity bits are ignored
 * when comparing, so 00..00 matches 01..01. */
static const uint8_t des_weak_keys[][8] = {
    {0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01},
    {0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE},
    {0x1F,0x1F,0x1F,0x1F,0x0E,0x0E,0x0E,0x0E},
    {0xE0,0xE0,0xE0,0xE0,0xF1,0xF1,0xF1,0xF1},
    {0x01,0xFE,0x01,0xFE,0x01,0xFE,0x01,0xFE},
    {0xFE,0x01,0xFE,0x01,0xFE,0x01,0xFE,0x01},
    {0x1F,0xE0,0x1F,0xE0,0x0E,0xF1,0x0E,0xF1},
    {0xE0,0x1F,0xE0,0x1F,0xF1,0x0E,0xF1,0x0E},
    {0x01,0xE0,0x01,0xE0,0x01,0xF1,0x01,0xF1},
    {0xE0,0x01,0xE0,0x01,0xF1,0x01,0xF1,0x01},
    {0x1F,0xFE,0x1F,0xFE,0x0E,0xFE,0x0E,0xFE},
    {0xFE,0x1F,0xFE,0x1F,0xFE,0x0E,0xFE,0x0E},
    {0x01,0x1F,0x01,0x1F,0x01,0x0E,0x01,0x0E},
    {0x1F,0x01,0x1F,0x01,0x0E,0x01,0x0E,0x01},
    {0xE0,0xFE,0xE0,0xFE,0xF1,0xFE,0xF1,0xFE},
    {0xFE,0xE0,0xFE,0xE0,0xFE,0xF1,0xFE,0xF1},
};

static int des_keys_equal_ignore_parity(const uint8_t a[8], const uint8_t b[8]) {
    int diff = 0;
    for (int i = 0; i < 8; i++)
        diff |= (a[i] ^ b[i]) & 0xFE;
    return diff == 0;
}

static uint64_t permuteBlock(uint64_t src, const uint8_t *perm, int n) {
    uint64_t block = 0;
    for (int pos = 0; pos < n; pos++) {
        uint64_t bit = (src >> perm[pos]) & 1;
        block |= bit << (uint64_t)(n - 1 - pos);
    }
    return block;
}

static uint64_t permuteInitialBlock(uint64_t block) {
    uint64_t b1 = block >> 48;
    uint64_t b2 = block << 48;
    block ^= b1 ^ b2 ^ (b1 << 48) ^ (b2 >> 48);

    b1 = (block >> 32) & 0xff00ffULL;
    b2 = block & 0xff00ff00ULL;
    block ^= (b1 << 32) ^ b2 ^ (b1 << 8) ^ (b2 << 24);

    b1 = block & 0x0f0f00000f0f0000ULL;
    b2 = block & 0x0000f0f00000f0f0ULL;
    block ^= b1 ^ b2 ^ (b1 >> 12) ^ (b2 << 12);

    b1 = block & 0x3300330033003300ULL;
    b2 = block & 0x00cc00cc00cc00ccULL;
    block ^= b1 ^ b2 ^ (b1 >> 6) ^ (b2 << 6);

    b1 = block & 0xaaaaaaaa55555555ULL;
    block ^= b1 ^ (b1 >> 33) ^ (b1 << 33);
    return block;
}

static uint64_t permuteFinalBlock(uint64_t block) {
    uint64_t b1 = block & 0xaaaaaaaa55555555ULL;
    block ^= b1 ^ (b1 >> 33) ^ (b1 << 33);

    b1 = block & 0x3300330033003300ULL;
    uint64_t b2 = block & 0x00cc00cc00cc00ccULL;
    block ^= b1 ^ b2 ^ (b1 >> 6) ^ (b2 << 6);

    b1 = block & 0x0f0f00000f0f0000ULL;
    b2 = block & 0x0000f0f00000f0f0ULL;
    block ^= b1 ^ b2 ^ (b1 >> 12) ^ (b2 << 12);

    b1 = (block >> 32) & 0xff00ffULL;
    b2 = block & 0xff00ff00ULL;
    block ^= (b1 << 32) ^ b2 ^ (b1 << 8) ^ (b2 << 24);

    b1 = block >> 48;
    b2 = block << 48;
    block ^= b1 ^ b2 ^ (b1 << 48) ^ (b2 >> 48);
    return block;
}

static void initFeistelBox(void) {
    if (NEVERC_ATOMIC_LOAD32(&feistelBoxInit) == 2)
        return;
    if (NEVERC_ATOMIC_CAS32(&feistelBoxInit, 0, 1)) {
        for (int s = 0; s < 8; s++) {
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 16; j++) {
                    uint64_t f = (uint64_t)sBoxes[s][i][j] << (4 * (7 - s));
                    f = permuteBlock(f, permutationFunction, 32);
                    uint8_t row = (uint8_t)(((i & 2) << 4) | (i & 1));
                    uint8_t col = (uint8_t)(j << 1);
                    uint8_t t = row | col;
                    f = (f << 1) | (f >> 31);
                    feistelBox[s][t] = (uint32_t)f;
                }
            }
        }
        NEVERC_ATOMIC_STORE32(&feistelBoxInit, 2);
        return;
    }
    while (NEVERC_ATOMIC_LOAD32(&feistelBoxInit) != 2) {
    }
}

static void feistel(uint32_t l, uint32_t r, uint64_t k0, uint64_t k1,
                    uint32_t *lo, uint32_t *ro) {
    uint32_t t;
    t = r ^ (uint32_t)(k0 >> 32);
    l ^= feistelBox[7][t & 0x3f] ^ feistelBox[5][(t >> 8) & 0x3f] ^
         feistelBox[3][(t >> 16) & 0x3f] ^ feistelBox[1][(t >> 24) & 0x3f];
    t = ((r << 28) | (r >> 4)) ^ (uint32_t)k0;
    l ^= feistelBox[6][t & 0x3f] ^ feistelBox[4][(t >> 8) & 0x3f] ^
         feistelBox[2][(t >> 16) & 0x3f] ^ feistelBox[0][(t >> 24) & 0x3f];

    t = l ^ (uint32_t)(k1 >> 32);
    r ^= feistelBox[7][t & 0x3f] ^ feistelBox[5][(t >> 8) & 0x3f] ^
         feistelBox[3][(t >> 16) & 0x3f] ^ feistelBox[1][(t >> 24) & 0x3f];
    t = ((l << 28) | (l >> 4)) ^ (uint32_t)k1;
    r ^= feistelBox[6][t & 0x3f] ^ feistelBox[4][(t >> 8) & 0x3f] ^
         feistelBox[2][(t >> 16) & 0x3f] ^ feistelBox[0][(t >> 24) & 0x3f];
    *lo = l;
    *ro = r;
}

static uint64_t unpack(uint64_t x) {
    return ((x >> (6*1)) & 0xff) << (8*0) |
           ((x >> (6*3)) & 0xff) << (8*1) |
           ((x >> (6*5)) & 0xff) << (8*2) |
           ((x >> (6*7)) & 0xff) << (8*3) |
           ((x >> (6*0)) & 0xff) << (8*4) |
           ((x >> (6*2)) & 0xff) << (8*5) |
           ((x >> (6*4)) & 0xff) << (8*6) |
           ((x >> (6*6)) & 0xff) << (8*7);
}

static uint64_t beU64(const uint8_t b[8]) {
    return ((uint64_t)b[0]<<56)|((uint64_t)b[1]<<48)|((uint64_t)b[2]<<40)|
           ((uint64_t)b[3]<<32)|((uint64_t)b[4]<<24)|((uint64_t)b[5]<<16)|
           ((uint64_t)b[6]<<8)|(uint64_t)b[7];
}

static void bePut64(uint8_t b[8], uint64_t v) {
    b[0]=(uint8_t)(v>>56); b[1]=(uint8_t)(v>>48); b[2]=(uint8_t)(v>>40);
    b[3]=(uint8_t)(v>>32); b[4]=(uint8_t)(v>>24); b[5]=(uint8_t)(v>>16);
    b[6]=(uint8_t)(v>>8);  b[7]=(uint8_t)v;
}

static void generateSubkeys(uint64_t subkeys[16], const uint8_t key[8]) {
    initFeistelBox();
    uint64_t k = beU64(key);
    uint64_t pk = permuteBlock(k, permutedChoice1, 56);

    uint32_t leftRotations[16], rightRotations[16];
    uint32_t lastL = (uint32_t)(pk >> 28);
    uint32_t lastR = (uint32_t)((pk << 4) >> 4);
    for (int i = 0; i < 16; i++) {
        uint32_t ll = ((lastL << (4 + ksRotations[i])) >> 4) |
                      ((lastL << 4) >> (32 - ksRotations[i]));
        leftRotations[i] = ll;
        lastL = ll;

        uint32_t rr = ((lastR << (4 + ksRotations[i])) >> 4) |
                      ((lastR << 4) >> (32 - ksRotations[i]));
        rightRotations[i] = rr;
        lastR = rr;
    }

    for (int i = 0; i < 16; i++) {
        uint64_t pc2in = ((uint64_t)leftRotations[i] << 28) | (uint64_t)rightRotations[i];
        subkeys[i] = unpack(permuteBlock(pc2in, permutedChoice2, 48));
    }
}

static void cryptBlock(const uint64_t subkeys[16], uint8_t dst[8],
                       const uint8_t src[8], int decrypt) {
    uint64_t b = beU64(src);
    b = permuteInitialBlock(b);
    uint32_t left = (uint32_t)(b >> 32);
    uint32_t right = (uint32_t)b;

    left = (left << 1) | (left >> 31);
    right = (right << 1) | (right >> 31);

    if (decrypt) {
        for (int i = 0; i < 8; i++)
            feistel(left, right, subkeys[15-2*i], subkeys[15-(2*i+1)], &left, &right);
    } else {
        for (int i = 0; i < 8; i++)
            feistel(left, right, subkeys[2*i], subkeys[2*i+1], &left, &right);
    }

    left = (left << 31) | (left >> 1);
    right = (right << 31) | (right >> 1);

    uint64_t preOutput = ((uint64_t)right << 32) | (uint64_t)left;
    bePut64(dst, permuteFinalBlock(preOutput));
}

static int des_ready(const neverc_des_cipher_t *c) {
    return c && c->ready == 1;
}

static int tdes_ready(const neverc_3des_cipher_t *c) {
    return c && c->c1.ready == 1 && c->c2.ready == 1 && c->c3.ready == 1;
}

int neverc_des_is_weak_key(const uint8_t key[8]) {
    if (!key) return -1;
    for (size_t i = 0; i < sizeof(des_weak_keys) / sizeof(des_weak_keys[0]); i++) {
        if (des_keys_equal_ignore_parity(key, des_weak_keys[i]))
            return 1;
    }
    return 0;
}

int neverc_3des_is_weak_key(const uint8_t key[24]) {
    if (!key) return -1;
    if (neverc_des_is_weak_key(key) == 1 ||
        neverc_des_is_weak_key(key + 8) == 1 ||
        neverc_des_is_weak_key(key + 16) == 1)
        return 1;
    /* K1==K2 or K2==K3 collapses 3DES to single DES. K1==K3 is two-key 3DES. */
    if (des_keys_equal_ignore_parity(key, key + 8) ||
        des_keys_equal_ignore_parity(key + 8, key + 16))
        return 1;
    return 0;
}

int neverc_des_init(neverc_des_cipher_t *c, const uint8_t key[8]) {
    if (!c) return -1;
    if (!key) {
        neverc_platform_secure_zero(c, sizeof(*c));
        return -1;
    }
    generateSubkeys(c->subkeys, key);
    c->ready = 1;
    return 0;
}

void neverc_des_encrypt_block(const neverc_des_cipher_t *c,
                              uint8_t dst[8], const uint8_t src[8]) {
    if (!des_ready(c) || !dst || !src) return;
    cryptBlock(c->subkeys, dst, src, 0);
}

void neverc_des_decrypt_block(const neverc_des_cipher_t *c,
                              uint8_t dst[8], const uint8_t src[8]) {
    if (!des_ready(c) || !dst || !src) return;
    cryptBlock(c->subkeys, dst, src, 1);
}

int neverc_3des_init(neverc_3des_cipher_t *c, const uint8_t key[24]) {
    if (!c) return -1;
    if (!key) {
        neverc_platform_secure_zero(c, sizeof(*c));
        return -1;
    }
    generateSubkeys(c->c1.subkeys, key);
    generateSubkeys(c->c2.subkeys, key + 8);
    generateSubkeys(c->c3.subkeys, key + 16);
    c->c1.ready = 1;
    c->c2.ready = 1;
    c->c3.ready = 1;
    return 0;
}

void neverc_3des_encrypt_block(const neverc_3des_cipher_t *c,
                               uint8_t dst[8], const uint8_t src[8]) {
    if (!tdes_ready(c) || !dst || !src) return;
    uint64_t b = beU64(src);
    b = permuteInitialBlock(b);
    uint32_t left = (uint32_t)(b >> 32);
    uint32_t right = (uint32_t)b;

    left = (left << 1) | (left >> 31);
    right = (right << 1) | (right >> 31);

    for (int i = 0; i < 8; i++)
        feistel(left, right, c->c1.subkeys[2*i], c->c1.subkeys[2*i+1], &left, &right);
    for (int i = 0; i < 8; i++)
        feistel(right, left, c->c2.subkeys[15-2*i], c->c2.subkeys[15-(2*i+1)], &right, &left);
    for (int i = 0; i < 8; i++)
        feistel(left, right, c->c3.subkeys[2*i], c->c3.subkeys[2*i+1], &left, &right);

    left = (left << 31) | (left >> 1);
    right = (right << 31) | (right >> 1);

    uint64_t preOutput = ((uint64_t)right << 32) | (uint64_t)left;
    bePut64(dst, permuteFinalBlock(preOutput));
}

void neverc_3des_decrypt_block(const neverc_3des_cipher_t *c,
                               uint8_t dst[8], const uint8_t src[8]) {
    if (!tdes_ready(c) || !dst || !src) return;
    uint64_t b = beU64(src);
    b = permuteInitialBlock(b);
    uint32_t left = (uint32_t)(b >> 32);
    uint32_t right = (uint32_t)b;

    left = (left << 1) | (left >> 31);
    right = (right << 1) | (right >> 31);

    for (int i = 0; i < 8; i++)
        feistel(left, right, c->c3.subkeys[15-2*i], c->c3.subkeys[15-(2*i+1)], &left, &right);
    for (int i = 0; i < 8; i++)
        feistel(right, left, c->c2.subkeys[2*i], c->c2.subkeys[2*i+1], &right, &left);
    for (int i = 0; i < 8; i++)
        feistel(left, right, c->c1.subkeys[15-2*i], c->c1.subkeys[15-(2*i+1)], &left, &right);

    left = (left << 31) | (left >> 1);
    right = (right << 31) | (right >> 1);

    uint64_t preOutput = ((uint64_t)right << 32) | (uint64_t)left;
    bePut64(dst, permuteFinalBlock(preOutput));
}
