/*===-- StrHashCompute.h - Compile-time string hash algorithms ---*- C++ -*-===*\
|*
|* Shared hash compute functions used by both Sema (builtin evaluation) and
|* StrHashFoldPass (IR constant folding).
|*
|* Adding a new algorithm:
|*   1. Add a computeXxx(StringRef) function below.
|*   2. Add a case to computeStrHash().
|*   3. Wire the algo ID in LangOpts / -fstrhash-algo.
|*
\*===----------------------------------------------------------------------===*/

#ifndef NEVERC_TRANSFORMS_STRHASH_STRHASHCOMPUTE_H
#define NEVERC_TRANSFORMS_STRHASH_STRHASHCOMPUTE_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace neverc {
namespace strhash {

inline uint64_t computeFNV32a(llvm::StringRef Bytes) {
  constexpr uint32_t Offset = 2166136261u;
  constexpr uint32_t Prime = 16777619u;
  uint32_t Hash = Offset;
  for (uint8_t C : Bytes) {
    Hash ^= static_cast<uint32_t>(C);
    Hash *= Prime;
  }
  return static_cast<uint64_t>(Hash);
}

inline uint64_t computeFNV64a(llvm::StringRef Bytes) {
  constexpr uint64_t Offset = 14695981039346656037ULL;
  constexpr uint64_t Prime = 1099511628211ULL;
  uint64_t Hash = Offset;
  for (uint8_t C : Bytes) {
    Hash ^= static_cast<uint64_t>(C);
    Hash *= Prime;
  }
  return Hash;
}

inline uint64_t computeXXHash64(llvm::StringRef Bytes) {
  constexpr uint64_t PRIME1 = 11400714785074694791ULL;
  constexpr uint64_t PRIME2 = 14029467366897019727ULL;
  constexpr uint64_t PRIME3 = 1609587929392839161ULL;
  constexpr uint64_t PRIME4 = 9650029242287828579ULL;
  constexpr uint64_t PRIME5 = 2870177450012600261ULL;

  const uint8_t *P = reinterpret_cast<const uint8_t *>(Bytes.data());
  unsigned Len = Bytes.size();
  const uint8_t *End = P + Len;

  auto read64 = [](const uint8_t *p) -> uint64_t {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
      v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
  };
  auto read32 = [](const uint8_t *p) -> uint32_t {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
      v |= static_cast<uint32_t>(p[i]) << (i * 8);
    return v;
  };
  auto rotl64 = [](uint64_t x, int r) -> uint64_t {
    return (x << r) | (x >> (64 - r));
  };
  auto round64 = [&](uint64_t acc, uint64_t input) -> uint64_t {
    acc += input * PRIME2;
    acc = rotl64(acc, 31);
    acc *= PRIME1;
    return acc;
  };
  auto mergeRound = [&](uint64_t acc, uint64_t val) -> uint64_t {
    val = round64(0, val);
    acc ^= val;
    acc = acc * PRIME1 + PRIME4;
    return acc;
  };

  uint64_t H64;
  if (Len >= 32) {
    uint64_t v1 = PRIME1 + PRIME2;
    uint64_t v2 = PRIME2;
    uint64_t v3 = 0;
    uint64_t v4 = 0ULL - PRIME1;
    const uint8_t *Limit = End - 32;
    do {
      v1 = round64(v1, read64(P)); P += 8;
      v2 = round64(v2, read64(P)); P += 8;
      v3 = round64(v3, read64(P)); P += 8;
      v4 = round64(v4, read64(P)); P += 8;
    } while (P <= Limit);
    H64 = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
    H64 = mergeRound(H64, v1);
    H64 = mergeRound(H64, v2);
    H64 = mergeRound(H64, v3);
    H64 = mergeRound(H64, v4);
  } else {
    H64 = PRIME5;
  }
  H64 += static_cast<uint64_t>(Len);

  while (P + 8 <= End) {
    uint64_t k1 = round64(0, read64(P));
    P += 8;
    H64 ^= k1;
    H64 = rotl64(H64, 27) * PRIME1 + PRIME4;
  }
  while (P + 4 <= End) {
    H64 ^= static_cast<uint64_t>(read32(P)) * PRIME1;
    P += 4;
    H64 = rotl64(H64, 23) * PRIME2 + PRIME3;
  }
  while (P < End) {
    H64 ^= static_cast<uint64_t>(*P) * PRIME5;
    P++;
    H64 = rotl64(H64, 11) * PRIME1;
  }

  H64 ^= H64 >> 33;
  H64 *= PRIME2;
  H64 ^= H64 >> 29;
  H64 *= PRIME3;
  H64 ^= H64 >> 32;
  return H64;
}

/// Dispatch to the correct algorithm.  Algo values: 1=fnv32a, 2=fnv64a, 3=xxhash64.
inline uint64_t computeStrHash(llvm::StringRef Bytes, unsigned Algo) {
  switch (Algo) {
  case 1:  return computeFNV32a(Bytes);
  case 3:  return computeXXHash64(Bytes);
  default: return computeFNV64a(Bytes);
  }
}

} // namespace strhash
} // namespace neverc

#endif // NEVERC_TRANSFORMS_STRHASH_STRHASHCOMPUTE_H
