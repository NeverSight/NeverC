# Local Reproduction

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
