//===- SupportHashTests.cpp - Known answers for the hashes LLVM uses ----===//
//
// This fork replaced LLVM's in-tree MD5, SHA-1 and SHA-256 with the C
// implementations in llvm/lib/CSupport, reached through the same
// llvm::MD5 / llvm::SHA1 / llvm::SHA256 classes the rest of the codebase
// calls.  Nothing checked that the replacements still answer correctly, and
// one of them did not: llvm::MD5 kept the streaming buffer on the C++ side
// and handed the C code a context whose byte counters it had never set, so
// every digest depended on whatever the stack held.  It was wrong on all six
// RFC 1321 vectors, and wrong differently between runs -- which is why the
// only visible symptom was that the source checksums in -g output changed on
// every build.
//
// The published vectors are the check that a hash is the hash it claims to
// be.  Nothing in the compiler's own output can substitute for them: a wrong
// digest is still 128 or 256 bits of plausible-looking hex, and every
// consumer here -- DWARF source checksums, DIE type signatures, ThinLTO
// global identifiers, anonymous global naming -- accepts it without complaint
// and only misbehaves later, somewhere else.
//
//===--------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/SHA256.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using namespace llvm;

namespace {

template <size_t N> std::string hex(const std::array<uint8_t, N> &Bytes) {
  std::string Out;
  char Pair[3];
  for (uint8_t Byte : Bytes) {
    std::snprintf(Pair, sizeof(Pair), "%02x", Byte);
    Out += Pair;
  }
  return Out;
}

ArrayRef<uint8_t> bytes(StringRef Text) {
  return ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(Text.data()),
                           Text.size());
}

// The three lengths that exercise the block boundary the streaming buffer is
// there to handle: shorter than a block, exactly a block, and longer.
constexpr StringLiteral SixtyFourBytes =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr StringLiteral SixtyFivebytes =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef!";

} // namespace

// RFC 1321, appendix A.5.
TEST(SupportHashTest, MD5MatchesThePublishedVectors) {
  EXPECT_EQ(hex(MD5::hash(bytes(""))), "d41d8cd98f00b204e9800998ecf8427e");
  EXPECT_EQ(hex(MD5::hash(bytes("a"))), "0cc175b9c0f1b6a831c399e269772661");
  EXPECT_EQ(hex(MD5::hash(bytes("abc"))), "900150983cd24fb0d6963f7d28e17f72");
  EXPECT_EQ(hex(MD5::hash(bytes("message digest"))),
            "f96b697d7cb7938d525a2f31aaf161d0");
  EXPECT_EQ(hex(MD5::hash(bytes("abcdefghijklmnopqrstuvwxyz"))),
            "c3fcd3d76192e4007dfb496cca67e13b");
  EXPECT_EQ(hex(MD5::hash(bytes("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnop"
                                "qrstuvwxyz0123456789"))),
            "d174ab98d277d9f5a5611c2c9f419d9f");
  EXPECT_EQ(hex(MD5::hash(bytes("123456789012345678901234567890123456789012"
                                "34567890123456789012345678901234567890"))),
            "57edf4a22be3c955ac49da2e2107b67a");
}

// FIPS 180-4.
TEST(SupportHashTest, SHA1MatchesThePublishedVectors) {
  EXPECT_EQ(hex(SHA1::hash(bytes(""))),
            "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  EXPECT_EQ(hex(SHA1::hash(bytes("abc"))),
            "a9993e364706816aba3e25717850c26c9cd0d89d");
  EXPECT_EQ(hex(SHA1::hash(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklm"
                                 "nlmnomnopnopq"))),
            "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST(SupportHashTest, SHA256MatchesThePublishedVectors) {
  EXPECT_EQ(hex(SHA256::hash(bytes(""))),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(hex(SHA256::hash(bytes("abc"))),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(hex(SHA256::hash(bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmk"
                                   "lmnlmnomnopnopq"))),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

// Feeding the same bytes in pieces must reach the same digest as feeding them
// at once.  This is what the one-shot vectors above cannot see: a wrapper that
// keeps its own buffer can carry the block boundary wrongly and still get the
// single-call case right.
TEST(SupportHashTest, StreamingAgreesWithOneShotAcrossTheBlockBoundary) {
  for (StringRef Text : {StringRef("abc"), StringRef(SixtyFourBytes),
                         StringRef(SixtyFivebytes)}) {
    SCOPED_TRACE(Text.size());
    const std::string Whole = hex(MD5::hash(bytes(Text)));
    for (size_t Split = 0; Split <= Text.size(); ++Split) {
      MD5 Hash;
      Hash.update(Text.take_front(Split));
      Hash.update(Text.drop_front(Split));
      MD5::MD5Result Piecewise;
      Hash.final(Piecewise);
      EXPECT_EQ(hex(Piecewise), Whole) << "split after " << Split << " bytes";
    }
  }
}

// A hash is a function of its input and nothing else.  A wrapper reading
// uninitialized state passes the vectors above only by luck and fails here:
// two calls from different places in one program disagree, which is how the
// same source file came out with two different checksums in one module.
TEST(SupportHashTest, TheSameInputHashesTheSameFromEveryCallSite) {
  const std::string Direct = hex(MD5::hash(bytes("neverc")));

  auto FromAnotherFrame = [] { return hex(MD5::hash(bytes("neverc"))); };
  EXPECT_EQ(FromAnotherFrame(), Direct);

  MD5 Streamed;
  Streamed.update(StringRef("neverc"));
  MD5::MD5Result Result;
  Streamed.final(Result);
  EXPECT_EQ(hex(Result), Direct);

  // result() reports the digest so far without ending the stream, so it must
  // agree with final() and leave the state able to continue.
  MD5 Resumable;
  Resumable.update(StringRef("nev"));
  Resumable.update(StringRef("erc"));
  EXPECT_EQ(hex(Resumable.result()), Direct);
  Resumable.update(StringRef("!"));
  EXPECT_EQ(hex(Resumable.result()), hex(MD5::hash(bytes("neverc!"))));
}
