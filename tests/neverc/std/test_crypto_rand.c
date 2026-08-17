#include "neverc/std/crypto/rand.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}
static void check_bool(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void test_read(void) {
    printf("[read]\n");
    check_int("zero-length NULL read",
              neverc_crypto_rand_read(NULL, 0), 0);
    check_int("reject NULL read buffer",
              neverc_crypto_rand_read(NULL, 1), -1);

    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));
    int err = neverc_crypto_rand_read(buf, sizeof(buf));
    check_int("read ok", err, 0);

    int all_zero = 1;
    for (int i = 0; i < 32; i++) if (buf[i]) { all_zero = 0; break; }
    check_bool("not all zero", all_zero, 0);

    uint8_t buf2[32];
    neverc_crypto_rand_read(buf2, sizeof(buf2));
    check_bool("different", memcmp(buf, buf2, 32) != 0, 1);
}

static void test_read_large(void) {
    printf("[read large]\n");
    uint8_t buf[1024];
    int err = neverc_crypto_rand_read(buf, sizeof(buf));
    check_int("large read ok", err, 0);
}

static void test_rand_int(void) {
    printf("[rand_int]\n");
    check_int("reject NULL rand_int output",
              neverc_crypto_rand_int(NULL, 100), -1);
    uint64_t val = 0x5a5a5a5a5a5a5a5aULL;
    check_int("reject max=0", neverc_crypto_rand_int(&val, 0), -1);
    check_bool("max=0 wipes output", val == 0, 1);
    int err = neverc_crypto_rand_int(&val, 100);
    check_int("rand_int ok", err, 0);
    check_bool("in range", val < 100, 1);

    int all_same = 1;
    uint64_t first;
    neverc_crypto_rand_int(&first, 1000);
    for (int i = 0; i < 10; i++) {
        neverc_crypto_rand_int(&val, 1000);
        if (val != first) { all_same = 0; break; }
    }
    check_bool("varied", all_same, 0);
}

static void test_prime(void) {
    printf("[prime]\n");
    uint8_t buf[8];

    check_int("reject NULL prime output",
              neverc_crypto_rand_prime(NULL, 16), -1);
    int err = neverc_crypto_rand_prime(buf, 16);
    check_int("prime 16-bit ok", err, 0);
    uint16_t p16 = buf[0] | (buf[1] << 8);
    check_bool("prime >= 2", p16 >= 2, 1);
    check_bool("prime odd", p16 & 1, 1);
    check_bool("prime high bit", (p16 >> 15) & 1, 1);

    /* Verify it's actually prime (trial division for small primes) */
    int is_prime = 1;
    for (int d = 2; d * d <= p16; d++) {
        if (p16 % d == 0) { is_prime = 0; break; }
    }
    check_bool("is prime", is_prime, 1);

    err = neverc_crypto_rand_prime(buf, 32);
    check_int("prime 32-bit ok", err, 0);

    err = neverc_crypto_rand_prime(buf, 8);
    check_int("prime 8-bit ok", err, 0);
    check_bool("8-bit odd", buf[0] & 1, 1);
}

int main(void) {
    printf("=== NeverC Crypto/Rand Module Tests ===\n\n");
    test_read();
    test_read_large();
    test_rand_int();
    test_prime();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
