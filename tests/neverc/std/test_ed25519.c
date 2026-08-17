#include "neverc/std/crypto/ed25519.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

#define INIT_THREAD_COUNT 8

typedef struct {
    int index;
    int ok;
} init_thread_context_t;

static int init_threads_start;

#ifdef _WIN32
static DWORD WINAPI init_thread_main(LPVOID argument) {
#else
static void *init_thread_main(void *argument) {
#endif
    init_thread_context_t *context = (init_thread_context_t *)argument;
    while (__atomic_load_n(&init_threads_start, __ATOMIC_ACQUIRE) == 0) {
    }

    unsigned char seed[32] = {0};
    unsigned char public_key[32], private_key[64], signature[64];
    const unsigned char message[] = "concurrent first use";
    seed[0] = (unsigned char)(context->index + 1);
    context->ok =
        neverc_ed25519_new_key_from_seed(
            seed, public_key, private_key) == 0 &&
        neverc_ed25519_sign(
            private_key, message, sizeof(message) - 1, signature) == 0 &&
        neverc_ed25519_verify(
            public_key, message, sizeof(message) - 1, signature) == 0;
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

static void test_concurrent_first_use(void) {
    printf("[concurrent_first_use]\n");
    init_thread_context_t contexts[INIT_THREAD_COUNT];
    int ok = 1;
#ifdef _WIN32
    HANDLE threads[INIT_THREAD_COUNT] = {0};
    for (int i = 0; i < INIT_THREAD_COUNT; ++i) {
        contexts[i].index = i;
        contexts[i].ok = 0;
        threads[i] = CreateThread(
            NULL, 0, init_thread_main, &contexts[i], 0, NULL);
        if (!threads[i])
            ok = 0;
    }
    __atomic_store_n(&init_threads_start, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < INIT_THREAD_COUNT; ++i) {
        if (threads[i]) {
            if (WaitForSingleObject(threads[i], INFINITE) != WAIT_OBJECT_0)
                ok = 0;
            CloseHandle(threads[i]);
        }
        if (!contexts[i].ok)
            ok = 0;
    }
#else
    pthread_t threads[INIT_THREAD_COUNT];
    int created = 0;
    for (int i = 0; i < INIT_THREAD_COUNT; ++i) {
        contexts[i].index = i;
        contexts[i].ok = 0;
        if (pthread_create(
                &threads[i], NULL, init_thread_main, &contexts[i]) != 0)
            break;
        ++created;
    }
    if (created != INIT_THREAD_COUNT)
        ok = 0;
    __atomic_store_n(&init_threads_start, 1, __ATOMIC_RELEASE);
    for (int i = 0; i < created; ++i) {
        if (pthread_join(threads[i], NULL) != 0 || !contexts[i].ok)
            ok = 0;
    }
#endif
    ASSERT_TRUE(ok);
}

static void test_keygen(void) {
    printf("[keygen]\n");
    unsigned char pub[32], priv[64];
    ASSERT_INT_EQ(neverc_ed25519_generate_key(pub, priv), 0);

    int pub_nonzero = 0, priv_nonzero = 0;
    for (int i = 0; i < 32; i++) if (pub[i]) pub_nonzero = 1;
    for (int i = 0; i < 64; i++) if (priv[i]) priv_nonzero = 1;
    ASSERT_TRUE(pub_nonzero);
    ASSERT_TRUE(priv_nonzero);

    ASSERT_TRUE(memcmp(priv + 32, pub, 32) == 0);
}

static void test_seed_roundtrip(void) {
    printf("[seed_roundtrip]\n");
    unsigned char seed[32] = {
        1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32
    };
    unsigned char pub[32], priv[64];
    neverc_ed25519_new_key_from_seed(seed, pub, priv);

    unsigned char seed2[32];
    neverc_ed25519_seed(priv, seed2);
    ASSERT_TRUE(memcmp(seed, seed2, 32) == 0);
}

static void test_deterministic_keygen(void) {
    printf("[deterministic_keygen]\n");
    unsigned char seed[32] = {0};
    seed[0] = 42;
    unsigned char pub1[32], priv1[64];
    unsigned char pub2[32], priv2[64];

    neverc_ed25519_new_key_from_seed(seed, pub1, priv1);
    neverc_ed25519_new_key_from_seed(seed, pub2, priv2);

    ASSERT_TRUE(memcmp(pub1, pub2, 32) == 0);
    ASSERT_TRUE(memcmp(priv1, priv2, 64) == 0);
}

static void test_sign_verify(void) {
    printf("[sign_verify]\n");
    unsigned char pub[32], priv[64];
    neverc_ed25519_generate_key(pub, priv);

    const char *msg = "Ed25519 test message";
    unsigned char sig[64];
    ASSERT_INT_EQ(neverc_ed25519_sign(priv, (const unsigned char *)msg, strlen(msg), sig), 0);
    ASSERT_INT_EQ(neverc_ed25519_verify(pub, (const unsigned char *)msg, strlen(msg), sig), 0);
}

static void test_wrong_message(void) {
    printf("[wrong_message]\n");
    unsigned char pub[32], priv[64];
    neverc_ed25519_generate_key(pub, priv);

    unsigned char sig[64];
    neverc_ed25519_sign(priv, (const unsigned char *)"msg1", 4, sig);
    ASSERT_TRUE(neverc_ed25519_verify(pub, (const unsigned char *)"msg2", 4, sig) != 0);
}

static void test_wrong_key(void) {
    printf("[wrong_key]\n");
    unsigned char pub1[32], priv1[64];
    unsigned char pub2[32], priv2[64];
    neverc_ed25519_generate_key(pub1, priv1);
    neverc_ed25519_generate_key(pub2, priv2);

    unsigned char sig[64];
    neverc_ed25519_sign(priv1, (const unsigned char *)"test", 4, sig);
    ASSERT_TRUE(neverc_ed25519_verify(pub2, (const unsigned char *)"test", 4, sig) != 0);
}

static void test_empty_message(void) {
    printf("[empty_message]\n");
    unsigned char pub[32], priv[64];
    neverc_ed25519_generate_key(pub, priv);

    unsigned char sig[64];
    ASSERT_INT_EQ(neverc_ed25519_sign(priv, NULL, 0, sig), 0);
    ASSERT_INT_EQ(neverc_ed25519_verify(pub, NULL, 0, sig), 0);
}

static void test_deterministic_signature(void) {
    printf("[deterministic_signature]\n");
    unsigned char seed[32] = {0};
    seed[0] = 99;
    unsigned char pub[32], priv[64];
    neverc_ed25519_new_key_from_seed(seed, pub, priv);

    const char *msg = "deterministic";
    unsigned char sig1[64], sig2[64];
    neverc_ed25519_sign(priv, (const unsigned char *)msg, strlen(msg), sig1);
    neverc_ed25519_sign(priv, (const unsigned char *)msg, strlen(msg), sig2);
    ASSERT_TRUE(memcmp(sig1, sig2, 64) == 0);
}

static void test_reject_noncanonical_scalar(void) {
    printf("[reject_noncanonical_scalar]\n");
    static const unsigned char order[32] = {
        0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    };
    unsigned char seed[32] = {7};
    unsigned char pub[32], priv[64], sig[64];
    const unsigned char msg[] = "malleability";
    neverc_ed25519_new_key_from_seed(seed, pub, priv);
    neverc_ed25519_sign(priv, msg, sizeof(msg) - 1, sig);
    ASSERT_INT_EQ(
        neverc_ed25519_verify(pub, msg, sizeof(msg) - 1, sig), 0);

    unsigned carry = 0;
    for (size_t i = 0; i < sizeof(order); ++i) {
        unsigned sum = (unsigned)sig[32 + i] + order[i] + carry;
        sig[32 + i] = (unsigned char)sum;
        carry = sum >> 8;
    }
    ASSERT_TRUE(
        neverc_ed25519_verify(pub, msg, sizeof(msg) - 1, sig) != 0);
}

static void test_reject_small_order_forgery(void) {
    printf("[reject_small_order_forgery]\n");
    unsigned char identity_public_key[32] = {1};
    unsigned char forged_signature[64] = {1};
    const unsigned char msg[] = "forged";
    ASSERT_TRUE(
        neverc_ed25519_verify(identity_public_key, msg,
                              sizeof(msg) - 1, forged_signature) != 0);
}

static void test_rfc8032_empty_message(void) {
    printf("[rfc8032_empty_message]\n");
    static const unsigned char seed[32] = {
        0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60,
        0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4,
        0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19,
        0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60,
    };
    static const unsigned char expected_pub[32] = {
        0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7,
        0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
        0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25,
        0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a,
    };
    static const unsigned char expected_sig[64] = {
        0xe5, 0x56, 0x43, 0x00, 0xc3, 0x60, 0xac, 0x72,
        0x90, 0x86, 0xe2, 0xcc, 0x80, 0x6e, 0x82, 0x8a,
        0x84, 0x87, 0x7f, 0x1e, 0xb8, 0xe5, 0xd9, 0x74,
        0xd8, 0x73, 0xe0, 0x65, 0x22, 0xbd, 0x21, 0x79,
        0xf7, 0x87, 0xad, 0x90, 0x48, 0x48, 0x91, 0x11,
        0x03, 0x1f, 0xb6, 0xfd, 0x90, 0x5b, 0x98, 0x05,
        0x6b, 0x9c, 0x91, 0x55, 0xae, 0xf9, 0x27, 0x22,
        0x4c, 0xfc, 0xb2, 0xa2, 0x02, 0xe3, 0x16, 0x0d,
    };
    unsigned char pub[32], priv[64], sig[64];
    ASSERT_INT_EQ(neverc_ed25519_new_key_from_seed(seed, pub, priv), 0);
    ASSERT_TRUE(memcmp(pub, expected_pub, 32) == 0);
    ASSERT_INT_EQ(neverc_ed25519_verify(pub, NULL, 0, expected_sig), 0);
    ASSERT_INT_EQ(neverc_ed25519_sign(priv, NULL, 0, sig), 0);
    ASSERT_TRUE(memcmp(sig, expected_sig, 64) == 0);
    ASSERT_INT_EQ(neverc_ed25519_verify(pub, NULL, 0, sig), 0);
}

static void test_rfc8032_ctx(void) {
    printf("[rfc8032_ed25519ctx]\n");
    static const unsigned char seed[32] = {
        0x03, 0x05, 0x33, 0x4e, 0x38, 0x1a, 0xf7, 0x8f,
        0x14, 0x1c, 0xb6, 0x66, 0xf6, 0x19, 0x9f, 0x57,
        0xbc, 0x34, 0x95, 0x33, 0x5a, 0x25, 0x6a, 0x95,
        0xbd, 0x2a, 0x55, 0xbf, 0x54, 0x66, 0x63, 0xf6,
    };
    static const unsigned char expected_pub[32] = {
        0xdf, 0xc9, 0x42, 0x5e, 0x4f, 0x96, 0x8f, 0x7f,
        0x0c, 0x29, 0xf0, 0x25, 0x9c, 0xf5, 0xf9, 0xae,
        0xd6, 0x85, 0x1c, 0x2b, 0xb4, 0xad, 0x8b, 0xfb,
        0x86, 0x0c, 0xfe, 0xe0, 0xab, 0x24, 0x82, 0x92,
    };
    static const unsigned char msg[16] = {
        0xf7, 0x26, 0x93, 0x6d, 0x19, 0xc8, 0x00, 0x49,
        0x4e, 0x3f, 0xda, 0xff, 0x20, 0xb2, 0x76, 0xa8,
    };
    static const unsigned char ctx_foo[] = {0x66, 0x6f, 0x6f};
    static const unsigned char ctx_bar[] = {0x62, 0x61, 0x72};
    static const unsigned char expected_foo[64] = {
        0x55, 0xa4, 0xcc, 0x2f, 0x70, 0xa5, 0x4e, 0x04,
        0x28, 0x8c, 0x5f, 0x4c, 0xd1, 0xe4, 0x5a, 0x7b,
        0xb5, 0x20, 0xb3, 0x62, 0x92, 0x91, 0x18, 0x76,
        0xca, 0xda, 0x73, 0x23, 0x19, 0x8d, 0xd8, 0x7a,
        0x8b, 0x36, 0x95, 0x0b, 0x95, 0x13, 0x00, 0x22,
        0x90, 0x7a, 0x7f, 0xb7, 0xc4, 0xe9, 0xb2, 0xd5,
        0xf6, 0xcc, 0xa6, 0x85, 0xa5, 0x87, 0xb4, 0xb2,
        0x1f, 0x4b, 0x88, 0x8e, 0x4e, 0x7e, 0xdb, 0x0d,
    };
    unsigned char pub[32], priv[64], sig[64], empty_sig[64], pure_sig[64];
    unsigned char empty = 0, too_long[256];
    ASSERT_INT_EQ(neverc_ed25519_new_key_from_seed(seed, pub, priv), 0);
    ASSERT_TRUE(memcmp(pub, expected_pub, 32) == 0);
    ASSERT_INT_EQ(
        neverc_ed25519_verify_ctx(
            pub, msg, sizeof(msg), ctx_foo, 3, expected_foo),
        0);
    ASSERT_INT_EQ(
        neverc_ed25519_sign_ctx(priv, msg, sizeof(msg), ctx_foo, 3, sig), 0);
    ASSERT_TRUE(memcmp(sig, expected_foo, 64) == 0);
    ASSERT_INT_EQ(
        neverc_ed25519_verify_ctx(pub, msg, sizeof(msg), ctx_foo, 3, sig), 0);
    ASSERT_TRUE(
        neverc_ed25519_verify_ctx(pub, msg, sizeof(msg), ctx_bar, 3, sig) != 0);
    ASSERT_TRUE(neverc_ed25519_verify(pub, msg, sizeof(msg), sig) != 0);
    ASSERT_TRUE(
        neverc_ed25519_verify_ctx(pub, msg, sizeof(msg), NULL, 0, sig) != 0);

    ASSERT_INT_EQ(
        neverc_ed25519_sign_ctx(priv, msg, sizeof(msg), NULL, 0, empty_sig), 0);
    ASSERT_INT_EQ(
        neverc_ed25519_verify_ctx(pub, msg, sizeof(msg), NULL, 0, empty_sig), 0);
    ASSERT_INT_EQ(
        neverc_ed25519_verify_ctx(
            pub, msg, sizeof(msg), &empty, 0, empty_sig), 0);
    ASSERT_TRUE(neverc_ed25519_verify(pub, msg, sizeof(msg), empty_sig) != 0);
    ASSERT_TRUE(
        neverc_ed25519_verify_ctx(
            pub, msg, sizeof(msg), ctx_foo, 3, empty_sig) != 0);
    ASSERT_INT_EQ(neverc_ed25519_sign(priv, msg, sizeof(msg), pure_sig), 0);
    ASSERT_TRUE(memcmp(empty_sig, pure_sig, 64) != 0);
    ASSERT_TRUE(memcmp(empty_sig, sig, 64) != 0);

    ASSERT_TRUE(
        neverc_ed25519_sign_ctx(priv, msg, sizeof(msg), NULL, 3, sig) != 0);
    memset(too_long, 'x', sizeof(too_long));
    ASSERT_TRUE(
        neverc_ed25519_sign_ctx(
            priv, msg, sizeof(msg), too_long, 256, sig) != 0);
    ASSERT_TRUE(
        neverc_ed25519_verify_ctx(
            pub, msg, sizeof(msg), too_long, 256, sig) != 0);
}

static void test_reject_invalid_points(void) {
    printf("[reject_invalid_points]\n");
    const unsigned char msg[] = "point";
    unsigned char sig[64] = {1};
    unsigned char y_eq_p[32] = {
        0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
    };
    ASSERT_TRUE(neverc_ed25519_verify(y_eq_p, msg, sizeof(msg) - 1, sig) != 0);

    unsigned char order2[32];
    memcpy(order2, y_eq_p, 32);
    order2[0] = 0xec; /* y = p-1, the order-2 point (0, -1) */
    ASSERT_TRUE(neverc_ed25519_verify(order2, msg, sizeof(msg) - 1, sig) != 0);

    unsigned char y_zero[32] = {0};
    ASSERT_TRUE(neverc_ed25519_verify(y_zero, msg, sizeof(msg) - 1, sig) != 0);

    unsigned char seed[32] = {3};
    unsigned char pub[32], priv[64], good_sig[64];
    neverc_ed25519_new_key_from_seed(seed, pub, priv);
    neverc_ed25519_sign(priv, msg, sizeof(msg) - 1, good_sig);
    memset(good_sig, 0xff, 32); /* invalid R encoding, y >= p */
    ASSERT_TRUE(neverc_ed25519_verify(
                    pub, msg, sizeof(msg) - 1, good_sig) != 0);
}

int main(void) {
    printf("=== NeverC crypto/ed25519 Tests ===\n");
    test_concurrent_first_use();
    test_keygen();
    test_seed_roundtrip();
    test_deterministic_keygen();
    test_sign_verify();
    test_wrong_message();
    test_wrong_key();
    test_empty_message();
    test_deterministic_signature();
    test_reject_noncanonical_scalar();
    test_reject_small_order_forgery();
    test_rfc8032_empty_message();
    test_rfc8032_ctx();
    test_reject_invalid_points();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
