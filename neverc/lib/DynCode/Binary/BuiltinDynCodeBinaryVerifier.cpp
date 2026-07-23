// The builtin structural dyncode binary verifier: see
// BuiltinDynCodeBinaryVerifier.h.

#include "Binary/BuiltinDynCodeBinaryVerifier.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace neverc {
namespace dyncode {

std::optional<uint64_t> firstDynCodeBadByte(ArrayRef<uint8_t> Bytes,
                                            ArrayRef<uint8_t> BadBytes) {
  if (BadBytes.empty())
    return std::nullopt;
  bool Forbidden[256] = {};
  for (uint8_t B : BadBytes)
    Forbidden[B] = true;
  for (uint64_t I = 0, E = Bytes.size(); I < E; ++I)
    if (Forbidden[Bytes[I]])
      return I;
  return std::nullopt;
}

llvm::Error verifyDynCodeBinary(const DynCodeImage &Image,
                                const DynCodeOptions &Opts) {
  const uint64_t Size = Image.size();
  if (Size == 0)
    return createStringError(errc::invalid_argument,
                             "dyncode binary verifier: image is empty");

  // dyncode requires the entry at output offset 0 (entry-at-zero).
  if (Image.entryOffset() != 0)
    return createStringError(
        errc::invalid_argument,
        "dyncode binary verifier: entry offset %llu is not 0 (entry-at-zero "
        "violated)",
        (unsigned long long)Image.entryOffset());
  if (Image.entryOffset() >= Size)
    return createStringError(
        errc::invalid_argument,
        "dyncode binary verifier: entry offset %llu is past the image end %llu",
        (unsigned long long)Image.entryOffset(), (unsigned long long)Size);

  if (Opts.MaxLength && Size > *Opts.MaxLength)
    return createStringError(
        errc::invalid_argument,
        "dyncode binary verifier: image size %llu exceeds max length %llu",
        (unsigned long long)Size, (unsigned long long)*Opts.MaxLength);

  if (Opts.Align > 1 && (Size % Opts.Align) != 0)
    return createStringError(
        errc::invalid_argument,
        "dyncode binary verifier: image size %llu is not aligned to %u",
        (unsigned long long)Size, Opts.Align);

  if (std::optional<uint64_t> Hit =
          firstDynCodeBadByte(Image.bytes(), Opts.BadBytes))
    return createStringError(
        errc::invalid_argument,
        "dyncode binary verifier: forbidden byte 0x%02x at offset 0x%llx in "
        "the final image (payload, decoder stub or padding)",
        (unsigned)Image.bytes()[*Hit], (unsigned long long)*Hit);

  return Error::success();
}

} // namespace dyncode
} // namespace neverc
