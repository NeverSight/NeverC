#ifndef NEVERC_INVOKE_INMEMORYFILESTORE_H
#define NEVERC_INVOKE_INMEMORYFILESTORE_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include <optional>
#include <shared_mutex>

namespace neverc {

/// Thread-safe in-memory file store for the integrated compiler+linker
/// pipeline.  The compiler writes LTO bitcode into memory buffers keyed
/// by their temp-file path, and the linker reads them back without
/// touching the filesystem.
///
/// StringMap allocates each entry on the heap, so value references
/// remain stable after insertion (safe for raw_svector_ostream).
/// std::shared_mutex allows concurrent lock-free reads during the
/// link phase when no more writes occur.
class InMemoryFileStore {
  mutable std::shared_mutex Mu;
  llvm::StringMap<llvm::SmallString<0>> Buffers;
  std::atomic<bool> Frozen{false};

public:
  static InMemoryFileStore &instance();

  /// Reserve a buffer for \p Path and return a mutable reference.
  /// The caller can wrap it in a raw_svector_ostream for writing.
  /// \p ReserveHint avoids reallocations when the approximate output
  /// size is known (e.g. bitcode for a typical C translation unit).
  llvm::SmallString<0> &create(llvm::StringRef Path, size_t ReserveHint = 0);

  /// Concurrent-safe lookup: returns the buffer if it exists and is
  /// non-empty.  After freeze(), skips locking entirely since no
  /// more writes can occur.
  std::optional<llvm::MemoryBufferRef> tryGet(llvm::StringRef Path) const;

  /// Signal that all writes are complete.  Subsequent tryGet() calls
  /// skip the shared_mutex entirely for zero-overhead reads.
  void freeze();

  /// Drop all stored buffers (called after linking is done).
  void clear();
};

} // namespace neverc

#endif // NEVERC_INVOKE_INMEMORYFILESTORE_H
