# NeverC Standard Library Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `github-ci-runtime-debugging` for every CI-only/runtime failure investigation. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Audit the complete NeverC `std` surface, fix only evidence-backed defects, add regression tests, and keep the `dev` branch green using GitHub Actions as the sole test environment.

**Architecture:** Partition the library into independently owned review domains plus one cross-cutting completeness audit, with one inherited same-model subagent producing evidence for each domain and the root agent integrating only confirmed fixes. Each defect follows a test-first commit exposed to GitHub through a temporary tag (never as a red `dev` HEAD), then a fix commit and one combined push to `dev`; CI-specific failures are reproduced on the matching GitHub runner with exact source/artifact provenance and a temporary dispatch-only workflow when necessary.

**Tech Stack:** C, C++17 test registry, CMake, Clang/LLVM, Python CI harness, GitHub Actions, `gh` CLI.

---

## Guardrails

- Integrate only by fast-forwarding `dev`; never publish a temporary remote branch. When the shared working tree contains unrelated changes, prepare the exact-path batch in a detached worktree at the recorded `origin/dev` SHA.
- Never compile or run tests on the local machine. Local actions are limited to read-only inspection, editing, Git bookkeeping, and GitHub/API operations.
- Record and preserve the current exact integration base before every batch, and inspect recent CI fixes before changing shared infrastructure. The original audit baseline was `c559a30f646021d8757d288a71b5e1414d1ec761`.
- Do not change code merely for style or speculation. Require a concrete violated contract, standard/reference mismatch, memory/concurrency hazard, or reproducible failing regression test.
- Prefer official standards and primary upstream repositories when semantics are uncertain.
- Keep temporary debug workflows `workflow_dispatch`-only. Remove them after the reproducer and final validation pass.
- A red test commit may be uploaded only through a temporary tag such as `std-red/<case>`; the remote `dev` ref must not point at it. Delete the local and remote tag after the assertion is proven.
- Before every push, fetch `origin/dev`, require the expected remote SHA, a clean index outside the planned patch, and no unexplained worktree changes. Never force-push.
- Push only one fix batch at a time and wait for all required exact-SHA checks before integrating another batch, so concurrency cancellation cannot hide an incomplete result.
- Do not rewrite or discard unrelated user changes. Stop integrating if the shared worktree is no longer clean for unexplained reasons.

## Active vertical slice (2026-09-01): executor native-join failure

**Files:**
- Test: `tests/neverc/std/test_thread_failures.c`
- Modify: `std/src/thread/thread.c`
- Inspect: `std/include/neverc/std/thread.h`

- [x] Confirm the root cause: shutdown discards a failed native-join handle, marks shutdown complete, and `free` releases storage that a live worker still accesses.
- [x] Add one public-API regression scenario that injects a native join failure, keeps the worker alive, then requires a real successful `shutdown` retry join before final release.
- [x] Review the one-worker tracer test for POSIX/Windows wrapper compatibility, allocation-count coupling, and false-green paths. This test proves only shutdown lifetime/retry, not `free`'s draining-return guarantee, multi-worker prefix, or concurrent-claim behavior.
- [x] Prepare test-only detached commit `845c39b2e00ff2c207ddc3fbfea6b559ddb04ab7` from integration base `cd38f0703ea18380257523a0143af25761839b03`; do not publish it while that base's required workflows are running.
- [x] Recreate the test-only commit as `325ec7942ceb117d20150cbfc63f742a4b5f14d8` on exact integration base `73f2b0a25547080585f1e308c50a7a2dd0bf4760`.
- [x] Push only the temporary RED tag and dispatch `debug-std-quick-check.yml` with `tests=thread_failures`, `sanitize=address,undefined`, empty defines/extra flags, and a 60-second timeout.
- [x] Require the current implementation to fail the lifecycle assertion rather than compilation, harness setup, or timeout; run `33433637531` failed only at the expected shutdown-retry assertion.
- [x] Add a joined-prefix cursor. Clear a handle and worker ID only after native join succeeds; stop at the first failure. Treat `shutdown_started` as a mutex-protected ownership claim: wait only while `shutdown_started && !shutdown_complete`, and on failure clear the claim plus broadcast so one waiter can loop and reclaim it.
- [x] Make `shutdown_complete` mean `joined_count == worker_count`: every native worker handle has been joined successfully. Keep runtime worker failure separate from a transient native-join attempt failure.
- [x] Commit the first GREEN as `a8acada314e10508f1a12886b49610d6829edab0`; exact-SHA run `33435850083` passed the retry regression under ASan/UBSan.
- [x] Add/tag the deterministic second RED as `05cfba754925cfac666208771ba3b5a76f1cf38e`, covering both public `free` teardown and partial-create cleanup with synchronized checkpoints and no sleeps; run `33438887063` failed only because both operations returned before worker-exit proof.
- [x] Implement independent `exited_count` accounting in `e6d550b731fe352ad46451b54982957c647bddac` so `free` preserves its draining guarantee even when native join cannot prove completion; exact-SHA run `33440089268` passed under ASan/UBSan. The existing public header already documents a draining shutdown, so no header edit was needed.
- [x] Re-audit the executor owners in `std/src/net/rpc/rpc_server.c` and `std/src/net/http/http2/http2_server.c`; both discard the executor pointer and proceed to release enclosing state after `free`, so the worker-exit proof is required and sufficient for their current ownership pattern.
- [x] Apply the same exit proof to partial-create cleanup, using the exact successfully created worker count as its target, so it never destroys storage reachable by a worker and does not create an ASan-visible heap orphan. Native thread-resource recovery after an irrecoverable join error remains best effort.
- [ ] After the one-worker tracer is GREEN, add a focused two-worker injected sequence (join index 0 succeeds, index 1 fails, retry starts at index 1) to lock the prefix-cursor behavior.
- [ ] Add a focused concurrent-shutdown case in which one claimant fails while another waits. Use a test-only handshake proving caller B has entered the stopped-condition wait before caller A may return the injected failure, and use atomic/synchronized injection counters; require B to wake, reclaim ownership, and finish after failure injection is disabled.
- [x] Keep the first GREEN, the draining-free RED, and the worker-exit GREEN as explicit separate commit boundaries; delete both temporary RED tags after their paired GREEN evidence is recorded.
- [ ] After all candidate and final-merge gates pass, push the complete history together to `dev` without force, rerun the identical targeted checks, and wait for the full exact-SHA acceptance matrix.

### Executor RED/GREEN evidence

| Stage | Commit | GitHub Actions evidence | Result |
|---|---|---|---|
| Retry RED | `325ec7942ceb117d20150cbfc63f742a4b5f14d8` | `33433637531` | Expected retry assertion failed; harness and sanitizer setup were valid. |
| Retry GREEN | `a8acada314e10508f1a12886b49610d6829edab0` | `33435850083` | `thread_failures` passed under ASan/UBSan. |
| Drain RED | `05cfba754925cfac666208771ba3b5a76f1cf38e` | `33438887063` | Expected `free` and partial-create worker-exit assertions failed. |
| Drain GREEN | `e6d550b731fe352ad46451b54982957c647bddac` | `33440089268` | `thread_failures` passed, `passed=1 failed=0`, ASan/UBSan clean. |
| Combined release candidate | `1632bed30fdd77a305a37e76d3aabb62f42d10c3` | `33441265315` | Exact-SHA `thread_failures` passed, `passed=1 failed=0`, ASan/UBSan clean; remaining release gates are tracked before the `dev` push. |

POSIX asynchronous `pthread_cancel` safety is out of scope for this slice: both `pthread_join` and `pthread_cond_wait` are cancellation points, and the public executor API does not currently promise cancellation cleanup. Addressing abandoned shutdown ownership would require a separate cleanup-handler contract and regression plan.

## Task 1: Record and protect the CI baseline

**Files:**
- Inspect: `.github/workflows/build-linux-x64.yml`
- Inspect: `.github/workflows/build-linux-arm64.yml`
- Inspect: `.github/workflows/build-macos-arm64.yml`
- Inspect: `.github/workflows/build-windows-x64.yml`
- Inspect: `.github/workflows/build-windows-x64-clang-lto.yml`
- Inspect: `.github/workflows/build-windows-arm64.yml`
- Inspect: `.github/workflows/build-windows-arm64-clang-lto.yml`
- Inspect: `.github/workflows/debug-std-quick-check.yml`

- [ ] Confirm `HEAD`, `dev`, and `origin/dev` are identical before the first edit.
- [ ] Record exact-SHA run IDs and conclusions for every workflow required by the acceptance matrix below; do not substitute a newer/older run.
- [ ] Read the last twelve changes affecting `.github/workflows`, `std`, and `tests/neverc/std`.
- [ ] If any baseline run is failing, characterize the failed step, signal, platform scope, and first bad commit before proposing a change.

### Exact-SHA acceptance matrix

| Changed paths | Required successful checks for the pushed SHA |
|---|---|
| Every ordinary push to `dev`, regardless of changed path | `linux-x64-neverc-build`, `linux-arm64-neverc-build`, `macos-arm64-neverc-build`, `windows-x64-neverc-build`, `windows-x64-neverc-build-clang-lto`, `windows-arm64-neverc-build`, and `windows-arm64-neverc-build-clang-lto` |
| Any `std/**` or `tests/neverc/std/**` implementation/test batch | `lint-test-deps` in addition to the universal seven-build matrix |
| Any `tests/**` or `docs/**` change (and the other exact paths listed by `lint-docs.yml`) | `lint-docs`; every `python-plugin-bindings` matrix check also applies when a `tests/**` path triggers it |
| Any `.github/workflows/**` change | Every `python-plugin-bindings` matrix check; `lint-docs` only when the changed path independently matches its actual filter (for example `.github/workflows/lint-docs.yml`, `tests/**`, or `docs/**`) |
| `std/src/crypto/**`, TLS/X.509 headers, or `std/src/math/big/**` | `tls-protocol-fuzz` in addition to the base matrix |
| `std/src/net/**` or network headers | `network-protocol-fuzz` in addition to the base matrix |
| HTTP or WebSocket paths | `http-websocket-interop` in addition to the network/base matrix |
| HTTP/2 or gRPC paths | `http2-grpc-interop` in addition to the network/base matrix |
| QUIC or HTTP/3 paths | `http3-quiche-interop` in addition to the network/base matrix |
| Any SHA for which GitHub schedules CodeQL or another externally managed required check | That exact-SHA check must also complete successfully; it cannot be inferred from a prior SHA |

Manual `debug-std-quick-check` runs are targeted evidence, not baseline checks. Record their source ref/SHA and all dispatch inputs (`tests`, `sanitize`, `defines`, `timeout`, and `extra`).

The single temporary-workflow addition commit intentionally uses `[skip ci]` and is exempt from the ordinary-push matrix because it contains only a `workflow_dispatch` reproducer. It is not an accepted final SHA. Its later deletion is a normal push and must pass every actually triggered check, including all seven builds and `python-plugin-bindings`.

## Task 2: Run 23 owned source-and-test audits

Each audit must inspect the public headers, implementation, manifest entries, matching `STD_TEST` registrations, and regression tests. Report exact file/line evidence, user-visible consequence, a minimal test idea, and confidence. Do not edit during the audit pass.

| Audit | Exact scope |
|---|---|
| 01 foundation | `std/src/{arena,bufio,bytes,cmp,context,cstring,errors,flag,fmt}`, matching `std/include/neverc/std/*.h`, and matching `tests/neverc/std/test_*.c` |
| 02 containers | `std/src/{container,maps,slices,sort}`, matching headers and tests |
| 03 lifetime/concurrency | `std/src/{sync,thread,weak,unique}`, matching headers and tests |
| 04 strconv/scalars | `std/src/strconv`, scalar files and shared internal headers (`_math_internal.h`, `_trig_reduce.h`) directly under `std/src/math`, matching headers and tests |
| 05 advanced math | `std/src/math/{big,bits,cmplx,rand}`, matching headers and tests |
| 06 hashes | `std/src/hash`, `std/include/neverc/std/hash*`, and hash tests |
| 07 simple encodings | `std/src/encoding/{ascii85,base32,base64,binary,hex}`, matching headers and tests |
| 08 structured encodings | `std/src/encoding/{asn1,csv,json,pem,protobuf,xml}`, matching headers and tests |
| 09 compression/archive | `std/src/{compress,archive}`, matching headers and tests |
| 10 symmetric crypto | `std/src/crypto/{aes,chacha20,chacha20poly1305,cipher,des,gcm,poly1305,rc4,subtle}`, matching headers and tests |
| 11 hashes/KDF/random | `std/src/crypto/{md5,sha1,sha224,sha256,sha3,sha384,sha512,sha512_224,sha512_256,hmac,hkdf,pbkdf2,rand}`, matching headers and tests |
| 12 classical public-key | `std/src/crypto/{dsa,ecdh,ecdsa,ed25519,elliptic,hpke,rsa}`, matching headers and tests |
| 13 post-quantum crypto | `std/src/crypto/{mldsa,mlkem}`, matching headers and tests |
| 14 TLS/X.509 | `std/src/crypto/{tls,x509}`, matching headers, interop workflows, and tests |
| 15 filesystem/OS | `std/src/{io,os,path}`, matching headers and tests |
| 16 text/parsing | `std/src/{regexp,text,html}`, matching headers and tests |
| 17 image | `std/src/image`, matching headers and tests |
| 18 network core | `std/src/net/{tcp,udp,netip,resolve,interface,mail,textproto,smtp,url}` plus `std/src/net/_net_*` and `std/src/net/idna_inc.h`, matching headers and tests |
| 19 HTTP/RPC/WebSocket | `std/src/net/{http,grpc,rpc,websocket}`, matching headers, interop workflows, and tests |
| 20 QUIC/HTTP3 | `std/src/net/{quic,http3}`, matching headers, interop workflows, and tests |
| 21 object/debug formats | `std/src/debug`, matching headers and tests |
| 22 remaining services | `std/src/{time,unicode,uuid,log,mime,index}`, matching headers and tests |
| 23 cross-cutting completeness | `std/CMakeLists.txt`, `std/manifest.json`, all umbrella/platform headers including `_modules.h`, `_platform.h`, `crypto.h`, `encoding.h`, `net.h`, `net/io.h`, `net/http/http2.h`, and `mime/rfc2047_safe.h`; `tests/neverc/StdLibTests.cpp`; every non-`test_*.c` asset under `tests/neverc/std`; `tests/neverc/{NetworkProtocolFuzzer.cpp,NetworkProtocolFuzzAdapters.c,TlsProtocolFuzzer.cpp}`; related interop/fuzz workflows and CI scripts |

- [ ] Generate a coverage ledger from `rg --files std tests/neverc/std` plus the named external fuzz/interop files, assigning every source, public/internal header, manifest entry, generated table, test, benchmark, support file, script, and relevant workflow to exactly one primary audit owner (secondary reviewers are allowed).
- [ ] Dispatch audits in batches of at most three beside the root agent until all 23 audit subagents have completed; omit model overrides so every subagent inherits the root model.
- [ ] Record every canonical agent task name, exact scope, completion status, and report path/summary in the coverage ledger before defining the final SHA.
- [ ] Triage every report against current source and existing tests; reject speculative, duplicate, or already-fixed findings.
- [ ] Rank confirmed defects by correctness/security impact and isolate independent fixes into separate commits.

## Task 3: Add one failing regression test per confirmed defect

**Files:**
- Modify: the exact existing or new focused test, including specialized `_oom`, `_abi`, `_concurrency`, `_entropy_failure`, fuzz, or interop files when appropriate.
- Modify when registration/dependencies change: `tests/neverc/StdLibTests.cpp`
- Modify when any symbol/header/method registration is missing or incorrect: `std/manifest.json` and its checked-in generated consumers.

- [ ] Write the smallest regression test that distinguishes correct behavior from current behavior.
- [ ] Include boundary, overflow, allocation-failure, aliasing, or concurrency coverage when it is part of the root cause.
- [ ] Register new tests or dependency sources in `tests/neverc/StdLibTests.cpp`.
- [ ] Review the diff without compiling or executing locally.
- [ ] Before creating the red test commit, classify the test as portable stock-Clang or native/NeverC-specific. If it is native/NeverC-specific and the reproducer is absent, restore the clean integration-base tree, execute Task 6's provisioning step as a standalone workflow-only `[skip ci]` commit on `dev`, wait until GitHub registers it, fetch/record that new integration base, and only then reapply the test patch.
- [ ] Commit the test-first change locally on `dev`, create a unique temporary tag at that commit, and push only the tag (never the `dev` ref).
- [ ] For a portable stock-Clang test, trigger `.github/workflows/debug-std-quick-check.yml` at the temporary tag with only the affected test names and complete dispatch inputs.
- [ ] For NeverC-specific syntax/builtins, ABI, Windows, macOS, ARM64, LTO, or compiler/toolchain behavior, skip quick-check and dispatch the already-provisioned native artifact-backed reproducer at the temporary tag.
- [ ] Confirm the intended assertion fails. Treat compilation failure, harness failure, timeout unrelated to the assertion, or an unexpected pass as invalid evidence; correct/reject the test before proceeding.
- [ ] Capture the run URL/log evidence, then retain the temporary tag only until the paired fix passes.

## Task 4: Implement the minimal source fix

**Files:**
- Modify: the exact implementation `.c` or internal `.h` file named by the confirmed finding, including generated tables when they are the defect source.
- Modify only if the contract/registration is wrong: the exact public header, `std/manifest.json`, or checked-in generated consumer.

- [ ] Restore the violated invariant using the existing module conventions or the authoritative upstream/reference structure.
- [ ] Avoid unrelated refactors and preserve ABI unless the defect itself is an ABI violation.
- [ ] Cross-check sibling paths for the same bug pattern.
- [ ] Review the patch and object list locally without running a build or test.
- [ ] Commit the fix locally after the red test commit.
- [ ] Fetch and require `origin/dev` to equal the recorded integration base, then push the test+fix history together to `dev` without force.

## Task 5: Validate the fix only on GitHub Actions

**Files:**
- Reuse: `.github/workflows/debug-std-quick-check.yml`
- Temporarily create if native reproduction is required: `.github/workflows/debug-std-runtime-repro.yml`

- [ ] Re-run the exact targeted quick-check test with the same sanitizer/defines/flags as the failing run.
- [ ] Require the targeted run to pass and inspect logs for sanitizer warnings, timeouts, and skipped assertions.
- [ ] For nonportable cases, run the same native reproducer with the post-fix source SHA/artifact and identical inputs.
- [ ] Delete the temporary red-test tag locally and remotely after paired validation.
- [ ] Wait for every check in the exact-SHA acceptance matrix; treat cancelled/skipped required jobs as unresolved and do not push another fix while they run.
- [ ] For a flaky failure, require repeated native runs with zero failures rather than accepting one green run.

## Task 6: Debug CI-only failures with hard evidence

**Files:**
- Temporarily create: `.github/workflows/debug-std-runtime-repro.yml`
- Modify after evidence identifies the culprit: the exact source/test/internal-header/registration file from the backtrace.

- [ ] Use `gh run view <run-id> --log-failed` to identify the failed step, signal, platform, determinism, and last green commit.
- [ ] Distinguish a compiler/tool crash from a generated-program crash.
- [ ] Give the dispatch-only workflow `contents: read` and `actions: read` permissions plus explicit inputs for source ref/SHA, run ID, artifact ID/name/digest, runner/platform, focused test, exact flags, iteration count, and expected result.
- [ ] Record producer and consumer provenance separately: `(artifact producer SHA, run ID, artifact ID/name, digest)` and `(checked-out consumer/test SHA)`. Require identical SHAs unless the exact producer-to-consumer diff is proven to contain only tests or dispatch harness files that cannot alter the compiler artifact; document that exception explicitly.
- [ ] For a post-upload failure, download and digest-verify the exact artifact on the GitHub runner. Never download CI binaries to the local machine and do not rebuild the full toolchain.
- [ ] If the failing run has no artifact, use the last compatible green compiler artifact to compile/run the exact failing-source test on the native runner; if the compiler build itself fails before artifacts, add focused diagnostics to the existing failing build step or build only the minimal implicated target on the matching runner and state why artifact reuse is impossible.
- [ ] Match native runners and symbols: Ubuntu x64 `ubuntu-22.04`/GDB/core, Ubuntu ARM64 `ubuntu-22.04-arm`/GDB/core, macOS ARM64 `macos-15`/LLDB, Windows x64 `windows-2022`/PDB plus ProcDump or CDB/WinDbg, Windows ARM64 `windows-11-arm` with the corresponding dump tooling.
- [ ] Repeat the reproducer enough times to exceed the observed failure rate; capture exit codes, determinism hashes (excluding benign debug metadata), dumps, all-thread backtraces, and the explicit pass/fail summary.
- [ ] Fix the evidenced source invariant and validate with the identical native reproducer.
- [ ] Provision the dispatch-only workflow on `dev` before any task attempts to dispatch it: add only that file, commit with `[skip ci]`, push, and wait until GitHub registers it on the default branch.
- [ ] Trigger it manually, wait for every dispatch run, and delete it on every success/rejection/abandonment path.
- [ ] Commit and push `.github/workflows/debug-std-runtime-repro.yml` deletion, then validate the cleanup SHA against all workflows triggered by a workflow-file change.

## Task 7: Close one audit iteration and continue monitoring

**Files:**
- Update: `docs/superpowers/plans/2026-08-30-std-library-hardening.md`

- [ ] Confirm all 23 audit reports and the complete coverage ledger were triaged and every accepted finding has a test and fix.
- [ ] Delete every temporary local/remote red-test tag and temporary debug workflow; push any required cleanup and wait for its exact-SHA checks.
- [ ] Commit the finalized plan/coverage tracking only after cleanup, then select that commit (or the later required cleanup commit) as the candidate final SHA; do not edit tracked files after it is selected.
- [ ] If any cleanup or tracking edit occurs after final-SHA selection, discard the old selection and restart the complete exact-SHA validation cycle on the new SHA.
- [ ] Confirm `git status` is clean and `dev` equals `origin/dev`.
- [ ] Confirm all required Actions for the final SHA succeeded.
- [ ] Re-scan analogous paths and open the next bounded audit iteration only when new evidence exists.
