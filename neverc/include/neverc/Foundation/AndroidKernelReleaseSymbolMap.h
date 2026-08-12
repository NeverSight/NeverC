#ifndef NEVERC_FOUNDATION_ANDROIDKERNELRELEASESYMBOLMAP_H
#define NEVERC_FOUNDATION_ANDROIDKERNELRELEASESYMBOLMAP_H

#include "neverc/Foundation/Core/OutputBundleTransaction.h"
#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>
#include <string>

namespace neverc {

/// One retained Android-kernel release symbol whose serialized name changed.
/// Exact loader/import names and symbols pruned from the final image are not
/// included because they need no crash-log translation.
struct AndroidKernelReleaseSymbolMapEntry {
  std::string OriginalName;
  std::string ReleaseName;
};

/// Sidecar data bound to one final `.ko` image.
struct AndroidKernelReleaseSymbolMap {
  std::array<uint8_t, 32> ImageSHA256{};
  llvm::SmallVector<AndroidKernelReleaseSymbolMapEntry, 64> Symbols;

  void clear() {
    ImageSHA256.fill(0);
    Symbols.clear();
  }
};

/// Returns the automatic sidecar path for \p ImagePath.
std::string androidKernelReleaseSymbolMapPath(llvm::StringRef ImagePath);

/// Returns the integrity record for the current image and adjacent build-state
/// files. Missing image/build-id files produce empty digest fields; a missing
/// optional build-extra file is represented by `-`.
llvm::Expected<std::string>
currentAndroidKernelBuildIntegrity(llvm::StringRef ImagePath);

/// Serializes a deterministic, versioned JSON sidecar. Entries are ordered by
/// release name so a crash-log spelling can be searched directly. Original
/// ELF names that are not UTF-8 are represented losslessly as Base64.
llvm::Expected<std::string>
serializeAndroidKernelReleaseSymbolMap(
    const AndroidKernelReleaseSymbolMap &Map);

/// Publishes one final image together with its release map. When \p Map is
/// null, the same transaction publishes the image and removes any stale map.
/// Android-kernel example builds may export `NEVERC_ANDROID_KERNEL_BUILD_ID`
/// and `NEVERC_ANDROID_KERNEL_BUILD_EXTRA`; their adjacent state files then
/// participate in this transaction instead of being written by make recipes.
/// Stream output is rejected because it cannot own a colocated sidecar.
/// When provided, \p FinalSummary receives the transaction's terminal state
/// even if publication returns a late durability or recovery error.
/// \p IsCancelled makes waits for an overlapping publisher interruptible.
llvm::Expected<OutputBundleSummary>
publishAndroidKernelReleaseOutput(
    OutputCoordinator &Coordinator, llvm::StringRef ImagePath,
    llvm::ArrayRef<uint8_t> Image,
    const AndroidKernelReleaseSymbolMap *Map,
    OutputLeaseOwner LeaseOwner = {},
    OutputBundleSummary *FinalSummary = nullptr,
    OutputCoordinator::CancellationCheck IsCancelled = {});

/// Removes an Android-kernel image and every adjacent release/build-state
/// sidecar under the same cross-process publication lock used by publish.
llvm::Expected<OutputBundleSummary>
cleanAndroidKernelReleaseOutput(
    OutputCoordinator &Coordinator, llvm::StringRef ImagePath,
    OutputLeaseOwner LeaseOwner = {},
    OutputBundleSummary *FinalSummary = nullptr,
    OutputCoordinator::CancellationCheck IsCancelled = {});

} // namespace neverc

#endif // NEVERC_FOUNDATION_ANDROIDKERNELRELEASESYMBOLMAP_H
