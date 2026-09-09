#ifndef NEVERC_TRANSLATE_ARTIFACTPLATFORM_H
#define NEVERC_TRANSLATE_ARTIFACTPLATFORM_H
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <system_error>
namespace neverc::translate {
/// File-object identity used only by artifact publication and rollback.
/// On Windows the original handle stays open so an ID cannot be reused after
/// another publisher replaces the destination. This does not make a later
/// compare-then-remove by pathname atomic with respect to replacement.
class ArtifactFileIdentity {
public:
  ArtifactFileIdentity();
  ~ArtifactFileIdentity();
  ArtifactFileIdentity(const ArtifactFileIdentity &) = delete;
  ArtifactFileIdentity &operator=(const ArtifactFileIdentity &) = delete;
  ArtifactFileIdentity(ArtifactFileIdentity &&) noexcept;
  ArtifactFileIdentity &operator=(ArtifactFileIdentity &&) noexcept;

  /// Capture a regular file without following its final symlink/reparse point.
  /// Failure preserves Result, and never falls back to a pathname identity.
  static std::error_code capture(llvm::StringRef Path,
                                ArtifactFileIdentity &Result);
  /// A non-regular or distinct object returns success with Same=false. Missing
  /// paths and metadata-query failures return an error; an empty token is invalid.
  std::error_code matches(llvm::StringRef Path, bool &Same) const;

private:
  struct State;
  std::unique_ptr<State> Value;
};

/// Atomic directory rename that refuses an existing destination.
std::error_code renameDirectoryExclusive(llvm::StringRef From,
                                         llvm::StringRef To);
} // namespace neverc::translate
#endif
