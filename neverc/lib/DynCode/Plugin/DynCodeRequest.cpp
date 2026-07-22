#include "DynCodeRequest.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/SHA256.h"

#include <algorithm>
#include <cstring>

namespace neverc {
namespace dyncode {

namespace {

NevercStringView sv(const std::string &S) {
  return NevercStringView{S.data(), static_cast<uint64_t>(S.size())};
}

// Deterministic canonical serializer. All variable-length fields are written
// with an explicit 8-byte little-endian length prefix so distinct field values
// can never collide by concatenation. Wall-clock/telemetry never participates.
struct Canonicalizer {
  std::string Buffer;

  void u64(uint64_t V) {
    uint8_t Bytes[8];
    for (unsigned I = 0; I < 8; ++I)
      Bytes[I] = static_cast<uint8_t>((V >> (I * 8)) & 0xFF);
    Buffer.append(reinterpret_cast<const char *>(Bytes), sizeof(Bytes));
  }
  void id(NevercInterfaceID ID) {
    u64(ID.High);
    u64(ID.Low);
  }
  void str(llvm::StringRef S) {
    u64(S.size());
    Buffer.append(S.data(), S.size());
  }
  void bytes(llvm::ArrayRef<uint8_t> B) {
    u64(B.size());
    Buffer.append(reinterpret_cast<const char *>(B.data()), B.size());
  }
};

std::array<uint8_t, 32> computeDigest(const FrozenDynCodeRequest &Req) {
  Canonicalizer C;
  const NevercTargetKey Key = Req.Target.view();
  C.str("neverc.dyncode.request.v1");
  C.id(Key.TargetID);
  C.str(llvm::StringRef(Key.RawTriple.Data, Key.RawTriple.Length));
  C.id(Key.ABIID);
  C.id(Key.CallingConventionID);
  C.id(Key.ObjectFormatID);
  C.u64(Key.RelocationModel);
  C.u64(Key.CodeModel);
  C.u64(Key.ExecutionLevel);
  C.u64(Key.PointerWidth);
  C.u64(Key.Endianness);
  C.str(llvm::StringRef(Key.SchemaDigest.Data, Key.SchemaDigest.Length));
  C.id(Req.ObjectFormat);
  C.u64(Req.ExecutionLevel);
  C.u64(Req.EntryKind);
  C.str(Req.EntrySymbol);
  C.u64(Req.EntryCandidates.size());
  for (const std::string &Cand : Req.EntryCandidates)
    C.str(Cand);
  C.u64(Req.PICFlags);
  C.u64(Req.Flags);
  C.u64(Req.MaxLength);
  C.u64(Req.Alignment);
  C.u64(Req.HasPadByte ? (0x100u | Req.PadByte) : 0u);
  C.bytes(Req.BadBytes);
  C.str(Req.BadByteProfile);
  C.str(Req.CharsetProviderID);
  C.str(Req.TransformConfigNamespace);
  C.str(Req.MainOutputSinkID);
  C.str(Req.ObjectOutputSinkID);
  C.str(Req.ReportOutputSinkID);

  return llvm::SHA256::hash(llvm::ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(C.Buffer.data()), C.Buffer.size()));
}

} // namespace

llvm::Expected<FrozenDynCodeRequest>
freezeDynCodeRequest(const DynCodeOptions &Opts,
                     const plugin::OwnedTargetKey &Target,
                     NevercObjectFormatID ObjectFormat) {
  FrozenDynCodeRequest Req;
  Req.Target = Target;
  Req.ObjectFormat = ObjectFormat;
  Req.ExecutionLevel = Opts.Level == ExecutionLevel::Kernel
                           ? NEVERC_DYNCODE_LEVEL_KERNEL
                           : NEVERC_DYNCODE_LEVEL_USER;

  if (!Opts.EntrySymbol.empty()) {
    Req.EntryKind = NEVERC_DYNCODE_ENTRY_EXPLICIT;
    Req.EntrySymbol = Opts.EntrySymbol;
  } else {
    Req.EntryKind = NEVERC_DYNCODE_ENTRY_CANDIDATE_LIST;
  }

  // First version dyncode images are always position independent and load at
  // logical base 0; the entry must sit at offset 0.
  Req.PICFlags = NEVERC_DYNCODE_PIC_ALLOW_PC_RELATIVE |
                 NEVERC_DYNCODE_PIC_REQUIRE_ENTRY_AT_ZERO;

  if (Opts.BadByteRewrite)
    Req.Flags |= NEVERC_DYNCODE_REQUEST_REWRITE_BAD_BYTES;
  if (Opts.InlineAll)
    Req.Flags |= NEVERC_DYNCODE_REQUEST_INLINE;
  if (Opts.AllBlr)
    Req.Flags |= NEVERC_DYNCODE_REQUEST_ALL_BLR;
  if (!Opts.KeepObjPath.empty())
    Req.Flags |= NEVERC_DYNCODE_REQUEST_KEEP_OBJECT;
  Req.Flags |= NEVERC_DYNCODE_REQUEST_DETERMINISTIC;

  Req.MaxLength = Opts.MaxLength.value_or(0);
  Req.Alignment = Opts.Align == 0 ? 1 : Opts.Align;
  if ((Req.Alignment & (Req.Alignment - 1)) != 0)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "dyncode alignment must be a power of two");

  if (Opts.PadByte) {
    Req.HasPadByte = true;
    Req.PadByte = *Opts.PadByte;
  }

  Req.BadBytes.assign(Opts.BadBytes.begin(), Opts.BadBytes.end());
  std::sort(Req.BadBytes.begin(), Req.BadBytes.end());
  Req.BadBytes.erase(std::unique(Req.BadBytes.begin(), Req.BadBytes.end()),
                     Req.BadBytes.end());
  if (Req.HasPadByte &&
      std::binary_search(Req.BadBytes.begin(), Req.BadBytes.end(),
                         static_cast<uint8_t>(Req.PadByte)))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "dyncode pad byte is also listed as a bad byte");

  Req.BadByteProfile = Opts.BadByteProfile;
  Req.CharsetProviderID = Opts.Charset;
  Req.TransformConfigNamespace = Opts.ObfuscateSpec;
  Req.MainOutputSinkID = "dyncode.main";
  Req.ObjectOutputSinkID =
      Opts.KeepObjPath.empty() ? std::string() : std::string("dyncode.keep-obj");
  Req.ReportOutputSinkID = std::string();

  Req.Digest = computeDigest(Req);
  return Req;
}

void fillRequestInfo(const FrozenDynCodeRequest &Req,
                     NevercDynCodeRequestInfo &Out,
                     std::vector<NevercStringView> &CandidateScratch) {
  std::memset(&Out, 0, sizeof(Out));
  Out.Header = {static_cast<uint32_t>(sizeof(NevercDynCodeRequestInfo)),
                NEVERC_DYNCODE_API_MAJOR, NEVERC_DYNCODE_API_MINOR, 0};
  Out.Target = Req.Target.view();
  Out.TargetSchemaDigest = Out.Target.SchemaDigest;
  Out.ObjectFormat = Req.ObjectFormat;
  Out.ExecutionLevel = Req.ExecutionLevel;

  Out.Entry.Kind = Req.EntryKind;
  Out.Entry.ExplicitSymbol = sv(Req.EntrySymbol);
  CandidateScratch.clear();
  CandidateScratch.reserve(Req.EntryCandidates.size());
  for (const std::string &Cand : Req.EntryCandidates)
    CandidateScratch.push_back(sv(Cand));
  Out.Entry.CandidateSymbols = {CandidateScratch.data(),
                                static_cast<uint64_t>(CandidateScratch.size()),
                                sizeof(NevercStringView)};

  Out.PICFlags = Req.PICFlags;
  Out.Flags = Req.Flags;
  Out.MaxLength = Req.MaxLength;
  Out.Alignment = Req.Alignment;
  Out.PadByte = Req.HasPadByte ? Req.PadByte : 0;
  Out.BadByteCount = static_cast<uint32_t>(Req.BadBytes.size());
  Out.BadByteSet = {Req.BadBytes.data(),
                    static_cast<uint64_t>(Req.BadBytes.size())};
  Out.BadByteProfile = sv(Req.BadByteProfile);
  Out.CharsetProviderID = sv(Req.CharsetProviderID);
  Out.TransformConfigNamespace = sv(Req.TransformConfigNamespace);
  Out.MainOutputSinkID = sv(Req.MainOutputSinkID);
  Out.ObjectOutputSinkID = sv(Req.ObjectOutputSinkID);
  Out.ReportOutputSinkID = sv(Req.ReportOutputSinkID);
  std::memcpy(Out.RequestDigest, Req.Digest.data(), 32);
}

} // namespace dyncode
} // namespace neverc
