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
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <system_error>

namespace linker {

void prefaultBuffer(uint8_t *buf, size_t size);
void unlinkAsync(llvm::StringRef path);
std::error_code tryCreateFile(llvm::StringRef path);
std::unique_ptr<llvm::raw_fd_ostream> openFile(llvm::StringRef file);

} // namespace linker

#endif // LINKER_CORE_SUPPORT_FILEIO_H
