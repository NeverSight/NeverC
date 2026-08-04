#ifndef NEVERC_RELEASE_RELEASECLIENT_H
#define NEVERC_RELEASE_RELEASECLIENT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <string>

namespace neverc {
namespace release {

/// Description of the compiler archive published for the current host.
struct HostDistribution {
  std::string Platform;
  std::string CompilerAsset;
  /// Optional directory wrapped around the install tree inside the ZIP.
  std::string ArchiveRoot;
  /// Executable path relative to ArchiveRoot.
  std::string ExecutableRelativePath;
};

/// Normalize `MAJOR.MINOR.PATCH` or `vMAJOR.MINOR.PATCH` to a release tag.
/// Returns "latest" only when the input is literally "latest" and an empty
/// string for every invalid input.
std::string normalizeReleaseTag(llvm::StringRef Tag);

/// Return the compiler release asset and ZIP layout for this process host.
llvm::Expected<HostDistribution> getHostDistribution();

/// Return the newest published, non-prerelease tag containing AssetName.
llvm::Expected<std::string>
queryLatestReleaseTagForAsset(llvm::StringRef AssetName);

/// Download one asset from a concrete release tag to Destination.
llvm::Error downloadReleaseAsset(llvm::StringRef ReleaseTag,
                                 llvm::StringRef AssetName,
                                 llvm::StringRef Destination);

/// Find and validate AssetName's lowercase SHA256 in a SHA256SUMS body.
llvm::Expected<std::string> parseChecksumManifest(llvm::StringRef Contents,
                                                  llvm::StringRef AssetName);

/// Verify ArchivePath against AssetName's entry in ManifestPath.
llvm::Error verifyReleaseAsset(llvm::StringRef ArchivePath,
                               llvm::StringRef ManifestPath,
                               llvm::StringRef AssetName);

/// Extract a ZIP using the platform's supported archive utility.
llvm::Error extractZip(llvm::StringRef ArchivePath,
                       llvm::StringRef DestinationDirectory);

/// Resolve `<root>` from an executable located at `<root>/bin/neverc[.exe]`.
llvm::Expected<std::string> resolveInstallRoot(const char *Argv0,
                                               void *MainAddress);

} // namespace release
} // namespace neverc

#endif // NEVERC_RELEASE_RELEASECLIENT_H
