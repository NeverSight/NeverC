/* -*- C++ -*-
 * This code is derived from (original license follows):
 *
 * This is an OpenSSL-compatible implementation of the RSA Data Security, Inc.
 * MD5 Message-Digest Algorithm (RFC 1321).
 *
 * Homepage:
 * http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5
 *
 * Author:
 * Alexander Peslyak, better known as Solar Designer <solar at openwall.com>
 *
 * This software was written by Alexander Peslyak in 2001.  No copyright is
 * claimed, and the software is hereby placed in the public domain.
 * In case this attempt to disclaim copyright and place the software in the
 * public domain is deemed null and void, then the software is
 * Copyright (c) 2001 Alexander Peslyak and it is hereby released to the
 * general public under the following terms:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted.
 *
 * There's ABSOLUTELY NO WARRANTY, express or implied.
 *
 * See md5.c for more information.
 */

#ifndef LLVM_SUPPORT_MD5_H
#define LLVM_SUPPORT_MD5_H

#include "csupport/md5.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Endian.h"
#include <array>
#include <assert.h>
#include <stdint.h>

namespace llvm {

namespace md5_inline {
inline char hexDigit(unsigned X, bool LowerCase) {
  assert(X < 16);
  static const char LUT[] = "0123456789ABCDEF";
  const uint8_t Offset = LowerCase ? 32 : 0;
  return static_cast<char>(LUT[X] | Offset);
}
inline void toHex(ArrayRef<uint8_t> Input, bool LowerCase,
                  SmallVectorImpl<char> &Output) {
  const size_t Length = Input.size();
  Output.resize_for_overwrite(Length * 2);
  for (size_t i = 0; i < Length; i++) {
    const uint8_t c = Input[i];
    Output[i * 2] = hexDigit(c >> 4, LowerCase);
    Output[i * 2 + 1] = hexDigit(c & 15, LowerCase);
  }
}
} // namespace md5_inline

class MD5 {
public:
  struct MD5Result : public std::array<uint8_t, 16> {
    SmallString<32> digest() const;

    uint64_t low() const {
      // Our MD5 implementation returns the result in little endian, so the low
      // word is first.
      using namespace support;
      return endian::read<uint64_t, ::llvm::endianness::little>(data());
    }

    uint64_t high() const {
      using namespace support;
      return endian::read<uint64_t, ::llvm::endianness::little>(data() + 8);
    }
    std::pair<uint64_t, uint64_t> words() const {
      using namespace support;
      return std::make_pair(high(), low());
    }
  };

  MD5();

  /// Updates the hash for the byte stream provided.
  void update(ArrayRef<uint8_t> Data);

  /// Updates the hash for the StringRef provided.
  void update(StringRef Str);

  /// Finishes off the hash and puts the result in result.
  void final(MD5Result &Result);

  /// Finishes off the hash, and returns the 16-byte hash data.
  MD5Result final();

  /// Finishes off the hash, and returns the 16-byte hash data.
  /// This is suitable for getting the MD5 at any time without invalidating the
  /// internal state, so that more calls can be made into `update`.
  MD5Result result();

  /// Translates the bytes in \p Res to a hex string that is
  /// deposited into \p Str. The result will be of length 32.
  static void stringifyResult(MD5Result &Result, SmallVectorImpl<char> &Str);

  /// Computes the hash for a given bytes.
  static MD5Result hash(ArrayRef<uint8_t> Data);

private:
  // Any 32-bit or wider unsigned integer data type will do.
  typedef uint32_t MD5_u32plus;

  // Internal State
  struct {
    MD5_u32plus a = 0x67452301;
    MD5_u32plus b = 0xefcdab89;
    MD5_u32plus c = 0x98badcfe;
    MD5_u32plus d = 0x10325476;
    MD5_u32plus hi = 0;
    MD5_u32plus lo = 0;
    uint8_t buffer[64];
    MD5_u32plus block[16];
  } InternalState;

  const uint8_t *body(ArrayRef<uint8_t> Data);
};

/// Helper to compute and return lower 64 bits of the given string's MD5 hash.
inline uint64_t MD5Hash(StringRef Str) {
  using namespace support;

  MD5 Hash;
  Hash.update(Str);
  MD5::MD5Result Result;
  Hash.final(Result);
  // Return the least significant word.
  return Result.low();
}

inline const uint8_t *MD5::body(ArrayRef<uint8_t> Data) {
  csupport_md5_ctx_t c;
  c.a = InternalState.a;
  c.b = InternalState.b;
  c.c = InternalState.c;
  c.d = InternalState.d;
  memcpy(c.block, InternalState.block, sizeof(c.block));
  csupport_md5_update(&c, Data.data(), Data.size());
  InternalState.a = c.a;
  InternalState.b = c.b;
  InternalState.c = c.c;
  InternalState.d = c.d;
  return Data.data() + Data.size();
}
inline MD5::MD5() {
  InternalState.a = 0x67452301;
  InternalState.b = 0xefcdab89;
  InternalState.c = 0x98badcfe;
  InternalState.d = 0x10325476;
  InternalState.lo = 0;
  InternalState.hi = 0;
}
inline void MD5::update(ArrayRef<uint8_t> Data) {
  MD5_u32plus saved_lo;
  unsigned long used, avail;
  const uint8_t *Ptr = Data.data();
  unsigned long Size = Data.size();
  saved_lo = InternalState.lo;
  if ((InternalState.lo = (saved_lo + Size) & 0x1fffffff) < saved_lo)
    InternalState.hi++;
  InternalState.hi += Size >> 29;
  used = saved_lo & 0x3f;
  if (used) {
    avail = 64 - used;
    if (Size < avail) {
      memcpy(&InternalState.buffer[used], Ptr, Size);
      return;
    }
    memcpy(&InternalState.buffer[used], Ptr, avail);
    Ptr += avail;
    Size -= avail;
    body(ArrayRef(InternalState.buffer, 64));
  }
  if (Size >= 64) {
    Ptr = body(ArrayRef(Ptr, Size & ~(unsigned long)0x3f));
    Size &= 0x3f;
  }
  memcpy(InternalState.buffer, Ptr, Size);
}
inline void MD5::update(StringRef S) {
  update(ArrayRef<uint8_t>((const uint8_t *)S.data(), S.size()));
}
inline void MD5::final(MD5Result &R) {
  unsigned long used, avail;
  used = InternalState.lo & 0x3f;
  InternalState.buffer[used++] = 0x80;
  avail = 64 - used;
  if (avail < 8) {
    memset(&InternalState.buffer[used], 0, avail);
    body(ArrayRef(InternalState.buffer, 64));
    used = 0;
    avail = 64;
  }
  memset(&InternalState.buffer[used], 0, avail - 8);
  InternalState.lo <<= 3;
  support::endian::write32le(&InternalState.buffer[56], InternalState.lo);
  support::endian::write32le(&InternalState.buffer[60], InternalState.hi);
  body(ArrayRef(InternalState.buffer, 64));
  support::endian::write32le(&R[0], InternalState.a);
  support::endian::write32le(&R[4], InternalState.b);
  support::endian::write32le(&R[8], InternalState.c);
  support::endian::write32le(&R[12], InternalState.d);
}
inline MD5::MD5Result MD5::final() {
  MD5Result R;
  final(R);
  return R;
}
inline MD5::MD5Result MD5::result() {
  auto S = InternalState;
  auto H = final();
  InternalState = S;
  return H;
}
inline SmallString<32> MD5::MD5Result::digest() const {
  SmallString<32> S;
  md5_inline::toHex(ArrayRef<uint8_t>(data(), size()), true, S);
  return S;
}
inline void MD5::stringifyResult(MD5Result &R, SmallVectorImpl<char> &S) {
  md5_inline::toHex(ArrayRef<uint8_t>(R.data(), R.size()), true, S);
}
inline MD5::MD5Result MD5::hash(ArrayRef<uint8_t> D) {
  MD5 H;
  H.update(D);
  MD5::MD5Result R;
  H.final(R);
  return R;
}

} // end namespace llvm

#endif // LLVM_SUPPORT_MD5_H
