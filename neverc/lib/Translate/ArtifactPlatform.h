#ifndef NEVERC_TRANSLATE_ARTIFACTPLATFORM_H
#define NEVERC_TRANSLATE_ARTIFACTPLATFORM_H
#include "llvm/ADT/StringRef.h"
#include <memory>
#include <system_error>
namespace llvm {
template <typename T> class SmallVectorImpl;
}
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

/// Resolve an existing absolute path, consuming Windows components in order so
/// a directory link is resolved before a subsequent '..'. The caller supplies
/// the base for relative inputs. Errors clear Result; Path may alias Result.
/// Windows limits input to 128 KiB of UTF-8 and 32768 suffix components, excluding
/// the drive or server/share root. Repeated separators do not count; a trailing
/// separator after the root counts as one implicit '.'. Exact \\?\ drive/UNC
/// paths and the existing //?/ alias retain whole native semantics. Windows
/// reads metadata through a zero-access handle, without enabling privileges;
/// native ACL/SMB query errors propagate. Widened input and final names allow
/// at most 32767 wchar_t units plus NUL, with one final-name buffer resize retry.
/// Resolved UTF-8 output must be lossless and is limited to 128 KiB.
std::error_code resolveExistingPath(llvm::StringRef Path,
                                    llvm::SmallVectorImpl<char> &Result);

/// Atomic directory rename that refuses an existing destination.
std::error_code renameDirectoryExclusive(llvm::StringRef From,
                                         llvm::StringRef To);
} // namespace neverc::translate
#endif
