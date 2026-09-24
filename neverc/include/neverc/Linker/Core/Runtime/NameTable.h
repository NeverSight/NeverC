//===----------------------------------------------------------------------===//
//
//  ShardedNameTable — symbol-name interning shared by the COFF and Mach-O
//  backends.
//
//  Symbol resolution runs in input order on one thread, but hashing every
//  referenced name there dominates loading large archives. The table maps a
//  name to a stable slot and is split into shards by hash, each with its own
//  lock, so worker threads can intern the names of archive members ahead of
//  resolution. Resolution then reaches the slot through a NameHint, without
//  hashing or comparing the name, and records the symbol in it.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_RUNTIME_NAMETABLE_H
#define LINKER_CORE_RUNTIME_NAMETABLE_H

#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"
#include <atomic>
#include <cstdint>
#include <memory>

namespace linker {

// A name's entry. A slot interned ahead of resolution has no symbol yet.
template <typename SymbolT> struct NameSlot {
  const char *data;
  uint32_t size;
  SymbolT *symbol = nullptr;

  llvm::StringRef name() const { return llvm::StringRef(data, size); }
};

// The slot of a name that is about to be inserted, and the address of that
// name in its input file. The address identifies the name exactly, since the
// file parses the name from the same bytes the slot was interned from.
template <typename SymbolT> struct NameHint {
  NameSlot<SymbolT> *slot = nullptr;
  const char *data = nullptr;

  NameSlot<SymbolT> *match(llvm::StringRef name) const {
    return slot && data == name.data() && slot->size == name.size() ? slot
                                                                    : nullptr;
  }
};

template <typename SymbolT> class ShardedNameTable {
public:
  using Slot = NameSlot<SymbolT>;

  ShardedNameTable() : shards(std::make_unique<Shard[]>(numShards)) {}

  // Returns the name's slot, creating it if needed.
  Slot *intern(llvm::StringRef name) {
    llvm::CachedHashStringRef key(name);
    Shard &shard = shardFor(key);
    Guard guard(shard.locked, concurrent);
    Slot *&slot = shard.slots[key];
    if (!slot)
      slot = new (shard.storage.template Allocate<Slot>())
          Slot{name.data(), static_cast<uint32_t>(name.size())};
    return slot;
  }

  Slot *lookup(llvm::StringRef name) const {
    llvm::CachedHashStringRef key(name);
    Shard &shard = shardFor(key);
    Guard guard(shard.locked, concurrent);
    return shard.slots.lookup(key);
  }

  // While enabled, every access locks its shard so that other threads may
  // intern concurrently.
  void setConcurrent(bool enabled) { concurrent = enabled; }

private:
  static constexpr unsigned shardBits = 8;
  static constexpr unsigned numShards = 1u << shardBits;

  struct Shard {
    std::atomic<bool> locked{false};
    llvm::DenseMap<llvm::CachedHashStringRef, Slot *> slots;
    llvm::BumpPtrAllocator storage;
  };

  class Guard {
  public:
    Guard(std::atomic<bool> &lock, bool active)
        : lock(active ? &lock : nullptr) {
      if (!this->lock)
        return;
      while (this->lock->exchange(true, std::memory_order_acquire))
        while (this->lock->load(std::memory_order_relaxed))
          ;
    }
    ~Guard() {
      if (lock)
        lock->store(false, std::memory_order_release);
    }

  private:
    std::atomic<bool> *lock;
  };

  // The shard comes from the high hash bits; each shard's own table indexes
  // with the low bits.
  Shard &shardFor(llvm::CachedHashStringRef key) const {
    return shards[key.hash() >> (32 - shardBits)];
  }

  std::unique_ptr<Shard[]> shards;
  bool concurrent = false;
};

} // namespace linker

#endif // LINKER_CORE_RUNTIME_NAMETABLE_H
