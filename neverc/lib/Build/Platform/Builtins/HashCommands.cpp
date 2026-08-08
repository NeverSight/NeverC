#include "Platform/Builtins/Internal.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace neverc {
namespace build {
namespace builtins {
namespace internal {

namespace {

// Compact SHA-512 (FIPS 180-4) for sha512sum. LLVM Support has SHA1/SHA256 only.
struct Sha512 {
  static constexpr size_t HashLen = 64;
  static constexpr size_t BlockLen = 128;

  std::array<uint64_t, 8> State{};
  uint64_t BitLenHi = 0;
  uint64_t BitLenLo = 0;
  uint8_t Buf[BlockLen]{};
  size_t BufLen = 0;

  Sha512() {
    State = {{0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
              0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
              0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
              0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL}};
  }

  static uint64_t rotr(uint64_t X, unsigned N) {
    return (X >> N) | (X << (64 - N));
  }
  static uint64_t Ch(uint64_t X, uint64_t Y, uint64_t Z) {
    return (X & Y) ^ (~X & Z);
  }
  static uint64_t Maj(uint64_t X, uint64_t Y, uint64_t Z) {
    return (X & Y) ^ (X & Z) ^ (Y & Z);
  }
  static uint64_t S0(uint64_t X) {
    return rotr(X, 28) ^ rotr(X, 34) ^ rotr(X, 39);
  }
  static uint64_t S1(uint64_t X) {
    return rotr(X, 14) ^ rotr(X, 18) ^ rotr(X, 41);
  }
  static uint64_t s0(uint64_t X) {
    return rotr(X, 1) ^ rotr(X, 8) ^ (X >> 7);
  }
  static uint64_t s1(uint64_t X) {
    return rotr(X, 19) ^ rotr(X, 61) ^ (X >> 6);
  }
  static uint64_t loadBe(const uint8_t *P) {
    return (uint64_t(P[0]) << 56) | (uint64_t(P[1]) << 48) |
           (uint64_t(P[2]) << 40) | (uint64_t(P[3]) << 32) |
           (uint64_t(P[4]) << 24) | (uint64_t(P[5]) << 16) |
           (uint64_t(P[6]) << 8) | uint64_t(P[7]);
  }
  static void storeBe(uint8_t *P, uint64_t V) {
    P[0] = uint8_t(V >> 56);
    P[1] = uint8_t(V >> 48);
    P[2] = uint8_t(V >> 40);
    P[3] = uint8_t(V >> 32);
    P[4] = uint8_t(V >> 24);
    P[5] = uint8_t(V >> 16);
    P[6] = uint8_t(V >> 8);
    P[7] = uint8_t(V);
  }

  void compress(const uint8_t *Block) {
    static constexpr uint64_t K[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
        0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
        0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
        0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
        0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
        0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
        0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
        0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
        0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
        0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
        0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
        0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
        0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
        0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
        0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
        0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
        0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
        0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
        0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
        0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

    uint64_t W[80];
    for (int I = 0; I < 16; ++I)
      W[I] = loadBe(Block + I * 8);
    for (int I = 16; I < 80; ++I)
      W[I] = s1(W[I - 2]) + W[I - 7] + s0(W[I - 15]) + W[I - 16];

    uint64_t A = State[0], B = State[1], C = State[2], D = State[3];
    uint64_t E = State[4], F = State[5], G = State[6], H = State[7];
    for (int I = 0; I < 80; ++I) {
      const uint64_t T1 = H + S1(E) + Ch(E, F, G) + K[I] + W[I];
      const uint64_t T2 = S0(A) + Maj(A, B, C);
      H = G;
      G = F;
      F = E;
      E = D + T1;
      D = C;
      C = B;
      B = A;
      A = T1 + T2;
    }
    State[0] += A;
    State[1] += B;
    State[2] += C;
    State[3] += D;
    State[4] += E;
    State[5] += F;
    State[6] += G;
    State[7] += H;
  }

  void update(const uint8_t *Data, size_t Len) {
    // Track bit length as a 128-bit value (lo/hi).
    const uint64_t Bits = uint64_t(Len) << 3;
    BitLenHi += Len >> 61;
    const uint64_t NewLo = BitLenLo + Bits;
    if (NewLo < BitLenLo)
      ++BitLenHi;
    BitLenLo = NewLo;

    if (BufLen) {
      const size_t Take = std::min(BlockLen - BufLen, Len);
      std::memcpy(Buf + BufLen, Data, Take);
      BufLen += Take;
      Data += Take;
      Len -= Take;
      if (BufLen == BlockLen) {
        compress(Buf);
        BufLen = 0;
      }
    }
    while (Len >= BlockLen) {
      compress(Data);
      Data += BlockLen;
      Len -= BlockLen;
    }
    if (Len) {
      std::memcpy(Buf, Data, Len);
      BufLen = Len;
    }
  }

  std::array<uint8_t, HashLen> final() {
    Buf[BufLen++] = 0x80;
    if (BufLen > 112) {
      std::memset(Buf + BufLen, 0, BlockLen - BufLen);
      compress(Buf);
      BufLen = 0;
    }
    std::memset(Buf + BufLen, 0, 112 - BufLen);
    storeBe(Buf + 112, BitLenHi);
    storeBe(Buf + 120, BitLenLo);
    compress(Buf);

    std::array<uint8_t, HashLen> Out{};
    for (int I = 0; I < 8; ++I)
      storeBe(Out.data() + I * 8, State[I]);
    return Out;
  }

  static std::array<uint8_t, HashLen> hash(llvm::ArrayRef<uint8_t> Data) {
    Sha512 Ctx;
    Ctx.update(Data.data(), Data.size());
    return Ctx.final();
  }
};

} // namespace

bool tryExecuteChecksum(llvm::ArrayRef<Token> Argv, int &ExitCode,
                        ChecksumKind Kind) {
  const char *Name = Kind == ChecksumKind::SHA512
                         ? "sha512sum"
                         : Kind == ChecksumKind::SHA256
                               ? "sha256sum"
                               : Kind == ChecksumKind::SHA1 ? "sha1sum"
                                                           : "md5sum";
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false; // -c / --tag / binary flags stay on the host tool
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false; // stdin

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: " << Name << ": " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::StringRef Data = (*Buf)->getBuffer();
    std::string Digest;
    const auto Bytes = llvm::ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(Data.data()), Data.size());
    if (Kind == ChecksumKind::SHA512) {
      const auto Hash = Sha512::hash(Bytes);
      Digest = llvm::toHex(llvm::ArrayRef<uint8_t>(Hash.data(), Hash.size()),
                           /*LowerCase=*/true);
    } else if (Kind == ChecksumKind::SHA256) {
      const auto Hash = llvm::SHA256::hash(Bytes);
      Digest = llvm::toHex(llvm::ArrayRef<uint8_t>(Hash.data(), Hash.size()),
                           /*LowerCase=*/true);
    } else if (Kind == ChecksumKind::SHA1) {
      const auto Hash = llvm::SHA1::hash(Bytes);
      Digest = llvm::toHex(llvm::ArrayRef<uint8_t>(Hash.data(), Hash.size()),
                           /*LowerCase=*/true);
    } else {
      // Hash the already-read buffer so md5sum cannot race with a second open.
      // Use toHex(..., LowerCase) so output matches GNU md5sum / sha*sum.
      const auto Hash = llvm::MD5::hash(Bytes);
      Digest = llvm::toHex(llvm::ArrayRef<uint8_t>(Hash.data(), Hash.size()),
                           /*LowerCase=*/true);
    }
    // GNU text-mode format: "<hash>  <path>"
    llvm::outs() << Digest << "  " << Path << '\n';
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}

bool tryExecuteMd5sum(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  return tryExecuteChecksum(Argv, ExitCode, ChecksumKind::MD5);
}

bool tryExecuteMd5(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Darwin/BSD `md5 -q FILE...` (quiet: hash only). Other forms stay on the
  // host tool so we do not invent the verbose `MD5 (file) = ...` layout.
  bool Quiet = false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-q") {
      Quiet = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg != "-")
      return false;
    Files.push_back(Argv[I]);
  }
  if (!Quiet || Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: md5: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::StringRef Data = (*Buf)->getBuffer();
    const auto Bytes = llvm::ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(Data.data()), Data.size());
    const auto Hash = llvm::MD5::hash(Bytes);
    llvm::outs() << llvm::toHex(
                        llvm::ArrayRef<uint8_t>(Hash.data(), Hash.size()),
                        /*LowerCase=*/true)
                 << '\n';
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}

bool tryExecuteSha1sum(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  return tryExecuteChecksum(Argv, ExitCode, ChecksumKind::SHA1);
}

bool tryExecuteSha256sum(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  return tryExecuteChecksum(Argv, ExitCode, ChecksumKind::SHA256);
}

bool tryExecuteSha512sum(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  return tryExecuteChecksum(Argv, ExitCode, ChecksumKind::SHA512);
}

uint32_t posixCksumCrc(llvm::StringRef Data) {
  // POSIX.1 cksum CRC (same polynomial/table style as GNU coreutils).
  static const uint32_t Crctab[256] = {
      0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
      0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
      0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
      0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
      0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
      0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
      0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
      0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
      0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
      0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
      0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
      0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
      0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
      0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
      0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
      0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
      0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
      0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
      0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
      0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
      0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
      0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
      0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
      0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
      0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
      0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
      0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
      0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
      0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
      0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
      0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
      0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
      0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
      0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
      0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
      0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
      0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
      0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
      0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
      0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
      0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
      0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
      0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4};
  uint32_t Crc = 0;
  for (unsigned char C : Data)
    Crc = (Crc << 8) ^ Crctab[((Crc >> 24) ^ C) & 0xFF];
  for (uint64_t Len = Data.size(); Len != 0; Len >>= 8)
    Crc = (Crc << 8) ^ Crctab[((Crc >> 24) ^ (Len & 0xFF)) & 0xFF];
  return ~Crc;
}


bool tryExecuteCksum(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false;
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: cksum: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::StringRef Data = (*Buf)->getBuffer();
    llvm::outs() << posixCksumCrc(Data) << ' ' << Data.size() << ' ' << Path
                 << '\n';
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


} // namespace internal
} // namespace builtins
} // namespace build
} // namespace neverc
