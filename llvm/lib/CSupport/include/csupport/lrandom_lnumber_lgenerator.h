#ifndef CSUPPORT_LRANDOM_LNUMBER_LGENERATOR_H
#define CSUPPORT_LRANDOM_LNUMBER_LGENERATOR_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int csupport_get_random_bytes(void *buffer, size_t size);

/* Deterministic hash from seed + salt string. */
uint64_t csupport_salted_hash(uint64_t seed, const char *salt, size_t salt_len);

/* Combine multiple uint32 seeds into a single hash. */
uint32_t csupport_hash_combine_seeds(const uint32_t *seeds, size_t count);

/* SplitMix64: fast non-cryptographic PRNG. Updates *state and returns value. */
uint64_t csupport_splitmix64(uint64_t *state);

#ifdef __cplusplus
}
#endif
#endif
