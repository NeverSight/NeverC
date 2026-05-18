//===----------------------------------------------------------------------===//
//
//  Session — `CommonLinkerContext`, the one object that owns every
//  per-link resource: the bump allocator, the string saver, the map of
//  per-type `SpecificAlloc<T>` arenas and the `ErrorHandler`.
//
//  A single process currently keeps exactly one `CommonLinkerContext`
//  alive through a file-scope pointer in `Session.cpp`.  The class sticks
//  to the upstream name (`CommonLinkerContext`) so cherry-picking patches
//  only has to rewrite include paths.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_CORE_RUNTIME_SESSION_H
#define LINKER_CORE_RUNTIME_SESSION_H

#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "llvm/Support/StringSaver.h"

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace linker {

struct SpecificAllocBase;

class CommonLinkerContext {
public:
  CommonLinkerContext();
  virtual ~CommonLinkerContext();

  static void destroy();

  llvm::BumpPtrAllocator bAlloc;
  llvm::StringSaver saver{bAlloc};
  llvm::DenseMap<void *, SpecificAllocBase *> instances;

  ErrorHandler e;
};

// Active process-wide context accessor.  Will become context-local once
// concurrent linker runs are supported.
CommonLinkerContext &commonContext();

template <typename T = CommonLinkerContext> T &context() {
  return static_cast<T &>(commonContext());
}

bool hasContext();

inline llvm::StringSaver &saver() { return context().saver; }
inline llvm::BumpPtrAllocator &bAlloc() { return context().bAlloc; }

} // namespace linker

#endif // LINKER_CORE_RUNTIME_SESSION_H
