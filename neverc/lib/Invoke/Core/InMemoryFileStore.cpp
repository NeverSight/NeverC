#include "neverc/Invoke/InMemoryFileStore.h"

using namespace neverc;

InMemoryFileStore &InMemoryFileStore::instance() {
  static InMemoryFileStore Store;
  return Store;
}

llvm::SmallString<0> &InMemoryFileStore::create(llvm::StringRef Path,
                                                size_t ReserveHint) {
  std::unique_lock Lock(Mu);
  auto [It, _] = Buffers.try_emplace(Path);
  auto &Buf = It->second;
  if (ReserveHint > 0)
    Buf.reserve(ReserveHint);
  return Buf;
}

std::optional<llvm::MemoryBufferRef>
InMemoryFileStore::tryGet(llvm::StringRef Path) const {
  if (Frozen.load(std::memory_order_acquire)) {
    auto It = Buffers.find(Path);
    if (It == Buffers.end() || It->second.empty())
      return std::nullopt;
    return llvm::MemoryBufferRef(
        llvm::StringRef(It->second.data(), It->second.size()), Path);
  }
  std::shared_lock Lock(Mu);
  auto It = Buffers.find(Path);
  if (It == Buffers.end() || It->second.empty())
    return std::nullopt;
  return llvm::MemoryBufferRef(
      llvm::StringRef(It->second.data(), It->second.size()), Path);
}

void InMemoryFileStore::freeze() {
  Frozen.store(true, std::memory_order_release);
}

void InMemoryFileStore::clear() {
  Frozen.store(false, std::memory_order_relaxed);
  std::unique_lock Lock(Mu);
  Buffers.clear();
}
