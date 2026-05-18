//===----------------------------------------------------------------------===//
//
//  Allocator — the per-type `SpecificAlloc<T>` arena used by `make<T>()` /
//  `makeThreadLocal<T>()` to carve linker objects (symbols, chunks, input
//  sections, ...) out of the context-owned bump allocator.
//
//  The resulting objects live as long as the active `CommonLinkerContext`
//  and are released in one shot when the context is destroyed.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_RUNTIME_ALLOCATOR_H
#define LINKER_CORE_RUNTIME_ALLOCATOR_H

#include "llvm/Support/Allocator.h"

namespace linker {

// Type-erased handle stashed inside `CommonLinkerContext::instances` so the
// per-type arenas can be found through a single dense map.
struct SpecificAllocBase {
  virtual ~SpecificAllocBase() = default;
  static SpecificAllocBase *getOrCreate(void *tag, size_t size, size_t align,
                                        SpecificAllocBase *(&creator)(void *));
};

// Arena of specific type `T`, created on-demand by `getOrCreate`.
template <class T> struct SpecificAlloc : public SpecificAllocBase {
  static SpecificAllocBase *create(void *storage) {
    return new (storage) SpecificAlloc<T>();
  }
  llvm::SpecificBumpPtrAllocator<T> alloc;
  static int tag;
};

// The address of this static member is only used as the key inside
// `CommonLinkerContext::instances`.  Its value does not matter.
template <class T> int SpecificAlloc<T>::tag = 0;

// Look up (and create on the first call) the arena backing type `T`.
template <typename T>
inline llvm::SpecificBumpPtrAllocator<T> &getSpecificAllocSingleton() {
  SpecificAllocBase *instance = SpecificAllocBase::getOrCreate(
      &SpecificAlloc<T>::tag, sizeof(SpecificAlloc<T>),
      alignof(SpecificAlloc<T>), SpecificAlloc<T>::create);
  return ((SpecificAlloc<T> *)instance)->alloc;
}

// Construct a `T` inside the context arena.  The instance is destroyed when
// the backend's `link()` returns (and `CommonLinkerContext::destroy()` runs).
template <typename T, typename... U> T *make(U &&...args) {
  return new (getSpecificAllocSingleton<T>().Allocate())
      T(std::forward<U>(args)...);
}

// Thread-local variant of `make<T>()`.  Used by parallel input-section
// initialisation where the callers guarantee that the allocated value
// outlives the enclosing `parallelForEach`.  Some backends (ELF) avoid
// the context indirection on perf-sensitive paths and reach for this
// version directly.
template <typename T>
inline llvm::SpecificBumpPtrAllocator<T> &
getSpecificAllocSingletonThreadLocal() {
  thread_local SpecificAlloc<T> instance;
  return instance.alloc;
}

template <typename T, typename... U> T *makeThreadLocal(U &&...args) {
  return new (getSpecificAllocSingletonThreadLocal<T>().Allocate())
      T(std::forward<U>(args)...);
}

template <typename T> T *makeThreadLocalN(size_t n) {
  return new (getSpecificAllocSingletonThreadLocal<T>().Allocate(n)) T[n];
}

} // namespace linker

#endif // LINKER_CORE_RUNTIME_ALLOCATOR_H
