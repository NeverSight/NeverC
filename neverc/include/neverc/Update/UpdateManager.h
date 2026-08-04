#ifndef NEVERC_UPDATE_UPDATEMANAGER_H
#define NEVERC_UPDATE_UPDATEMANAGER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <string>
#include <vector>

namespace neverc {
namespace update {

/// Relationship of the target release to the current release.
enum class VersionRelation { Older, Same, Newer };

llvm::Expected<VersionRelation> compareReleaseTags(llvm::StringRef CurrentTag,
                                                   llvm::StringRef TargetTag);

/// Enumerate files under bin/, lib/, and pluginsdk/ in an extracted compiler
/// install tree. runtime/ is intentionally ignored, including in Windows
/// release archives that bundle it.
llvm::Expected<std::vector<std::string>>
collectCompilerInstallFiles(llvm::StringRef InstallTreeRoot);

int runUpdate(int Argc, const char **Argv, const char *Argv0);

/// Private post-exit update helper used on Windows.
int runUpdateHelper(int Argc, const char **Argv, const char *Argv0);

} // namespace update
} // namespace neverc

#endif // NEVERC_UPDATE_UPDATEMANAGER_H
