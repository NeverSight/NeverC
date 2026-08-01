//===- TlsProtocolFuzzer.cpp - TLS/X.509 protocol fuzzing ---------------===//

#include "neverc/std/crypto/tls.h"
#include "neverc/std/crypto/x509.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

constexpr size_t MaxInputSize = 64 * 1024;

uint16_t readU16(const uint8_t *data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                               data[1]);
}

size_t derObjectSize(const uint8_t *data, size_t size) {
  if (size < 2 || data[0] != 0x30)
    return 0;
  uint8_t firstLength = data[1];
  if ((firstLength & 0x80) == 0) {
    size_t total = 2 + firstLength;
    return total <= size ? total : 0;
  }

  size_t lengthBytes = firstLength & 0x7f;
  if (lengthBytes == 0 || lengthBytes > sizeof(size_t) ||
      lengthBytes > size - 2 || data[2] == 0)
    return 0;
  size_t bodySize = 0;
  for (size_t i = 0; i < lengthBytes; ++i) {
    if (bodySize > (MaxInputSize >> 8))
      return 0;
    bodySize = (bodySize << 8) | data[2 + i];
  }
  size_t headerSize = 2 + lengthBytes;
  if (bodySize > size - headerSize)
    return 0;
  return headerSize + bodySize;
}

uint16_t signatureScheme(uint8_t selector) {
  constexpr std::array<uint16_t, 5> Schemes = {
      NEVERC_TLS_SIGNATURE_ECDSA_SECP256R1_SHA256,
      NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA256,
      NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA384,
      NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA512,
      NEVERC_TLS_SIGNATURE_ED25519,
  };
  return Schemes[selector % Schemes.size()];
}

void parseCertificate(const uint8_t *der, size_t derSize,
                      const uint8_t *tail, size_t tailSize) {
  neverc_x509_cert_t certificate;
  int parsed = neverc_x509_parse_certificate(&certificate, der, derSize);
  if (parsed != 0) {
    neverc_x509_cert_free(&certificate);
    return;
  }

  if (tailSize != 0) {
    std::array<char, 256> hostname{};
    size_t hostnameSize = std::min(tailSize, hostname.size() - 1);
    std::memcpy(hostname.data(), tail, hostnameSize);
    (void)neverc_x509_verify_hostname(&certificate, hostname.data());
  }

  if (tailSize >= 3) {
    size_t transcriptSize = (tail[1] & 1) != 0 ? 48 : 32;
    if (tailSize >= 2 + transcriptSize + 1) {
      const uint8_t *transcript = tail + 2;
      const uint8_t *signature = transcript + transcriptSize;
      size_t signatureSize = tailSize - 2 - transcriptSize;
      (void)neverc_tls_verify_certificate_verify(
          &certificate, signatureScheme(tail[0]), (tail[1] >> 1) & 1,
          transcript, transcriptSize, signature, signatureSize);
    }
  }

  neverc_x509_cert_free(&certificate);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (data == nullptr || size == 0 || size > MaxInputSize)
    return 0;

  if ((data[0] & 3) == 2) {
    (void)neverc_tls_test_fuzz_handshake_reassembly(data + 1, size - 1);
    return 0;
  }

  size_t certificateSize = derObjectSize(data, size);
  if (certificateSize != 0) {
    parseCertificate(data, certificateSize, data + certificateSize,
                     size - certificateSize);
    return 0;
  }

  if ((data[0] & 1) == 0) {
    parseCertificate(data + 1, size - 1, nullptr, 0);
    return 0;
  }

  if (size < 3)
    return 0;
  certificateSize = readU16(data + 1);
  if (certificateSize == 0 || certificateSize > size - 3)
    return 0;
  parseCertificate(data + 3, certificateSize,
                   data + 3 + certificateSize,
                   size - 3 - certificateSize);
  return 0;
}
