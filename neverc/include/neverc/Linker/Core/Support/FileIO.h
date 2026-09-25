//===----------------------------------------------------------------------===//
//
//  FileIO — the output-side filesystem helpers every backend uses when
//  producing its final artefact.  `tryCreateFile` is the pre-flight check
//  for write permissions; `openFile` opens the output stream;
//  `unlinkAsync` removes an existing artefact off the hot path.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_SUPPORT_FILEIO_H
#define LINKER_CORE_SUPPORT_FILEIO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <system_error>
#include <thread>
#include <vector>

namespace llvm {
class FileOutputBuffer;
} // namespace llvm

namespace linker {

void prefaultBuffer(uint8_t *buf, size_t size, bool fileBacked);
/// With `keepUntilExit`, the old file's storage is released only when the
/// process exits, for a process that exits right after the link.
void unlinkAsync(llvm::StringRef path, bool keepUntilExit = false);
std::error_code tryCreateFile(llvm::StringRef path);
std::unique_ptr<llvm::raw_fd_ostream> openFile(llvm::StringRef file);

/// An output file created before its final size is known. Faulting in the
/// pages of a large output dominates writing it to memory-backed storage, so
/// that work starts early and runs on background threads that only use
/// otherwise idle CPUs, overlapping the rest of the link.
class EarlyOutputFile {
public:
  /// Creates the temporary file for `path` with `estimatedSize` bytes and
  /// starts faulting them in. Returns null where that is not supported, in
  /// which case the output is created as usual.
  static std::unique_ptr<EarlyOutputFile>
  start(llvm::StringRef path, size_t estimatedSize, bool executable,
        unsigned threads);
  ~EarlyOutputFile();

  /// Stops the background work and resizes the file to `size`, faulting in
  /// any pages not faulted in yet. The buffer commits and discards like one
  /// from FileOutputBuffer::create(). With `keepMappedAtCommit`, committing
  /// only renames the file and leaves the mapping to process exit, for a
  /// process that exits right after the link.
  llvm::Expected<std::unique_ptr<llvm::FileOutputBuffer>>
  finish(size_t size, bool keepMappedAtCommit = false);

private:
  struct State;
  explicit EarlyOutputFile(std::unique_ptr<State> state);
  void stopWorkers();
  std::unique_ptr<State> state;
};

} // namespace linker

#endif // LINKER_CORE_SUPPORT_FILEIO_H
