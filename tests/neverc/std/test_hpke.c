/*
 * Test suite for HPKE (RFC 9180) — neverc std crypto/hpke.
 *
 * Tests:
 *   1. Round-trip with DHKEM(X25519) + HKDF-SHA256 + AES-128-GCM
 *   2. Round-trip with DHKEM(X25519) + HKDF-SHA256 + ChaCha20-Poly1305
 *   3. Round-trip with DHKEM(P-256) + HKDF-SHA256 + AES-128-GCM
 *   4. One-shot seal/open convenience API
 *   5. Multi-message sequencing (sender.seal × N, recipient.open × N)
 *   6. Wrong-key rejection
 *   7. Tampered ciphertext rejection
 *   8. AAD binding
 *   9. Export secret derivation consistency
 *  10. AES-256-GCM round-trip
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "neverc/std/crypto/hpke.h"
#include "neverc/std/crypto/ecdh.h"
#include "neverc/std/crypto/rand.h"

static int tests_run   = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do {                                      \
    tests_run++;                                                    \
    if (!(cond)) { printf("  FAIL: %s\n", msg); return; }          \
    tests_passed++;                                                 \
} while(0)

/* ── helpers ─────────────────────────────────────────────── */

static void gen_keypair(uint16_t kem_id, uint8_t *priv, int *priv_len,
                        uint8_t *pub, int *pub_len) {
    neverc_ecdh_curve_t curve;
    switch (kem_id) {
    case NEVERC_HPKE_KEM_X25519_SHA256: curve = NEVERC_ECDH_CURVE_X25519; break;
    case NEVERC_HPKE_KEM_P256_SHA256:   curve = NEVERC_ECDH_CURVE_P256;   break;
    default: return;
    }
    neverc_ecdh_key_t k;
    neverc_ecdh_generate_key(curve, &k);
    *priv_len = neverc_ecdh_private_key_bytes(&k, priv, 48);
    *pub_len  = neverc_ecdh_public_key_bytes(&k, pub, 97);
}

/* ── test cases ─────────────────────────────────────────── */

static void test_x25519_aes128gcm(void) {
    printf("  x25519 + hkdf-sha256 + aes-128-gcm ... ");
    uint8_t priv[48], pub[97];
    int priv_len, pub_len;
    gen_keypair(NEVERC_HPKE_KEM_X25519_SHA256, priv, &priv_len, pub, &pub_len);

    const uint8_t info[] = "test-info";
    const uint8_t msg[] = "Hello, HPKE!";

    neverc_hpke_sender_t s;
    uint8_t enc[NEVERC_HPKE_MAX_ENC_SIZE];
    size_t enc_len;
    int rc = neverc_hpke_sender_new(&s, enc, &enc_len,
                NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                NEVERC_HPKE_AEAD_AES128GCM,
                pub, (size_t)pub_len, info, sizeof(info)-1);
    ASSERT(rc == 0, "sender_new");

    uint8_t ct[256];
    int ct_len = neverc_hpke_sender_seal(&s, NULL, 0, msg, sizeof(msg)-1, ct);
    ASSERT(ct_len == (int)sizeof(msg)-1 + 16, "seal length");

    neverc_hpke_recipient_t r;
    rc = neverc_hpke_recipient_new(&r, enc, enc_len,
                NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                NEVERC_HPKE_AEAD_AES128GCM,
                priv, (size_t)priv_len, info, sizeof(info)-1);
    ASSERT(rc == 0, "recipient_new");

    uint8_t pt[256];
    int pt_len = neverc_hpke_recipient_open(&r, NULL, 0, ct, (size_t)ct_len, pt);
    ASSERT(pt_len == (int)sizeof(msg)-1, "open length");
    ASSERT(memcmp(pt, msg, (size_t)pt_len) == 0, "plaintext match");
    printf("ok\n");
}

static void test_x25519_chacha20poly1305(void) {
    printf("  x25519 + hkdf-sha256 + chacha20poly1305 ... ");
    uint8_t priv[48], pub[97];
    int priv_len, pub_len;
    gen_keypair(NEVERC_HPKE_KEM_X25519_SHA256, priv, &priv_len, pub, &pub_len);

    const uint8_t msg[] = "ChaCha20 HPKE test";
    neverc_hpke_sender_t s;
    uint8_t enc[NEVERC_HPKE_MAX_ENC_SIZE];
    size_t enc_len;
    int rc = neverc_hpke_sender_new(&s, enc, &enc_len,
                NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                NEVERC_HPKE_AEAD_CHACHA20POLY1305,
                pub, (size_t)pub_len, NULL, 0);
    ASSERT(rc == 0, "sender_new");

    uint8_t ct[256];
    int ct_len = neverc_hpke_sender_seal(&s, NULL, 0, msg, sizeof(msg)-1, ct);
    ASSERT(ct_len > 0, "seal");

    neverc_hpke_recipient_t r;
    rc = neverc_hpke_recipient_new(&r, enc, enc_len,
                NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                NEVERC_HPKE_AEAD_CHACHA20POLY1305,
                priv, (size_t)priv_len, NULL, 0);
    ASSERT(rc == 0, "recipient_new");

    uint8_t pt[256];
    int pt_len = neverc_hpke_recipient_open(&r, NULL, 0, ct, (size_t)ct_len, pt);
    ASSERT(pt_len == (int)sizeof(msg)-1, "open");
    ASSERT(memcmp(pt, msg, (size_t)pt_len) == 0, "match");
    printf("ok\n");
}

static void test_p256_aes128gcm(void) {
    printf("  p256 + hkdf-sha256 + aes-128-gcm ... ");
    uint8_t priv[48], pub[97];
    int priv_len, pub_len;
    gen_keypair(NEVERC_HPKE_KEM_P256_SHA256, priv, &priv_len, pub, &pub_len);

    const uint8_t msg[] = "P-256 HPKE test";
    neverc_hpke_sender_t s;
    uint8_t enc[NEVERC_HPKE_MAX_ENC_SIZE];
    size_t enc_len;
    int rc = neverc_hpke_sender_new(&s, enc, &enc_len,
                NEVERC_HPKE_KEM_P256_SHA256, NEVERC_HPKE_KDF_SHA256,
                NEVERC_HPKE_AEAD_AES128GCM,
                pub, (size_t)pub_len, NULL, 0);
    ASSERT(rc == 0, "sender_new");

    uint8_t ct[256];
    int ct_len = neverc_hpke_sender_seal(&s, NULL, 0, msg, sizeof(msg)-1, ct);
    ASSERT(ct_len > 0, "seal");

    neverc_hpke_recipient_t r;
    rc = neverc_hpke_recipient_new(&r, enc, enc_len,
                NEVERC_HPKE_KEM_P256_SHA256, NEVERC_HPKE_KDF_SHA256,
                NEVERC_HPKE_AEAD_AES128GCM,
                priv, (size_t)priv_len, NULL, 0);
    ASSERT(rc == 0, "recipient_new");

    uint8_t pt[256];
    int pt_len = neverc_hpke_recipient_open(&r, NULL, 0, ct, (size_t)ct_len, pt);
    ASSERT(pt_len == (int)sizeof(msg)-1, "open");
    ASSERT(memcmp(pt, msg, (size_t)pt_len) == 0, "match");
    printf("ok\n");
}

static void test_oneshot_seal_open(void) {
    printf("  one-shot seal/open ... ");
    uint8_t priv[48], pub[97];
    int priv_len, pub_len;
    gen_keypair(NEVERC_HPKE_KEM_X25519_SHA256, priv, &priv_len, pub, &pub_len);

    const uint8_t msg[] = "one-shot test";
    uint8_t ct[256];
    size_t ct_len;
    int rc = neverc_hpke_seal(NEVERC_HPKE_KEM_X25519_SHA256,
                              NEVERC_HPKE_KDF_SHA256,
                              NEVERC_HPKE_AEAD_AES128GCM,
                              pub, (size_t)pub_len, NULL, 0,
                              msg, sizeof(msg)-1, ct, &ct_len);
    ASSERT(rc == 0, "seal");
    ASSERT(ct_len == 32 + sizeof(msg)-1 + 16, "seal length");

    uint8_t pt[256];
    size_t pt_len;
    rc = neverc_hpke_open(NEVERC_HPKE_KEM_X25519_SHA256,
                          NEVERC_HPKE_KDF_SHA256,
                          NEVERC_HPKE_AEAD_AES128GCM,
                          priv, (size_t)priv_len, NULL, 0,
                          ct, ct_len, pt, &pt_len);
    ASSERT(rc == 0, "open");
    ASSERT(pt_len == sizeof(msg)-1, "open length");
    ASSERT(memcmp(pt, msg, pt_len) == 0, "match");
    printf("ok\n");
}

static void test_multi_message(void) {
    printf("  multi-message sequencing ... ");
    uint8_t priv[48], pub[97];
    int priv_len, pub_len;
    gen_keypair(NEVERC_HPKE_KEM_X25519_SHA256, priv, &priv_len, pub, &pub_len);

    neverc_hpke_sender_t s;
    uint8_t enc[NEVERC_HPKE_MAX_ENC_SIZE];
    size_t enc_len;
    neverc_hpke_sender_new(&s, enc, &enc_len,
                           NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                           NEVERC_HPKE_AEAD_AES128GCM,
                           pub, (size_t)pub_len, NULL, 0);

    neverc_hpke_recipient_t r;
    neverc_hpke_recipient_new(&r, enc, enc_len,
                              NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                              NEVERC_HPKE_AEAD_AES128GCM,
                              priv, (size_t)priv_len, NULL, 0);

    int ok = 1;
    for (int i = 0; i < 5; i++) {
        char msg[32];
        int len = sprintf(msg, "message %d", i);
        uint8_t ct[256], pt[256];
        int ct_len = neverc_hpke_sender_seal(&s, NULL, 0,
                         (const uint8_t*)msg, (size_t)len, ct);
        if (ct_len < 0) { ok = 0; break; }
        int pt_len = neverc_hpke_recipient_open(&r, NULL, 0,
                         ct, (size_t)ct_len, pt);
        if (pt_len != len || memcmp(pt, msg, (size_t)len) != 0)
            { ok = 0; break; }
    }
    ASSERT(ok, "5 messages round-trip");
    printf("ok\n");
}

static void test_wrong_key(void) {
    printf("  wrong-key rejection ... ");
    uint8_t priv1[48], pub1[97], priv2[48], pub2[97];
    int pl1, pkl1, pl2, pkl2;
    gen_keypair(NEVERC_HPKE_KEM_X25519_SHA256, priv1, &pl1, pub1, &pkl1);
    gen_keypair(NEVERC_HPKE_KEM_X25519_SHA256, priv2, &pl2, pub2, &pkl2);

    const uint8_t msg[] = "secret";
    uint8_t ct[256];
    size_t ct_len;
    neverc_hpke_seal(NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                     NEVERC_HPKE_AEAD_AES128GCM,
                     pub1, (size_t)pkl1, NULL, 0, msg, sizeof(msg)-1, ct, &ct_len);

    uint8_t pt[256];
    size_t pt_len;
    int rc = neverc_hpke_open(NEVERC_HPKE_KEM_X25519_SHA256,
                              NEVERC_HPKE_KDF_SHA256,
                              NEVERC_HPKE_AEAD_AES128GCM,
                              priv2, (size_t)pl2, NULL, 0,
                              ct, ct_len, pt, &pt_len);
    ASSERT(rc != 0, "should fail with wrong key");
    printf("ok\n");
}

static void test_tampered_ciphertext(void) {
    printf("  tampered ciphertext rejection ... ");
    uint8_t priv[48], pub[97];
    int priv_len, pub_len;
    gen_keypair(NEVERC_HPKE_KEM_X25519_SHA256, priv, &priv_len, pub, &pub_len);

    const uint8_t msg[] = "integrity check";
    uint8_t ct[256];
    size_t ct_len;
    neverc_hpke_seal(NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                     NEVERC_HPKE_AEAD_AES128GCM,
                     pub, (size_t)pub_len, NULL, 0, msg, sizeof(msg)-1, ct, &ct_len);

    ct[ct_len - 1] ^= 0xFF;

    uint8_t pt[256];
    size_t pt_len;
    int rc = neverc_hpke_open(NEVERC_HPKE_KEM_X25519_SHA256,
                              NEVERC_HPKE_KDF_SHA256,
                              NEVERC_HPKE_AEAD_AES128GCM,
                              priv, (size_t)priv_len, NULL, 0,
                              ct, ct_len, pt, &pt_len);
    ASSERT(rc != 0, "tampered ct should fail");
    printf("ok\n");
}

static void test_aad_binding(void) {
    printf("  AAD binding ... ");
    uint8_t priv[48], pub[97];
    int priv_len, pub_len;
    gen_keypair(NEVERC_HPKE_KEM_X25519_SHA256, priv, &priv_len, pub, &pub_len);

    neverc_hpke_sender_t s;
    uint8_t enc[NEVERC_HPKE_MAX_ENC_SIZE];
    size_t enc_len;
    neverc_hpke_sender_new(&s, enc, &enc_len,
                           NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                           NEVERC_HPKE_AEAD_AES128GCM,
                           pub, (size_t)pub_len, NULL, 0);

    const uint8_t msg[] = "aad test";
    const uint8_t aad[] = "associated data";
    uint8_t ct[256];
    int ct_len = neverc_hpke_sender_seal(&s, aad, sizeof(aad)-1,
                                         msg, sizeof(msg)-1, ct);
    ASSERT(ct_len > 0, "seal with AAD");

    neverc_hpke_recipient_t r;
    neverc_hpke_recipient_new(&r, enc, enc_len,
                              NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                              NEVERC_HPKE_AEAD_AES128GCM,
                              priv, (size_t)priv_len, NULL, 0);

    uint8_t pt[256];
    const uint8_t wrong_aad[] = "wrong data";
    int pt_len = neverc_hpke_recipient_open(&r, wrong_aad, sizeof(wrong_aad)-1,
                                            ct, (size_t)ct_len, pt);
    ASSERT(pt_len < 0, "wrong AAD should fail");
    printf("ok\n");
}

static void test_export_secret(void) {
    printf("  export secret consistency ... ");
    uint8_t priv[48], pub[97];
    int priv_len, pub_len;
    gen_keypair(NEVERC_HPKE_KEM_X25519_SHA256, priv, &priv_len, pub, &pub_len);

    neverc_hpke_sender_t s;
    uint8_t enc[NEVERC_HPKE_MAX_ENC_SIZE];
    size_t enc_len;
    neverc_hpke_sender_new(&s, enc, &enc_len,
                           NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                           NEVERC_HPKE_AEAD_AES128GCM,
                           pub, (size_t)pub_len, NULL, 0);

    neverc_hpke_recipient_t r;
    neverc_hpke_recipient_new(&r, enc, enc_len,
                              NEVERC_HPKE_KEM_X25519_SHA256, NEVERC_HPKE_KDF_SHA256,
                              NEVERC_HPKE_AEAD_AES128GCM,
                              priv, (size_t)priv_len, NULL, 0);

    uint8_t exp_s[32], exp_r[32];
    const uint8_t ectx[] = "export-ctx";
    int rc1 = neverc_hpke_sender_export(&s, ectx, sizeof(ectx)-1, exp_s, 32);
    int rc2 = neverc_hpke_recipient_export(&r, ectx, sizeof(ectx)-1, exp_r, 32);
    ASSERT(rc1 == 0 && rc2 == 0, "export succeeds");
    ASSERT(memcmp(exp_s, exp_r, 32) == 0, "sender and recipient export match");
    printf("ok\n");
}

static void test_aes256gcm(void) {
    printf("  x25519 + hkdf-sha256 + aes-256-gcm ... ");
    uint8_t priv[48], pub[97];
    int priv_len, pub_len;
    gen_keypair(NEVERC_HPKE_KEM_X25519_SHA256, priv, &priv_len, pub, &pub_len);

    const uint8_t msg[] = "AES-256-GCM test";
    uint8_t ct[256];
    size_t ct_len;
    int rc = neverc_hpke_seal(NEVERC_HPKE_KEM_X25519_SHA256,
                              NEVERC_HPKE_KDF_SHA256,
                              NEVERC_HPKE_AEAD_AES256GCM,
                              pub, (size_t)pub_len, NULL, 0,
                              msg, sizeof(msg)-1, ct, &ct_len);
    ASSERT(rc == 0, "seal");

    uint8_t pt[256];
    size_t pt_len;
    rc = neverc_hpke_open(NEVERC_HPKE_KEM_X25519_SHA256,
                          NEVERC_HPKE_KDF_SHA256,
                          NEVERC_HPKE_AEAD_AES256GCM,
                          priv, (size_t)priv_len, NULL, 0,
                          ct, ct_len, pt, &pt_len);
    ASSERT(rc == 0, "open");
    ASSERT(pt_len == sizeof(msg)-1 && memcmp(pt, msg, pt_len) == 0, "match");
    printf("ok\n");
}

int main(void) {
    printf("crypto/hpke tests:\n");
    test_x25519_aes128gcm();
    test_x25519_chacha20poly1305();
    test_p256_aes128gcm();
    test_oneshot_seal_open();
    test_multi_message();
    test_wrong_key();
    test_tampered_ciphertext();
    test_aad_binding();
    test_export_secret();
    test_aes256gcm();
    printf("%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
