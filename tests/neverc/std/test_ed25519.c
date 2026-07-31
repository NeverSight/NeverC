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
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
