#ifndef NEVERC_FOUNDATION_CORE_OUTPUTCOORDINATOR_H
#define NEVERC_FOUNDATION_CORE_OUTPUTCOORDINATOR_H

#include "neverc/Foundation/Core/OutputPathLease.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace neverc {

struct OutputLeaseOwner {
  uint64_t High = 0;
  uint64_t Low = 0;

  explicit operator bool() const { return High != 0 || Low != 0; }
  bool operator==(const OutputLeaseOwner &Other) const {
    return High == Other.High && Low == Other.Low;
  }
};

class OutputCoordinator {
public:
  using CancellationCheck = std::function<bool()>;

  llvm::Expected<OutputPathLease>
  acquire(llvm::StringRef Path,
          CancellationCheck IsCancelled = CancellationCheck(),
          OutputLeaseOwner LeaseOwner = {});

  llvm::Expected<std::vector<OutputPathLease>>
  acquireAll(llvm::ArrayRef<llvm::StringRef> Paths,
             CancellationCheck IsCancelled = CancellationCheck(),
             OutputLeaseOwner LeaseOwner = {});

  llvm::Expected<std::string>
  canonicalize(llvm::StringRef Path) const;

private:
  void release(llvm::StringRef CanonicalPath);

  mutable std::mutex Mutex;
  std::condition_variable Available;
  std::map<std::string, OutputLeaseOwner> ActivePaths;

  friend class OutputPathLease;
};

} // namespace neverc

#endif
