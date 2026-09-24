#include "Linker/COFF/COFFLinkerContext.h"
#include "Linker/COFF/Chunks.h"
#include "Linker/COFF/Symbols.h"
#include "Linker/Core/Runtime/Stopwatch.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/TimeProfiler.h"
#include <algorithm>
#include <vector>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif
#include "Linker/Core/Runtime/LinkerParallel.h"

namespace linker::coff {

namespace {
// Liveness is plain reachability, so the marked set does not depend on the
// order sections are visited. Workers claim each chunk exactly once with an
// atomic exchange of its live flag and then scan it; the flags on import files
// only ever change from false to true.
bool claimLive(bool &live) {
#if defined(_MSC_VER) && !defined(__clang__)
  if (*reinterpret_cast<volatile bool *>(&live))
    return false;
  return !_InterlockedExchange8(reinterpret_cast<volatile char *>(&live), 1);
#else
  if (__atomic_load_n(&live, __ATOMIC_RELAXED))
    return false;
  return !__atomic_exchange_n(&live, true, __ATOMIC_RELAXED);
#endif
}

void setLive(bool &flag) {
#if defined(_MSC_VER) && !defined(__clang__)
  _InterlockedExchange8(reinterpret_cast<volatile char *>(&flag), 1);
#else
  __atomic_store_n(&flag, true, __ATOMIC_RELAXED);
#endif
}

void addSymParallel(Symbol *b, llvm::SmallVectorImpl<SectionChunk *> &stack) {
  if (auto *sym = dyn_cast<DefinedRegular>(b)) {
    SectionChunk *c = sym->getChunk();
    if (claimLive(c->live))
      stack.push_back(c);
  } else if (auto *sym = dyn_cast<DefinedImportData>(b)) {
    setLive(sym->file->live);
  } else if (auto *sym = dyn_cast<DefinedImportThunk>(b)) {
    setLive(sym->wrappedSym->file->live);
    setLive(sym->wrappedSym->file->thunkLive);
  }
}

// Marks everything reachable from the already-live chunks in \p frontier.
// Each round splits the frontier across tasks; a task scans a bounded number
// of chunks depth-first and hands its unscanned remainder to the next round,
// so a long chain found by one task is shared out again.
void markParallel(SmallVector<SectionChunk *, 0> frontier) {
  constexpr size_t chunksPerTask = 256;
  while (!frontier.empty()) {
    const size_t tasks =
        std::min(frontier.size(), size_t(parallelThreadCount()) * 8);
    const size_t step = (frontier.size() + tasks - 1) / tasks;
    std::vector<SmallVector<SectionChunk *, 0>> stacks(tasks);
    parallelFor(0, tasks, [&](size_t t) {
      const size_t begin = t * step;
      const size_t end = std::min(frontier.size(), begin + step);
      if (begin >= end)
        return;
      SmallVector<SectionChunk *, 0> &stack = stacks[t];
      stack.append(frontier.begin() + begin, frontier.begin() + end);
      for (size_t visited = 0; visited != chunksPerTask && !stack.empty();
           ++visited) {
        SectionChunk *sc = stack.pop_back_val();
        for (Symbol *b : sc->symbols())
          if (b)
            addSymParallel(b, stack);
        for (SectionChunk &child : sc->children())
          if (claimLive(child.live))
            stack.push_back(&child);
      }
    });
    frontier.clear();
    for (SmallVector<SectionChunk *, 0> &stack : stacks)
      frontier.append(stack.begin(), stack.end());
  }
}
} // namespace

// Set live bit on for each reachable chunk. Unmarked (unreachable)
// COMDAT chunks will be ignored by Writer, so they will be excluded
// from the final output.
void markLive(COFFLinkerContext &ctx) {
  llvm::TimeTraceScope timeScope("Mark live");
  ScopedTimer t(ctx.gcTimer);

  // We build up a worklist of sections which have been marked as live. We only
  // push into the worklist when we discover an unmarked section, and we mark
  // as we push, so sections never appear twice in the list.
  SmallVector<SectionChunk *, 256> worklist;

  // COMDAT section chunks are dead by default. Add non-COMDAT chunks. Do not
  // traverse DWARF sections. They are live, but they should not keep other
  // sections alive.
  for (Chunk *c : ctx.symtab.getChunks())
    if (auto *sc = dyn_cast<SectionChunk>(c))
      if (sc->live && !sc->isDWARF())
        worklist.push_back(sc);

  auto enqueue = [&](SectionChunk *c) {
    if (c->live)
      return;
    c->live = true;
    worklist.push_back(c);
  };

  auto addSym = [&](Symbol *b) {
    if (auto *sym = dyn_cast<DefinedRegular>(b))
      enqueue(sym->getChunk());
    else if (auto *sym = dyn_cast<DefinedImportData>(b))
      sym->file->live = true;
    else if (auto *sym = dyn_cast<DefinedImportThunk>(b))
      sym->wrappedSym->file->live = sym->wrappedSym->file->thunkLive = true;
  };

  // Add GC root chunks.
  for (Symbol *b : ctx.config.gcroot)
    addSym(b);

  if (parallelEnabled()) {
    markParallel(
        SmallVector<SectionChunk *, 0>(worklist.begin(), worklist.end()));
    return;
  }

  while (!worklist.empty()) {
    SectionChunk *sc = worklist.pop_back_val();
    assert(sc->live && "We mark as live when pushing onto the worklist!");

    // Mark all symbols listed in the relocation table for this section.
    for (Symbol *b : sc->symbols())
      if (b)
        addSym(b);

    // Mark associative sections if any.
    for (SectionChunk &c : sc->children())
      enqueue(&c);
  }
}
} // namespace linker::coff
