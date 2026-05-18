//===----------------------------------------------------------------------===//
//
//  Chunks — `splitIntoChunks`: slice a flat `ArrayRef<uint8_t>` into a
//  series of fixed-size windows.  Consumed by the section / output
//  writers that chop a payload into independently hashable pieces before
//  handing the work off to `parallelFor`.
//
//  Header-only because the entire body is a short inline function.  The
//  function sits in `linker::` (not a sub-namespace) so the call sites
//  that already `using namespace linker;` keep the short form, while the
//  new name makes it clear the argument is a byte buffer rather than
//  e.g. a `StringRef`.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_SUPPORT_CHUNKS_H
#define LINKER_CORE_SUPPORT_CHUNKS_H

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <vector>

namespace linker {

/// Slice `arr` into contiguous `chunkSize`-byte windows; the tail that
/// cannot reach `chunkSize` bytes is emitted as the last element.  An
/// empty input yields an empty vector.
inline std::vector<llvm::ArrayRef<uint8_t>>
splitIntoChunks(llvm::ArrayRef<uint8_t> arr, size_t chunkSize) {
  std::vector<llvm::ArrayRef<uint8_t>> out;
  while (arr.size() > chunkSize) {
    out.push_back(arr.take_front(chunkSize));
    arr = arr.drop_front(chunkSize);
  }
  if (!arr.empty())
    out.push_back(arr);
  return out;
}

} // namespace linker

#endif // LINKER_CORE_SUPPORT_CHUNKS_H
