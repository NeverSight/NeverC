//==- SHA1.h - SHA1 implementation for LLVM                     --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This code is taken from public domain
// (http://oauth.googlecode.com/svn/code/c/liboauth/src/sha1.c)
// and modified by wrapping it in a C++ interface for LLVM,
// and removing unnecessary code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SHA1_H
#define LLVM_SUPPORT_SHA1_H

#include "csupport/ls_lh_la1.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <array>
#include <stdint.h>

namespace llvm {

/// A class that wrap the SHA1 algorithm.
class SHA1 {
public:
  SHA1() { init(); }

  void init() { csupport_sha1_init(&InternalState); }

  void update(ArrayRef<uint8_t> Data) {
    csupport_sha1_update(&InternalState, Data.data(), Data.size());
  }

  void update(StringRef Str) {
    csupport_sha1_update_string(&InternalState, Str.data(), Str.size());
  }

  std::array<uint8_t, CSUPPORT_SHA1_HASH_LENGTH> final() {
    std::array<uint8_t, CSUPPORT_SHA1_HASH_LENGTH> r;
    csupport_sha1_final(&InternalState, r.data());
    return r;
  }

  std::array<uint8_t, CSUPPORT_SHA1_HASH_LENGTH> result() {
    std::array<uint8_t, CSUPPORT_SHA1_HASH_LENGTH> r;
    csupport_sha1_result(&InternalState, r.data());
    return r;
  }

  static std::array<uint8_t, CSUPPORT_SHA1_HASH_LENGTH>
  hash(ArrayRef<uint8_t> Data) {
    std::array<uint8_t, CSUPPORT_SHA1_HASH_LENGTH> r;
    csupport_sha1_hash(Data.data(), Data.size(), r.data());
    return r;
  }

private:
  // The C implementation owns the whole algorithm, so this holds its context
  // and nothing else.  Restating the layout here instead would put the size,
  // field order and alignment of a struct the C code writes through under the
  // care of a comment, and nothing would report the day the two drifted apart.
  csupport_sha1_ctx_t InternalState;
};

} // namespace llvm

#endif
