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
#include "neverc/std/crypto/gcm.h"
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

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int decode_hex(const char *hex, uint8_t *out, size_t out_len) {
    if (!hex || !out || strlen(hex) != out_len * 2U) return -1;
    for (size_t i = 0; i < out_len; i++) {
        int high = hex_value(hex[i * 2U]);
        int low = hex_value(hex[i * 2U + 1U]);
        if (high < 0 || low < 0) return -1;
        out[i] = (uint8_t)((high << 4) | low);
    }
    return 0;
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

static void test_rfc9180_x25519_aes128_vector(void) {
    printf("  RFC 9180 A.1.1 base vector ... ");
    uint8_t sk[32], enc[32], info[20], aad[7], ciphertext[45];
    uint8_t expected_plaintext[29], expected_key[16], expected_nonce[12];
    uint8_t expected_export[32];

    ASSERT(decode_hex(
               "4612c550263fc8ad58375df3f557aac531d26850903e55a9f23f21d8534e8ac8",
               sk, sizeof(sk)) == 0,
           "decode RFC recipient key");
    ASSERT(decode_hex(
               "37fda3567bdbd628e88668c3c8d7e97d1d1253b6d4ea6d44c150f741f1bf4431",
               enc, sizeof(enc)) == 0,
           "decode RFC encapsulation");
    ASSERT(decode_hex(
               "4f6465206f6e2061204772656369616e2055726e",
               info, sizeof(info)) == 0,
           "decode RFC info");
    ASSERT(decode_hex("436f756e742d30", aad, sizeof(aad)) == 0,
           "decode RFC aad");
    ASSERT(decode_hex(
               "f938558b5d72f1a23810b4be2ab4f84331acc02fc97babc53a52ae8218a355a9"
               "6d8770ac83d07bea87e13c512a",
               ciphertext, sizeof(ciphertext)) == 0,
           "decode RFC ciphertext");
    ASSERT(decode_hex(
               "4265617574792069732074727574682c20747275746820626561757479",
               expected_plaintext, sizeof(expected_plaintext)) == 0,
           "decode RFC plaintext");
    ASSERT(decode_hex("4531685d41d65f03dc48f6b8302c05b0",
                      expected_key, sizeof(expected_key)) == 0,
           "decode RFC key");
    ASSERT(decode_hex("56d890e5accaaf011cff4b7d",
                      expected_nonce, sizeof(expected_nonce)) == 0,
           "decode RFC nonce");
    ASSERT(decode_hex(
               "3853fe2b4035195a573ffc53856e77058e15d9ea064de3e59f4961d0095250ee",
               expected_export, sizeof(expected_export)) == 0,
           "decode RFC export");

    neverc_hpke_recipient_t recipient;
    ASSERT(neverc_hpke_recipient_new(
               &recipient, enc, sizeof(enc),
               NEVERC_HPKE_KEM_X25519_SHA256,
               NEVERC_HPKE_KDF_SHA256,
               NEVERC_HPKE_AEAD_AES128GCM,
               sk, sizeof(sk), info, sizeof(info)) == 0,
           "RFC recipient setup");
    ASSERT(recipient.ctx.key_len == (int)sizeof(expected_key) &&
               memcmp(recipient.ctx.key, expected_key,
                      sizeof(expected_key)) == 0,
           "RFC traffic key");
    ASSERT(recipient.ctx.nonce_len == (int)sizeof(expected_nonce) &&
               memcmp(recipient.ctx.base_nonce, expected_nonce,
                      sizeof(expected_nonce)) == 0,
           "RFC base nonce");

    uint8_t plaintext[sizeof(expected_plaintext)];
    ASSERT(neverc_hpke_recipient_open(
               &recipient, aad, sizeof(aad), ciphertext, sizeof(ciphertext),
               plaintext) == (int)sizeof(plaintext),
           "RFC ciphertext opens");
    ASSERT(memcmp(plaintext, expected_plaintext, sizeof(plaintext)) == 0,
           "RFC plaintext matches");

    uint8_t exported[sizeof(expected_export)];
    ASSERT(neverc_hpke_recipient_export(
               &recipient, NULL, 0, exported, sizeof(exported)) == 0,
           "RFC export");
    ASSERT(memcmp(exported, expected_export, sizeof(exported)) == 0,
           "RFC exported value");
    printf("ok\n");
}

static void test_rfc9180_p256_aes128_vector(void) {
    printf("  RFC 9180 A.3.1 base vector ... ");
    uint8_t sk[32], enc[65], info[20], aad[7], ciphertext[45];
    uint8_t expected_plaintext[29], expected_key[16], expected_nonce[12];

    ASSERT(decode_hex(
               "f3ce7fdae57e1a310d87f1ebbde6f328be0a99cdbcadf4d6589cf29de4b8ffd2",
               sk, sizeof(sk)) == 0,
           "decode P-256 recipient key");
    ASSERT(decode_hex(
               "04a92719c6195d5085104f469a8b9814d5838ff72b60501e2c4466e5e67b325"
               "ac98536d7b61a1af4b78e5b7f951c0900be863c403ce65c9bfcb9382657222d18c4",
               enc, sizeof(enc)) == 0,
           "decode P-256 encapsulation");
    ASSERT(decode_hex(
               "4f6465206f6e2061204772656369616e2055726e",
               info, sizeof(info)) == 0,
           "decode P-256 info");
    ASSERT(decode_hex("436f756e742d30", aad, sizeof(aad)) == 0,
           "decode P-256 aad");
    ASSERT(decode_hex(
               "5ad590bb8baa577f8619db35a36311226a896e7342a6d836d8b7bcd2f20b6c7f"
               "9076ac232e3ab2523f39513434",
               ciphertext, sizeof(ciphertext)) == 0,
           "decode P-256 ciphertext");
    ASSERT(decode_hex(
               "4265617574792069732074727574682c20747275746820626561757479",
               expected_plaintext, sizeof(expected_plaintext)) == 0,
           "decode P-256 plaintext");
    ASSERT(decode_hex("868c066ef58aae6dc589b6cfdd18f97e",
                      expected_key, sizeof(expected_key)) == 0,
           "decode P-256 key");
    ASSERT(decode_hex("4e0bc5018beba4bf004cca59",
                      expected_nonce, sizeof(expected_nonce)) == 0,
           "decode P-256 nonce");

    neverc_hpke_recipient_t recipient;
    ASSERT(neverc_hpke_recipient_new(
               &recipient, enc, sizeof(enc),
               NEVERC_HPKE_KEM_P256_SHA256,
               NEVERC_HPKE_KDF_SHA256,
               NEVERC_HPKE_AEAD_AES128GCM,
               sk, sizeof(sk), info, sizeof(info)) == 0,
           "P-256 RFC recipient setup");
    ASSERT(memcmp(recipient.ctx.key, expected_key,
                  sizeof(expected_key)) == 0,
           "P-256 RFC traffic key");
    ASSERT(memcmp(recipient.ctx.base_nonce, expected_nonce,
                  sizeof(expected_nonce)) == 0,
           "P-256 RFC base nonce");

    uint8_t plaintext[sizeof(expected_plaintext)];
    ASSERT(neverc_hpke_recipient_open(
               &recipient, aad, sizeof(aad), ciphertext, sizeof(ciphertext),
               plaintext) == (int)sizeof(plaintext),
           "P-256 RFC ciphertext opens");
    ASSERT(memcmp(plaintext, expected_plaintext, sizeof(plaintext)) == 0,
           "P-256 RFC plaintext matches");
    printf("ok\n");
}

static void test_limits_and_invalid_inputs(void) {
    printf("  limits and invalid inputs ... ");
    uint8_t priv[48], pub[97];
    int priv_len, pub_len;
    gen_keypair(
        NEVERC_HPKE_KEM_X25519_SHA256,
        priv, &priv_len, pub, &pub_len);

    neverc_hpke_sender_t sender;
    uint8_t enc[NEVERC_HPKE_MAX_ENC_SIZE];
    size_t enc_len = 0;
    int rc = neverc_hpke_sender_new(
        &sender, enc, &enc_len,
        NEVERC_HPKE_KEM_X25519_SHA256,
        NEVERC_HPKE_KDF_SHA256,
        NEVERC_HPKE_AEAD_AES128GCM,
        pub, (size_t)pub_len, NULL, 0);
    ASSERT(rc == 0, "sender setup for limits");

    neverc_hpke_recipient_t recipient;
    rc = neverc_hpke_recipient_new(
        &recipient, enc, enc_len,
        NEVERC_HPKE_KEM_X25519_SHA256,
        NEVERC_HPKE_KDF_SHA256,
        NEVERC_HPKE_AEAD_AES128GCM,
        priv, (size_t)priv_len, NULL, 0);
    ASSERT(rc == 0, "recipient setup for limits");

    uint8_t output[64];
    memset(output, 0xa5, sizeof(output));
    sender.ctx.seq_num = UINT64_MAX;
    ASSERT(neverc_hpke_sender_seal(
               &sender, NULL, 0,
               (const uint8_t *)"x", 1U, output) == -1,
           "sender sequence exhaustion");
    ASSERT(output[0] == 0xa5, "exhausted sender writes nothing");

    uint8_t max_nonce[12];
    memcpy(max_nonce, recipient.ctx.base_nonce, sizeof(max_nonce));
    for (size_t i = 0; i < 8U; i++)
        max_nonce[sizeof(max_nonce) - 1U - i] ^= 0xffU;
    neverc_gcm_ctx gcm;
    ASSERT(neverc_gcm_init(
               &gcm, recipient.ctx.key, recipient.ctx.key_len) == 0,
           "initialize maximum-sequence fixture");
    const uint8_t max_message = 0x42;
    ASSERT(neverc_gcm_seal(
               &gcm, max_nonce, &max_message, 1U, NULL, 0,
               output, output + 1U) == 0,
           "create authenticated maximum-sequence ciphertext");

    recipient.ctx.seq_num = UINT64_MAX;
    uint8_t opened = 0xa5;
    ASSERT(neverc_hpke_recipient_open(
               &recipient, NULL, 0, output, 17U, &opened) == -1,
           "recipient sequence exhaustion");
    ASSERT(opened == 0xa5, "exhausted recipient writes nothing");

    output[0] = 0x5a;
    ASSERT(neverc_hpke_sender_export(
               &sender, NULL, 0, output,
               (size_t)UINT16_MAX + 1U) == -1,
           "oversized export rejected");
    ASSERT(output[0] == 0x5a, "oversized export writes nothing");

    neverc_hpke_sender_t invalid_sender;
    enc_len = sizeof(enc);
    ASSERT(neverc_hpke_sender_new(
               &invalid_sender, enc, &enc_len,
               NEVERC_HPKE_KEM_X25519_SHA256,
               NEVERC_HPKE_KDF_SHA256,
               NEVERC_HPKE_AEAD_AES128GCM,
               pub, (size_t)pub_len,
               (const uint8_t *)"x", SIZE_MAX) == -1,
           "oversized info rejected");
    ASSERT(enc_len == 0U, "failed sender setup publishes no encapsulation");

    size_t output_len = 99U;
    ASSERT(neverc_hpke_seal(
               NEVERC_HPKE_KEM_X25519_SHA256,
               NEVERC_HPKE_KDF_SHA256,
               NEVERC_HPKE_AEAD_AES128GCM,
               NULL, 0, NULL, 0, NULL, 0,
               output, &output_len) == -1,
           "null public key rejected");
    ASSERT(output_len == 0U, "failed seal clears output length");
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
    test_rfc9180_x25519_aes128_vector();
    test_rfc9180_p256_aes128_vector();
    test_limits_and_invalid_inputs();
    printf("%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
