#ifndef NEVERC_PLUGIN_LINK_ANDROIDKERNELRELEASEINPUTVERIFIER_H
#define NEVERC_PLUGIN_LINK_ANDROIDKERNELRELEASEINPUTVERIFIER_H

#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace neverc::plugin {

class BuiltinAndroidKernelReleaseNativeOutputAttestor;

struct AndroidKernelReleaseELFABI {
  uint16_t Machine = 0;
  uint32_t Flags = 0;
  uint8_t OSABI = 0;
  uint8_t ABIVersion = 0;

  bool operator==(const AndroidKernelReleaseELFABI &Other) const {
    return Machine == Other.Machine && Flags == Other.Flags &&
           OSABI == Other.OSABI && ABIVersion == Other.ABIVersion;
  }
  bool operator!=(const AndroidKernelReleaseELFABI &Other) const {
    return !(*this == Other);
  }
};

/// One retained, anonymously named native ELF section. The ObjectGraph reader
/// must invent a non-empty `$section.N` placeholder for this section, so the
/// complete native fact is retained here as an order-independent multiset
/// member. The digest covers the exact file-backed payload; type and size keep
/// SHT_NOBITS distinct without materializing its zero fill.
struct AndroidKernelReleaseAnonymousSectionFact {
  uint32_t Type = 0;
  uint64_t Flags = 0;
  uint64_t Size = 0;
  uint64_t Alignment = 0;
  uint64_t EntrySize = 0;
  uint32_t Link = 0;
  uint32_t Info = 0;
  std::array<uint8_t, 32> PayloadDigest{};

  bool operator==(const AndroidKernelReleaseAnonymousSectionFact &Other) const;
  bool operator<(const AndroidKernelReleaseAnonymousSectionFact &Other) const;
};

/// Audited native facts that ObjectGraph deliberately cannot represent. This
/// is an unbound input contract: only the immutable-input verifier can create
/// one, and no public fields let a caller forge the result of that audit.
class AndroidKernelReleaseInputContract {
public:
  AndroidKernelReleaseInputContract(const AndroidKernelReleaseInputContract &) =
      default;
  AndroidKernelReleaseInputContract(
      AndroidKernelReleaseInputContract &&) noexcept = default;
  AndroidKernelReleaseInputContract &
  operator=(const AndroidKernelReleaseInputContract &) = default;
  AndroidKernelReleaseInputContract &
  operator=(AndroidKernelReleaseInputContract &&) noexcept = default;

  const AndroidKernelReleaseELFABI &abi() const { return ABI; }
  bool hasRetainedAnonymousSymbols() const {
    return HasRetainedAnonymousSymbols;
  }
  bool hasRetainedAnonymousSections() const {
    return !RetainedAnonymousSections.empty();
  }
  llvm::ArrayRef<AndroidKernelReleaseAnonymousSectionFact>
  retainedAnonymousSections() const {
    return RetainedAnonymousSections;
  }
  bool requiresNativeImagePassthrough() const {
    return ABI.Flags != 0 || ABI.OSABI != 0 || ABI.ABIVersion != 0 ||
           HasRetainedAnonymousSymbols || hasRetainedAnonymousSections();
  }
  bool
  hasSameNativeFacts(const AndroidKernelReleaseInputContract &Other) const {
    return ABI == Other.ABI &&
           HasRetainedAnonymousSymbols == Other.HasRetainedAnonymousSymbols &&
           RetainedAnonymousSections == Other.RetainedAnonymousSections;
  }

private:
  AndroidKernelReleaseInputContract(
      AndroidKernelReleaseELFABI ABIValue,
      bool HasRetainedAnonymousSymbolsValue,
      std::vector<AndroidKernelReleaseAnonymousSectionFact>
          RetainedAnonymousSectionsValue)
      : ABI(ABIValue),
        HasRetainedAnonymousSymbols(HasRetainedAnonymousSymbolsValue),
        RetainedAnonymousSections(std::move(RetainedAnonymousSectionsValue)) {}

  friend llvm::Expected<AndroidKernelReleaseInputContract>
  verifyAndroidKernelReleaseObjectMergeInputs(
      llvm::ArrayRef<PluginObjectGraph *> Objects,
      llvm::ArrayRef<llvm::ArrayRef<uint8_t>> InputImages,
      NevercTargetKey Target, llvm::StringRef Boundary);

  AndroidKernelReleaseELFABI ABI;
  bool HasRetainedAnonymousSymbols = false;
  std::vector<AndroidKernelReleaseAnonymousSectionFact>
      RetainedAnonymousSections;
};

/// Unforgeable authority held only by the direct built-in byte-merger adapter.
/// A caller may inspect or share ownership of an existing bound token, but
/// cannot mint one from arbitrary bytes without this capability.
class AndroidKernelReleaseNativeOutputBindingAuthority {
public:
  ~AndroidKernelReleaseNativeOutputBindingAuthority() = default;
  AndroidKernelReleaseNativeOutputBindingAuthority(
      const AndroidKernelReleaseNativeOutputBindingAuthority &) = delete;
  AndroidKernelReleaseNativeOutputBindingAuthority &
  operator=(const AndroidKernelReleaseNativeOutputBindingAuthority &) = delete;

private:
  AndroidKernelReleaseNativeOutputBindingAuthority() = default;

  friend class BuiltinAndroidKernelReleaseNativeOutputAttestor;
};

/// A trusted native merger's complete output bound to one audited input
/// contract. Only the direct built-in adapter's private binding authority can
/// create this non-copyable type; callers can only propagate the exact shared
/// attestation identity issued by that adapter.
class AndroidKernelReleaseBoundOutputContract {
public:
  ~AndroidKernelReleaseBoundOutputContract() = default;
  AndroidKernelReleaseBoundOutputContract(
      const AndroidKernelReleaseBoundOutputContract &) = delete;
  AndroidKernelReleaseBoundOutputContract(
      AndroidKernelReleaseBoundOutputContract &&) noexcept = delete;
  AndroidKernelReleaseBoundOutputContract &
  operator=(const AndroidKernelReleaseBoundOutputContract &) = delete;
  AndroidKernelReleaseBoundOutputContract &
  operator=(AndroidKernelReleaseBoundOutputContract &&) noexcept = delete;

  const AndroidKernelReleaseInputContract &inputContract() const {
    return InputContract;
  }
  const std::array<uint8_t, 32> &nativeOutputDigest() const {
    return NativeOutputDigest;
  }
  llvm::ArrayRef<AndroidKernelReleaseAnonymousSectionFact>
  outputAnonymousSections() const {
    return OutputAnonymousSections;
  }
  bool requiresNativeImagePassthrough() const {
    return InputContract.requiresNativeImagePassthrough();
  }
  bool
  matchesInputContract(const AndroidKernelReleaseInputContract &Other) const {
    return InputContract.hasSameNativeFacts(Other);
  }

private:
  AndroidKernelReleaseBoundOutputContract(
      AndroidKernelReleaseInputContract InputContractValue,
      std::array<uint8_t, 32> NativeOutputDigestValue,
      std::vector<AndroidKernelReleaseAnonymousSectionFact>
          OutputAnonymousSectionsValue)
      : InputContract(std::move(InputContractValue)),
        NativeOutputDigest(NativeOutputDigestValue),
        OutputAnonymousSections(std::move(OutputAnonymousSectionsValue)) {}

  friend llvm::Expected<
      std::shared_ptr<const AndroidKernelReleaseBoundOutputContract>>
  bindAndroidKernelReleaseNativeOutput(
      llvm::ArrayRef<uint8_t> Image,
      const AndroidKernelReleaseInputContract &Contract,
      const AndroidKernelReleaseNativeOutputBindingAuthority &Authority,
      llvm::StringRef Boundary);

  AndroidKernelReleaseInputContract InputContract;
  std::array<uint8_t, 32> NativeOutputDigest{};
  std::vector<AndroidKernelReleaseAnonymousSectionFact> OutputAnonymousSections;
};

/// Audits the immutable native bytes that back every ObjectGraph before an
/// Android release merge is dispatched to either a built-in or third-party
/// provider. ObjectGraph intentionally normalizes several ELF-only facts, so
/// this boundary is the last place that can reject them without trusting a
/// provider or a lossy graph rewrite.
llvm::Expected<AndroidKernelReleaseInputContract>
verifyAndroidKernelReleaseObjectMergeInputs(
    llvm::ArrayRef<PluginObjectGraph *> Objects,
    llvm::ArrayRef<llvm::ArrayRef<uint8_t>> InputImages, NevercTargetKey Target,
    llvm::StringRef Boundary);

/// Capability-gated binding used only by the direct built-in merger adapter.
/// Input anonymous-section contributions may be legally folded by `-r`, so
/// their multiset cannot itself be the final-image oracle. The bound contract
/// stores the exact post-merge multiset and whole-image digest before any
/// replaceable ObjectGraph/output phase can run.
llvm::Expected<std::shared_ptr<const AndroidKernelReleaseBoundOutputContract>>
bindAndroidKernelReleaseNativeOutput(
    llvm::ArrayRef<uint8_t> Image,
    const AndroidKernelReleaseInputContract &Contract,
    const AndroidKernelReleaseNativeOutputBindingAuthority &Authority,
    llvm::StringRef Boundary);

/// Verifies the ABI header and native-only metadata on a final image against
/// the immutable input contract. This is intentionally separate from the
/// canonical module verifier so provider selection cannot weaken provenance.
llvm::Error verifyAndroidKernelReleaseOutputContract(
    llvm::ArrayRef<uint8_t> Image,
    const AndroidKernelReleaseInputContract &Contract,
    llvm::StringRef Boundary);

/// Verifies a final image byte-for-byte against a trusted native merger output
/// token. Unlike the unbound overload, this accepts native-only provenance.
llvm::Error verifyAndroidKernelReleaseOutputContract(
    llvm::ArrayRef<uint8_t> Image,
    const AndroidKernelReleaseBoundOutputContract &Contract,
    llvm::StringRef Boundary);

/// Validates the ELF64 relocation symbol-count boundary without requiring a
/// multi-gigabyte test image. Every real input takes this same check before any
/// symbol index is narrowed to uint32_t.
llvm::Error verifyAndroidKernelReleaseSymbolCount(uint64_t SymbolCount,
                                                  llvm::StringRef Boundary);

} // namespace neverc::plugin

#endif // NEVERC_PLUGIN_LINK_ANDROIDKERNELRELEASEINPUTVERIFIER_H
