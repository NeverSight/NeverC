#ifndef LINKER_COFF_EMIT_PECHECKSUM_H
#define LINKER_COFF_EMIT_PECHECKSUM_H

#include "Linker/Core/Runtime/ContentHashWorkers.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Endian.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace linker::coff::detail {

inline size_t peChecksumChunkEnd(size_t ImageSize, size_t Begin,
                                 size_t ChunkBytes) {
  assert(Begin <= ImageSize && "PE checksum chunk starts past the image");
  return Begin + std::min(ChunkBytes, ImageSize - Begin);
}

/// Compute the PE optional-header checksum while treating the four-byte
/// checksum field itself as zero. ExplicitlySerial preserves a requested
/// one-thread link and is also suitable for callers outside a linker session.
inline uint32_t computePEChecksum(llvm::ArrayRef<uint8_t> Image,
                                  size_t ChecksumOffset,
                                  bool ExplicitlySerial) {
  assert(ChecksumOffset <= Image.size() && Image.size() - ChecksumOffset >= 4 &&
         "PE checksum field is outside the image");
  assert((ChecksumOffset & 1) == 0 && "PE checksum field must be word-aligned");
  assert(Image.size() <= std::numeric_limits<uint32_t>::max() &&
         "PE images cannot encode a checksum length above 4 GiB");

  constexpr size_t ChunkBytes = 1024 * 1024;
  const size_t ChunkCount = std::max<size_t>(
      1, Image.size() / ChunkBytes + (Image.size() % ChunkBytes != 0));
  std::vector<uint64_t> PartialSums(ChunkCount);

  linker::detail::runContentHashChunks(
      Image.size(), ChunkCount, ExplicitlySerial, [&](size_t ChunkIndex) {
        const size_t Begin = ChunkIndex * ChunkBytes;
        const size_t End = peChecksumChunkEnd(Image.size(), Begin, ChunkBytes);
        uint64_t Sum = 0;
        for (size_t I = Begin; I + 1 < End; I += 2) {
          if (I >= ChecksumOffset && I < ChecksumOffset + 4)
            continue;
          Sum += llvm::support::endian::read16le(Image.data() + I);
        }
        if (End == Image.size() && (Image.size() & 1) != 0)
          Sum += Image.back();
        PartialSums[ChunkIndex] = Sum;
      });

  uint64_t Sum = 0;
  for (uint64_t Partial : PartialSums)
    Sum += Partial;
  while (Sum >> 16)
    Sum = (Sum & 0xffff) + (Sum >> 16);
  return static_cast<uint32_t>(Sum) + static_cast<uint32_t>(Image.size());
}

} // namespace linker::coff::detail

#endif // LINKER_COFF_EMIT_PECHECKSUM_H
