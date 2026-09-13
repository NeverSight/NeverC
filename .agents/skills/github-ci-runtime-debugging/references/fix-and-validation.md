# Fix And Validation

## Step 6 — Map the backtrace to a root cause

Read the core-dump backtrace top-to-bottom. Look for, in order:
- The faulting frame and whether `this`/pointers are bogus (`<error: Cannot access memory ...>`) →
  **heap corruption**, and the crashing frame is often just the *victim*, not the culprit.
- Whether you're inside a worker thread of a `parallelFor`/`parallelForEach`/thread pool → suspect a
  **concurrency bug**. Note **nested parallelism** (a parallel loop whose body starts another).
- Allocator frames (bump/arena/`make<>`-style) under a parallel context → a **non-thread-safe global
  allocator** being called concurrently. Many linkers/compilers ship a fast global arena that is
  *only* safe to call serially, plus a thread-local variant for parallel code — using the wrong one
  in a parallel loop corrupts the heap.

Common root-cause families for "native-only / intermittent":
| Symptom | Likely cause |
|---|---|
| Different output bytes each run | ASLR-ordered container iteration; thread-order-dependent emission |
| Heap corruption, bogus `this` in a worker thread | concurrent use of a non-thread-safe global allocator/cache |
| Crash only with real parallelism, never under qemu | data race exposed by native scheduling/timing |
| `movaps`/`movdqa` SIGSEGV inside libc | mis-generated 16-byte stack misalignment |
| Crashes with garbage values, ASan clean | uninitialized read (use MSan/Valgrind) |

## Step 7 — Fix at the source, then validate on native

- Prefer the **reference implementation's** structure if the buggy code was "ported from" one (e.g.
  LLD/LLVM): compare against upstream and restore the invariant that was broken (e.g. "this loop must
  be serial because it allocates from the shared arena").
- For the non-thread-safe-allocator-in-parallel pattern, the usual fixes are: make the loop serial,
  or switch to the thread-local allocator, or guard with a lock — pick what matches the codebase's
  existing convention.
- **Validate with the same harness that reproduced it**: rebuild with the fix, then run the
  high-iteration native repro and require **0 crashes and a single deterministic hash**. A single
  green CI run is NOT proof for a flaky bug.

## Step 8 — Cross-check analogous code paths

A bug found in one backend/module usually has siblings. Audit the same pattern elsewhere:
```bash
# every parallel loop, and every global-arena allocation, then cross-reference
rg -n "parallelFor|parallelForEach|std::thread|TaskGroup" <dirs>
rg -n "\bmake<|globalAlloc|arena|BumpPtrAllocator" <dirs>
```
For each parallel loop, ask: does its body (transitively) touch a shared, non-thread-safe resource
(global allocator, global map/cache, shared builder)? Confirm sibling backends (COFF/MachO/...) are
either serial on that path or use thread-safe allocation.

## Pitfalls / red flags
- Chasing `-g` hash differences (benign) instead of real codegen non-determinism.
- Concluding "not reproducible" after a handful of runs — flaky bugs need high iteration counts.
- Trusting qemu to model alignment/CET/native scheduling — it doesn't.
- Fixing the crashing (victim) frame instead of the corrupting (culprit) thread.
- Pushing an unvalidated fix and trusting one green run.
- Forgetting to remove the throwaway debug workflow after the investigation.

## Quick reference
1. Characterize: which step, what signal, deterministic vs flaky, which platforms, first bad commit.
2. Repro locally in a CI-matching container (`--platform`, matching glibc, `--cpuset-cpus`).
3. Reuse the CI artifact (`gh run download`) — don't rebuild.
4. Separate tool-crash vs program-crash.
5. Determinism hash + sanitizers; recognize what emulation hides.
6. If needed, push a `workflow_dispatch` + `[skip ci]` debug job → high-iteration repro + gdb on cores on a real runner.
7. Read the backtrace for races / nested parallelism / non-thread-safe global allocators.
8. Fix at the source (match upstream/reference), validate with the high-iteration native repro (0 crashes, 1 hash).
9. Cross-check sibling code paths; clean up the debug workflow.
