//==- llvm/Support/RandomNumberGenerator.h - RNG for diversity ---*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an abstraction for deterministic random number
// generation (RNG).  Note that the current implementation is not
// cryptographically secure as it uses the C++11 <random> facilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_
#define LLVM_SUPPORT_RANDOMNUMBERGENERATOR_H_

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ManagedStatic.h"
#include <fcntl.h>
#include <random>
#include <system_error>
#ifdef _WIN32
#include "llvm/Support/Windows/WindowsSupport.h"
#else
#include <unistd.h>
#endif

namespace llvm {
class StringRef;

/// A random number generator.
///
/// Instances of this class should not be shared across threads. The
/// seed should be set by passing the -rng-seed=<uint64> option. Use
/// Module::createRNG to create a new RNG instance for use with that
/// module.
class RandomNumberGenerator {

  // 64-bit Mersenne Twister by Matsumoto and Nishimura, 2000
  // http://en.cppreference.com/w/cpp/numeric/random/mersenne_twister_engine
  // This RNG is deterministically portable across C++11
  // implementations.
  using generator_type = std::mt19937_64;

public:
  using result_type = generator_type::result_type;

  /// Returns a random number in the range [0, Max).
  result_type operator()();

  static constexpr result_type min() { return generator_type::min(); }
  static constexpr result_type max() { return generator_type::max(); }

private:
  /// Seeds and salts the underlying RNG engine.
  ///
  /// This constructor should not be used directly. Instead use
  /// Module::createRNG to create a new RNG salted with the Module ID.
  RandomNumberGenerator(StringRef Salt);

  generator_type Generator;

  // Noncopyable.
  RandomNumberGenerator(const RandomNumberGenerator &other) = delete;
  RandomNumberGenerator &operator=(const RandomNumberGenerator &other) = delete;

  friend class Module;
};

// Get random vector of specified size
/// Fill Buffer with Size random bytes. Returns 0 on success, errno on failure.
int getRandomBytes(void *Buffer, size_t Size);

namespace detail {
struct CreateSeed {
  static void *call() {
    return new cl::opt<uint64_t>(
        "rng-seed", cl::value_desc("seed"), cl::Hidden,
        cl::desc("Seed for the random number generator"), cl::init(0));
  }
};
inline ManagedStatic<cl::opt<uint64_t>, CreateSeed> &getRNGSeed() {
  static ManagedStatic<cl::opt<uint64_t>, CreateSeed> Seed;
  return Seed;
}
} // namespace detail

inline void initRandomSeedOptions() { *detail::getRNGSeed(); }

inline RandomNumberGenerator::RandomNumberGenerator(StringRef Salt) {
  auto &Seed = detail::getRNGSeed();
  SmallVector<uint32_t, 32> Data;
  Data.resize(2 + Salt.size());
  Data[0] = *Seed;
  Data[1] = *Seed >> 32;
  memcpy(Data.begin() + 2, Salt.data(), Salt.size() * sizeof(Salt[0]));
  uint64_t CombinedSeed = 0;
  for (size_t i = 0; i < Data.size(); ++i)
    CombinedSeed = CombinedSeed * 6364136223846793005ULL + Data[i] + 1;
  Generator.seed((uint32_t)(CombinedSeed ^ (CombinedSeed >> 32)));
}
inline RandomNumberGenerator::result_type RandomNumberGenerator::operator()() {
  return Generator();
}
inline int getRandomBytes(void *Buffer, size_t Size) {
#ifdef _WIN32
  HCRYPTPROV hProvider;
  if (CryptAcquireContext(&hProvider, 0, 0, PROV_RSA_FULL,
                          CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
    ScopedCryptContext ScopedHandle(hProvider);
    if (CryptGenRandom(hProvider, Size, (BYTE *)(Buffer)))
      return 0;
  }
  return (int)GetLastError();
#else
  int Fd = open("/dev/urandom", O_RDONLY);
  if (Fd != -1) {
    int ret = 0;
    ssize_t BytesRead = read(Fd, Buffer, Size);
    if (BytesRead == -1)
      ret = errno;
    else if (BytesRead != (ssize_t)(Size))
      ret = EIO;
    if (close(Fd) == -1)
      ret = errno;
    return ret;
  }
  return errno;
#endif
}

} // namespace llvm

#endif
