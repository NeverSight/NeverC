#include "neverc/std/crypto/des.h"
#include "neverc/std/crypto/md5.h"
#include "neverc/std/crypto/sha1.h"
#include "neverc/std/crypto/sha224.h"
#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/sha3.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t buf[64];
} v3389_md5_ctx;

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t buf[64];
} v3389_sha1_ctx;

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buf[64];
} v3389_sha256_ctx;

typedef struct {
    uint64_t state[25];
    uint8_t buf[200];
    size_t rate;
    size_t buf_len;
    uint8_t suffix;
    int squeezed;
} v3389_sha3_ctx;

typedef struct {
    uint64_t subkeys[16];
} v3389_des_ctx;

typedef struct {
    v3389_des_ctx c1;
    v3389_des_ctx c2;
    v3389_des_ctx c3;
} v3389_3des_ctx;

#define ABI_TYPE_EQ(current, legacy)                                      \
    _Static_assert(sizeof(current) == sizeof(legacy), "v3389 size ABI");  \
    _Static_assert(_Alignof(current) == _Alignof(legacy),                 \
                   "v3389 alignment ABI")
#define ABI_FIELD_EQ(current, legacy, field)                              \
    _Static_assert(offsetof(current, field) == offsetof(legacy, field),   \
                   "v3389 field offset ABI")

ABI_TYPE_EQ(neverc_md5_ctx, v3389_md5_ctx);
ABI_FIELD_EQ(neverc_md5_ctx, v3389_md5_ctx, state);
ABI_FIELD_EQ(neverc_md5_ctx, v3389_md5_ctx, count);
ABI_FIELD_EQ(neverc_md5_ctx, v3389_md5_ctx, buf);

ABI_TYPE_EQ(neverc_sha1_ctx, v3389_sha1_ctx);
ABI_FIELD_EQ(neverc_sha1_ctx, v3389_sha1_ctx, state);
ABI_FIELD_EQ(neverc_sha1_ctx, v3389_sha1_ctx, count);
ABI_FIELD_EQ(neverc_sha1_ctx, v3389_sha1_ctx, buf);

ABI_TYPE_EQ(neverc_sha256_ctx, v3389_sha256_ctx);
ABI_TYPE_EQ(neverc_sha224_ctx, v3389_sha256_ctx);
ABI_FIELD_EQ(neverc_sha256_ctx, v3389_sha256_ctx, state);
ABI_FIELD_EQ(neverc_sha256_ctx, v3389_sha256_ctx, count);
ABI_FIELD_EQ(neverc_sha256_ctx, v3389_sha256_ctx, buf);

ABI_TYPE_EQ(neverc_sha3_ctx, v3389_sha3_ctx);
ABI_FIELD_EQ(neverc_sha3_ctx, v3389_sha3_ctx, state);
ABI_FIELD_EQ(neverc_sha3_ctx, v3389_sha3_ctx, buf);
ABI_FIELD_EQ(neverc_sha3_ctx, v3389_sha3_ctx, rate);
ABI_FIELD_EQ(neverc_sha3_ctx, v3389_sha3_ctx, buf_len);
ABI_FIELD_EQ(neverc_sha3_ctx, v3389_sha3_ctx, suffix);
ABI_FIELD_EQ(neverc_sha3_ctx, v3389_sha3_ctx, squeezed);

ABI_TYPE_EQ(neverc_des_cipher_t, v3389_des_ctx);
ABI_FIELD_EQ(neverc_des_cipher_t, v3389_des_ctx, subkeys);
ABI_TYPE_EQ(neverc_3des_cipher_t, v3389_3des_ctx);
ABI_FIELD_EQ(neverc_3des_cipher_t, v3389_3des_ctx, c1);
ABI_FIELD_EQ(neverc_3des_cipher_t, v3389_3des_ctx, c2);
ABI_FIELD_EQ(neverc_3des_cipher_t, v3389_3des_ctx, c3);

enum { CANARY_SIZE = 32 };

#define GUARDED_TYPE(name, current, legacy) \
    typedef struct {                        \
        union { current now; legacy old; } context; \
        uint8_t canary[CANARY_SIZE];        \
    } name

GUARDED_TYPE(guarded_md5_t, neverc_md5_ctx, v3389_md5_ctx);
GUARDED_TYPE(guarded_sha1_t, neverc_sha1_ctx, v3389_sha1_ctx);
GUARDED_TYPE(guarded_sha256_t, neverc_sha256_ctx, v3389_sha256_ctx);
GUARDED_TYPE(guarded_sha3_t, neverc_sha3_ctx, v3389_sha3_ctx);
GUARDED_TYPE(guarded_des_t, neverc_des_cipher_t, v3389_des_ctx);
GUARDED_TYPE(guarded_3des_t, neverc_3des_cipher_t, v3389_3des_ctx);

static void set_canary(uint8_t canary[CANARY_SIZE]) {
    memset(canary, 0xa5, CANARY_SIZE);
}

static int canary_ok(const uint8_t canary[CANARY_SIZE]) {
    for (size_t i = 0; i < CANARY_SIZE; i++) {
        if (canary[i] != 0xa5)
            return 0;
    }
    return 1;
}

static int hash_context_canaries(void) {
    const uint8_t message[] = "abc";
    uint8_t digest[64];

    guarded_md5_t md5;
    set_canary(md5.canary);
    neverc_md5_init(&md5.context.now);
    neverc_md5_update(&md5.context.now, message, 3);
    neverc_md5_final(&md5.context.now, digest);
    neverc_md5_final(&md5.context.now, digest);
    if (!canary_ok(md5.canary)) return -1;
    neverc_md5_init(&md5.context.now);
    md5.context.now.count = UINT64_MAX / 8U;
    neverc_md5_update(&md5.context.now, message, 1);
    neverc_md5_final(&md5.context.now, digest);
    if (!canary_ok(md5.canary)) return -1;

    guarded_sha1_t sha1;
    set_canary(sha1.canary);
    neverc_sha1_init(&sha1.context.now);
    neverc_sha1_update(&sha1.context.now, message, 3);
    neverc_sha1_final(&sha1.context.now, digest);
    neverc_sha1_final(&sha1.context.now, digest);
    if (!canary_ok(sha1.canary)) return -1;

    guarded_sha256_t sha256;
    set_canary(sha256.canary);
    neverc_sha256_init(&sha256.context.now);
    neverc_sha256_update(&sha256.context.now, message, 3);
    neverc_sha256_final(&sha256.context.now, digest);
    neverc_sha256_final(&sha256.context.now, digest);
    if (!canary_ok(sha256.canary)) return -1;
    neverc_sha224_init(&sha256.context.now);
    neverc_sha224_update(&sha256.context.now, message, 3);
    neverc_sha224_final(&sha256.context.now, digest);
    if (!canary_ok(sha256.canary)) return -1;

    guarded_sha3_t sha3;
    set_canary(sha3.canary);
    neverc_sha3_256_init(&sha3.context.now);
    neverc_sha3_256_update(&sha3.context.now, message, 3);
    neverc_sha3_256_final(&sha3.context.now, digest);
    neverc_sha3_256_final(&sha3.context.now, digest);
    if (!canary_ok(sha3.canary)) return -1;
    neverc_shake128_init(&sha3.context.now);
    neverc_shake128_update(&sha3.context.now, message, 3);
    neverc_shake128_squeeze(&sha3.context.now, digest, sizeof(digest));
    neverc_shake128_squeeze(&sha3.context.now, digest, sizeof(digest));
    return canary_ok(sha3.canary) ? 0 : -1;
}

static int des_context_canaries(void) {
    const uint8_t key[24] = {0};
    const uint8_t input[8] = {0};
    uint8_t output[8];

    guarded_des_t des;
    set_canary(des.canary);
    if (neverc_des_init(&des.context.now, key) != 0)
        return -1;
    neverc_des_encrypt_block(&des.context.now, output, input);
    if (!canary_ok(des.canary) ||
        neverc_des_init(&des.context.now, NULL) != -1 ||
        !canary_ok(des.canary))
        return -1;

    guarded_3des_t tdes;
    set_canary(tdes.canary);
    if (neverc_3des_init(&tdes.context.now, key) != 0)
        return -1;
    neverc_3des_encrypt_block(&tdes.context.now, output, input);
    if (!canary_ok(tdes.canary) ||
        neverc_3des_init(&tdes.context.now, NULL) != -1 ||
        !canary_ok(tdes.canary))
        return -1;
    return 0;
}

int main(void) {
    if (hash_context_canaries() != 0 || des_context_canaries() != 0) {
        fputs("released crypto context ABI canary failed\n", stderr);
        return 1;
    }
    puts("passed");
    return 0;
}
