#!/usr/bin/env python3
"""Regenerate the built-in Authenticode test-signing identity.

`-ftest-sign` signs driver images with a self-signed certificate whose private
key ships inside the compiler.  That is intentional and is the whole point: it
lets a driver satisfy the code-integrity check on a machine that has test
signing on and this certificate trusted.  It is not a secret and provides no
authenticity guarantee.

Running this replaces both the embedded key material and the .cer users import,
which invalidates every previously signed image.  Only do it if the identity
must change.

    python3 utils/gen-test-sign-key.py

Writes:
    neverc/lib/Linker/Backends/COFF/Emit/CoffTestSignKey.cpp
    utils/neverc-test-signing.cer
"""
import datetime
import pathlib
import subprocess
import sys
import tempfile

try:
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID
except ImportError:
    sys.exit("needs the 'cryptography' package: pip install cryptography")

ROOT = pathlib.Path(__file__).resolve().parent.parent
KEY_CPP = ROOT / "neverc/lib/Linker/Backends/COFF/Emit/CoffTestSignKey.cpp"
CER = ROOT / "utils/neverc-test-signing.cer"

SUBJECT = x509.Name([
    x509.NameAttribute(NameOID.COUNTRY_NAME, "US"),
    x509.NameAttribute(NameOID.ORGANIZATION_NAME, "NeverC"),
    x509.NameAttribute(NameOID.COMMON_NAME, "NeverC Test Signing"),
])


def build_identity():
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (
        x509.CertificateBuilder()
        .subject_name(SUBJECT)
        .issuer_name(SUBJECT)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(days=1))
        .not_valid_after(now + datetime.timedelta(days=365 * 20))
        # Self-signed and trusted directly, so it must be usable as its own
        # chain root as well as an end-entity code-signing certificate.
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True, content_commitment=False,
                key_encipherment=False, data_encipherment=False,
                key_agreement=False, key_cert_sign=True, crl_sign=False,
                encipher_only=False, decipher_only=False,
            ),
            critical=True,
        )
        .add_extension(
            x509.ExtendedKeyUsage([x509.ObjectIdentifier("1.3.6.1.5.5.7.3.3")]),
            critical=False,
        )
        .sign(key, hashes.SHA256())
    )
    return key, cert


def carray(data: bytes, indent: str = "    ") -> str:
    lines, cur = [], []
    for b in data:
        cur.append(f"0x{b:02x},")
        if len(cur) == 12:
            lines.append(indent + " ".join(cur))
            cur = []
    if cur:
        lines.append(indent + " ".join(cur))
    return "\n".join(lines)


def main() -> int:
    key, cert = build_identity()
    nums = key.private_numbers()
    pub = nums.public_numbers
    width = (pub.n.bit_length() + 7) // 8

    modulus = pub.n.to_bytes(width, "big")
    private_exp = nums.d.to_bytes(width, "big")
    cert_der = cert.public_bytes(serialization.Encoding.DER)
    issuer_der = cert.issuer.public_bytes()
    serial = cert.serial_number
    serial_der = serial.to_bytes(max(1, (serial.bit_length() + 8) // 8), "big")
    expiry = cert.not_valid_after_utc.year

    KEY_CPP.write_text(f'''//===--- CoffTestSignKey.cpp - Built-in Authenticode test identity --------===//
//
// Generated material for `-ftest-sign`.  This is a throwaway self-signed
// identity whose private key ships in the compiler: it exists so a driver can
// satisfy the code-integrity check on a machine that has test signing enabled
// and this certificate trusted.  It is deliberately NOT a secret and must
// never be used to sign anything that leaves a test machine.
//
// Subject/Issuer: CN=NeverC Test Signing, O=NeverC, C=US
// Valid until:    {expiry}
//
// Regenerate with utils/gen-test-sign-key.py (this invalidates every image
// signed with the previous identity).
//
//===----------------------------------------------------------------------===//

#include "Linker/COFF/TestSign.h"

namespace linker {{
namespace coff {{
namespace testsign {{

// DER-encoded X.509 certificate, embedded verbatim in the PKCS#7 SignedData.
const uint8_t CertificateDer[] = {{
{carray(cert_der)}
}};
const size_t CertificateDerSize = sizeof(CertificateDer);

// RSA modulus (big-endian).  Signing is m^d mod n.
const uint8_t Modulus[] = {{
{carray(modulus)}
}};
const size_t ModulusSize = sizeof(Modulus);

const uint8_t PrivateExponent[] = {{
{carray(private_exp)}
}};
const size_t PrivateExponentSize = sizeof(PrivateExponent);

const unsigned PublicExponent = {pub.e};

// DER-encoded issuer Name, used by SignerInfo's issuerAndSerialNumber.
const uint8_t IssuerDer[] = {{
{carray(issuer_der)}
}};
const size_t IssuerDerSize = sizeof(IssuerDer);

// Serial number as a big-endian magnitude (no leading sign byte).
const uint8_t SerialNumber[] = {{
{carray(serial_der)}
}};
const size_t SerialNumberSize = sizeof(SerialNumber);

}} // namespace testsign
}} // namespace coff
}} // namespace linker
''')

    CER.write_bytes(cert_der)
    print(f"wrote {KEY_CPP.relative_to(ROOT)}")
    print(f"wrote {CER.relative_to(ROOT)}  ({len(cert_der)} bytes, valid to {expiry})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
