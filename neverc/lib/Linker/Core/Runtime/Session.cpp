//===----------------------------------------------------------------------===//
//
//  Session — the one `CommonLinkerContext` that owns every per-link
//  resource (arena allocator, string saver, `ErrorHandler`, per-type
//  `SpecificAlloc<T>` pool).  A single process currently keeps exactly
//  one such context active at a time through the `lctx` file-scope
//  pointer; see the comment below for the path toward thread-local
//  contexts.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Runtime/Session.h"
#include "Linker/Core/Runtime/Allocator.h"

using namespace llvm;
using namespace linker;

// Process-wide pointer to the currently active linker context.  This
// "one context per process" shape is transitional global state until
// the context is threaded through every call site (or made thread_local
// so a process can host several concurrent linker runs).
namespace {
CommonLinkerContext *lctx;
} // namespace

CommonLinkerContext::CommonLinkerContext() { lctx = this; }

CommonLinkerContext::~CommonLinkerContext() {
  assert(lctx);
  // Single-shot teardown: skip per-arena destructors (~12% of link
  // wallclock); the OS reclaims arena slabs on process exit. The
  // ErrorHandler member below still fires cleanupCallback() to reset
  // backend global state for any subsequent in-process link.
  lctx = nullptr;
}

CommonLinkerContext &linker::commonContext() {
  assert(lctx);
  return *lctx;
}

bool linker::hasContext() { return lctx != nullptr; }

void CommonLinkerContext::destroy() {
  if (lctx == nullptr)
    return;
  delete lctx;
}
