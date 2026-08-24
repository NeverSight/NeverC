/* Canonical wyhash final-v3 known-answer and length-boundary tests. */
#include "../../../std/src/hash/_wyhash_final3.h"
#include <stdio.h>
#include <stdint.h>

typedef struct {
    size_t len;
    uint64_t seed0;
    uint64_t seed1;
} wyhash_kat_t;

/* Expected values were generated once from the pinned upstream reference,
 * rather than from the implementation under test. */
static const wyhash_kat_t kats[] = {
    {   0, 0x42bc986dc5eec4d3ULL, 0x520686c8a0b997d2ULL },
    {   1, 0xb3d6d3b9284dd5bbULL, 0x28d03d71aaee9a1cULL },
    {   2, 0x95b7533ae68f3b39ULL, 0x22f1f4c28966a93dULL },
    {   3, 0xcc725323742751c3ULL, 0xd6488d744ea84d19ULL },
    {   4, 0xd6fca0569f809c16ULL, 0x0532a3b524b91e93ULL },
    {   7, 0x85177957e607b1b3ULL, 0x7fa4f3316173b734ULL },
    {   8, 0xaeb4dcf43fe71bceULL, 0x7946dc1585254812ULL },
    {   9, 0x14b2f94736f47044ULL, 0x301f6af423a2669aULL },
    {  15, 0x4346287ac50de820ULL, 0xbf698af6ec20ad38ULL },
    {  16, 0xf94cf44c2e56939eULL, 0xfd4e9e37bf3bb262ULL },
    {  17, 0x16ee002df6ea678dULL, 0x6824599169e6339dULL },
    {  47, 0xbec84672f97bc678ULL, 0x69aa1a12283fe88fULL },
    {  48, 0xbb2b75029b8184ccULL, 0x7d53defb8dde9e13ULL },
    {  49, 0xacba806b25fd12a6ULL, 0x7dda2968cffab1abULL },
    {  95, 0x4ab192e86442a5c9ULL, 0xef6256b13a883c3dULL },
    {  96, 0xa57b42796f71b2a5ULL, 0x8615fffd0d23f11cULL },
    {  97, 0x739d0a2721a847e5ULL, 0x56275dc8f192e4adULL },
    { 127, 0xc985bb39b24474f9ULL, 0xbb54778f85188503ULL },
    { 128, 0x627a9115da544cf1ULL, 0xac3086a9a98a2ef1ULL },
    { 129, 0xba6c644b3548a1d1ULL, 0x20c63c257965e3dbULL },
    { 143, 0xcafb6a068229dea0ULL, 0xb8250baa67be5107ULL },
    { 144, 0x5b579a1f91bdb60eULL, 0x73ecc3a0958ba4f4ULL },
    { 145, 0xc1c51af65f7b774bULL, 0x812382b2253c2f29ULL },
};

static int check_hash(const uint8_t *data, size_t len, uint64_t seed,
                      uint64_t expected) {
    uint64_t got = nci_wyhash_final3(data, len, seed);
    if (got == expected)
        return 0;
    printf("  FAIL: len=%llu seed=0x%016llx got=0x%016llx expected=0x%016llx\n",
           (unsigned long long)len, (unsigned long long)seed,
           (unsigned long long)got, (unsigned long long)expected);
    return 1;
}

int main(void) {
    uint8_t data[256];
    int failures = 0;
    for (size_t i = 0; i < sizeof(data); ++i)
        data[i] = (uint8_t)(i * 131U + 17U);

    /* Independent cross-check: eldruin/wyhash-rs,
     * tests/integration.rs::final3_default_constructed. */
    {
        const uint8_t zero = 0;
        failures += check_hash(&zero, 1, 0, 0x22a2d5db3856770fULL);
    }

    for (size_t i = 0; i < sizeof(kats) / sizeof(kats[0]); ++i) {
        failures += check_hash(data, kats[i].len, 0, kats[i].seed0);
        failures += check_hash(data, kats[i].len,
                               0x0123456789abcdefULL, kats[i].seed1);
    }

    /* Empty input is explicitly allowed to use a NULL pointer. */
    failures += check_hash(NULL, 0, 0, 0x42bc986dc5eec4d3ULL);

    if (failures != 0) {
        printf("wyhash_final3: %d failed\n", failures);
        return 1;
    }
    printf("wyhash_final3: passed\n");
    return 0;
}
