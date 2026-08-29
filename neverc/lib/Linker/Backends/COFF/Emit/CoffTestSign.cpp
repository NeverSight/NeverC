//===--- CoffTestSign.cpp - Authenticode test signing --------------------===//
//
// Implements `--test-sign`: append an Authenticode signature to a linked PE
// image.  See TestSign.h for what the signature is and is not worth.
//
// The pieces, in the order the format needs them:
//
//   1. Authenticode digest -- SHA-256 over the image with the three fields
//      that the signature itself perturbs excluded (PE checksum, certificate
//      table directory entry, and the attribute certificate area).
//   2. SpcIndirectDataContent -- the structure Authenticode actually signs,
//      binding that digest to a "this is a PE image" type tag.
//   3. PKCS#7 SignedData -- the digest of (2) signed with RSA PKCS#1 v1.5,
//      carrying the certificate so a verifier can chain it.
//   4. WIN_CERTIFICATE -- an 8-byte-aligned wrapper appended to the file,
//      with the certificate table directory entry pointed at it.
//
//===----------------------------------------------------------------------===//

#include "Linker/COFF/TestSign.h"
#include "PEChecksum.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

using namespace llvm;

namespace linker {
namespace coff {

namespace {

using Bytes = std::vector<uint8_t>;

// ===----------------------------------------------------------------------===
// Minimal DER encoder
// ===----------------------------------------------------------------------===
//
// Only what the structures below need.  Everything is built bottom-up: a
// value's contents are encoded first, then wrapped, so lengths are always
// known by the time they are written.

enum : uint8_t {
  TagInteger = 0x02,
  TagBitString = 0x03,
  TagOctetString = 0x04,
  TagNull = 0x05,
  TagOid = 0x06,
  TagSequence = 0x30,
  TagSet = 0x31,
  // [0] constructed, used for SignedData's explicit content and for the
  // certificates slot.
  TagContext0 = 0xA0,
};

void appendLength(Bytes &out, size_t len) {
  if (len < 0x80) {
    out.push_back(static_cast<uint8_t>(len));
    return;
  }
  // Long form: one length-of-length byte, then big-endian minimal bytes.
  uint8_t buf[sizeof(size_t)];
  unsigned n = 0;
  for (size_t v = len; v; v >>= 8)
    buf[n++] = static_cast<uint8_t>(v & 0xff);
  out.push_back(static_cast<uint8_t>(0x80 | n));
  while (n)
    out.push_back(buf[--n]);
}

Bytes tlv(uint8_t tag, ArrayRef<uint8_t> content) {
  Bytes out;
  out.push_back(tag);
  appendLength(out, content.size());
  out.insert(out.end(), content.begin(), content.end());
  return out;
}

void append(Bytes &dst, ArrayRef<uint8_t> src) {
  dst.insert(dst.end(), src.begin(), src.end());
}

/// DER INTEGER from a big-endian magnitude, dropping redundant leading zeros
/// and re-adding one when the top bit would otherwise read as negative.
Bytes derInteger(ArrayRef<uint8_t> magnitude) {
  size_t first = 0;
  while (first + 1 < magnitude.size() && magnitude[first] == 0)
    ++first;
  Bytes v(magnitude.begin() + first, magnitude.end());
  if (!v.empty() && (v[0] & 0x80))
    v.insert(v.begin(), 0x00);
  if (v.empty())
    v.push_back(0x00);
  return tlv(TagInteger, v);
}

Bytes derInteger(unsigned value) {
  uint8_t be[4] = {static_cast<uint8_t>(value >> 24),
                   static_cast<uint8_t>(value >> 16),
                   static_cast<uint8_t>(value >> 8),
                   static_cast<uint8_t>(value)};
  return derInteger(ArrayRef<uint8_t>(be, 4));
}

/// OIDs are pre-encoded: the set is fixed and small, so encoding them from
/// dotted form at runtime would be machinery with exactly one shape of input.
namespace oid {
// 1.2.840.113549.1.7.2 -- signedData
const uint8_t SignedData[] = {0x2a, 0x86, 0x48, 0x86, 0xf7,
                              0x0d, 0x01, 0x07, 0x02};
// 1.2.840.113549.1.7.1 -- data
const uint8_t Data[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x01};
// 1.3.6.1.4.1.311.2.1.4 -- SPC_INDIRECT_DATA_OBJID
const uint8_t SpcIndirectData[] = {0x2b, 0x06, 0x01, 0x04, 0x01,
                                   0x82, 0x37, 0x02, 0x01, 0x04};
// 1.3.6.1.4.1.311.2.1.15 -- SPC_PE_IMAGE_DATA_OBJID
const uint8_t SpcPeImageData[] = {0x2b, 0x06, 0x01, 0x04, 0x01,
                                  0x82, 0x37, 0x02, 0x01, 0x0f};
// 2.16.840.1.101.3.4.2.1 -- sha256
const uint8_t Sha256[] = {0x60, 0x86, 0x48, 0x01, 0x65,
                          0x03, 0x04, 0x02, 0x01};
// 1.2.840.113549.1.1.1 -- rsaEncryption
const uint8_t RsaEncryption[] = {0x2a, 0x86, 0x48, 0x86, 0xf7,
                                 0x0d, 0x01, 0x01, 0x01};
// 1.2.840.113549.1.9.3 -- contentType
const uint8_t ContentType[] = {0x2a, 0x86, 0x48, 0x86, 0xf7,
                               0x0d, 0x01, 0x09, 0x03};
// 1.2.840.113549.1.9.4 -- messageDigest
const uint8_t MessageDigest[] = {0x2a, 0x86, 0x48, 0x86, 0xf7,
                                 0x0d, 0x01, 0x09, 0x04};
// 1.3.6.1.4.1.311.2.1.11 -- SPC_STATEMENT_TYPE
const uint8_t SpcStatementType[] = {0x2b, 0x06, 0x01, 0x04, 0x01,
                                    0x82, 0x37, 0x02, 0x01, 0x0b};
// 1.3.6.1.4.1.311.2.1.21 -- SPC_INDIVIDUAL_SP_KEY_PURPOSE
const uint8_t SpcIndividualPurpose[] = {0x2b, 0x06, 0x01, 0x04, 0x01,
                                        0x82, 0x37, 0x02, 0x01, 0x15};
} // namespace oid

Bytes derOid(ArrayRef<uint8_t> body) { return tlv(TagOid, body); }

/// AlgorithmIdentifier ::= SEQUENCE { algorithm OID, parameters NULL }
Bytes algorithmIdentifier(ArrayRef<uint8_t> algOid) {
  Bytes inner = derOid(algOid);
  append(inner, tlv(TagNull, {}));
  return tlv(TagSequence, inner);
}

// ===----------------------------------------------------------------------===
// RSA PKCS#1 v1.5 signing
// ===----------------------------------------------------------------------===

/// Big-endian bignum modular exponentiation, just enough for one RSA private
/// operation.  Schoolbook multiply plus binary long division: a 2048-bit
/// modexp lands in single-digit milliseconds, which is irrelevant next to the
/// link it follows.
class BigUInt {
public:
  BigUInt() = default;
  explicit BigUInt(ArrayRef<uint8_t> be) {
    // Store little-endian 32-bit limbs.
    size_t n = be.size();
    for (size_t i = n; i > 0;) {
      uint32_t limb = 0;
      for (unsigned b = 0; b < 4 && i > 0; ++b)
        limb |= static_cast<uint32_t>(be[--i]) << (8 * b);
      limbs.push_back(limb);
    }
    trim();
  }

  bool isZero() const { return limbs.empty(); }

  unsigned bitLength() const {
    if (limbs.empty())
      return 0;
    unsigned top = 32;
    uint32_t hi = limbs.back();
    while (top && !(hi & (1u << (top - 1))))
      --top;
    return static_cast<unsigned>((limbs.size() - 1) * 32) + top;
  }

  bool testBit(unsigned i) const {
    size_t limb = i / 32;
    return limb < limbs.size() && ((limbs[limb] >> (i % 32)) & 1);
  }

  Bytes toBytes(size_t width) const {
    Bytes out(width, 0);
    for (size_t i = 0; i < width; ++i) {
      size_t limb = i / 4;
      if (limb < limbs.size())
        out[width - 1 - i] = static_cast<uint8_t>(limbs[limb] >> (8 * (i % 4)));
    }
    return out;
  }

  static BigUInt mul(const BigUInt &a, const BigUInt &b) {
    BigUInt r;
    if (a.isZero() || b.isZero())
      return r;
    r.limbs.assign(a.limbs.size() + b.limbs.size(), 0);
    for (size_t i = 0; i < a.limbs.size(); ++i) {
      uint64_t carry = 0;
      for (size_t j = 0; j < b.limbs.size(); ++j) {
        uint64_t cur = r.limbs[i + j] +
                       static_cast<uint64_t>(a.limbs[i]) * b.limbs[j] + carry;
        r.limbs[i + j] = static_cast<uint32_t>(cur);
        carry = cur >> 32;
      }
      size_t k = i + b.limbs.size();
      while (carry) {
        uint64_t cur = r.limbs[k] + carry;
        r.limbs[k] = static_cast<uint32_t>(cur);
        carry = cur >> 32;
        ++k;
      }
    }
    r.trim();
    return r;
  }

  /// this %= m, by shift-and-subtract over the bits above the modulus.
  void modAssign(const BigUInt &m) {
    if (m.isZero() || cmp(*this, m) < 0)
      return;
    unsigned shift = bitLength() - m.bitLength();
    BigUInt d = m;
    d.shlAssign(shift);
    for (unsigned i = 0; i <= shift; ++i) {
      if (cmp(*this, d) >= 0)
        subAssign(d);
      d.shrOneAssign();
    }
  }

  static BigUInt modExp(const BigUInt &base, const BigUInt &exp,
                        const BigUInt &mod) {
    BigUInt result;
    result.limbs.push_back(1);
    BigUInt b = base;
    b.modAssign(mod);
    unsigned bits = exp.bitLength();
    for (unsigned i = 0; i < bits; ++i) {
      if (exp.testBit(i)) {
        result = mul(result, b);
        result.modAssign(mod);
      }
      b = mul(b, b);
      b.modAssign(mod);
    }
    return result;
  }

private:
  std::vector<uint32_t> limbs; // little-endian limbs, no trailing zeros

  void trim() {
    while (!limbs.empty() && limbs.back() == 0)
      limbs.pop_back();
  }

  static int cmp(const BigUInt &a, const BigUInt &b) {
    if (a.limbs.size() != b.limbs.size())
      return a.limbs.size() < b.limbs.size() ? -1 : 1;
    for (size_t i = a.limbs.size(); i > 0; --i)
      if (a.limbs[i - 1] != b.limbs[i - 1])
        return a.limbs[i - 1] < b.limbs[i - 1] ? -1 : 1;
    return 0;
  }

  void subAssign(const BigUInt &o) {
    int64_t borrow = 0;
    for (size_t i = 0; i < limbs.size(); ++i) {
      int64_t cur = static_cast<int64_t>(limbs[i]) - borrow -
                    (i < o.limbs.size() ? static_cast<int64_t>(o.limbs[i]) : 0);
      if (cur < 0) {
        cur += (int64_t(1) << 32);
        borrow = 1;
      } else {
        borrow = 0;
      }
      limbs[i] = static_cast<uint32_t>(cur);
    }
    trim();
  }

  void shlAssign(unsigned bits) {
    if (isZero() || !bits)
      return;
    unsigned limbShift = bits / 32, bitShift = bits % 32;
    std::vector<uint32_t> out(limbs.size() + limbShift + 1, 0);
    for (size_t i = 0; i < limbs.size(); ++i) {
      uint64_t v = static_cast<uint64_t>(limbs[i]) << bitShift;
      out[i + limbShift] |= static_cast<uint32_t>(v);
      out[i + limbShift + 1] |= static_cast<uint32_t>(v >> 32);
    }
    limbs = std::move(out);
    trim();
  }

  void shrOneAssign() {
    uint32_t carry = 0;
    for (size_t i = limbs.size(); i > 0; --i) {
      uint32_t cur = limbs[i - 1];
      limbs[i - 1] = (cur >> 1) | (carry << 31);
      carry = cur & 1;
    }
    trim();
  }
};

/// EMSA-PKCS1-v1_5 encode a SHA-256 digest, then raise it to d mod n.
Bytes rsaSignSha256(ArrayRef<uint8_t> digest) {
  ArrayRef<uint8_t> mod(testsign::Modulus, testsign::ModulusSize);
  const size_t k = mod.size();

  // DigestInfo ::= SEQUENCE { AlgorithmIdentifier, OCTET STRING digest }
  Bytes digestInfo = algorithmIdentifier(oid::Sha256);
  append(digestInfo, tlv(TagOctetString, digest));
  digestInfo = tlv(TagSequence, digestInfo);

  // EM = 0x00 || 0x01 || 0xFF... || 0x00 || DigestInfo
  //
  // PKCS#1 wants at least 8 padding octets.  The built-in key is 2048-bit so
  // there is no way to get near that, but regenerating it at some tiny size
  // would wrap the subtraction below into an enormous allocation instead of
  // failing.
  assert(digestInfo.size() + 11 <= k && "signing key too small for SHA-256");
  Bytes em;
  em.push_back(0x00);
  em.push_back(0x01);
  size_t padLen = k - digestInfo.size() - 3;
  em.insert(em.end(), padLen, 0xff);
  em.push_back(0x00);
  append(em, digestInfo);

  BigUInt sig = BigUInt::modExp(BigUInt(em), BigUInt(ArrayRef<uint8_t>(
                                                testsign::PrivateExponent,
                                                testsign::PrivateExponentSize)),
                                BigUInt(mod));
  return sig.toBytes(k);
}

// ===----------------------------------------------------------------------===
// PE layout
// ===----------------------------------------------------------------------===

/// Offsets into the image that signing needs.  Everything is validated before
/// use: this runs on a file the linker just wrote, but a malformed header
/// should produce a diagnostic rather than a stray write.
struct PeLayout {
  size_t checksumOffset = 0; // PE optional header CheckSum field
  size_t certDirOffset = 0;  // data directory entry 4 (certificate table)
  bool plus = false;         // PE32+ (as opposed to PE32)
};

Expected<PeLayout> readPeLayout(ArrayRef<uint8_t> image) {
  auto fail = [](const char *what) {
    return createStringError(inconvertibleErrorCode(),
                             std::string("test signing: ") + what);
  };
  if (image.size() < 0x40)
    return fail("image too small for a DOS header");
  uint32_t peOff = support::endian::read32le(image.data() + 0x3c);
  // COFF header is 20 bytes; the optional header magic follows it.
  if (peOff + 24 > image.size())
    return fail("PE header offset out of range");
  if (memcmp(image.data() + peOff, "PE\0\0", 4) != 0)
    return fail("missing PE signature");

  size_t coff = peOff + 4;
  uint16_t optSize = support::endian::read16le(image.data() + coff + 16);
  size_t opt = coff + 20;
  if (opt + optSize > image.size() || optSize < 96)
    return fail("optional header out of range");

  PeLayout l;
  uint16_t magic = support::endian::read16le(image.data() + opt);
  l.plus = magic == 0x20b;
  if (!l.plus && magic != 0x10b)
    return fail("unrecognised optional header magic");

  l.checksumOffset = opt + 64;
  // Data directories follow the fixed part of the optional header, which is
  // longer in PE32+ because its address fields are 64-bit.
  size_t dirStart = opt + (l.plus ? 112 : 96);
  const size_t certDirIndex = 4;
  l.certDirOffset = dirStart + certDirIndex * 8;
  if (l.certDirOffset + 8 > opt + optSize)
    return fail("certificate data directory missing");
  return l;
}

/// Authenticode digest: SHA-256 over the file, skipping the checksum field,
/// the certificate table directory entry, and any existing certificate area.
/// Those three are what the signature itself changes, so they cannot be part
/// of what it covers.
///
/// Hashing the remainder as one run is equivalent to the spec's per-section
/// walk only because the sections we emit are already in ascending file order.
/// A linker change that reorders them would have to hash section by section.
Bytes authenticodeDigest(ArrayRef<uint8_t> image, const PeLayout &l) {
  SHA256 hash;
  auto update = [&](size_t begin, size_t end) {
    if (begin < end)
      hash.update(image.slice(begin, end - begin));
  };

  update(0, l.checksumOffset);
  update(l.checksumOffset + 4, l.certDirOffset);
  update(l.certDirOffset + 8, image.size());

  auto digest = hash.final();
  return Bytes(digest.begin(), digest.end());
}

// ===----------------------------------------------------------------------===
// Authenticode structures
// ===----------------------------------------------------------------------===

/// SpcIndirectDataContent ::= SEQUENCE {
///   data          SEQUENCE { type OID, value [SpcPeImageData] },
///   messageDigest SEQUENCE { AlgorithmIdentifier, OCTET STRING }
/// }
Bytes buildSpcIndirectData(ArrayRef<uint8_t> imageDigest) {
  // SpcPeImageData ::= SEQUENCE {
  //     flags SpcPeImageFlags DEFAULT { includeResources },
  //     file  [0] EXPLICIT SpcLink
  // }
  //   SpcLink   ::= CHOICE { ..., file [2] EXPLICIT SpcString }
  //   SpcString ::= CHOICE { unicode [0] IMPLICIT BMPSTRING, ... }
  //
  // Three nested context tags, outermost first: [0] for the field, [2] for the
  // CHOICE arm, [0] for the string.  Getting the order or the count wrong
  // still produces a well-formed-looking blob whose digest verifies, so this
  // was checked against what signtool/osslsigncode actually emit.
  //
  // flags is BIT STRING 0x80 with 7 unused bits, i.e. just includeResources.
  static const uint8_t includeResources[] = {0x07, 0x80};
  static const uint8_t obsolete[] = {0x00, 0x3c, 0x00, 0x3c, 0x00, 0x3c, 0x00,
                                     0x4f, 0x00, 0x62, 0x00, 0x73, 0x00, 0x6f,
                                     0x00, 0x6c, 0x00, 0x65, 0x00, 0x74, 0x00,
                                     0x65, 0x00, 0x3e, 0x00, 0x3e, 0x00, 0x3e};
  Bytes spcString = tlv(0x80, obsolete);      // SpcString.unicode
  Bytes spcLink = tlv(0xA2, spcString);       // SpcLink.file
  Bytes spcLinkField = tlv(0xA0, spcLink);    // SpcPeImageData.file
  Bytes flags = tlv(TagBitString, includeResources);

  Bytes peImage = flags;
  append(peImage, spcLinkField);
  peImage = tlv(TagSequence, peImage);

  Bytes dataField = derOid(oid::SpcPeImageData);
  append(dataField, peImage);
  dataField = tlv(TagSequence, dataField);

  Bytes digestField = algorithmIdentifier(oid::Sha256);
  append(digestField, tlv(TagOctetString, imageDigest));
  digestField = tlv(TagSequence, digestField);

  Bytes content = dataField;
  append(content, digestField);
  return tlv(TagSequence, content);
}

/// One `Attribute ::= SEQUENCE { type OID, values SET OF ANY }`.
Bytes attribute(ArrayRef<uint8_t> typeOid, ArrayRef<uint8_t> value) {
  Bytes a = derOid(typeOid);
  append(a, tlv(TagSet, value));
  return tlv(TagSequence, a);
}

/// PKCS#7 SignedData wrapping the SpcIndirectDataContent.
///
/// The signature covers the authenticated attributes, not the content
/// directly.  PKCS#7 permits omitting them -- the signature would then cover
/// the content octets -- but Authenticode verifiers assume they are present
/// and reject a signature computed the other way, so they are mandatory here
/// in practice.
Bytes buildSignedData(ArrayRef<uint8_t> spcContent) {
  // Authenticode digests the SpcIndirectDataContent *value*, i.e. with its own
  // outer tag and length stripped.
  uint8_t lenByte = spcContent[1];
  size_t hdr = (lenByte & 0x80) ? 2 + (lenByte & 0x7f) : 2;
  ArrayRef<uint8_t> content = spcContent.drop_front(hdr);

  SHA256 contentHash;
  contentHash.update(content);
  auto cd = contentHash.final();

  // Authenticated attributes.  DER orders the members of a SET OF by their
  // encodings, which is not the same as ordering them by type OID: the length
  // octet is compared before the OID is reached, so a longer attribute sorts
  // after a shorter one that shares a prefix.  Encode them, then sort.
  //
  // signingTime is left out on purpose: it would make the output depend on the
  // wall clock for no benefit here.
  std::vector<Bytes> attrs;
  attrs.push_back(attribute(oid::ContentType, derOid(oid::SpcIndirectData)));
  attrs.push_back(
      attribute(oid::MessageDigest,
                tlv(TagOctetString, ArrayRef<uint8_t>(cd.data(), cd.size()))));
  attrs.push_back(
      attribute(oid::SpcStatementType,
                tlv(TagSequence, derOid(oid::SpcIndividualPurpose))));
  std::sort(attrs.begin(), attrs.end());

  Bytes authAttrs;
  for (const Bytes &a : attrs)
    append(authAttrs, a);

  // The signature is over these attributes encoded as a SET, even though they
  // travel in the SignerInfo under an implicit [0].  Signing the [0] form
  // instead produces a structurally valid blob that no verifier accepts.
  Bytes attrsForSigning = tlv(TagSet, authAttrs);
  SHA256 attrHash;
  attrHash.update(attrsForSigning);
  auto ad = attrHash.final();
  Bytes signature = rsaSignSha256(ArrayRef<uint8_t>(ad.data(), ad.size()));

  Bytes attrsInSignerInfo = tlv(TagContext0, authAttrs);

  // contentInfo ::= SEQUENCE { contentType OID, content [0] EXPLICIT }
  Bytes contentInfo = derOid(oid::SpcIndirectData);
  append(contentInfo, tlv(TagContext0, spcContent));
  contentInfo = tlv(TagSequence, contentInfo);

  // digestAlgorithms ::= SET OF AlgorithmIdentifier
  Bytes digestAlgos = tlv(TagSet, algorithmIdentifier(oid::Sha256));

  // certificates [0] IMPLICIT SET OF Certificate
  Bytes certs = tlv(TagContext0, ArrayRef<uint8_t>(testsign::CertificateDer,
                                                   testsign::CertificateDerSize));

  // SignerInfo ::= SEQUENCE {
  //   version 1, issuerAndSerialNumber, digestAlgorithm,
  //   authenticatedAttributes [0] IMPLICIT, digestEncryptionAlgorithm,
  //   encryptedDigest }
  Bytes issuerAndSerial(testsign::IssuerDer,
                        testsign::IssuerDer + testsign::IssuerDerSize);
  append(issuerAndSerial,
         derInteger(ArrayRef<uint8_t>(testsign::SerialNumber,
                                      testsign::SerialNumberSize)));
  issuerAndSerial = tlv(TagSequence, issuerAndSerial);

  Bytes signerInfo = derInteger(1u);
  append(signerInfo, issuerAndSerial);
  append(signerInfo, algorithmIdentifier(oid::Sha256));
  append(signerInfo, attrsInSignerInfo);
  append(signerInfo, algorithmIdentifier(oid::RsaEncryption));
  append(signerInfo, tlv(TagOctetString, signature));
  signerInfo = tlv(TagSequence, signerInfo);

  Bytes signerInfos = tlv(TagSet, signerInfo);

  Bytes signedData = derInteger(1u); // version
  append(signedData, digestAlgos);
  append(signedData, contentInfo);
  append(signedData, certs);
  append(signedData, signerInfos);
  signedData = tlv(TagSequence, signedData);

  // ContentInfo ::= SEQUENCE { contentType signedData, content [0] SignedData }
  Bytes outer = derOid(oid::SignedData);
  append(outer, tlv(TagContext0, signedData));
  return tlv(TagSequence, outer);
}

} // namespace

Error testSignImage(StringRef path) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> mb =
      MemoryBuffer::getFile(path, /*IsText=*/false);
  if (!mb)
    return createStringError(mb.getError(),
                             "test signing: cannot read " + path);

  Bytes image(reinterpret_cast<const uint8_t *>((*mb)->getBufferStart()),
              reinterpret_cast<const uint8_t *>((*mb)->getBufferEnd()));
  mb->reset();

  Expected<PeLayout> layout = readPeLayout(image);
  if (!layout)
    return layout.takeError();

  // An image that already carries a certificate would need the old one
  // stripped first; the linker never emits one, so treat it as a bug.
  if (support::endian::read32le(image.data() + layout->certDirOffset + 4) != 0)
    return createStringError(inconvertibleErrorCode(),
                             "test signing: image already has a certificate "
                             "table");

  constexpr size_t MaxPEFileSize = std::numeric_limits<uint32_t>::max();
  if (image.size() > MaxPEFileSize - 7)
    return createStringError(inconvertibleErrorCode(),
                             "test signing: image is too large to align");

  // The attribute certificate must start 8-byte aligned.
  while (image.size() % 8)
    image.push_back(0);
  const uint32_t certOffset = static_cast<uint32_t>(image.size());

  Bytes digest = authenticodeDigest(image, *layout);
  Bytes pkcs7 = buildSignedData(buildSpcIndirectData(digest));

  // WIN_CERTIFICATE { dwLength, wRevision = 0x0200, wCertificateType = 0x0002,
  //                   bCertificate[] }, the whole thing padded to 8 bytes.
  constexpr size_t HeaderSize = 8;
  if (pkcs7.size() > MaxPEFileSize - HeaderSize - 7)
    return createStringError(inconvertibleErrorCode(),
                             "test signing: certificate is too large");
  const size_t CertLength = HeaderSize + pkcs7.size();
  const size_t Padded = (CertLength + 7) & ~size_t(7);
  if (image.size() > MaxPEFileSize - Padded)
    return createStringError(inconvertibleErrorCode(),
                             "test signing: signed image is too large");

  Bytes entry(Padded, 0);
  support::endian::write32le(entry.data(), static_cast<uint32_t>(CertLength));
  support::endian::write16le(entry.data() + 4, 0x0200);
  support::endian::write16le(entry.data() + 6, 0x0002);
  std::copy(pkcs7.begin(), pkcs7.end(), entry.begin() + HeaderSize);
  image.insert(image.end(), entry.begin(), entry.end());

  // Point the certificate table at it, then checksum the finished image.
  support::endian::write32le(image.data() + layout->certDirOffset, certOffset);
  support::endian::write32le(image.data() + layout->certDirOffset + 4,
                             static_cast<uint32_t>(Padded));
  support::endian::write32le(
      image.data() + layout->checksumOffset,
      detail::computePEChecksum(image, layout->checksumOffset,
                                /*ExplicitlySerial=*/true));

  std::error_code ec;
  raw_fd_ostream os(path, ec, sys::fs::OF_None);
  if (ec)
    return createStringError(ec, "test signing: cannot write " + path);
  os.write(reinterpret_cast<const char *>(image.data()), image.size());
  os.close();
  if (os.has_error())
    return createStringError(os.error(), "test signing: write failed");
  return Error::success();
}

} // namespace coff
} // namespace linker
