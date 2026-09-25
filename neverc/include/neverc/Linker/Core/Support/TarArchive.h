//===----------------------------------------------------------------------===//
//
//  TarArchive — writes a POSIX tar archive of a link's inputs, for
//  --reproduce.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_SUPPORT_TARARCHIVE_H
#define LINKER_CORE_SUPPORT_TARARCHIVE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <mutex>
#include <string>

namespace linker {

/// Appends files to a tar archive under a common base directory. Each path
/// is stored once; later appends of the same path are ignored. Safe to use
/// from several threads.
class TarArchive {
public:
  static llvm::Expected<std::unique_ptr<TarArchive>>
  create(llvm::StringRef archivePath, llvm::StringRef baseDir);

  void append(llvm::StringRef path, llvm::StringRef contents);

private:
  TarArchive(std::unique_ptr<llvm::raw_fd_ostream> os, llvm::StringRef baseDir)
      : os(std::move(os)), baseDir(baseDir) {}

  std::unique_ptr<llvm::raw_fd_ostream> os;
  std::string baseDir;
  llvm::StringSet<> seen;
  std::mutex mu;
};

/// Returns an absolute path with its root removed, so that an archive
/// member keeps the path's directories: "/usr/lib/x.a" -> "usr/lib/x.a".
std::string pathRelativeToRoot(llvm::StringRef path);

} // namespace linker

#endif
