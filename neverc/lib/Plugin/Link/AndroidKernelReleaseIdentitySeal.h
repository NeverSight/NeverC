#ifndef NEVERC_PLUGIN_LINK_ANDROIDKERNELRELEASEIDENTITYSEAL_H
#define NEVERC_PLUGIN_LINK_ANDROIDKERNELRELEASEIDENTITYSEAL_H

#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace neverc::plugin {

enum class AndroidKernelSymbolNameState : uint8_t;

/// Immutable graph-owner identities captured after release finalization and
/// before any replaceable ObjectGraph phase. This is also the graph half of
/// the ReleaseLayoutIdentitySeal: every retained logical section is bound to
/// its stable entity ID, final ordinal, and exact name. Exact-name and mapped
/// canonical symbols are bound to their owners so a plugin cannot invalidate
/// the release symbol map while preserving a sorted multiset.
class AndroidKernelReleaseGraphIdentitySeal {
public:
  AndroidKernelReleaseGraphIdentitySeal(
      const AndroidKernelReleaseGraphIdentitySeal &) = default;
  AndroidKernelReleaseGraphIdentitySeal(
      AndroidKernelReleaseGraphIdentitySeal &&) noexcept = default;
  AndroidKernelReleaseGraphIdentitySeal &
  operator=(const AndroidKernelReleaseGraphIdentitySeal &) = default;
  AndroidKernelReleaseGraphIdentitySeal &
  operator=(AndroidKernelReleaseGraphIdentitySeal &&) noexcept = default;

private:
  struct Impl;

  explicit AndroidKernelReleaseGraphIdentitySeal(
      std::shared_ptr<const Impl> StateValue)
      : State(std::move(StateValue)) {}

  friend llvm::Expected<AndroidKernelReleaseGraphIdentitySeal>
  captureAndroidKernelReleaseGraphIdentitySeal(
      const PluginObjectGraph &Object, AndroidKernelSymbolNameState NameState,
      llvm::StringRef Boundary);
  friend llvm::Error verifyAndroidKernelReleaseGraphIdentitySeal(
      const PluginObjectGraph &Object, AndroidKernelSymbolNameState NameState,
      const AndroidKernelReleaseGraphIdentitySeal &Seal,
      llvm::StringRef Boundary);

  std::shared_ptr<const Impl> State;
};

/// Immutable serialized-owner identities bound immediately after the trusted
/// write boundary and before the entire replaceable post-write phase. Exact
/// names and mapped canonical names are tied to raw symbol-table slots, while
/// the image half of the ReleaseLayoutIdentitySeal ties every retained logical
/// section ordinal to its exact name. This prevents later phases from silently
/// invalidating the release symbol map.
class AndroidKernelReleaseImageIdentitySeal {
public:
  AndroidKernelReleaseImageIdentitySeal(
      const AndroidKernelReleaseImageIdentitySeal &) = default;
  AndroidKernelReleaseImageIdentitySeal(
      AndroidKernelReleaseImageIdentitySeal &&) noexcept = default;
  AndroidKernelReleaseImageIdentitySeal &
  operator=(const AndroidKernelReleaseImageIdentitySeal &) = default;
  AndroidKernelReleaseImageIdentitySeal &
  operator=(AndroidKernelReleaseImageIdentitySeal &&) noexcept = default;

private:
  struct Impl;

  explicit AndroidKernelReleaseImageIdentitySeal(
      std::shared_ptr<const Impl> StateValue)
      : State(std::move(StateValue)) {}

  friend llvm::Expected<AndroidKernelReleaseImageIdentitySeal>
  captureAndroidKernelReleaseImageIdentitySeal(llvm::ArrayRef<uint8_t> Image,
                                               llvm::StringRef Boundary);
  friend llvm::Error verifyAndroidKernelReleaseImageIdentitySeal(
      llvm::ArrayRef<uint8_t> Image,
      const AndroidKernelReleaseImageIdentitySeal &Seal,
      llvm::StringRef Boundary);

  std::shared_ptr<const Impl> State;
};

/// Captures the finalized graph projection. Portable graphs must be exactly
/// round-trippable by the release writer; canonical graphs use reader-owned
/// native facts. Plugins cannot mint CanonicalRelease provenance.
llvm::Expected<AndroidKernelReleaseGraphIdentitySeal>
captureAndroidKernelReleaseGraphIdentitySeal(
    const PluginObjectGraph &Object, AndroidKernelSymbolNameState NameState,
    llvm::StringRef Boundary);

/// Captures the independently serialized AArch64 ELF release projection.
llvm::Expected<AndroidKernelReleaseImageIdentitySeal>
captureAndroidKernelReleaseImageIdentitySeal(llvm::ArrayRef<uint8_t> Image,
                                             llvm::StringRef Boundary);

llvm::Error verifyAndroidKernelReleaseGraphIdentitySeal(
    const PluginObjectGraph &Object, AndroidKernelSymbolNameState NameState,
    const AndroidKernelReleaseGraphIdentitySeal &Seal,
    llvm::StringRef Boundary);

llvm::Error verifyAndroidKernelReleaseImageIdentitySeal(
    llvm::ArrayRef<uint8_t> Image,
    const AndroidKernelReleaseImageIdentitySeal &Seal,
    llvm::StringRef Boundary);

} // namespace neverc::plugin

#endif // NEVERC_PLUGIN_LINK_ANDROIDKERNELRELEASEIDENTITYSEAL_H
