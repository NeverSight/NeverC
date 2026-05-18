/*===- RandomNumberGenerator.c - RNG (pure C) -------------------*- C -*-===*/
#include "include/csupport/lrandom_lnumber_lgenerator.h"
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
int csupport_get_random_bytes(void *buffer, size_t size) {
  NTSTATUS status = BCryptGenRandom(NULL, (PUCHAR)buffer, (ULONG)size,
                                     BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  return (status >= 0) ? 0 : -1;
}
#elif defined(__APPLE__)
#include <stdlib.h>
int csupport_get_random_bytes(void *buffer, size_t size) {
  arc4random_buf(buffer, size);
  return 0;
}
#else
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
int csupport_get_random_bytes(void *buffer, size_t size) {
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) return -1;
  ssize_t n = read(fd, buffer, size);
  close(fd);
  return (n == (ssize_t)size) ? 0 : -1;
}
#endif

uint64_t csupport_salted_hash(uint64_t seed, const char *salt, size_t salt_len) {
  uint64_t hash = seed;
  for (size_t i = 0; i < salt_len; i++) {
    hash ^= (uint64_t)(unsigned char)salt[i];
    hash *= 0x5851F42D4C957F2DULL;
    hash ^= hash >> 27;
  }
  return hash;
}

uint32_t csupport_hash_combine_seeds(const uint32_t *seeds, size_t count) {
  uint32_t hash = 0;
  for (size_t i = 0; i < count; i++) {
    hash ^= seeds[i];
    hash *= 2654435761U;
    hash ^= hash >> 16;
  }
  return hash;
}

uint64_t csupport_splitmix64(uint64_t *state) {
  uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}
