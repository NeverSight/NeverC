#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/SHA256.h"

#include <cstring>

using namespace llvm;

namespace neverc {
namespace dyncode {
namespace {

Error imageError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "dyncode image: " + Message);
}

/// Checked ``a + b``; fails instead of wrapping.
Expected<uint64_t> checkedAdd(uint64_t A, uint64_t B, const Twine &What) {
  if (A > UINT64_MAX - B)
    return imageError(What + " overflows a 64-bit size");
  return A + B;
}

} // namespace

Error DynCodeImage::ensureMutable() const {
  if (State != DynCodeImageState::Candidate)
    return imageError("image is no longer a mutable candidate");
  return Error::success();
}

Error DynCodeImage::setOutputAlignment(uint64_t Align) {
  if (Align == 0 || (Align & (Align - 1)))
    return imageError("output alignment must be a power of two");
  Alignment = Align;
  return Error::success();
}

Expected<ArrayRef<uint8_t>> DynCodeImage::read(uint64_t Offset,
                                               uint64_t Length) const {
  auto End = checkedAdd(Offset, Length, "read range");
  if (!End)
    return End.takeError();
  if (*End > Bytes.size())
    return imageError("read runs past the end of the image");
  return ArrayRef<uint8_t>(Bytes.data() + Offset, Length);
}

Error DynCodeImage::write(uint64_t Offset, ArrayRef<uint8_t> Data) {
  if (Error E = ensureMutable())
    return E;
  auto End = checkedAdd(Offset, Data.size(), "write range");
  if (!End)
    return End.takeError();
  if (*End > Bytes.size())
    return imageError("write runs past the end of the image");
  if (!Data.empty())
    std::memcpy(Bytes.data() + Offset, Data.data(), Data.size());
  return Error::success();
}

Error DynCodeImage::append(ArrayRef<uint8_t> Data) {
  if (Error E = ensureMutable())
    return E;
  auto NewSize = checkedAdd(Bytes.size(), Data.size(), "append");
  if (!NewSize)
    return NewSize.takeError();
  if (Budget && *NewSize > *Budget)
    return imageError("append exceeds the image size budget");
  Bytes.append(Data.begin(), Data.end());
  return Error::success();
}

Error DynCodeImage::insert(uint64_t Offset, ArrayRef<uint8_t> Data) {
  if (Error E = ensureMutable())
    return E;
  if (Offset > Bytes.size())
    return imageError("insert offset is past the end of the image");
  auto NewSize = checkedAdd(Bytes.size(), Data.size(), "insert");
  if (!NewSize)
    return NewSize.takeError();
  if (Budget && *NewSize > *Budget)
    return imageError("insert exceeds the image size budget");
  Bytes.insert(Bytes.begin() + Offset, Data.begin(), Data.end());
  return Error::success();
}

Error DynCodeImage::resize(uint64_t NewSize, uint8_t Fill) {
  if (Error E = ensureMutable())
    return E;
  if (Budget && NewSize > *Budget)
    return imageError("resize exceeds the image size budget");
  if (NewSize > SIZE_MAX)
    return imageError("resize exceeds addressable memory");
  Bytes.resize(static_cast<size_t>(NewSize), Fill);
  return Error::success();
}

Error DynCodeImage::replaceRange(uint64_t Offset, uint64_t Length,
                                 ArrayRef<uint8_t> Data) {
  if (Error E = ensureMutable())
    return E;
  auto End = checkedAdd(Offset, Length, "replace range");
  if (!End)
    return End.takeError();
  if (*End > Bytes.size())
    return imageError("replace range runs past the end of the image");
  // New size = size - Length + Data.size(), checked.
  uint64_t Remaining = Bytes.size() - Length;
  auto NewSize = checkedAdd(Remaining, Data.size(), "replace range result");
  if (!NewSize)
    return NewSize.takeError();
  if (Budget && *NewSize > *Budget)
    return imageError("replace range exceeds the image size budget");
  auto First = Bytes.begin() + Offset;
  Bytes.erase(First, First + Length);
  Bytes.insert(Bytes.begin() + Offset, Data.begin(), Data.end());
  return Error::success();
}

std::array<uint8_t, 32> DynCodeImage::digest() const {
  SHA256 Hash;
  Hash.update(ArrayRef<uint8_t>(Bytes.data(), Bytes.size()));
  return Hash.final();
}

Error DynCodeImage::markVerified() {
  if (State != DynCodeImageState::Candidate)
    return imageError("only a candidate image can be verified");
  State = DynCodeImageState::Verified;
  return Error::success();
}

Error DynCodeImage::markCommitted() {
  if (State != DynCodeImageState::Verified)
    return imageError("only a verified image can be committed");
  State = DynCodeImageState::Committed;
  return Error::success();
}

Error DynCodeImage::markAborted() {
  if (State == DynCodeImageState::Committed)
    return imageError("a committed image cannot be aborted");
  State = DynCodeImageState::Aborted;
  return Error::success();
}

} // namespace dyncode
} // namespace neverc
