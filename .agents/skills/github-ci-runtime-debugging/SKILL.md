---
name: github-ci-runtime-debugging
description: Use when a CI/CD pipeline (GitHub Actions or similar) surfaces a runtime bug that is platform-specific ("fails on linux-x64 but passes on arm64/macos/windows"), intermittent/flaky (different tests fail each run, segfaults, "Segmentation fault (core dumped)", non-deterministic), or does not reproduce locally ("works on my machine"). Covers reproducing in a CI-matching environment (Docker + qemu for cross-arch hosts), reusing prebuilt CI artifacts instead of rebuilding, distinguishing a tool/compiler crash from a built-program crash, determinism / ASLR / data-race triage, pushing a minimal workflow_dispatch debug job to a real runner, and capturing core-dump backtraces. Trigger phrases: "CI fails but works locally", "only fails on x64/arm64", "flaky test", "random segfault in CI", "core dumped", "can't reproduce the CI crash".
---

# Debugging Platform-Specific & Intermittent Runtime Bugs from CI

## Overview

Some bugs only appear in CI: they are **platform-specific** (one arch/OS fails), **intermittent**
(different tests fail each run, random segfaults), or simply **don't reproduce on your laptop**.
Guessing wastes hours. This skill is a reproduction-first playbook: get the failure under a debugger
on a machine that matches CI, then let the evidence (backtrace, determinism, core dump) name the
root cause.

**Core principle:** Reproduce in a CI-matching environment and capture hard evidence (backtrace /
core dump / determinism hashes) BEFORE proposing a fix. Most "unreproducible" bugs are reproducible
once you match the environment and crank up iterations.

**The single biggest time-saver:** Don't rebuild the toolchain. Download the exact artifact CI
already produced (`gh run download`) and reproduce against it.

## Step 0 — Characterize the failure from the CI logs

Answer these from the failing run's logs before touching code. Use `gh`:

```bash
gh run list --workflow <wf>.yml -L 30                 # recent runs: which passed/failed
gh run view <run-id> --log | grep -iE "fail|segmentation|\*\*\*|error" | head -50
gh run view <run-id> --log-failed                     # only failed steps
```

Determine:
- **Which step fails** — building the tool? running the test? a link step? (decides "tool vs program" below)
- **The signal** — assertion? `Segmentation fault (core dumped)` (SIGSEGV)? exit 137 (SIGKILL/OOM)? timeout?
- **Deterministic or flaky** — compare the failed-test list across several failed runs. If *different*
  tests fail each run, the bug is **non-deterministic** (race / uninitialized memory / ASLR-dependent
  ordering), not a logic bug in those specific tests.
- **Platform scope** — diff the same commit across platform workflows (linux-x64 vs linux-arm64 vs
  macos vs windows). "x64 fails, arm64 passes" narrows it enormously.
- **First bad commit** — scan history for the last green run; correlate with what changed.

```bash
# Did the SAME tests fail, or different ones each run? (flaky signal)
for id in <id1> <id2> <id3>; do echo "== $id =="; \
  gh run view $id --log | grep -oE "Test #[0-9]+: [A-Za-z._]+ .*Failed"; done
```

## Step 1 — Reproduce in a CI-matching environment locally (first attempt)

Match what matters: **architecture**, **libc/OS version**, **compiler/flags**.

- On an **arm64 Mac** debugging a **linux-x64** failure, use Docker with `--platform linux/amd64`
  (Docker runs it under qemu emulation). Match the CI base image / glibc (e.g. `ubuntu:22.04`).
- Bake deps into a small image once so you can iterate:

```bash
docker build --platform linux/amd64 -t repro:img - <<'EOF'
FROM --platform=linux/amd64 ubuntu:22.04
RUN apt-get update -qq && apt-get install -y -qq \
    gcc g++ libc6-dev gdb binutils file ca-certificates >/dev/null
EOF
docker run --rm --platform linux/amd64 -v "$PWD":/work:ro repro:img \
  bash -lc 'cd /work && <reproduce the failing compile/run>'
```

- **Control parallelism/cores** with `--cpuset-cpus=0-3` (changes `nproc` / `hardware_concurrency`),
  since partition counts and races often depend on core count.

If it reproduces here, debug it here (gdb, ASan/UBSan). If it does NOT, go to Step 4.

## Step 2 — Reuse the prebuilt CI artifact (don't rebuild the toolchain)

If the failing thing is a heavy artifact (a compiler/linker/large binary), download it instead of a
multi-hour rebuild:

```bash
gh api repos/<owner>/<repo>/actions/runs/<run-id>/artifacts -q '.artifacts[].name'
gh run download <run-id> -n <artifact-name> -D dl     # 100s of MB is normal; can be slow
# unzip / extract, chmod +x, sanity check it runs under the matching platform
```

Run that exact artifact inside the `--platform linux/amd64` container. This is the highest-fidelity
local repro: it's literally the binary CI used.

## Step 3 — Is it the TOOL crashing or the PROGRAM it produced?

A "compile failed with segfault" can be either the compiler/linker crashing OR the produced binary
crashing when run. They need different fixes. Tells:

- **LLVM/Clang-based tools** install a crash handler (`InitLLVM` / `PrintStackTraceOnErrorSignal`).
  If the tool itself crashes you'll usually see `PLEASE submit a bug report ... Stack dump:`. Its
  **absence** (just bare `Segmentation fault (core dumped)`) suggests the *built program* crashed.
- The test harness often runs `tool ... && ./built_program`. Reproduce the two halves separately:
  compile to an artifact, then run the artifact, and see which one faults.
- Beware shell/`popen` output routing: `sh -c "prog 2>err"` may write the shell's
  "Segmentation fault" message to a different fd than the program's captured stderr. Don't over-read
  which stream the message landed in; verify with explicit exit codes (`echo $?`; signal = `128+N`).

## Step 4 — When local emulation can't reproduce: triage WHY

If the program runs fine under qemu/Docker but crashes on the native CI runner, the cause is almost
always something **emulation does not model**. Triage:

1. **Determinism check (do this first).** Compile/produce the artifact N times and hash it:
   ```bash
   for i in $(seq 1 12); do <tool> <flags> -o /tmp/b <src>; sha256sum /tmp/b; done | sort -u
   ```
   - Identical hashes → deterministic output; the crash is CPU/runtime-state specific (see below).
   - Different hashes → **non-deterministic codegen/output** (data race, ASLR-dependent container
     iteration order, thread-order-dependent emission). This is itself the bug to chase.
   - GOTCHA: `-g`/debug info legitimately varies between builds (timestamps, hashes) without changing
     behavior. Strip `-g` for the determinism check, or compare only code sections, or you'll chase a
     benign red herring.

2. **What qemu/emulation hides (native-only crashes):**
   - **Stack/SSE alignment** — x86-64 needs 16-byte stack alignment at calls; misaligned `movaps`/
     `movdqa` faults on real CPUs but qemu is lenient. Check function prologues (`push`/`sub rsp`)
     vs call sites.
   - **Data races / weak vs strong memory model** — real parallelism + native scheduling trigger
     races that qemu's serialized timing hides. (Note: pure memory-model races often hit arm64
     *more*; if x86 fails and arm64 passes, also consider allocator/timing/core-count races.)
   - **ASLR** — randomized pointers make pointer-keyed container iteration (`DenseMap<T*>`,
     `unordered_map`, sorting by address) order differently each run → non-deterministic output.
   - **Uninitialized memory** — fresh `mmap` pages are zero on both, but other garbage differs;
     ASan catches OOB/UAF, **MSan/Valgrind** catch uninitialized reads.
   - **CPU features / CET / IBT** — `.note.gnu.property` IBT without `endbr64`, AVX on non-AVX, etc.

3. **Sanitizers are arch-independent** for many bugs — run ASan+UBSan locally (even on a different
   arch) to clear or implicate the source code itself:
   ```bash
   cc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer <srcs> -o /tmp/s && /tmp/s
   ```
   Clean sanitizers + a crash only in CI ⇒ strongly implicates the **toolchain/codegen/linker**, not
   the source.

## Step 5 — Reproduce on a REAL runner with a throwaway debug workflow

When you need native hardware, add a minimal `workflow_dispatch` job. It downloads the existing
artifact (no rebuild) and runs the failing case under a debugger, many times.

Key tricks:
- `on: workflow_dispatch` only, so it never runs automatically.
- Put `[skip ci]` in the commit message so pushing the workflow file does NOT trigger the heavy
  build matrix. Trigger it manually with `gh workflow run`.
- Match the failing runner's OS (`runs-on: ubuntu-22.04`, etc.).
- Capture core dumps and symbolize them. Loop enough to catch low-probability flakes.

```yaml
name: debug-repro
on:
  workflow_dispatch:
    inputs:
      run_id:     { description: 'run id to pull artifact from', required: false, default: '' }
      iterations: { description: 'repeat count', required: false, default: '80' }
permissions: { contents: read, actions: read }
jobs:
  repro:
    runs-on: ubuntu-22.04            # match the failing runner
    steps:
      - uses: actions/checkout@v4
        with: { path: src }
      - run: sudo apt-get update && sudo apt-get install -y gcc libc6-dev gdb p7zip-full file
      - name: Download artifact (no rebuild)
        env: { GH_TOKEN: '${{ github.token }}' }
        run: |
          set -x
          RID="${{ github.event.inputs.run_id }}"
          [ -z "$RID" ] && RID=$(gh run list --workflow <build>.yml -L 30 \
              --json databaseId,status -q '[.[]|select(.status=="completed")][0].databaseId' \
              -R "${{ github.repository }}")
          gh run download "$RID" -n <artifact> -D dl -R "${{ github.repository }}"
          7z x dl/*.zip -o. >/dev/null; chmod +x <tool-path>
      - name: Reproduce + determinism + backtrace
        run: |
          set +e
          N="${{ github.event.inputs.iterations }}"
          ulimit -c unlimited
          echo "$PWD/core.%e.%p" | sudo tee /proc/sys/kernel/core_pattern   # overrides apport
          echo "nproc=$(nproc)"
          declare -A H; fails=0
          for i in $(seq 1 "$N"); do
            rm -f /tmp/out core.*
            <tool> <EXACT-CI-FLAGS> -o /tmp/out <src> 2>/tmp/cerr
            if [ $? -ne 0 ]; then echo "[#$i] TOOL crash"; head -20 /tmp/cerr
              for c in core.*; do gdb -batch -ex 'thread apply all bt' <tool> "$c" 2>&1|head -60; done
              fails=$((fails+1)); continue; fi
            H["$(sha256sum /tmp/out|cut -c1-16)"]=1     # NOTE: omit -g so hashes are meaningful
            /tmp/out >/dev/null 2>&1
            if [ $? -ne 0 ]; then echo "[#$i] PROGRAM crash"
              for c in core.*; do
                gdb -batch -ex bt -ex 'info registers rip rsp' -ex 'x/i $rip' /tmp/out "$c" 2>&1|head -50
              done; fails=$((fails+1)); fi
          done
          echo "distinct binary hashes=${#H[@]}  fails=$fails"
          [ ${#H[@]} -gt 1 ] && echo "*** NON-DETERMINISTIC OUTPUT ***"
```

Drive it and read results:
```bash
git add .github/workflows/debug-repro.yml
git commit -m "ci: on-demand repro [skip ci]" && git push
gh workflow run debug-repro.yml --ref <branch> -f iterations=80
# wait, then:
gh run view <id> --log | grep -F "Reproduce + determinism + backtrace" | sed -E 's/^[^\t]*\t[^\t]*\t//'
```

Tips: size `iterations` to the observed failure rate (2 of 560 tests ≈ 0.4% → dozens–hundreds of
iterations to catch one). Add a parallel-stress variant (N background workers sharing any on-disk
cache) to surface concurrency/cache races.

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
