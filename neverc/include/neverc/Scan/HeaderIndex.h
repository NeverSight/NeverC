#ifndef NEVERC_LEX_HEADERINDEX_H
#define NEVERC_LEX_HEADERINDEX_H

#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Scan/HeaderIndexTypes.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>
#include <optional>

namespace neverc {

struct HMapBucket;
struct HMapHeader;

class HeaderIndexImpl {
  std::unique_ptr<const llvm::MemoryBuffer> FileBuffer;
  bool NeedsBSwap;
  mutable llvm::StringMap<llvm::StringRef> ReverseMap;

public:
  HeaderIndexImpl(std::unique_ptr<const llvm::MemoryBuffer> File,
                  bool NeedsBSwap)
      : FileBuffer(std::move(File)), NeedsBSwap(NeedsBSwap) {}

  // Check for a valid header and extract the byte swap.
  static bool checkHeader(const llvm::MemoryBuffer &File, bool &NeedsByteSwap);

  // Make a call for every Key in the map.
  template <typename Func> void forEachKey(Func Callback) const {
    const HMapHeader &Hdr = getHeader();
    unsigned NumBuckets = getEndianAdjustedWord(Hdr.NumBuckets);

    for (unsigned Bucket = 0; Bucket < NumBuckets; ++Bucket) {
      HMapBucket B = getBucket(Bucket);
      if (B.Key != HMAP_EmptyBucketKey)
        if (std::optional<llvm::StringRef> Key = getString(B.Key))
          Callback(*Key);
    }
  }

  llvm::StringRef lookupFilename(llvm::StringRef Filename,
                                 llvm::SmallVectorImpl<char> &DestPath) const;

  llvm::StringRef getFileName() const;

  void dump() const;

  llvm::StringRef reverseLookupFilename(llvm::StringRef DestPath) const;

private:
  unsigned getEndianAdjustedWord(unsigned X) const;
  const HMapHeader &getHeader() const;
  HMapBucket getBucket(unsigned BucketNo) const;

  std::optional<llvm::StringRef> getString(unsigned StrTabIdx) const;
};

class HeaderIndex : private HeaderIndexImpl {
  HeaderIndex(std::unique_ptr<const llvm::MemoryBuffer> File, bool BSwap)
      : HeaderIndexImpl(std::move(File), BSwap) {}

public:
  static std::unique_ptr<HeaderIndex> Create(FileEntryRef FE, FileManager &FM);

  using HeaderIndexImpl::dump;
  using HeaderIndexImpl::forEachKey;
  using HeaderIndexImpl::getFileName;
  using HeaderIndexImpl::lookupFilename;
  using HeaderIndexImpl::reverseLookupFilename;
};

} // end namespace neverc.

#endif
