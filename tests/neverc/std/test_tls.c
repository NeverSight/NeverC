#include "neverc/std/crypto/tls.h"
#include "neverc/std/crypto/ed25519.h"
#include "neverc/std/encoding/base64.h"
#include "neverc/std/net/tcp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT
#define NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT 1
#endif

#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
static const char *TEST_CERT_PEM;
static const char *TEST_KEY_PEM;
static const char *TEST_PKCS8_KEY_PEM;
static const char *MISMATCHED_KEY_PEM;
static const char *TEST_CLIENT_CERT_PEM;
static const char *TEST_CLIENT_KEY_PEM;

#if defined(NEVERC_TLS_TESTING)
int neverc_tls_test_config_set_handshake_fragment_size(
    neverc_tls_config_t *cfg, size_t fragment_size);
int neverc_tls_test_handshake_reassembly(void);
int neverc_tls_test_key_schedule_failures(void);
int neverc_tls_test_record_write_failure(void);
int neverc_tls_test_reject_ccs_after_handshake(void);
int neverc_tls_test_discard_ccs_before_handshake(void);
int neverc_tls_test_encrypted_extensions_forbidden(void);
int neverc_tls_test_certificate_request_schemes(void);
int neverc_tls_test_did_resume(neverc_tls_conn_t *conn);
int neverc_tls_test_corrupt_client_session(
    neverc_tls_config_t *cfg);
int neverc_tls_test_expire_client_session(
    neverc_tls_config_t *cfg);
int neverc_tls_test_expire_server_sessions(
    neverc_tls_config_t *cfg);
int neverc_tls_test_set_client_session_alpn(
    neverc_tls_config_t *cfg, const char *alpn);
int neverc_tls_test_resize_client_session_ticket(
    neverc_tls_config_t *cfg, size_t ticket_len);
#endif

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_not_null(const char *name, const void *ptr) {
    tests_run++;
    if (ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got NULL\n", name); }
}

static void check_null(const char *name, const void *ptr) {
    tests_run++;
    if (!ptr) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got non-NULL\n", name); }
}

/* ===== Config tests ===== */

static void test_config(void) {
    printf("[config]\n");

    neverc_tls_config_t *cfg = neverc_tls_config_new();
    check_not_null("config_new", cfg);

    neverc_tls_config_insecure_skip_verify(cfg);
    neverc_tls_config_set_server_name(cfg, "example.com");
    check_int("add_custom_root_certificate",
              neverc_tls_config_add_root_certificates_mem(
                  cfg, TEST_CERT_PEM, strlen(TEST_CERT_PEM)),
              0);
    static const char invalid_root_certificate[] =
        "-----BEGIN CERTIFICATE-----\nYQ==\n"
        "-----END CERTIFICATE-----\n";
    check_int("reject_invalid_custom_root_certificate",
              neverc_tls_config_add_root_certificates_mem(
                  cfg, invalid_root_certificate,
                  sizeof(invalid_root_certificate) - 1) != 0,
              1);

    const char *protos[] = {"h2", "http/1.1"};
    neverc_tls_config_set_alpn(cfg, protos, 2);
    check_int("set_require_and_verify_client_auth",
              neverc_tls_config_set_client_auth(
                  cfg, NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY),
              0);
    check_int("reject_invalid_client_auth_mode",
              neverc_tls_config_set_client_auth(cfg, 2) != 0, 1);
    check_int("reset_client_auth",
              neverc_tls_config_set_client_auth(
                  cfg, NEVERC_TLS_CLIENT_AUTH_NONE),
              0);
#if defined(NEVERC_TLS_TESTING)
    check_int("handshake_fragment_and_coalescing_reassembly",
              neverc_tls_test_handshake_reassembly(), 0);
    check_int("key_schedule_failure_paths_are_atomic",
              neverc_tls_test_key_schedule_failures(), 0);
    check_int("record_write_failure_closes_without_advancing_sequence",
              neverc_tls_test_record_write_failure(), 0);
    check_int("reject_ccs_after_handshake",
              neverc_tls_test_reject_ccs_after_handshake(), 0);
    check_int("discard_ccs_before_handshake",
              neverc_tls_test_discard_ccs_before_handshake(), 0);
    check_int("reject_forbidden_encrypted_extensions",
              neverc_tls_test_encrypted_extensions_forbidden(), 0);
    check_int("certificate_request_schemes",
              neverc_tls_test_certificate_request_schemes(), 0);
    check_int("reject_oversized_test_fragment",
              neverc_tls_test_config_set_handshake_fragment_size(
                  cfg, 16385) != 0,
              1);
#endif

    check_int("load_valid_certificate_key_pair",
              neverc_tls_config_load_cert_mem(
                  cfg, TEST_CERT_PEM, TEST_KEY_PEM),
              0);
    check_int("load_valid_pkcs8_certificate_key_pair",
              neverc_tls_config_load_cert_mem(
                  cfg, TEST_CERT_PEM, TEST_PKCS8_KEY_PEM),
              0);
    check_int("reject_mismatched_private_key",
              neverc_tls_config_load_cert_mem(
                  cfg, TEST_CERT_PEM, MISMATCHED_KEY_PEM) != 0,
              1);
    check_int("reject_malformed_certificate_pem",
              neverc_tls_config_load_cert_mem(
                  cfg,
                  "-----BEGIN CERTIFICATE-----\n!!!!\n"
                  "-----END CERTIFICATE-----\n",
                  TEST_KEY_PEM) != 0,
              1);
    check_int("reject_malformed_private_key_pem",
              neverc_tls_config_load_cert_mem(
                  cfg, TEST_CERT_PEM,
                  "-----BEGIN EC PRIVATE KEY-----\nAAAA\n"
                  "-----END EC PRIVATE KEY-----\n") != 0,
              1);

    neverc_tls_config_free(cfg);
    tests_run++; tests_passed++; /* free didn't crash */
}

/* ===== Self-signed cert for testing ===== */

/* Self-signed P-256 certificate/key pair generated with OpenSSL through
 * cryptography 44.0.0 for localhost tests only. */
static const char *TEST_CERT_PEM =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBgjCCASigAwIBAgIJTmV2ZXJDVExTMAoGCCqGSM49BAMCMBQxEjAQBgNVBAMM\n"
    "CWxvY2FsaG9zdDAeFw0yNTAxMDEwMDAwMDBaFw0zNTAxMDEwMDAwMDBaMBQxEjAQ\n"
    "BgNVBAMMCWxvY2FsaG9zdDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABBX4gK+v\n"
    "5DI//0U8VG4jHfQ+RmkltlYdGsoiK2yypNi1Pg9FlprxoLEnMBd3iJPXyWXZXeEc\n"
    "urkgOPvislmTWeWjYzBhMAwGA1UdEwEB/wQCMAAwDgYDVR0PAQH/BAQDAgeAMBMG\n"
    "A1UdJQQMMAoGCCsGAQUFBwMBMCwGA1UdEQQlMCOCCWxvY2FsaG9zdIcEfwAAAYcQ\n"
    "AAAAAAAAAAAAAAAAAAAAATAKBggqhkjOPQQDAgNIADBFAiBLuVqTeDOD9x0suosv\n"
    "BUX1bJ3YwUqavMG1lP0/I7BM2wIhAJp6w5O/mUdQSHXz+xvQcjhM/awHU5cEkn9F\n"
    "OSd9jEup\n"
    "-----END CERTIFICATE-----\n";

static const char *TEST_KEY_PEM =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEIDkqk6tPKsMt9xd0yT+C31OoGv1mP2iXEeOfHufkrVo8oAoGCCqGSM49\n"
    "AwEHoUQDQgAEFfiAr6/kMj//RTxUbiMd9D5GaSW2Vh0ayiIrbLKk2LU+D0WWmvGg\n"
    "sScwF3eIk9fJZdld4Ry6uSA4++KyWZNZ5Q==\n"
    "-----END EC PRIVATE KEY-----\n";

static const char *TEST_PKCS8_KEY_PEM =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgOSqTq08qwy33F3TJ\n"
    "P4LfU6ga/WY/aJcR458e5+StWjyhRANCAAQV+ICvr+QyP/9FPFRuIx30PkZpJbZW\n"
    "HRrKIitssqTYtT4PRZaa8aCxJzAXd4iT18ll2V3hHLq5IDj74rJZk1nl\n"
    "-----END PRIVATE KEY-----\n";

static const char *MISMATCHED_KEY_PEM =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEIP59MqisH3/JHQEN8KXlBcE/TF4MJB4bk/O/IbVlpR+xoAoGCCqGSM49\n"
    "AwEHoUQDQgAEtskf1ldYDvesKmTSp+y9OCL0IKA+1CayNdqBbkBH3QKW/qF/7JlP\n"
    "BSDBdI5uQAjO9rogYFtLM+kP+uE2XUjUpw==\n"
    "-----END EC PRIVATE KEY-----\n";

static const char *TEST_CLIENT_CERT_PEM =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBsjCCAVmgAwIBAgIUWMy6G/0mL0UjwPKpVjwx72YlAaUwCgYIKoZIzj0EAwIw\n"
    "HTEbMBkGA1UEAwwSbmV2ZXJjLXRlc3QtY2xpZW50MCAXDTI2MDczMTE4NDgyMFoY\n"
    "DzIxMjYwNzA3MTg0ODIwWjAdMRswGQYDVQQDDBJuZXZlcmMtdGVzdC1jbGllbnQw\n"
    "WTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAARhOGzAwgaFBOjytGGxL6J2xIWVn2Ly\n"
    "OJ94htioQif4ZfC93fZsxfS1jlhia9EoNScBIaizLAFahJ49wD5BcSMAo3UwczAd\n"
    "BgNVHQ4EFgQUL/kZhkaUW9aD6Jn8laRmJhdRm0IwHwYDVR0jBBgwFoAUL/kZhkaU\n"
    "W9aD6Jn8laRmJhdRm0IwDAYDVR0TAQH/BAIwADAOBgNVHQ8BAf8EBAMCB4AwEwYD\n"
    "VR0lBAwwCgYIKwYBBQUHAwIwCgYIKoZIzj0EAwIDRwAwRAIgEN2tqj6w7eHK5eYZ\n"
    "pp2cskNlFzZmT7zBrUurafyYwdkCIHjN+f1IxVufo3N+RDa77YspktwG/GQdwxDf\n"
    "irl8lUso\n"
    "-----END CERTIFICATE-----\n";

static const char *TEST_CLIENT_KEY_PEM =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgAqPkGQQJ8uVpXwse\n"
    "p0UeUzYnzjCQshlzpB62nuxAM8yhRANCAARhOGzAwgaFBOjytGGxL6J2xIWVn2Ly\n"
    "OJ94htioQif4ZfC93fZsxfS1jlhia9EoNScBIaizLAFahJ49wD5BcSMA\n"
    "-----END PRIVATE KEY-----\n";

static int decode_certificate_pem(uint8_t *der, size_t der_capacity) {
    const char *body = strchr(TEST_CERT_PEM, '\n');
    const char *end = strstr(TEST_CERT_PEM, "-----END CERTIFICATE-----");
    if (!body || !end)
        return -1;
    ++body;

    char base64[1024];
    size_t base64_len = 0;
    for (const char *p = body; p < end; ++p) {
        if (*p == '\r' || *p == '\n')
            continue;
        if (base64_len >= sizeof(base64))
            return -1;
        base64[base64_len++] = *p;
    }
    if (neverc_base64_decoded_len(base64_len) > der_capacity)
        return -1;
    return neverc_base64_decode(der, base64, base64_len);
}

static void test_certificate_chain_verification(void) {
    printf("[certificate_chain_verification]\n");
    neverc_tls_config_t *cfg = neverc_tls_config_new();
    check_not_null("chain_config_new", cfg);
    neverc_tls_config_set_server_name(cfg, "localhost");
    check_int("chain_add_custom_root",
              neverc_tls_config_add_root_certificates_mem(
                  cfg, TEST_CERT_PEM, strlen(TEST_CERT_PEM)),
              0);

    uint8_t certificate_der[512];
    int certificate_der_len =
        decode_certificate_pem(certificate_der, sizeof(certificate_der));
    check_int("chain_decode_certificate",
              certificate_der_len > 0, 1);
    neverc_x509_time_t verification_time =
        {2026, 1, 1, 0, 0, 0};
    check_int("verify_custom_root_chain",
              neverc_tls_verify_server_certificate_chain(
                  cfg, certificate_der,
                  certificate_der_len > 0 ?
                      (size_t)certificate_der_len : 0,
                  NULL, &verification_time),
              0);

    neverc_tls_config_set_server_name(cfg, "other.example");
    check_int("reject_custom_root_wrong_hostname",
              neverc_tls_verify_server_certificate_chain(
                  cfg, certificate_der,
                  certificate_der_len > 0 ?
                      (size_t)certificate_der_len : 0,
                  NULL, &verification_time) != 0,
              1);
    neverc_tls_config_free(cfg);
}

static void test_certificate_verify_signing(void) {
    printf("[certificate_verify_signing]\n");

    neverc_tls_config_t *cfg = neverc_tls_config_new();
    check_not_null("signing_config_new", cfg);
    check_int("signing_load_certificate",
              neverc_tls_config_load_cert_mem(
                  cfg, TEST_CERT_PEM, TEST_KEY_PEM),
              0);

    uint8_t transcript_hash[32];
    for (size_t i = 0; i < sizeof(transcript_hash); ++i)
        transcript_hash[i] = (uint8_t)(0xa0 + i);
    uint8_t signature[80];
    size_t signature_len = 0;
    uint16_t signature_scheme = 0;
    check_int("sign_server_certificate_verify",
              neverc_tls_sign_certificate_verify(
                  cfg, 1, transcript_hash, sizeof(transcript_hash),
                  &signature_scheme, signature, sizeof(signature),
                  &signature_len),
              0);
    check_int("signing_scheme",
              signature_scheme,
              NEVERC_TLS_SIGNATURE_ECDSA_SECP256R1_SHA256);
    check_int("signing_der_size",
              signature_len >= 68 && signature_len <= 72,
              1);

    uint8_t certificate_der[512];
    int certificate_der_len =
        decode_certificate_pem(certificate_der, sizeof(certificate_der));
    check_int("decode_signing_certificate",
              certificate_der_len > 0, 1);
    neverc_x509_cert_t certificate;
    memset(&certificate, 0, sizeof(certificate));
    check_int("parse_signing_certificate",
              neverc_x509_parse_certificate(
                  &certificate, certificate_der,
                  certificate_der_len > 0 ?
                      (size_t)certificate_der_len : 0),
              0);
    check_int("verify_generated_certificate_verify",
              neverc_tls_verify_certificate_verify(
                  &certificate, signature_scheme, 1,
                  transcript_hash, sizeof(transcript_hash),
                  signature, signature_len),
              0);
    transcript_hash[0] ^= 1;
    check_int("reject_generated_signature_for_other_transcript",
              neverc_tls_verify_certificate_verify(
                  &certificate, signature_scheme, 1,
                  transcript_hash, sizeof(transcript_hash),
                  signature, signature_len) != 0,
              1);
    transcript_hash[0] ^= 1;

    check_int("reject_signing_bad_transcript_size",
              neverc_tls_sign_certificate_verify(
                  cfg, 1, transcript_hash, sizeof(transcript_hash) - 1,
                  &signature_scheme, signature, sizeof(signature),
                  &signature_len) != 0,
              1);
    check_int("reject_signing_small_output",
              neverc_tls_sign_certificate_verify(
                  cfg, 1, transcript_hash, sizeof(transcript_hash),
                  &signature_scheme, signature, 8,
                  &signature_len) != 0,
              1);

    neverc_x509_cert_free(&certificate);
    neverc_tls_config_free(cfg);
}

static uint8_t test_rsa_public_key_der[] = {
    0x30, 0x81, 0x89, 0x02, 0x81, 0x81, 0x00, 0xc9,
    0x47, 0xe5, 0x4c, 0xcb, 0x82, 0x55, 0xd4, 0x9c,
    0x8a, 0xe6, 0x4c, 0xdc, 0x78, 0xa0, 0x45, 0x5f,
    0x2a, 0xce, 0x16, 0x91, 0x47, 0x7f, 0xc7, 0x9a,
    0xe7, 0x9b, 0x11, 0xbd, 0xd0, 0x7a, 0xf6, 0x5d,
    0x9e, 0x6d, 0x4b, 0x77, 0x0a, 0x46, 0xc9, 0x35,
    0x98, 0x95, 0x89, 0xe0, 0xba, 0xad, 0x27, 0x8e,
    0xe0, 0x16, 0x40, 0xfa, 0xfe, 0x2a, 0x63, 0xf2,
    0xa5, 0x7d, 0xd0, 0x8e, 0x01, 0xb9, 0xf2, 0x44,
    0x47, 0xd7, 0xc2, 0xaf, 0x39, 0x5d, 0x34, 0x18,
    0x80, 0x68, 0x0b, 0x9e, 0x79, 0x40, 0x41, 0xf7,
    0x57, 0xd2, 0xb0, 0xe7, 0xbd, 0x88, 0xea, 0xb8,
    0x60, 0xcf, 0xe2, 0x44, 0x16, 0x28, 0x71, 0x58,
    0x1f, 0xeb, 0xbc, 0xc9, 0x33, 0x80, 0x04, 0x3e,
    0xe0, 0xb9, 0x8e, 0xe2, 0x35, 0x57, 0x9c, 0x2e,
    0x2f, 0xde, 0x3e, 0x61, 0xa6, 0xe1, 0xf0, 0x54,
    0x57, 0x78, 0x74, 0x13, 0xf8, 0x88, 0x19, 0x02,
    0x03, 0x01, 0x00, 0x01,
};

static const uint8_t test_rsa_pss_certificate_verify[] = {
    0x73, 0xc4, 0x37, 0x5f, 0x67, 0xcb, 0x43, 0xb5,
    0xcf, 0x36, 0xc8, 0x6e, 0x9a, 0xea, 0x58, 0x76,
    0x43, 0x7b, 0xf1, 0x5f, 0xeb, 0x31, 0xa7, 0xfb,
    0xfc, 0x61, 0x26, 0x80, 0x90, 0xd4, 0x94, 0xf1,
    0xbe, 0x87, 0x17, 0x26, 0x26, 0x6d, 0xdb, 0x24,
    0x60, 0x13, 0x4e, 0xf5, 0x5a, 0x98, 0xfe, 0xe2,
    0x0e, 0x4b, 0x80, 0x40, 0x4f, 0xc3, 0xc9, 0x95,
    0x38, 0xd9, 0x8b, 0xa1, 0x21, 0x40, 0x29, 0xce,
    0x83, 0x20, 0x0b, 0x82, 0x6f, 0x87, 0xb4, 0x81,
    0xf5, 0x02, 0x80, 0x1f, 0xa6, 0xa6, 0xd5, 0x76,
    0xe5, 0xcd, 0x6f, 0x8e, 0x68, 0x6a, 0x76, 0x61,
    0x54, 0x7e, 0x94, 0x5b, 0x07, 0x45, 0x7a, 0x8a,
    0x54, 0x7f, 0xc0, 0xa0, 0x61, 0x14, 0x36, 0xb9,
    0xb8, 0x66, 0x13, 0x31, 0x94, 0x15, 0x11, 0x65,
    0x15, 0xdc, 0x3a, 0xa1, 0x5e, 0x7b, 0x31, 0xfc,
    0x12, 0x1b, 0x49, 0xfe, 0x41, 0xb4, 0xac, 0x66,
};

/*
 * Generated with cryptography 44.0.0 backed by OpenSSL. The SHA-384 vector
 * signs a 48-byte TLS transcript hash; the SHA-512 vector signs a 32-byte
 * transcript hash. Both use MGF1 with the signature hash and salt length equal
 * to the signature hash length.
 */
static const uint8_t test_rsa_sha2_public_key_der[] = {
    0x30, 0x82, 0x01, 0x0a, 0x02, 0x82, 0x01, 0x01,
    0x00, 0xac, 0x2d, 0xbe, 0x10, 0x72, 0x4c, 0xb0,
    0xb0, 0xb9, 0xa1, 0x58, 0xcd, 0x51, 0xfd, 0x46,
    0xde, 0xa9, 0x62, 0x12, 0xee, 0x50, 0x8a, 0xa0,
    0x6a, 0x65, 0x93, 0xf0, 0x1c, 0xc3, 0x89, 0x94,
    0x78, 0x76, 0x4a, 0x89, 0x5e, 0x26, 0xe7, 0x32,
    0xdf, 0xb8, 0x99, 0xa0, 0x3a, 0xbf, 0xb8, 0x5e,
    0x12, 0x05, 0x9e, 0xdf, 0xc2, 0xda, 0x06, 0xcb,
    0xdc, 0x72, 0x07, 0x0c, 0x9e, 0x0f, 0xe9, 0xeb,
    0xd5, 0x3c, 0x36, 0x51, 0xf0, 0x67, 0x41, 0x9f,
    0x00, 0xea, 0xfc, 0x1e, 0x08, 0x93, 0xac, 0x7b,
    0x18, 0xdb, 0x81, 0xca, 0xf2, 0xf0, 0x3a, 0x62,
    0xb4, 0x1a, 0x6d, 0x50, 0x8d, 0x2f, 0x40, 0xc9,
    0x72, 0xfe, 0x95, 0xa5, 0x7d, 0x56, 0x33, 0x8e,
    0x2d, 0x3e, 0x44, 0x9d, 0xa5, 0x3a, 0x8d, 0x84,
    0xb5, 0x81, 0xc4, 0x6b, 0xc4, 0x4b, 0xac, 0xfb,
    0xf4, 0xf7, 0xf1, 0x5e, 0xd5, 0xad, 0xa4, 0x6f,
    0x62, 0x75, 0xa2, 0x8b, 0x37, 0xdf, 0x02, 0x65,
    0x53, 0xa4, 0xb9, 0x67, 0xbb, 0xfa, 0xee, 0x12,
    0x62, 0xad, 0xf1, 0x89, 0x33, 0xf3, 0x4e, 0x7a,
    0x3f, 0xd8, 0x78, 0x50, 0xa4, 0xa1, 0xe4, 0xca,
    0x22, 0x8e, 0xc6, 0x60, 0x54, 0x48, 0x8a, 0xd9,
    0xee, 0xd7, 0x23, 0x5f, 0x1f, 0x4d, 0x93, 0x9b,
    0x9e, 0x6b, 0xe0, 0x33, 0x5d, 0x02, 0x7a, 0xbc,
    0xba, 0x56, 0x66, 0x71, 0xcc, 0x78, 0xce, 0x76,
    0x63, 0x2c, 0x59, 0x97, 0x46, 0xd2, 0xc0, 0xc8,
    0x3b, 0x53, 0xc7, 0xf7, 0x67, 0xf9, 0x6d, 0xd3,
    0xe2, 0x0c, 0x9b, 0xbd, 0xc6, 0xa5, 0x49, 0xc6,
    0x47, 0x87, 0x4a, 0xda, 0xc2, 0xd2, 0x28, 0xa1,
    0x99, 0x2d, 0xb2, 0x62, 0x58, 0x2c, 0xf1, 0x73,
    0x14, 0x4d, 0x85, 0x31, 0xb5, 0x4a, 0x5f, 0x7e,
    0xae, 0x8e, 0x58, 0x0b, 0x9d, 0x32, 0xfa, 0x90,
    0x80, 0x6c, 0x32, 0x71, 0xc7, 0x28, 0x1e, 0xed,
    0x49, 0x02, 0x03, 0x01, 0x00, 0x01,
};

static const uint8_t test_rsa_pss_sha384_certificate_verify[] = {
    0xa3, 0x6c, 0x6f, 0x3c, 0x39, 0xb4, 0x4c, 0xf4,
    0xc5, 0x3a, 0xd4, 0x40, 0xff, 0x13, 0x17, 0xf0,
    0xa7, 0x77, 0x42, 0x85, 0xf4, 0x06, 0x03, 0xe7,
    0xe4, 0xa3, 0xbc, 0x95, 0x9a, 0x28, 0x1b, 0xb7,
    0x22, 0x25, 0x78, 0x51, 0x14, 0x1f, 0x71, 0x9c,
    0x86, 0xeb, 0xc7, 0x02, 0x1c, 0xbc, 0x29, 0x98,
    0xf2, 0xb3, 0x85, 0x70, 0x83, 0xc3, 0x27, 0x26,
    0x38, 0x8b, 0x28, 0x75, 0x18, 0x3e, 0xdd, 0xd5,
    0xab, 0x7c, 0x99, 0x69, 0xd3, 0xae, 0x38, 0xc5,
    0x3c, 0x86, 0xbc, 0xa2, 0x67, 0x77, 0x83, 0xfd,
    0x01, 0x71, 0x4a, 0x83, 0x8d, 0x29, 0x30, 0x9e,
    0xbf, 0xf0, 0xbe, 0x2f, 0x77, 0x45, 0xff, 0x76,
    0xd0, 0x8b, 0x18, 0x24, 0x28, 0x5d, 0x18, 0x81,
    0xa2, 0xbd, 0x16, 0xfd, 0xab, 0x24, 0x3f, 0xc7,
    0x62, 0xfe, 0x51, 0xae, 0xac, 0x28, 0x42, 0xfe,
    0xcb, 0x2c, 0xd0, 0x6c, 0xd0, 0x23, 0x94, 0x65,
    0xff, 0x44, 0x5f, 0x9c, 0x57, 0xcc, 0x8b, 0x25,
    0x95, 0xea, 0x49, 0x19, 0x5b, 0x48, 0xbb, 0x76,
    0x9b, 0x79, 0xc6, 0xb8, 0xda, 0xc6, 0x78, 0x9c,
    0x3c, 0xbf, 0xf5, 0x6b, 0xa7, 0x52, 0xed, 0x04,
    0x60, 0xb5, 0x95, 0x6f, 0xaf, 0x05, 0x57, 0xa8,
    0x3e, 0xa4, 0xda, 0x33, 0x1c, 0xa1, 0xc5, 0x6f,
    0x95, 0x17, 0xc1, 0x43, 0x35, 0xe4, 0xe9, 0xce,
    0xc7, 0xd0, 0x92, 0x63, 0x9b, 0xd5, 0x1c, 0xf5,
    0x1d, 0xe4, 0x61, 0x22, 0x8f, 0x27, 0x9f, 0x5d,
    0x0e, 0x7e, 0xc3, 0x91, 0x2e, 0x33, 0x14, 0xec,
    0x3a, 0x9f, 0xf5, 0x53, 0xfd, 0x9d, 0x48, 0xe0,
    0xb6, 0xae, 0x91, 0x5b, 0xcf, 0x33, 0xfa, 0x3d,
    0x0e, 0xc1, 0xe3, 0x81, 0xf4, 0x8e, 0xab, 0x9c,
    0x36, 0x29, 0x77, 0x88, 0x3c, 0x40, 0x47, 0xe0,
    0x8c, 0xc2, 0x2c, 0x4d, 0xf7, 0x40, 0x9d, 0x26,
    0xac, 0x8f, 0xd5, 0xb3, 0x3d, 0xf2, 0x6f, 0xac,
};

static const uint8_t test_rsa_pss_sha512_certificate_verify[] = {
    0x84, 0xf0, 0xc3, 0xfa, 0xa0, 0x14, 0xfe, 0x19,
    0x3a, 0x7d, 0xea, 0xbf, 0xd3, 0xf1, 0x81, 0xa9,
    0xd2, 0x30, 0x57, 0xfe, 0xb0, 0x03, 0x0d, 0x41,
    0xc5, 0x18, 0x17, 0xf2, 0xab, 0x12, 0xf1, 0xbf,
    0xc4, 0x05, 0xc9, 0x70, 0xe4, 0xf1, 0xad, 0xc0,
    0xf6, 0xde, 0x35, 0x25, 0x38, 0xcf, 0x45, 0xcd,
    0xdf, 0xaf, 0xaf, 0x13, 0xf7, 0x2a, 0x0a, 0x25,
    0xce, 0x27, 0x74, 0x7a, 0x57, 0xbb, 0x18, 0x6c,
    0xb9, 0x87, 0x48, 0xce, 0x89, 0xba, 0x5e, 0xc6,
    0x22, 0x7b, 0x69, 0xc3, 0x20, 0x21, 0x5d, 0xe2,
    0xbc, 0x26, 0x91, 0x29, 0xae, 0x7c, 0xa0, 0x3b,
    0x1f, 0x6e, 0x92, 0x4b, 0xf5, 0xbf, 0xa7, 0x77,
    0x97, 0x82, 0x40, 0x85, 0x31, 0x08, 0x3b, 0xa5,
    0xcf, 0x60, 0x32, 0x16, 0x55, 0x28, 0xd7, 0x2a,
    0xe2, 0xef, 0x43, 0xa7, 0x27, 0x4c, 0xb4, 0x13,
    0x32, 0xd3, 0x39, 0x5d, 0x9a, 0xd0, 0x63, 0x1c,
    0xa8, 0x7f, 0xbe, 0x5d, 0xcc, 0x59, 0x04, 0x9f,
    0x8f, 0x53, 0x0a, 0x2a, 0xb5, 0x01, 0x6f, 0xa8,
    0x58, 0xb6, 0x60, 0xf8, 0xe7, 0x05, 0xba, 0xdc,
    0x6c, 0x2f, 0xf7, 0x7e, 0xb8, 0x40, 0x4c, 0xde,
    0x8a, 0x6b, 0x53, 0x46, 0x4e, 0xb5, 0x79, 0x49,
    0x15, 0x2f, 0x5b, 0xe1, 0xad, 0xda, 0x58, 0xa9,
    0x6e, 0x0a, 0xcc, 0x9e, 0x31, 0xd7, 0x54, 0xd7,
    0x89, 0x0d, 0x88, 0xa2, 0x31, 0x14, 0x4e, 0xc2,
    0x2b, 0x35, 0x86, 0x65, 0x41, 0x16, 0x89, 0x55,
    0x9e, 0x61, 0x39, 0x4e, 0x1d, 0x19, 0xee, 0x7d,
    0xea, 0x35, 0xcc, 0x5e, 0x31, 0xdd, 0xeb, 0xe3,
    0xce, 0xd9, 0x81, 0x3c, 0xf1, 0xc1, 0xa3, 0xb5,
    0x6a, 0xf5, 0x29, 0x9c, 0x0f, 0x45, 0x3b, 0xc1,
    0xe5, 0xe2, 0xbe, 0xc1, 0x2f, 0x43, 0x4a, 0x56,
    0xf0, 0x1a, 0xf8, 0xec, 0xd0, 0x7c, 0x74, 0x9a,
    0x8b, 0x96, 0xef, 0xeb, 0x18, 0xef, 0x79, 0x70,
};

static void test_certificate_verify(void) {
    printf("[certificate_verify]\n");
    neverc_x509_cert_t certificate;
    memset(&certificate, 0, sizeof(certificate));
    certificate.key_algorithm = NEVERC_X509_KEY_RSA;
    certificate.public_key = test_rsa_public_key_der;
    certificate.public_key_len = sizeof(test_rsa_public_key_der);

    uint8_t transcript_hash[32];
    for (size_t i = 0; i < sizeof(transcript_hash); ++i)
        transcript_hash[i] = (uint8_t)i;
    check_int(
        "rsa_pss_server_certificate_verify",
        neverc_tls_verify_certificate_verify(
            &certificate,
            NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA256,
            1, transcript_hash, sizeof(transcript_hash),
            test_rsa_pss_certificate_verify,
            sizeof(test_rsa_pss_certificate_verify)),
        0);

    transcript_hash[0] ^= 1;
    check_int(
        "certificate_verify_transcript_mismatch",
        neverc_tls_verify_certificate_verify(
            &certificate,
            NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA256,
            1, transcript_hash, sizeof(transcript_hash),
            test_rsa_pss_certificate_verify,
            sizeof(test_rsa_pss_certificate_verify)) != 0,
        1);
    transcript_hash[0] ^= 1;

    check_int(
        "certificate_verify_context_mismatch",
        neverc_tls_verify_certificate_verify(
            &certificate,
            NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA256,
            0, transcript_hash, sizeof(transcript_hash),
            test_rsa_pss_certificate_verify,
            sizeof(test_rsa_pss_certificate_verify)) != 0,
        1);

    uint8_t tampered_signature[
        sizeof(test_rsa_pss_certificate_verify)];
    memcpy(tampered_signature, test_rsa_pss_certificate_verify,
           sizeof(tampered_signature));
    tampered_signature[0] ^= 1;
    check_int(
        "certificate_verify_tampered_signature",
        neverc_tls_verify_certificate_verify(
            &certificate,
            NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA256,
            1, transcript_hash, sizeof(transcript_hash),
            tampered_signature, sizeof(tampered_signature)) != 0,
        1);
    check_int(
        "certificate_verify_key_scheme_mismatch",
        neverc_tls_verify_certificate_verify(
            &certificate,
            NEVERC_TLS_SIGNATURE_ECDSA_SECP256R1_SHA256,
            1, transcript_hash, sizeof(transcript_hash),
            test_rsa_pss_certificate_verify,
            sizeof(test_rsa_pss_certificate_verify)) != 0,
        1);

    certificate.public_key = (uint8_t *)test_rsa_sha2_public_key_der;
    certificate.public_key_len = sizeof(test_rsa_sha2_public_key_der);
    uint8_t transcript_hash_sha384[48];
    for (size_t i = 0; i < sizeof(transcript_hash_sha384); ++i)
        transcript_hash_sha384[i] = (uint8_t)i;
    check_int(
        "rsa_pss_sha384_server_certificate_verify",
        neverc_tls_verify_certificate_verify(
            &certificate,
            NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA384,
            1, transcript_hash_sha384, sizeof(transcript_hash_sha384),
            test_rsa_pss_sha384_certificate_verify,
            sizeof(test_rsa_pss_sha384_certificate_verify)),
        0);
    transcript_hash_sha384[0] ^= 1;
    check_int(
        "rsa_pss_sha384_transcript_mismatch",
        neverc_tls_verify_certificate_verify(
            &certificate,
            NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA384,
            1, transcript_hash_sha384, sizeof(transcript_hash_sha384),
            test_rsa_pss_sha384_certificate_verify,
            sizeof(test_rsa_pss_sha384_certificate_verify)) != 0,
        1);
    transcript_hash_sha384[0] ^= 1;
    check_int(
        "certificate_verify_invalid_transcript_size",
        neverc_tls_verify_certificate_verify(
            &certificate,
            NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA384,
            1, transcript_hash_sha384,
            sizeof(transcript_hash_sha384) - 1,
            test_rsa_pss_sha384_certificate_verify,
            sizeof(test_rsa_pss_sha384_certificate_verify)) != 0,
        1);
    check_int(
        "rsa_pss_sha512_server_certificate_verify",
        neverc_tls_verify_certificate_verify(
            &certificate,
            NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA512,
            1, transcript_hash, sizeof(transcript_hash),
            test_rsa_pss_sha512_certificate_verify,
            sizeof(test_rsa_pss_sha512_certificate_verify)),
        0);
    check_int(
        "rsa_pss_sha2_scheme_mismatch",
        neverc_tls_verify_certificate_verify(
            &certificate,
            NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA512,
            1, transcript_hash_sha384, sizeof(transcript_hash_sha384),
            test_rsa_pss_sha384_certificate_verify,
            sizeof(test_rsa_pss_sha384_certificate_verify)) != 0,
        1);

    static const char server_context[] =
        "TLS 1.3, server CertificateVerify";
    uint8_t signed_content[
        64 + sizeof(server_context) - 1 + 1 + sizeof(transcript_hash)];
    memset(signed_content, 0x20, 64);
    memcpy(signed_content + 64, server_context,
           sizeof(server_context) - 1);
    signed_content[64 + sizeof(server_context) - 1] = 0;
    memcpy(signed_content + 64 + sizeof(server_context),
           transcript_hash, sizeof(transcript_hash));

    uint8_t seed[NEVERC_ED25519_SEED_SIZE] = {9};
    uint8_t public_key[NEVERC_ED25519_PUBLIC_KEY_SIZE];
    uint8_t private_key[NEVERC_ED25519_PRIVATE_KEY_SIZE];
    uint8_t ed25519_signature[NEVERC_ED25519_SIGNATURE_SIZE];
    check_int(
        "certificate_verify_ed25519_key",
        neverc_ed25519_new_key_from_seed(
            seed, public_key, private_key),
        0);
    check_int(
        "certificate_verify_ed25519_sign",
        neverc_ed25519_sign(
            private_key, signed_content, sizeof(signed_content),
            ed25519_signature),
        0);
    memset(&certificate, 0, sizeof(certificate));
    certificate.key_algorithm = NEVERC_X509_KEY_ED25519;
    certificate.public_key = public_key;
    certificate.public_key_len = sizeof(public_key);
    check_int(
        "ed25519_server_certificate_verify",
        neverc_tls_verify_certificate_verify(
            &certificate, NEVERC_TLS_SIGNATURE_ED25519,
            1, transcript_hash, sizeof(transcript_hash),
            ed25519_signature, sizeof(ed25519_signature)),
        0);
}

/* ===== TLS client-server test ===== */

#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
typedef struct {
    neverc_tcp_conn_t *tcp;
    neverc_tls_config_t *config;
    int handshake_ok;
    int key_update_ok;
    int peer_certificate_present;
    char received[256];
    char second_received[256];
    char negotiated_alpn[256];
    char requested_server_name[256];
} server_ctx_t;

typedef struct {
    neverc_tcp_conn_t *tcp;
    neverc_tls_config_t *config;
    int handshake_ok;
    int did_resume;
    int exchange_ok;
} resumption_server_ctx_t;

typedef struct {
    neverc_tcp_conn_t *tcp;
    int response_mode;
    int saw_client_hello;
    int alert_description;
} fake_server_ctx_t;

#define TLS_CONCURRENT_WRITER_COUNT 4
#define TLS_CONCURRENT_WRITE_COUNT  12
#define TLS_CONCURRENT_PAYLOAD_SIZE 32

typedef struct {
    neverc_tls_conn_t *conn;
    int writer_id;
    int ok;
} concurrent_writer_ctx_t;

typedef struct {
    neverc_tls_conn_t *conn;
    int result;
    char data[8];
} concurrent_reader_ctx_t;

typedef struct {
    neverc_tcp_conn_t *tcp;
    neverc_tls_config_t *config;
    int ok;
    size_t counts[TLS_CONCURRENT_WRITER_COUNT];
} concurrent_server_ctx_t;

static int tcp_read_exact(
    neverc_tcp_conn_t *tcp, uint8_t *buffer, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        int result = neverc_tcp_read(
            tcp, buffer + offset, length - offset);
        if (result <= 0)
            return -1;
        offset += (size_t)result;
    }
    return 0;
}

#ifdef _WIN32
static DWORD WINAPI tls_server_thread(LPVOID arg) {
#else
static void *tls_server_thread(void *arg) {
#endif
    server_ctx_t *ctx = (server_ctx_t *)arg;

    const char *err = NULL;
    neverc_tls_conn_t *conn =
        neverc_tls_server(ctx->tcp, ctx->config, &err);
    if (conn) {
        ctx->handshake_ok = 1;
        const char *negotiated_alpn = neverc_tls_alpn(conn);
        const char *requested_server_name =
            neverc_tls_server_name(conn);
        if (negotiated_alpn)
            snprintf(ctx->negotiated_alpn,
                     sizeof(ctx->negotiated_alpn),
                     "%s", negotiated_alpn);
        if (requested_server_name)
            snprintf(ctx->requested_server_name,
                     sizeof(ctx->requested_server_name),
                     "%s", requested_server_name);
        size_t peer_certificate_len = 0;
        ctx->peer_certificate_present =
            neverc_tls_peer_certificate(
                conn, &peer_certificate_len) != NULL &&
            peer_certificate_len > 0;
        ctx->key_update_ok =
            neverc_tls_key_update(conn, 1) == 0;
        char buf[256];
        int n = neverc_tls_read(conn, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            memcpy(ctx->received, buf, (size_t)(n + 1));
        }

        if (neverc_tls_write(conn, "pong", 4) != 4)
            ctx->handshake_ok = 0;
        n = neverc_tls_read(conn, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            memcpy(ctx->second_received, buf, (size_t)(n + 1));
        }
        if (neverc_tls_write(conn, "pong2", 5) != 5)
            ctx->handshake_ok = 0;
        neverc_tls_close(conn);
    }
    neverc_tcp_close(ctx->tcp);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
static DWORD WINAPI tls_handshake_only_server_thread(LPVOID arg) {
#else
static void *tls_handshake_only_server_thread(void *arg) {
#endif
    server_ctx_t *ctx = (server_ctx_t *)arg;
    const char *err = NULL;
    neverc_tls_conn_t *conn =
        neverc_tls_server(ctx->tcp, ctx->config, &err);
    if (conn) {
        ctx->handshake_ok = 1;
        size_t peer_certificate_len = 0;
        ctx->peer_certificate_present =
            neverc_tls_peer_certificate(
                conn, &peer_certificate_len) != NULL &&
            peer_certificate_len > 0;
        neverc_tls_close(conn);
    }
    neverc_tcp_close(ctx->tcp);

#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
static DWORD WINAPI resumption_server_thread(LPVOID arg) {
#else
static void *resumption_server_thread(void *arg) {
#endif
    resumption_server_ctx_t *ctx =
        (resumption_server_ctx_t *)arg;
    const char *err = NULL;
    neverc_tls_conn_t *conn =
        neverc_tls_server(ctx->tcp, ctx->config, &err);
    if (conn) {
        ctx->handshake_ok = 1;
#if defined(NEVERC_TLS_TESTING)
        ctx->did_resume = neverc_tls_test_did_resume(conn);
#endif
        char request[8] = {0};
        int request_len =
            neverc_tls_read(conn, request, sizeof(request));
        if (request_len == 4 &&
            memcmp(request, "ping", 4) == 0 &&
            neverc_tls_write(conn, "pong", 4) == 4)
            ctx->exchange_ok = 1;
        neverc_tls_close(conn);
    }
    neverc_tcp_close(ctx->tcp);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
static DWORD WINAPI concurrent_writer_thread(LPVOID arg) {
#else
static void *concurrent_writer_thread(void *arg) {
#endif
    concurrent_writer_ctx_t *ctx =
        (concurrent_writer_ctx_t *)arg;
    uint8_t payload[TLS_CONCURRENT_PAYLOAD_SIZE];
    memset(payload, 'A' + ctx->writer_id, sizeof(payload));
    ctx->ok = 1;
    for (int i = 0; i < TLS_CONCURRENT_WRITE_COUNT; ++i) {
        if (ctx->writer_id == 0 &&
            i == TLS_CONCURRENT_WRITE_COUNT / 2 &&
            neverc_tls_key_update(ctx->conn, 0) != 0) {
            ctx->ok = 0;
            break;
        }
        if (neverc_tls_write(
                ctx->conn, payload, sizeof(payload)) !=
            (int)sizeof(payload)) {
            ctx->ok = 0;
            break;
        }
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
static DWORD WINAPI concurrent_reader_thread(LPVOID arg) {
#else
static void *concurrent_reader_thread(void *arg) {
#endif
    concurrent_reader_ctx_t *ctx =
        (concurrent_reader_ctx_t *)arg;
    ctx->result = neverc_tls_read(
        ctx->conn, ctx->data, sizeof(ctx->data));
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
static DWORD WINAPI concurrent_tls_server_thread(LPVOID arg) {
#else
static void *concurrent_tls_server_thread(void *arg) {
#endif
    concurrent_server_ctx_t *ctx =
        (concurrent_server_ctx_t *)arg;
    const char *err = NULL;
    neverc_tls_conn_t *conn =
        neverc_tls_server(ctx->tcp, ctx->config, &err);
    if (conn) {
        const size_t expected_per_writer =
            TLS_CONCURRENT_WRITE_COUNT *
            TLS_CONCURRENT_PAYLOAD_SIZE;
        const size_t expected_total =
            TLS_CONCURRENT_WRITER_COUNT *
            expected_per_writer;
        size_t received = 0;
        int valid = 1;
        uint8_t buffer[257];
        while (received < expected_total) {
            int n = neverc_tls_read(
                conn, buffer, sizeof(buffer));
            if (n <= 0) {
                valid = 0;
                break;
            }
            received += (size_t)n;
            if (received > expected_total) {
                valid = 0;
                break;
            }
            for (int i = 0; i < n; ++i) {
                int writer_id = buffer[i] - 'A';
                if (writer_id < 0 ||
                    writer_id >= TLS_CONCURRENT_WRITER_COUNT) {
                    valid = 0;
                    break;
                }
                ctx->counts[writer_id]++;
            }
            if (!valid)
                break;
        }
        for (int i = 0;
             i < TLS_CONCURRENT_WRITER_COUNT && valid; ++i) {
            if (ctx->counts[i] != expected_per_writer)
                valid = 0;
        }
        if (valid &&
            neverc_tls_write(conn, "done", 4) == 4)
            ctx->ok = 1;
        neverc_tls_close(conn);
    }
    neverc_tcp_close(ctx->tcp);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

#ifdef _WIN32
static DWORD WINAPI fake_server_thread(LPVOID arg) {
#else
static void *fake_server_thread(void *arg) {
#endif
    fake_server_ctx_t *ctx = (fake_server_ctx_t *)arg;
    uint8_t header[5];
    uint8_t payload[1024];
    if (tcp_read_exact(ctx->tcp, header, sizeof(header)) == 0) {
        size_t record_len =
            ((size_t)header[3] << 8) | header[4];
        if (header[0] == 22 && record_len <= sizeof(payload) &&
            tcp_read_exact(ctx->tcp, payload, record_len) == 0 &&
            record_len >= 4 && payload[0] == 1) {
            ctx->saw_client_hello = 1;
            static const uint8_t unexpected_record[] = {
                23, 0x03, 0x03, 0x00, 0x01, 0
            };
            if (ctx->response_mode == 0) {
                (void)neverc_tcp_write(
                    ctx->tcp, unexpected_record,
                    sizeof(unexpected_record));
            } else if (ctx->response_mode == 1) {
                uint8_t malformed_server_hello[49] = {0};
                malformed_server_hello[0] = 22;
                malformed_server_hello[1] = 0x03;
                malformed_server_hello[2] = 0x03;
                malformed_server_hello[4] = 44;
                malformed_server_hello[5] = 2;
                malformed_server_hello[8] = 40;
                malformed_server_hello[9] = 0x03;
                malformed_server_hello[10] = 0x03;
                malformed_server_hello[43] = 0xff;
                (void)neverc_tcp_write(
                    ctx->tcp, malformed_server_hello,
                    sizeof(malformed_server_hello));
            } else if (ctx->response_mode == 2) {
                static const uint8_t oversized_server_hello[] = {
                    22, 0x03, 0x03, 0x00, 0x04,
                    2, 0x01, 0x00, 0x00
                };
                (void)neverc_tcp_write(
                    ctx->tcp, oversized_server_hello,
                    sizeof(oversized_server_hello));
            } else if (ctx->response_mode == 3) {
                static const uint8_t wrong_version_record[] = {
                    22, 0x03, 0x02, 0x00, 0x04,
                    2, 0x00, 0x00, 0x00
                };
                (void)neverc_tcp_write(
                    ctx->tcp, wrong_version_record,
                    sizeof(wrong_version_record));
            } else {
                /* HelloRetryRequest magic in ServerHello.random. */
                uint8_t hello_retry[5 + 4 + 2 + 32];
                memset(hello_retry, 0, sizeof(hello_retry));
                hello_retry[0] = 22;
                hello_retry[1] = 0x03;
                hello_retry[2] = 0x03;
                hello_retry[4] = 38;
                hello_retry[5] = 2;
                hello_retry[8] = 34;
                hello_retry[9] = 0x03;
                hello_retry[10] = 0x03;
                static const uint8_t hrr_random[32] = {
                    0xCF, 0x21, 0xAD, 0x74, 0xE5, 0x9A, 0x61, 0x11,
                    0xBE, 0x1D, 0x8C, 0x02, 0x1E, 0x65, 0xB8, 0x91,
                    0xC2, 0xA2, 0x11, 0x16, 0x7A, 0xBB, 0x8C, 0x5E,
                    0x07, 0x9E, 0x09, 0xE2, 0xC8, 0xA8, 0x33, 0x9C
                };
                memcpy(hello_retry + 11, hrr_random, sizeof(hrr_random));
                (void)neverc_tcp_write(
                    ctx->tcp, hello_retry, sizeof(hello_retry));
            }

            if (tcp_read_exact(
                    ctx->tcp, header, sizeof(header)) == 0) {
                record_len =
                    ((size_t)header[3] << 8) | header[4];
                if (header[0] == 21 && record_len == 2 &&
                    tcp_read_exact(
                        ctx->tcp, payload, record_len) == 0 &&
                    payload[0] == 2) {
                    ctx->alert_description = payload[1];
                }
            }
        }
    }
    neverc_tcp_close(ctx->tcp);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void test_unexpected_server_record_alert(void) {
    printf("[unexpected_server_record_alert]\n");
    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    check_int("create_alert_pipe",
              neverc_tcp_pipe(&client_tcp, &server_tcp), 0);
    if (!client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return;
    }

    fake_server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tcp = server_tcp;
    ctx.alert_description = -1;
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, fake_server_thread, &ctx, 0, NULL);
    int thread_started = thread != NULL;
#else
    pthread_t thread;
    int thread_started =
        pthread_create(&thread, NULL, fake_server_thread, &ctx) == 0;
#endif
    check_int("start_fake_server_thread", thread_started, 1);
    if (!thread_started) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return;
    }

    neverc_tls_config_t *config = neverc_tls_config_new();
    neverc_tls_config_insecure_skip_verify(config);
    const char *err = NULL;
    neverc_tls_conn_t *client =
        neverc_tls_client(client_tcp, config, &err);
    check_null("reject_unexpected_server_record", client);
    neverc_tls_close(client);
    neverc_tcp_close(client_tcp);
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    check_int("fake_server_saw_client_hello",
              ctx.saw_client_hello, 1);
    check_int("send_unexpected_message_alert",
              ctx.alert_description, 10);
    neverc_tls_config_free(config);
}

static void test_malformed_server_hello_alert(void) {
    printf("[malformed_server_hello_alert]\n");
    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    check_int("create_malformed_hello_pipe",
              neverc_tcp_pipe(&client_tcp, &server_tcp), 0);
    if (!client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return;
    }

    fake_server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tcp = server_tcp;
    ctx.response_mode = 1;
    ctx.alert_description = -1;
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, fake_server_thread, &ctx, 0, NULL);
    int thread_started = thread != NULL;
#else
    pthread_t thread;
    int thread_started =
        pthread_create(&thread, NULL, fake_server_thread, &ctx) == 0;
#endif
    check_int("start_malformed_hello_thread", thread_started, 1);
    if (!thread_started) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return;
    }

    neverc_tls_config_t *config = neverc_tls_config_new();
    neverc_tls_config_insecure_skip_verify(config);
    const char *err = NULL;
    neverc_tls_conn_t *client =
        neverc_tls_client(client_tcp, config, &err);
    check_null("reject_malformed_server_hello", client);
    neverc_tls_close(client);
    neverc_tcp_close(client_tcp);
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    check_int("malformed_server_saw_client_hello",
              ctx.saw_client_hello, 1);
    check_int("send_decode_error_alert",
              ctx.alert_description, 50);
    neverc_tls_config_free(config);
}

static void test_plaintext_record_version_ignored(void) {
    printf("[plaintext_record_version_ignored]\n");
    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    check_int("create_wrong_record_version_pipe",
              neverc_tcp_pipe(&client_tcp, &server_tcp), 0);
    if (!client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return;
    }

    fake_server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tcp = server_tcp;
    ctx.response_mode = 3;
    ctx.alert_description = -1;
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, fake_server_thread, &ctx, 0, NULL);
    int thread_started = thread != NULL;
#else
    pthread_t thread;
    int thread_started =
        pthread_create(&thread, NULL, fake_server_thread, &ctx) == 0;
#endif
    check_int("start_wrong_record_version_thread",
              thread_started, 1);
    if (!thread_started) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return;
    }

    neverc_tls_config_t *config = neverc_tls_config_new();
    neverc_tls_config_insecure_skip_verify(config);
    const char *err = NULL;
    neverc_tls_conn_t *client =
        neverc_tls_client(client_tcp, config, &err);
    check_null("reject_malformed_server_hello", client);
    neverc_tls_close(client);
    neverc_tcp_close(client_tcp);
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    check_int("wrong_version_server_saw_client_hello",
              ctx.saw_client_hello, 1);
    check_int("ignore_plaintext_version_and_send_decode_error",
              ctx.alert_description, 50);
    neverc_tls_config_free(config);
}

static void test_hello_retry_request_alert(void) {
    printf("[hello_retry_request_alert]\n");
    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    check_int("create_hrr_pipe",
              neverc_tcp_pipe(&client_tcp, &server_tcp), 0);
    if (!client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return;
    }

    fake_server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tcp = server_tcp;
    ctx.response_mode = 4;
    ctx.alert_description = -1;
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, fake_server_thread, &ctx, 0, NULL);
    int thread_started = thread != NULL;
#else
    pthread_t thread;
    int thread_started =
        pthread_create(&thread, NULL, fake_server_thread, &ctx) == 0;
#endif
    check_int("start_hrr_thread", thread_started, 1);
    if (!thread_started) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return;
    }

    neverc_tls_config_t *config = neverc_tls_config_new();
    neverc_tls_config_insecure_skip_verify(config);
    const char *err = NULL;
    neverc_tls_conn_t *client =
        neverc_tls_client(client_tcp, config, &err);
    check_null("reject_hello_retry_request", client);
    neverc_tls_close(client);
    neverc_tcp_close(client_tcp);
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    check_int("hrr_server_saw_client_hello",
              ctx.saw_client_hello, 1);
    check_int("send_handshake_failure_for_hrr",
              ctx.alert_description, 40);
    neverc_tls_config_free(config);
}

static void test_oversized_server_handshake_alert(void) {
    printf("[oversized_server_handshake_alert]\n");
    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    check_int("create_oversized_server_handshake_pipe",
              neverc_tcp_pipe(&client_tcp, &server_tcp), 0);
    if (!client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return;
    }

    fake_server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tcp = server_tcp;
    ctx.response_mode = 2;
    ctx.alert_description = -1;
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, fake_server_thread, &ctx, 0, NULL);
    int thread_started = thread != NULL;
#else
    pthread_t thread;
    int thread_started =
        pthread_create(&thread, NULL, fake_server_thread, &ctx) == 0;
#endif
    check_int("start_oversized_server_handshake_thread",
              thread_started, 1);
    if (!thread_started) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return;
    }

    neverc_tls_config_t *config = neverc_tls_config_new();
    neverc_tls_config_insecure_skip_verify(config);
    const char *err = NULL;
    neverc_tls_conn_t *client =
        neverc_tls_client(client_tcp, config, &err);
    check_null("reject_oversized_server_handshake", client);
    neverc_tls_close(client);
    neverc_tcp_close(client_tcp);
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    check_int("oversized_server_saw_client_hello",
              ctx.saw_client_hello, 1);
    check_int("oversized_server_send_decode_error_alert",
              ctx.alert_description, 50);
    neverc_tls_config_free(config);
}

static void test_malformed_client_hello_alert(void) {
    printf("[malformed_client_hello_alert]\n");
    neverc_tls_config_t *config = neverc_tls_config_new();
    check_not_null("malformed_client_config", config);
    if (!config)
        return;
    check_int("malformed_client_load_certificate",
              neverc_tls_config_load_cert_mem(
                  config, TEST_CERT_PEM, TEST_KEY_PEM),
              0);

    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    check_int("create_malformed_client_pipe",
              neverc_tcp_pipe(&client_tcp, &server_tcp), 0);
    if (!client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        neverc_tls_config_free(config);
        return;
    }

    server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tcp = server_tcp;
    ctx.config = config;
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, tls_server_thread, &ctx, 0, NULL);
    int thread_started = thread != NULL;
#else
    pthread_t thread;
    int thread_started =
        pthread_create(&thread, NULL, tls_server_thread, &ctx) == 0;
#endif
    check_int("start_malformed_client_thread", thread_started, 1);
    if (!thread_started) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        neverc_tls_config_free(config);
        return;
    }

    static const uint8_t malformed_client_hello[] = {
        22, 0x03, 0x03, 0x00, 0x04,
        1, 0, 0, 0
    };
    check_int("send_malformed_client_hello",
              neverc_tcp_write(
                  client_tcp, malformed_client_hello,
                  sizeof(malformed_client_hello)) ==
                  (int)sizeof(malformed_client_hello),
              1);

    uint8_t header[5];
    uint8_t alert[2];
    int received_decode_error =
        tcp_read_exact(client_tcp, header, sizeof(header)) == 0 &&
        header[0] == 21 && header[3] == 0 && header[4] == 2 &&
        tcp_read_exact(client_tcp, alert, sizeof(alert)) == 0 &&
        alert[0] == 2 && alert[1] == 50;
    check_int("server_send_decode_error_alert",
              received_decode_error, 1);
    neverc_tcp_close(client_tcp);
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    check_int("server_reject_malformed_client_hello",
              ctx.handshake_ok, 0);
    neverc_tls_config_free(config);
}

static void test_oversized_client_handshake_alert(void) {
    printf("[oversized_client_handshake_alert]\n");
    neverc_tls_config_t *config = neverc_tls_config_new();
    check_not_null("oversized_client_config", config);
    if (!config)
        return;
    check_int("oversized_client_load_certificate",
              neverc_tls_config_load_cert_mem(
                  config, TEST_CERT_PEM, TEST_KEY_PEM),
              0);

    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    check_int("create_oversized_client_pipe",
              neverc_tcp_pipe(&client_tcp, &server_tcp), 0);
    if (!client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        neverc_tls_config_free(config);
        return;
    }

    server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tcp = server_tcp;
    ctx.config = config;
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, tls_server_thread, &ctx, 0, NULL);
    int thread_started = thread != NULL;
#else
    pthread_t thread;
    int thread_started =
        pthread_create(&thread, NULL, tls_server_thread, &ctx) == 0;
#endif
    check_int("start_oversized_client_thread", thread_started, 1);
    if (!thread_started) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        neverc_tls_config_free(config);
        return;
    }

    static const uint8_t oversized_client_hello[] = {
        22, 0x03, 0x03, 0x00, 0x04,
        1, 0x01, 0x00, 0x00
    };
    check_int("send_oversized_client_hello",
              neverc_tcp_write(
                  client_tcp, oversized_client_hello,
                  sizeof(oversized_client_hello)) ==
                  (int)sizeof(oversized_client_hello),
              1);
    (void)neverc_tcp_shutdown_write(client_tcp);

    uint8_t header[5];
    uint8_t alert[2];
    int received_decode_error =
        tcp_read_exact(client_tcp, header, sizeof(header)) == 0 &&
        header[0] == 21 && header[3] == 0 && header[4] == 2 &&
        tcp_read_exact(client_tcp, alert, sizeof(alert)) == 0 &&
        alert[0] == 2 && alert[1] == 50;
    check_int("oversized_client_send_decode_error_alert",
              received_decode_error, 1);
    neverc_tcp_close(client_tcp);
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    check_int("server_reject_oversized_client_handshake",
              ctx.handshake_ok, 0);
    neverc_tls_config_free(config);
}

static int run_resumption_exchange(
    neverc_tls_config_t *server_config,
    neverc_tls_config_t *client_config,
    int *client_did_resume,
    int *server_did_resume) {
    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    if (neverc_tcp_pipe(&client_tcp, &server_tcp) != 0 ||
        !client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return 0;
    }

    resumption_server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tcp = server_tcp;
    ctx.config = server_config;
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, resumption_server_thread, &ctx, 0, NULL);
    int thread_started = thread != NULL;
#else
    pthread_t thread;
    int thread_started = pthread_create(
        &thread, NULL, resumption_server_thread, &ctx) == 0;
#endif
    if (!thread_started) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        return 0;
    }

    int client_ok = 0;
    const char *err = NULL;
    neverc_tls_conn_t *client =
        neverc_tls_client(client_tcp, client_config, &err);
    if (client) {
#if defined(NEVERC_TLS_TESTING)
        if (client_did_resume)
            *client_did_resume =
                neverc_tls_test_did_resume(client);
#else
        (void)client_did_resume;
#endif
        char response[8] = {0};
        client_ok =
            neverc_tls_write(client, "ping", 4) == 4 &&
            neverc_tls_read(
                client, response, sizeof(response)) == 4 &&
            memcmp(response, "pong", 4) == 0;
        neverc_tls_close(client);
    }
    neverc_tcp_close(client_tcp);
#ifdef _WIN32
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_join(thread, NULL);
#endif
    if (server_did_resume)
        *server_did_resume = ctx.did_resume;
    return client_ok && ctx.handshake_ok && ctx.exchange_ok;
}
#endif

static void test_client_server(void) {
    printf("[client_server]\n");

#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    neverc_tls_config_t *server_config = neverc_tls_config_new();
    neverc_tls_config_t *client_config = neverc_tls_config_new();
    check_not_null("server_config_new", server_config);
    check_not_null("client_config_new", client_config);
    if (!server_config || !client_config) {
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }

    check_int("load_server_certificate",
              neverc_tls_config_load_cert_mem(
                  server_config, TEST_CERT_PEM, TEST_KEY_PEM),
              0);
    neverc_tls_config_set_server_name(client_config, "localhost");
    check_int("load_client_root",
              neverc_tls_config_add_root_certificates_mem(
                  client_config, TEST_CERT_PEM, strlen(TEST_CERT_PEM)),
              0);
    const char *client_alpn[] = {"http/1.1", "h2"};
    const char *server_alpn[] = {"h2", "http/1.1"};
    neverc_tls_config_set_alpn(
        client_config, client_alpn, 2);
    neverc_tls_config_set_alpn(
        server_config, server_alpn, 2);
#if defined(NEVERC_TLS_TESTING)
    check_int("fragment_server_handshake_records",
              neverc_tls_test_config_set_handshake_fragment_size(
                  server_config, 3),
              0);
    check_int("fragment_client_handshake_records",
              neverc_tls_test_config_set_handshake_fragment_size(
                  client_config, 5),
              0);
#endif

    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    check_int("create_tls_pipe",
              neverc_tcp_pipe(&client_tcp, &server_tcp), 0);
    if (!client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }

    server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tcp = server_tcp;
    ctx.config = server_config;
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, tls_server_thread, &ctx, 0, NULL);
    int thread_started = thread != NULL;
#else
    pthread_t thread;
    int thread_started =
        pthread_create(&thread, NULL, tls_server_thread, &ctx) == 0;
#endif
    check_int("start_tls_server_thread", thread_started, 1);

    if (thread_started) {
        const char *err = NULL;
        neverc_tls_conn_t *client =
            neverc_tls_client(client_tcp, client_config, &err);
        check_not_null("client_handshake", client);
        if (client) {
            check_int("client_negotiated_server_preferred_alpn",
                      neverc_tls_alpn(client) &&
                      strcmp(neverc_tls_alpn(client), "h2") == 0,
                      1);
            check_int("client_outbound_sni_state",
                      neverc_tls_server_name(client) &&
                      strcmp(neverc_tls_server_name(client),
                             "localhost") == 0,
                      1);
            check_int("reject_invalid_key_update_request",
                      neverc_tls_key_update(client, 2) != 0, 1);
            check_int("client_write",
                      neverc_tls_write(client, "ping", 4), 4);
            char response[8] = {0};
            int response_len =
                neverc_tls_read(client, response, sizeof(response));
            check_int("client_read_length", response_len, 4);
            check_int("client_read_payload",
                      response_len == 4 &&
                      memcmp(response, "pong", 4) == 0,
                      1);
            check_int("client_second_key_update",
                      neverc_tls_key_update(client, 0), 0);
            check_int("client_write_after_key_update",
                      neverc_tls_write(client, "ping2", 5), 5);
            memset(response, 0, sizeof(response));
            response_len =
                neverc_tls_read(client, response, sizeof(response));
            check_int("client_read_after_key_update_length",
                      response_len, 5);
            check_int("client_read_after_key_update_payload",
                      response_len == 5 &&
                      memcmp(response, "pong2", 5) == 0,
                      1);
            check_int("client_receive_close_notify",
                      neverc_tls_read(client, response, sizeof(response)),
                      0);
            neverc_tls_close(client);
        }
        neverc_tcp_close(client_tcp);
#ifdef _WIN32
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
#else
        pthread_join(thread, NULL);
#endif
        check_int("server_handshake", ctx.handshake_ok, 1);
        check_int("server_key_update", ctx.key_update_ok, 1);
        check_int("server_read_payload",
                  strcmp(ctx.received, "ping") == 0, 1);
        check_int("server_read_after_key_update",
                  strcmp(ctx.second_received, "ping2") == 0, 1);
        check_int("server_negotiated_alpn",
                  strcmp(ctx.negotiated_alpn, "h2") == 0, 1);
        check_int("server_received_sni",
                  strcmp(ctx.requested_server_name,
                         "localhost") == 0,
                  1);
    } else {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
    }

    neverc_tls_config_free(server_config);
    neverc_tls_config_free(client_config);
#else
    neverc_tls_config_t *cfg = neverc_tls_config_new();
    check_not_null("config for server", cfg);
    check_int("load server certificate",
              neverc_tls_config_load_cert_mem(
                  cfg, TEST_CERT_PEM, TEST_KEY_PEM),
              0);
    neverc_tls_config_free(cfg);
#endif
}

static void test_session_resumption(void) {
    printf("[session_resumption]\n");

#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT) && \
    defined(NEVERC_TLS_TESTING)
    neverc_tls_config_t *server_config = neverc_tls_config_new();
    neverc_tls_config_t *client_config = neverc_tls_config_new();
    check_not_null("resumption_server_config", server_config);
    check_not_null("resumption_client_config", client_config);
    if (!server_config || !client_config) {
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }
    check_int("resumption_load_server_certificate",
              neverc_tls_config_load_cert_mem(
                  server_config, TEST_CERT_PEM, TEST_KEY_PEM),
              0);
    neverc_tls_config_set_server_name(client_config, "localhost");
    check_int("resumption_load_client_root",
              neverc_tls_config_add_root_certificates_mem(
                  client_config, TEST_CERT_PEM,
                  strlen(TEST_CERT_PEM)),
              0);

    int client_resumed = -1;
    int server_resumed = -1;
    check_int("resumption_initial_exchange",
              run_resumption_exchange(
                  server_config, client_config,
                  &client_resumed, &server_resumed),
              1);
    check_int("resumption_initial_client_full",
              client_resumed, 0);
    check_int("resumption_initial_server_full",
              server_resumed, 0);

    client_resumed = server_resumed = 0;
    check_int("resumption_second_exchange",
              run_resumption_exchange(
                  server_config, client_config,
                  &client_resumed, &server_resumed),
              1);
    check_int("resumption_client_selected_psk",
              client_resumed, 1);
    check_int("resumption_server_selected_psk",
              server_resumed, 1);

    /* A resumed handshake issues and caches a replacement ticket. */
    client_resumed = server_resumed = 0;
    check_int("resumption_rotated_ticket_exchange",
              run_resumption_exchange(
                  server_config, client_config,
                  &client_resumed, &server_resumed),
              1);
    check_int("resumption_rotated_ticket_client",
              client_resumed, 1);
    check_int("resumption_rotated_ticket_server",
              server_resumed, 1);

    check_int("resumption_additional_root_invalidates_client_ticket",
              neverc_tls_config_add_root_certificates_mem(
                  client_config, TEST_CLIENT_CERT_PEM,
                  strlen(TEST_CLIENT_CERT_PEM)),
              0);
    client_resumed = server_resumed = -1;
    check_int("resumption_root_change_falls_back",
              run_resumption_exchange(
                  server_config, client_config,
                  &client_resumed, &server_resumed),
              1);
    check_int("resumption_root_change_client_full",
              client_resumed, 0);
    check_int("resumption_root_change_server_full",
              server_resumed, 0);

    check_int("resumption_reload_server_identity",
              neverc_tls_config_load_cert_mem(
                  server_config, TEST_CERT_PEM, TEST_KEY_PEM),
              0);
    client_resumed = server_resumed = -1;
    check_int("resumption_identity_change_falls_back",
              run_resumption_exchange(
                  server_config, client_config,
                  &client_resumed, &server_resumed),
              1);
    check_int("resumption_identity_change_client_full",
              client_resumed, 0);
    check_int("resumption_identity_change_server_full",
              server_resumed, 0);

    check_int("resumption_expire_server_ticket",
              neverc_tls_test_expire_server_sessions(
                  server_config),
              0);
    client_resumed = server_resumed = -1;
    check_int("resumption_expired_server_ticket_falls_back",
              run_resumption_exchange(
                  server_config, client_config,
                  &client_resumed, &server_resumed),
              1);
    check_int("resumption_server_expiry_client_full",
              client_resumed, 0);
    check_int("resumption_server_expiry_server_full",
              server_resumed, 0);

    check_int("resumption_expire_cached_ticket",
              neverc_tls_test_expire_client_session(
                  client_config),
              0);
    client_resumed = server_resumed = -1;
    check_int("resumption_expired_ticket_falls_back",
              run_resumption_exchange(
                  server_config, client_config,
                  &client_resumed, &server_resumed),
              1);
    check_int("resumption_expired_client_full",
              client_resumed, 0);
    check_int("resumption_expired_server_full",
              server_resumed, 0);
    neverc_tls_config_free(server_config);
    neverc_tls_config_free(client_config);

    server_config = neverc_tls_config_new();
    client_config = neverc_tls_config_new();
    check_not_null("binder_server_config", server_config);
    check_not_null("binder_client_config", client_config);
    if (!server_config || !client_config) {
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }
    check_int("binder_load_server_certificate",
              neverc_tls_config_load_cert_mem(
                  server_config, TEST_CERT_PEM, TEST_KEY_PEM),
              0);
    neverc_tls_config_set_server_name(client_config, "localhost");
    check_int("binder_load_client_root",
              neverc_tls_config_add_root_certificates_mem(
                  client_config, TEST_CERT_PEM,
                  strlen(TEST_CERT_PEM)),
              0);
    check_int("binder_prime_ticket",
              run_resumption_exchange(
                  server_config, client_config, NULL, NULL),
              1);
    check_int("binder_corrupt_cached_psk",
              neverc_tls_test_corrupt_client_session(
                  client_config),
              0);
    check_int("binder_reject_corrupted_value",
              run_resumption_exchange(
                  server_config, client_config, NULL, NULL),
              0);
    neverc_tls_config_free(server_config);
    neverc_tls_config_free(client_config);

    server_config = neverc_tls_config_new();
    client_config = neverc_tls_config_new();
    check_not_null("resumption_alpn_server_config", server_config);
    check_not_null("resumption_alpn_client_config", client_config);
    if (!server_config || !client_config) {
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }
    check_int("resumption_alpn_load_server_certificate",
              neverc_tls_config_load_cert_mem(
                  server_config, TEST_CERT_PEM, TEST_KEY_PEM),
              0);
    neverc_tls_config_set_server_name(client_config, "localhost");
    check_int("resumption_alpn_load_client_root",
              neverc_tls_config_add_root_certificates_mem(
                  client_config, TEST_CERT_PEM,
                  strlen(TEST_CERT_PEM)),
              0);
    const char *resumption_alpn[] = {"h2", "http/1.1"};
    neverc_tls_config_set_alpn(
        server_config, resumption_alpn, 2);
    neverc_tls_config_set_alpn(
        client_config, resumption_alpn, 2);
    check_int("resumption_alpn_prime_ticket",
              run_resumption_exchange(
                  server_config, client_config, NULL, NULL),
              1);
    check_int("resumption_alpn_corrupt_cached_binding",
              neverc_tls_test_set_client_session_alpn(
                  client_config, "http/1.1"),
              0);
    check_int("resumption_alpn_reject_changed_binding",
              run_resumption_exchange(
                  server_config, client_config, NULL, NULL),
              0);
    neverc_tls_config_free(server_config);
    neverc_tls_config_free(client_config);

    server_config = neverc_tls_config_new();
    client_config = neverc_tls_config_new();
    check_not_null("large_client_hello_server_config", server_config);
    check_not_null("large_client_hello_client_config", client_config);
    if (!server_config || !client_config) {
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }
    check_int("large_client_hello_load_server_certificate",
              neverc_tls_config_load_cert_mem(
                  server_config, TEST_CERT_PEM, TEST_KEY_PEM),
              0);
    neverc_tls_config_set_server_name(client_config, "localhost");
    check_int("large_client_hello_load_client_root",
              neverc_tls_config_add_root_certificates_mem(
                  client_config, TEST_CERT_PEM,
                  strlen(TEST_CERT_PEM)),
              0);
    /* Fill the ALPN list to TLS_MAX_ALPN_LIST (8 * 256) so a max-sized
     * ticket cannot fit in TLS_CLIENT_HELLO_CAPACITY and must fall back. */
    char long_protocol_storage[8][256];
    const char *long_protocols[8];
    for (size_t i = 0; i < 8; ++i) {
        memset(long_protocol_storage[i], (int)('a' + i), 255);
        long_protocol_storage[i][255] = '\0';
        long_protocols[i] = long_protocol_storage[i];
    }
    neverc_tls_config_set_alpn(server_config, long_protocols, 1);
    neverc_tls_config_set_alpn(client_config, long_protocols, 8);
    check_int("large_client_hello_prime_ticket",
              run_resumption_exchange(
                  server_config, client_config, NULL, NULL),
              1);
    check_int("large_client_hello_expand_cached_ticket",
              neverc_tls_test_resize_client_session_ticket(
                  client_config, 2048),
              0);
    client_resumed = server_resumed = -1;
    check_int("large_client_hello_falls_back_safely",
              run_resumption_exchange(
                  server_config, client_config,
                  &client_resumed, &server_resumed),
              1);
    check_int("large_client_hello_client_full",
              client_resumed, 0);
    check_int("large_client_hello_server_full",
              server_resumed, 0);
    neverc_tls_config_free(server_config);
    neverc_tls_config_free(client_config);
#endif
}

static void test_alpn_no_overlap(void) {
    printf("[alpn_no_overlap]\n");

#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    neverc_tls_config_t *server_config = neverc_tls_config_new();
    neverc_tls_config_t *client_config = neverc_tls_config_new();
    check_not_null("alpn_mismatch_server_config", server_config);
    check_not_null("alpn_mismatch_client_config", client_config);
    if (!server_config || !client_config) {
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }
    check_int("alpn_mismatch_load_server_certificate",
              neverc_tls_config_load_cert_mem(
                  server_config, TEST_CERT_PEM, TEST_KEY_PEM),
              0);
    neverc_tls_config_set_server_name(client_config, "localhost");
    check_int("alpn_mismatch_load_client_root",
              neverc_tls_config_add_root_certificates_mem(
                  client_config, TEST_CERT_PEM,
                  strlen(TEST_CERT_PEM)),
              0);
    const char *server_alpn[] = {"h2"};
    const char *client_alpn[] = {"http/1.1"};
    neverc_tls_config_set_alpn(server_config, server_alpn, 1);
    neverc_tls_config_set_alpn(client_config, client_alpn, 1);

    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    check_int("create_alpn_mismatch_pipe",
              neverc_tcp_pipe(&client_tcp, &server_tcp), 0);
    if (!client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }

    server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tcp = server_tcp;
    ctx.config = server_config;
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, tls_handshake_only_server_thread,
        &ctx, 0, NULL);
    int thread_started = thread != NULL;
#else
    pthread_t thread;
    int thread_started = pthread_create(
        &thread, NULL, tls_handshake_only_server_thread,
        &ctx) == 0;
#endif
    check_int("start_alpn_mismatch_server", thread_started, 1);
    if (thread_started) {
        const char *err = NULL;
        neverc_tls_conn_t *client =
            neverc_tls_client(client_tcp, client_config, &err);
        check_null("reject_alpn_without_overlap", client);
        neverc_tls_close(client);
        neverc_tcp_close(client_tcp);
#ifdef _WIN32
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
#else
        pthread_join(thread, NULL);
#endif
        check_int("server_reject_alpn_without_overlap",
                  ctx.handshake_ok, 0);
    } else {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
    }
    neverc_tls_config_free(server_config);
    neverc_tls_config_free(client_config);
#endif
}

static void test_concurrent_record_io(void) {
    printf("[concurrent_record_io]\n");

#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    neverc_tls_config_t *server_config = neverc_tls_config_new();
    neverc_tls_config_t *client_config = neverc_tls_config_new();
    check_not_null("concurrent_server_config_new", server_config);
    check_not_null("concurrent_client_config_new", client_config);
    if (!server_config || !client_config) {
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }

    check_int("concurrent_load_server_certificate",
              neverc_tls_config_load_cert_mem(
                  server_config, TEST_CERT_PEM, TEST_KEY_PEM),
              0);
    neverc_tls_config_set_server_name(client_config, "localhost");
    check_int("concurrent_load_client_root",
              neverc_tls_config_add_root_certificates_mem(
                  client_config, TEST_CERT_PEM,
                  strlen(TEST_CERT_PEM)),
              0);
#if defined(NEVERC_TLS_TESTING)
    check_int("concurrent_fragment_server_handshake",
              neverc_tls_test_config_set_handshake_fragment_size(
                  server_config, 13),
              0);
    check_int("concurrent_fragment_client_handshake",
              neverc_tls_test_config_set_handshake_fragment_size(
                  client_config, 17),
              0);
#endif

    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    check_int("create_concurrent_tls_pipe",
              neverc_tcp_pipe(&client_tcp, &server_tcp), 0);
    if (!client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }

    concurrent_server_ctx_t server_ctx;
    memset(&server_ctx, 0, sizeof(server_ctx));
    server_ctx.tcp = server_tcp;
    server_ctx.config = server_config;
#ifdef _WIN32
    HANDLE server_thread = CreateThread(
        NULL, 0, concurrent_tls_server_thread,
        &server_ctx, 0, NULL);
    int server_started = server_thread != NULL;
#else
    pthread_t server_thread;
    int server_started = pthread_create(
        &server_thread, NULL,
        concurrent_tls_server_thread, &server_ctx) == 0;
#endif
    check_int("start_concurrent_tls_server", server_started, 1);
    if (server_started) {
        const char *err = NULL;
        neverc_tls_conn_t *client =
            neverc_tls_client(client_tcp, client_config, &err);
        check_not_null("concurrent_client_handshake", client);
        if (client) {
            concurrent_writer_ctx_t writer_ctx[
                TLS_CONCURRENT_WRITER_COUNT];
            memset(writer_ctx, 0, sizeof(writer_ctx));
#ifdef _WIN32
            HANDLE writer_threads[
                TLS_CONCURRENT_WRITER_COUNT] = {0};
#else
            pthread_t writer_threads[
                TLS_CONCURRENT_WRITER_COUNT];
#endif
            int writers_started = 0;
            for (int i = 0;
                 i < TLS_CONCURRENT_WRITER_COUNT; ++i) {
                writer_ctx[i].conn = client;
                writer_ctx[i].writer_id = i;
#ifdef _WIN32
                writer_threads[i] = CreateThread(
                    NULL, 0, concurrent_writer_thread,
                    &writer_ctx[i], 0, NULL);
                if (!writer_threads[i])
                    break;
#else
                if (pthread_create(
                        &writer_threads[i], NULL,
                        concurrent_writer_thread,
                        &writer_ctx[i]) != 0)
                    break;
#endif
                writers_started++;
            }
            check_int("start_concurrent_tls_writers",
                      writers_started,
                      TLS_CONCURRENT_WRITER_COUNT);
            for (int i = 0; i < writers_started; ++i) {
#ifdef _WIN32
                WaitForSingleObject(writer_threads[i], INFINITE);
                CloseHandle(writer_threads[i]);
#else
                pthread_join(writer_threads[i], NULL);
#endif
                check_int("concurrent_tls_writer_ok",
                          writer_ctx[i].ok, 1);
            }

            if (writers_started ==
                TLS_CONCURRENT_WRITER_COUNT) {
                concurrent_reader_ctx_t reader_ctx[2];
                memset(reader_ctx, 0, sizeof(reader_ctx));
#ifdef _WIN32
                HANDLE reader_threads[2] = {0};
#else
                pthread_t reader_threads[2];
#endif
                int readers_started = 0;
                for (int i = 0; i < 2; ++i) {
                    reader_ctx[i].conn = client;
#ifdef _WIN32
                    reader_threads[i] = CreateThread(
                        NULL, 0, concurrent_reader_thread,
                        &reader_ctx[i], 0, NULL);
                    if (!reader_threads[i])
                        break;
#else
                    if (pthread_create(
                            &reader_threads[i], NULL,
                            concurrent_reader_thread,
                            &reader_ctx[i]) != 0)
                        break;
#endif
                    readers_started++;
                }
                check_int("start_concurrent_tls_readers",
                          readers_started, 2);
                int response_bytes = 0;
                int saw_done = 0;
                int reader_results_valid = 1;
                for (int i = 0; i < readers_started; ++i) {
#ifdef _WIN32
                    WaitForSingleObject(
                        reader_threads[i], INFINITE);
                    CloseHandle(reader_threads[i]);
#else
                    pthread_join(reader_threads[i], NULL);
#endif
                    if (reader_ctx[i].result < 0) {
                        reader_results_valid = 0;
                    } else {
                        response_bytes += reader_ctx[i].result;
                    }
                    if (reader_ctx[i].result == 4 &&
                        memcmp(reader_ctx[i].data,
                               "done", 4) == 0)
                        saw_done = 1;
                }
                check_int("concurrent_tls_reader_results",
                          reader_results_valid, 1);
                check_int("concurrent_tls_response_length",
                          response_bytes, 4);
                check_int("concurrent_tls_response_payload",
                          saw_done, 1);
            }
            neverc_tls_close(client);
        }
        neverc_tcp_close(client_tcp);
#ifdef _WIN32
        WaitForSingleObject(server_thread, INFINITE);
        CloseHandle(server_thread);
#else
        pthread_join(server_thread, NULL);
#endif
        check_int("concurrent_tls_server_ok",
                  server_ctx.ok, 1);
        int counts_ok = 1;
        const size_t expected_per_writer =
            TLS_CONCURRENT_WRITE_COUNT *
            TLS_CONCURRENT_PAYLOAD_SIZE;
        for (int i = 0; i < TLS_CONCURRENT_WRITER_COUNT; ++i) {
            if (server_ctx.counts[i] != expected_per_writer)
                counts_ok = 0;
        }
        check_int("concurrent_tls_payload_counts",
                  counts_ok, 1);
    } else {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
    }

    neverc_tls_config_free(server_config);
    neverc_tls_config_free(client_config);
#endif
}

static void test_mutual_tls(void) {
    printf("[mutual_tls]\n");

#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    neverc_tls_config_t *server_config = neverc_tls_config_new();
    neverc_tls_config_t *client_config = neverc_tls_config_new();
    check_not_null("mtls_server_config_new", server_config);
    check_not_null("mtls_client_config_new", client_config);
    if (!server_config || !client_config) {
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }

    check_int("mtls_load_server_certificate",
              neverc_tls_config_load_cert_mem(
                  server_config, TEST_CERT_PEM, TEST_KEY_PEM),
              0);
    check_int("mtls_add_client_root",
              neverc_tls_config_add_root_certificates_mem(
                  server_config, TEST_CLIENT_CERT_PEM,
                  strlen(TEST_CLIENT_CERT_PEM)),
              0);
    check_int("mtls_require_client_certificate",
              neverc_tls_config_set_client_auth(
                  server_config,
                  NEVERC_TLS_CLIENT_AUTH_REQUIRE_AND_VERIFY),
              0);

    neverc_tls_config_set_server_name(client_config, "localhost");
    check_int("mtls_add_server_root",
              neverc_tls_config_add_root_certificates_mem(
                  client_config, TEST_CERT_PEM,
                  strlen(TEST_CERT_PEM)),
              0);
    check_int("mtls_load_client_certificate",
              neverc_tls_config_load_cert_mem(
                  client_config, TEST_CLIENT_CERT_PEM,
                  TEST_CLIENT_KEY_PEM),
              0);
#if defined(NEVERC_TLS_TESTING)
    check_int("fragment_mtls_server_handshake_records",
              neverc_tls_test_config_set_handshake_fragment_size(
                  server_config, 11),
              0);
    check_int("fragment_mtls_client_handshake_records",
              neverc_tls_test_config_set_handshake_fragment_size(
                  client_config, 3),
              0);
#endif

    neverc_tls_config_t *no_certificate_config =
        neverc_tls_config_new();
    check_not_null(
        "mtls_no_certificate_config_new",
        no_certificate_config);
    if (no_certificate_config) {
        neverc_tls_config_set_server_name(
            no_certificate_config, "localhost");
        check_int("mtls_no_certificate_add_server_root",
                  neverc_tls_config_add_root_certificates_mem(
                      no_certificate_config, TEST_CERT_PEM,
                      strlen(TEST_CERT_PEM)),
                  0);
#if defined(NEVERC_TLS_TESTING)
        check_int(
            "fragment_mtls_missing_certificate_handshake_records",
            neverc_tls_test_config_set_handshake_fragment_size(
                no_certificate_config, 2),
            0);
#endif

        neverc_tcp_conn_t *missing_client_tcp = NULL;
        neverc_tcp_conn_t *missing_server_tcp = NULL;
        check_int("create_mtls_missing_certificate_pipe",
                  neverc_tcp_pipe(
                      &missing_client_tcp, &missing_server_tcp),
                  0);
        if (missing_client_tcp && missing_server_tcp) {
            server_ctx_t missing_ctx;
            memset(&missing_ctx, 0, sizeof(missing_ctx));
            missing_ctx.tcp = missing_server_tcp;
            missing_ctx.config = server_config;
#ifdef _WIN32
            HANDLE missing_thread = CreateThread(
                NULL, 0, tls_handshake_only_server_thread,
                &missing_ctx, 0, NULL);
            int missing_thread_started = missing_thread != NULL;
#else
            pthread_t missing_thread;
            int missing_thread_started = pthread_create(
                &missing_thread, NULL,
                tls_handshake_only_server_thread,
                &missing_ctx) == 0;
#endif
            check_int(
                "start_mtls_missing_certificate_server",
                missing_thread_started, 1);
            if (missing_thread_started) {
                const char *missing_error = NULL;
                neverc_tls_conn_t *missing_client =
                    neverc_tls_client(
                        missing_client_tcp,
                        no_certificate_config,
                        &missing_error);
#ifdef _WIN32
                WaitForSingleObject(missing_thread, INFINITE);
                CloseHandle(missing_thread);
#else
                pthread_join(missing_thread, NULL);
#endif
                check_int(
                    "mtls_reject_missing_client_certificate",
                    missing_ctx.handshake_ok, 0);
                neverc_tls_close(missing_client);
                neverc_tcp_close(missing_client_tcp);
            } else {
                neverc_tcp_close(missing_client_tcp);
                neverc_tcp_close(missing_server_tcp);
            }
        } else {
            neverc_tcp_close(missing_client_tcp);
            neverc_tcp_close(missing_server_tcp);
        }
        neverc_tls_config_free(no_certificate_config);
    }

    neverc_tcp_conn_t *client_tcp = NULL;
    neverc_tcp_conn_t *server_tcp = NULL;
    check_int("create_mtls_pipe",
              neverc_tcp_pipe(&client_tcp, &server_tcp), 0);
    if (!client_tcp || !server_tcp) {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
        neverc_tls_config_free(server_config);
        neverc_tls_config_free(client_config);
        return;
    }

    server_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.tcp = server_tcp;
    ctx.config = server_config;
#ifdef _WIN32
    HANDLE thread = CreateThread(
        NULL, 0, tls_server_thread, &ctx, 0, NULL);
    int thread_started = thread != NULL;
#else
    pthread_t thread;
    int thread_started =
        pthread_create(&thread, NULL, tls_server_thread, &ctx) == 0;
#endif
    check_int("start_mtls_server_thread", thread_started, 1);
    if (thread_started) {
        const char *err = NULL;
        neverc_tls_conn_t *client =
            neverc_tls_client(client_tcp, client_config, &err);
        check_not_null("mtls_client_handshake", client);
        if (client) {
            check_int("mtls_client_write",
                      neverc_tls_write(client, "ping", 4), 4);
            char response[8] = {0};
            int response_len =
                neverc_tls_read(client, response, sizeof(response));
            check_int("mtls_client_read",
                      response_len == 4 &&
                      memcmp(response, "pong", 4) == 0,
                      1);
            check_int("mtls_client_write_after_key_update",
                      neverc_tls_write(client, "ping2", 5), 5);
            memset(response, 0, sizeof(response));
            response_len =
                neverc_tls_read(client, response, sizeof(response));
            check_int("mtls_client_read_after_key_update",
                      response_len == 5 &&
                      memcmp(response, "pong2", 5) == 0,
                      1);
            check_int("mtls_client_receive_close_notify",
                      neverc_tls_read(client, response, sizeof(response)),
                      0);
            neverc_tls_close(client);
        }
        neverc_tcp_close(client_tcp);
#ifdef _WIN32
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
#else
        pthread_join(thread, NULL);
#endif
        check_int("mtls_server_handshake", ctx.handshake_ok, 1);
        check_int("mtls_server_peer_certificate",
                  ctx.peer_certificate_present, 1);
        check_int("mtls_server_read_payload",
                  strcmp(ctx.received, "ping") == 0, 1);
        check_int("mtls_server_read_after_key_update",
                  strcmp(ctx.second_received, "ping2") == 0, 1);
    } else {
        neverc_tcp_close(client_tcp);
        neverc_tcp_close(server_tcp);
    }

    neverc_tls_config_free(server_config);
    neverc_tls_config_free(client_config);
#endif
}

/* ===== Dial error test ===== */

static void test_dial_errors(void) {
    printf("[dial_errors]\n");

    const char *err = NULL;

    /* Dial to non-existent address should fail */
    neverc_tls_conn_t *conn = neverc_tls_dial("127.0.0.1:1", NULL, &err);
    check_null("dial bad port", conn);
    check_not_null("dial error msg", err);
    check_int("dial fails closed while verification is incomplete",
              err && strstr(err, "unavailable") != NULL, 1);

    neverc_tls_config_t *cfg = neverc_tls_config_new();
    check_not_null("listen fail-closed config", cfg);
    err = NULL;
    neverc_tls_listener_t *listener =
        neverc_tls_listen("127.0.0.1:0", cfg, &err);
    check_null("listen unavailable", listener);
    check_int("listen fail-closed error",
              err && strstr(err, "unavailable") != NULL, 1);

    err = NULL;
    check_null("server unavailable",
               neverc_tls_server(NULL, cfg, &err));
    check_int("server fail-closed error",
              err && strstr(err, "unavailable") != NULL, 1);

    err = NULL;
    check_null("client unavailable",
               neverc_tls_client(NULL, cfg, &err));
    check_int("client fail-closed error",
              err && strstr(err, "unavailable") != NULL, 1);

    err = NULL;
    check_null("accept unavailable",
               neverc_tls_accept(NULL, &err));
    check_int("accept fail-closed error",
              err && strstr(err, "unavailable") != NULL, 1);
    neverc_tls_config_free(cfg);
}

/* ===== Cipher suite test ===== */

static void test_cipher_suites(void) {
    printf("[cipher_suites]\n");

    check_int("aes_128_gcm_sha256", NEVERC_TLS_AES_128_GCM_SHA256, 0x1301);
    check_int("aes_256_gcm_sha384", NEVERC_TLS_AES_256_GCM_SHA384, 0x1302);
    check_int("chacha20_poly1305", NEVERC_TLS_CHACHA20_POLY1305_SHA256, 0x1303);
    check_int("group_x25519", NEVERC_TLS_GROUP_X25519, 0x001D);
    check_int("group_p256", NEVERC_TLS_GROUP_SECP256R1, 0x0017);
    check_int("tls_version_13", NEVERC_TLS_VERSION_13, 0x0304);
}

/* ===== Main ===== */

int main(void) {
    printf("=== NeverC TLS Tests ===\n");

    test_config();
    test_cipher_suites();
    test_certificate_chain_verification();
    test_certificate_verify_signing();
    test_certificate_verify();
#if !defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    test_dial_errors();
#endif
    test_client_server();
    test_session_resumption();
    test_alpn_no_overlap();
#if defined(NEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT)
    test_concurrent_record_io();
    test_mutual_tls();
    test_unexpected_server_record_alert();
    test_malformed_server_hello_alert();
    test_plaintext_record_version_ignored();
    test_hello_retry_request_alert();
    test_oversized_server_handshake_alert();
    test_malformed_client_hello_alert();
    test_oversized_client_handshake_alert();
#endif

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
