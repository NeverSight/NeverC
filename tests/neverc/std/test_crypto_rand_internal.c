#include "neverc/std/crypto/rand.h"
#include <stdint.h>
#include <stdio.h>

/* Exercise the platform-specific modular multiplication and deterministic
 * primality helpers directly. This test is linked without rand.c because the
 * implementation is included here. */
#include "../../../std/src/crypto/rand/rand.c"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    CHECK(mulmod64(2, 3, 3) == 0);
    CHECK(mulmod64(UINT64_MAX - 1, UINT64_MAX - 1, UINT64_MAX) == 1);

    CHECK(is_probably_prime(2));
    CHECK(is_probably_prime(UINT64_C(18446744073709551557)));
    CHECK(!is_probably_prime(UINT64_C(341550071728321)));
    CHECK(!is_probably_prime(UINT64_MAX));

    puts("passed");
    return 0;
}
