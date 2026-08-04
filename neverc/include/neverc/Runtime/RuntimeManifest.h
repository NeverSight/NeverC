#ifndef NEVERC_RUNTIME_RUNTIMEMANIFEST_H
#define NEVERC_RUNTIME_RUNTIMEMANIFEST_H

#include "llvm/ADT/StringRef.h"

#include <map>
#include <optional>
#include <string>

namespace neverc {
namespace runtime {

/// Git release tag for this neverc build, e.g. "v3389.1.2".
std::string getCompilerReleaseTag();

/// Normalize a user-supplied release tag. Accepts "3389.1.2" or "v3389.1.2".
/// Returns "latest" only when \p Tag is literally "latest".
std::string normalizeReleaseTag(llvm::StringRef Tag);

/// Resolve the release tag to download.
/// When \p UserVersion is empty, returns the compiler tag unless \p DefaultToLatest.
std::string resolveFetchReleaseTag(llvm::StringRef UserVersion,
                                   bool DefaultToLatest);

/// Read the recorded release tag for \p TargetName, if any.
std::optional<std::string>
getInstalledReleaseTag(llvm::StringRef RuntimeDir, llvm::StringRef TargetName);

/// Return every target recorded in the manifest.
std::map<std::string, std::string>
readInstalledTargetVersions(llvm::StringRef RuntimeDir);

/// Record or update \p TargetName at \p ReleaseTag in runtime/manifest.json.
bool recordInstalledTarget(llvm::StringRef RuntimeDir,
                           llvm::StringRef TargetName,
                           llvm::StringRef ReleaseTag);

/// Remove \p TargetName from runtime/manifest.json.
bool removeInstalledTarget(llvm::StringRef RuntimeDir,
                           llvm::StringRef TargetName);

} // namespace runtime
} // namespace neverc

#endif // NEVERC_RUNTIME_RUNTIMEMANIFEST_H
