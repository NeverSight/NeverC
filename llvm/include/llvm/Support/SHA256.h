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

#include "csupport/ls_lh_la256.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <array>
#include <stdint.h>

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
  // The C implementation owns the whole algorithm, so this holds its context
  // and nothing else.  Restating the layout here instead would put the size,
  // field order and alignment of a struct the C code writes through under the
  // care of a comment, and nothing would report the day the two drifted apart.
  csupport_sha256_ctx_t InternalState;
};

} // namespace llvm

#endif // LLVM_SUPPORT_SHA256_H
