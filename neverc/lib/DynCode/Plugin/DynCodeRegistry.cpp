#include "DynCodeRegistry.h"

#include "llvm/ADT/StringRef.h"

#include <algorithm>

namespace neverc {
namespace dyncode {

namespace {

// The eight first-version built-in dyncode triples: macOS, Linux, Android and
// Windows, each in x86_64 and AArch64. iOS is covered at the source/object
// layer only and is intentionally not a built-in dyncode target here.
constexpr const char *BuiltinTriples[] = {
    "x86_64-apple-macosx",         "aarch64-apple-macosx",
    "x86_64-unknown-linux-gnu",    "aarch64-unknown-linux-gnu",
    "x86_64-unknown-linux-android29", "aarch64-unknown-linux-android29",
    "x86_64-pc-windows-msvc",      "aarch64-pc-windows-msvc",
};

bool validSchemaDigest(NevercStringView Digest) {
  if (Digest.Length != 64 || Digest.Data == nullptr)
    return false;
  for (uint64_t I = 0; I < Digest.Length; ++I) {
    const char C = Digest.Data[I];
    const bool Hex = (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f');
    if (!Hex)
      return false;
  }
  return true;
}

} // namespace

llvm::Error DynCodeRegistry::registerBuiltinTargets() {
  for (const char *Triple : BuiltinTriples) {
    llvm::Expected<OwnedDynCodeTargetDescriptor> DescOr =
        buildBuiltinDynCodeTarget(Triple, NEVERC_DYNCODE_LEVEL_USER);
    if (!DescOr)
      return DescOr.takeError();
    if (llvm::Error E = registerTarget(std::move(*DescOr)))
      return E;
  }
  return llvm::Error::success();
}

llvm::Error DynCodeRegistry::registerTarget(OwnedDynCodeTargetDescriptor Desc) {
  if (!idNonzero(Desc.DynCodeTargetID))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "dyncode target ID must be non-zero");
  if (!idNonzero(Desc.ObjectFormat))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "dyncode target object format must be "
                                   "non-zero");
  if (!idNonzero(Desc.UnderlyingTargetID))
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "dyncode target TargetKey ID must be "
                                   "non-zero");
  if (!validSchemaDigest(Desc.Target.view().SchemaDigest))
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "dyncode target schema digest is not a 64-character hex string");

  for (const OwnedDynCodeTargetDescriptor &Existing : Targets) {
    if (idEqual(Existing.DynCodeTargetID, Desc.DynCodeTargetID))
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "duplicate dyncode target ID registration");
    if (targetIDEqual(Existing.UnderlyingTargetID, Desc.UnderlyingTargetID) &&
        idEqual(Existing.ObjectFormat, Desc.ObjectFormat))
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "conflicting dyncode target: another descriptor already claims this "
          "(TargetKey, object format) pair");
  }

  Targets.push_back(std::move(Desc));
  return llvm::Error::success();
}

const OwnedDynCodeTargetDescriptor *
DynCodeRegistry::findTargetByDynCodeID(NevercDynCodeTargetID ID) const {
  for (const OwnedDynCodeTargetDescriptor &Desc : Targets)
    if (idEqual(Desc.DynCodeTargetID, ID))
      return &Desc;
  return nullptr;
}

const OwnedDynCodeTargetDescriptor *
DynCodeRegistry::findTargetByKey(NevercTargetID TargetID,
                                 NevercObjectFormatID Format) const {
  for (const OwnedDynCodeTargetDescriptor &Desc : Targets)
    if (targetIDEqual(Desc.UnderlyingTargetID, TargetID) &&
        idEqual(Desc.ObjectFormat, Format))
      return &Desc;
  return nullptr;
}

} // namespace dyncode
} // namespace neverc
