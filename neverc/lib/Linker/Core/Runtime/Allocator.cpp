//===----------------------------------------------------------------------===//
//
//  Allocator — on-demand `SpecificAlloc<T>` factory keyed by type.
//
//  The factory lives inside the active `CommonLinkerContext` and produces
//  one `SpecificBumpPtrAllocator<T>` per type on the first `make<T>()`
//  call; every subsequent request returns the same allocator.  Every
//  backend holds symbol / chunk / section instances in these arenas so
//  the full set is torn down in one shot when the link finishes.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/Session.h"

using namespace llvm;
using namespace linker;

SpecificAllocBase *
linker::SpecificAllocBase::getOrCreate(void *tag, size_t size, size_t align,
                                       SpecificAllocBase *(&creator)(void *)) {
  auto &instances = context().instances;
  auto &instance = instances[tag];
  if (instance == nullptr) {
    void *storage = context().bAlloc.Allocate(size, align);
    instance = creator(storage);
  }
  return instance;
}
