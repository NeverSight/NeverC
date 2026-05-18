//====- SHA256.cpp - SHA256 implementation ---*- C++ -* ======//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/*
 *  The SHA-256 Secure Hash Standard was published by NIST in 2002.
 *
 *  http://csrc.nist.gov/publications/fips/fips180-2/fips180-2.pdf
 *
 *   The implementation is based on nacl's sha256 implementation [0] and LLVM's
 *  pre-exsiting SHA1 code [1].
 *
 *   [0] https://hyperelliptic.org/nacl/nacl-20110221.tar.bz2 (public domain
 *       code)
 *   [1] llvm/lib/Support/SHA1.{h,cpp}
 */
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SHA256_H
#define LLVM_SUPPORT_SHA256_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <array>
#include <stdint.h>

extern "C" {
void csupport_sha256_init(void *ctx);
void csupport_sha256_update(void *ctx, const uint8_t *data, size_t len);
void csupport_sha256_update_string(void *ctx, const char *str, size_t len);
void csupport_sha256_final(void *ctx, uint8_t result[32]);
void csupport_sha256_result(void *ctx, uint8_t result[32]);
void csupport_sha256_hash(const uint8_t *data, size_t len, uint8_t result[32]);
}

namespace llvm {

class SHA256 {
public:
  explicit SHA256() { init(); }

  void init() { csupport_sha256_init(&InternalState); }

  void update(ArrayRef<uint8_t> Data) {
    csupport_sha256_update(&InternalState, Data.data(), Data.size());
  }

  void update(StringRef Str) {
    csupport_sha256_update_string(&InternalState, Str.data(), Str.size());
  }

  std::array<uint8_t, 32> final() {
    std::array<uint8_t, 32> r;
    csupport_sha256_final(&InternalState, r.data());
    return r;
  }

  std::array<uint8_t, 32> result() {
    std::array<uint8_t, 32> r;
    csupport_sha256_result(&InternalState, r.data());
    return r;
  }

  static std::array<uint8_t, 32> hash(ArrayRef<uint8_t> Data) {
    std::array<uint8_t, 32> r;
    csupport_sha256_hash(Data.data(), Data.size(), r.data());
    return r;
  }

private:
  /// Define some constants.
  /// "static constexpr" would be cleaner but MSVC does not support it yet.
  enum { BLOCK_LENGTH = 64 };
  enum { HASH_LENGTH = 32 };

  // Internal State
  struct {
    union {
      uint8_t C[BLOCK_LENGTH];
      uint32_t L[BLOCK_LENGTH / 4];
    } Buffer;
    uint32_t State[HASH_LENGTH / 4];
    uint32_t ByteCount;
    uint8_t BufferOffset;
  } InternalState;

  // Helper
  void writebyte(uint8_t data);
  void hashBlock();
  void addUncounted(uint8_t data);
  void pad();

  void final(std::array<uint32_t, HASH_LENGTH / 4> &HashResult);
};

} // namespace llvm

#endif // LLVM_SUPPORT_SHA256_H
