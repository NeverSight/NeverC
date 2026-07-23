// The dyncode binary phase executor: see DynCodeBinaryPhaseExecutor.h.

#include "Binary/DynCodeBinaryPhaseExecutor.h"
#include "Binary/BuiltinDynCodeBinaryVerifier.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include <string>

using namespace llvm;

namespace neverc {
namespace dyncode {

namespace {

llvm::Error record(DynCodeReport &Report, uint64_t PhaseOrder,
                   const char *Provider, const char *Key,
                   const std::string &Value) {
  return Report.addRecord({PhaseOrder, Provider, Key, Value});
}

/// Applies size / alignment / padding to the image, matching the pad-byte and
/// max-length semantics of the legacy finalizer but through the bounded
/// builder.  The pad byte itself is subject to the bad-byte policy.
llvm::Error applyDynCodeSizing(DynCodeImage &Image, const DynCodeOptions &Opts,
                               uint64_t &OutPadding) {
  OutPadding = 0;
  const uint8_t Pad = Opts.PadByte.value_or(0x00);
  const bool WantsPadding = Opts.Align > 1 || Opts.MaxLength.has_value();

  if (WantsPadding && llvm::is_contained(Opts.BadBytes, Pad))
    return createStringError(
        errc::invalid_argument,
        "dyncode binary: pad byte 0x%02x is in the bad-byte set; pass "
        "-fdyncode-pad=<byte> with a byte the bad-byte audit accepts",
        (unsigned)Pad);

  const uint64_t Start = Image.size();

  if (Opts.Align > 1) {
    uint64_t Mask = static_cast<uint64_t>(Opts.Align) - 1;
    uint64_t Aligned = (Image.size() + Mask) & ~Mask;
    if (Aligned > Image.size())
      if (llvm::Error E = Image.resize(Aligned, Pad))
        return E;
  }

  if (Opts.MaxLength) {
    uint64_t Limit = *Opts.MaxLength;
    if (Image.size() > Limit)
      return createStringError(
          errc::invalid_argument,
          "dyncode binary: image size %llu exceeds -fdyncode-max-length=%llu",
          (unsigned long long)Image.size(), (unsigned long long)Limit);
    if (Image.size() < Limit)
      if (llvm::Error E = Image.resize(Limit, Pad))
        return E;
  }

  OutPadding = Image.size() - Start;
  Image.setPaddingSize(OutPadding);
  return Error::success();
}

} // namespace

llvm::Error runDynCodeBinaryPhases(DynCodeImage &Image, DynCodeReport &Report,
                                   const DynCodeOptions &Opts,
                                   const DynCodeRewriteRegistry &Rewrites,
                                   const DynCodeCharsetRegistry &Charsets) {
  ArrayRef<uint8_t> BadBytes(Opts.BadBytes);

  // Bad-byte rewrite chain (phase dyncode.binary.bad_byte_rewrite).  Disabling
  // it selects an explicit no-op step; the topology and the final audit are
  // unchanged.
  if (Opts.BadByteRewrite && !Rewrites.empty() && !BadBytes.empty()) {
    uint64_t Changes = 0;
    if (llvm::Error E = Rewrites.runChain(Image, BadBytes, Changes))
      return E;
    if (llvm::Error E = record(Report, 29, "builtin.bad_byte_rewrite",
                               "bytes.changed", std::to_string(Changes)))
      return E;
  } else if (llvm::Error E = record(Report, 29, "builtin.bad_byte_rewrite",
                                    "disposition", "no-op")) {
    return E;
  }

  // Charset encode (phase dyncode.binary.charset_encode).  Exactly one provider
  // is selected by stable ID; an unknown ID is a hard error.
  if (!Opts.Charset.empty()) {
    if (llvm::Error E = Charsets.run(Opts.Charset, Image, BadBytes))
      return E;
    if (llvm::Error E = record(Report, 30, "builtin.charset_encode",
                               "encoder", Opts.Charset))
      return E;
  }

  // Size / alignment / padding (phase dyncode.binary.size_align).
  uint64_t Padding = 0;
  if (llvm::Error E = applyDynCodeSizing(Image, Opts, Padding))
    return E;
  if (llvm::Error E = record(Report, 31, "builtin.size_align", "padding.bytes",
                             std::to_string(Padding)))
    return E;

  // Final structural verifier (phase dyncode.verify, sealed).
  if (llvm::Error E = verifyDynCodeBinary(Image, Opts))
    return E;
  if (llvm::Error E = record(Report, 33, "builtin.binary_verifier", "result",
                             "accepted"))
    return E;

  return Image.markVerified();
}

} // namespace dyncode
} // namespace neverc
