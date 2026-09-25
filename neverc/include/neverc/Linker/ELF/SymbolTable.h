#ifndef LINKER_ELF_SYMBOL_TABLE_H
#define LINKER_ELF_SYMBOL_TABLE_H

#include "Linker/ELF/ELFHotState.h"
#include "Linker/ELF/Symbols.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace linker::elf {

class InputFile;
class SharedFile;

// One entry per distinct symbol-table key. Keys may be interned ahead of
// symbol resolution (from many threads) so that the ordered resolution pass
// finds each name without hashing it. `symbol` stays null until resolution
// first inserts the name, which keeps the symbol order identical to inserting
// names on demand.
struct SymbolNameSlot {
  Symbol *symbol;
  uint32_t size;
  // The hash of the name, which `name` spells, for SymbolTable's lock-free
  // index.
  uint32_t hash;
  const char *name;
  // The file whose COMDAT group with this signature prevails, if any.
  const InputFile *comdatOwner;
};

// SymbolTable is a bucket of all known symbols, including defined,
// undefined, or lazy symbols (the last one is symbols in archive
// files whose archive members are not yet loaded).
//
// We put all symbols of all files to a SymbolTable, and the
// SymbolTable selects the "best" symbols if there are name
// conflicts. For example, obviously, a defined symbol is better than
// an undefined symbol. Or, if there's a conflict between a lazy and a
// undefined, it'll read an archive member to read a real definition
// to replace the lazy symbol. The logic is implemented in the
// add*() functions, which are called by input files as they are parsed. There
// is one add* function per symbol type.
class SymbolTable {
public:
  SymbolTable() : nameShards(std::make_unique<NameShard[]>(numNameShards)) {}

  ArrayRef<Symbol *> getSymbols() const { return symVector; }

  void wrap(Symbol *sym, Symbol *real, Symbol *wrap);

  Symbol *insert(StringRef name);

  // Thread-safe: return the slot for `name` (a key without an "@@" version),
  // creating an empty slot if needed.
  SymbolNameSlot *intern(llvm::CachedHashStringRef name);

  // Insert the symbol named by a slot that was interned from a name without
  // any '@'. `data` points at the caller's copy of that name.
  Symbol *insertInterned(SymbolNameSlot *slot, const char *data,
                         bool hasVersionSuffix = false) {
    if (slot->symbol)
      return slot->symbol;
    return createSymbol(slot, llvm::StringRef(data, slot->size),
                        hasVersionSuffix);
  }

  // Size the name table for roughly `expectedNames` distinct names.
  void reserveNames(size_t expectedNames);

  template <typename T> Symbol *addSymbol(const T &newSym) {
    Symbol *sym = insert(newSym.getName());
    sym->resolve(newSym);
    return sym;
  }
  Symbol *addAndCheckDuplicate(const Defined &newSym);

  void scanVersionScript();

  Symbol *find(StringRef name);

  void handleDynamicList();

  // Set of .so files to not link the same shared object file more than once.
  llvm::DenseMap<llvm::CachedHashStringRef, SharedFile *> soNames;

  // Comdat groups define "link once" sections. If two comdat groups have the
  // same name, only one of them is linked, and the other is ignored. The first
  // file to claim a signature owns it. Claims happen in input order.
  bool claimComdat(SymbolNameSlot *signature, const InputFile *file) {
    if (signature->comdatOwner)
      return false;
    signature->comdatOwner = file;
    return true;
  }
  bool claimComdat(StringRef signature, const InputFile *file) {
    return claimComdat(intern(llvm::CachedHashStringRef(signature)), file);
  }
  const InputFile *getComdatOwner(StringRef signature) {
    SymbolNameSlot *slot = lookup(llvm::CachedHashStringRef(signature));
    return slot ? slot->comdatOwner : nullptr;
  }

private:
  SmallVector<Symbol *, 0> findByVersion(SymbolVersion ver);
  SmallVector<Symbol *, 0> findAllByVersion(SymbolVersion ver,
                                            bool includeNonDefault);

  bool assignExactVersion(SymbolVersion ver, uint16_t versionId);
  void assignWildcardVersion(SymbolVersion ver, uint16_t versionId);

  Symbol *createSymbol(SymbolNameSlot *slot, StringRef name,
                       bool hasVersionSuffix);
  SymbolNameSlot *lookup(llvm::CachedHashStringRef name);

  // Global symbols and a sharded map from symbol name to the index. The order
  // is not defined. We can use an arbitrary order, but it has to be
  // deterministic even when cross linking.
  // Critical sections are a single probe into a pre-sized map, so a spinning
  // lock avoids the futex round trips a blocking mutex incurs under load.
  class NameShardLock {
  public:
    void lock() {
      while (locked.exchange(true, std::memory_order_acquire))
        while (locked.load(std::memory_order_relaxed))
          std::this_thread::yield();
    }
    void unlock() { locked.store(false, std::memory_order_release); }

  private:
    std::atomic<bool> locked{false};
  };
  // Locks a shard only while names may be interned concurrently.
  class NameShardGuard {
  public:
    NameShardGuard(NameShardLock &lock, bool concurrent)
        : lock(concurrent ? &lock : nullptr) {
      if (this->lock)
        this->lock->lock();
    }
    ~NameShardGuard() {
      if (lock)
        lock->unlock();
    }

  private:
    NameShardLock *lock;
  };

public:
  // Set while worker threads may call intern(); otherwise the table is only
  // used from one thread and shard locks are skipped.
  void setConcurrentInterning(bool concurrent) {
    concurrentInterning = concurrent;
  }

private:
  bool concurrentInterning = false;
  struct alignas(64) NameShard {
    NameShardLock mutex;
    llvm::DenseMap<llvm::CachedHashStringRef, SymbolNameSlot *> map;
    llvm::SpecificBumpPtrAllocator<SymbolNameSlot> slots;
  };
  static constexpr unsigned numNameShardBits = 10;
  static constexpr unsigned numNameShards = 1u << numNameShardBits;
  NameShard &shardFor(llvm::CachedHashStringRef name) {
    return nameShards[(name.hash() * 0x9E3779B1u) >> (32 - numNameShardBits)];
  }
  std::unique_ptr<NameShard[]> nameShards;
  // A lock-free index of the slots in nameShards, which lets intern() find
  // an existing name without taking a shard lock. It only speeds lookups up:
  // a name missing from it is looked up in its shard. Fixed size; names that
  // do not fit are left to the shards.
  std::unique_ptr<std::atomic<SymbolNameSlot *>[]> fastIndex;
  size_t fastIndexMask = 0;
  SymbolNameSlot *findFast(llvm::CachedHashStringRef name) const;
  void publishFast(SymbolNameSlot *slot);
  // Arena for global symbols, looked up once per link rather than per symbol.
  llvm::SpecificBumpPtrAllocator<SymbolUnion> *symbolArena = nullptr;
  SmallVector<Symbol *, 0> symVector;
};

inline SymbolTable &elfSymtab() {
  return elfHotState<SymbolTable>(HotSymtab);
}

} // namespace linker::elf

#endif
