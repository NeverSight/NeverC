# HTTP/3 Shutdown CI Root-Cause Investigation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Identify and fix the root cause of the latest HTTP/3 shutdown CI failure, integrate the fix into `dev`, verify current CI/CD is green, and remove temporary debug workflows without changing the developer worktree's pre-existing edits.

**Architecture:** Work from a detached copy of failing commit `2310d3c4f443cadce2c863089eb306231ef5df0c` on a temporary debug branch. Add compile-time-gated diagnostics at the HTTP/3 client parser and QUIC connection-close boundaries, enable them only in the existing `workflow_dispatch` harness, and repeat the single failing test on `ubuntu-22.04`. Transfer the native evidence into this plan, replace the root-fix gate below with an evidence-backed exact implementation, review it, then implement and validate on `dev`; remove all diagnostics and temporary workflows only after the reproducer and current CI/CD are green.

**Tech Stack:** C11, NeverC std HTTP/3 and QUIC internals, Clang 22, ASan/UBSan, GitHub Actions, GitHub CLI.

---

### Task 0: Apply the CI Runtime Debugging Playbook

**Files:**
- Read: `.agents/skills/github-ci-runtime-debugging/SKILL.md`

- [x] **Step 1: Read the complete skill before investigation**

Follow its reproduction-first sequence for Tasks 1-6: characterize logs and platform scope, prefer the exact lightweight artifact/harness, distinguish tool crash from produced-program/test failure, use the native `workflow_dispatch` runner because local emulation is unavailable, validate with high iterations, cross-check sibling paths, and delete temporary workflows after proof.

- [x] **Step 2: Record why native runner execution is required**

Expected: investigation evidence states that the failing platform is native Ubuntu x64 while the local host is macOS arm64 with an unavailable Docker daemon.

### Task 1: Add Boundary Diagnostics

**Files:**
- Modify: `std/src/net/http3/http3_server.c`
- Modify: `std/src/net/quic/quic_transport.c`
- Modify: `.github/workflows/debug-network-sanitizer-gates.yml`

- [x] **Step 1: Add an HTTP/3 trace helper guarded by `NEVERC_CI_HTTP3_TRACE`**

The helper writes stage, stream ID, connection state, stream state, receive length, receive offset, known final size, FIN flag, response body length, status code, and whether final headers/trailers were seen to stderr. The disabled definition is a no-op so production behavior and timing remain unchanged.

- [x] **Step 2: Label every failure return in `h3_stream_read` and `h3_client_read_response`**

Emit distinct stages for exhausted polling, closed connection, frame-type varint, frame-length varint, payload read, QPACK parse, invalid DATA ordering, append failure, forbidden request-stream frame, missing final headers, and content-length mismatch.

- [x] **Step 3: Log the received CONNECTION_CLOSE transition**

Immediately after parsing CONNECTION_CLOSE, log packet number, error code, reason, and each application request stream's buffered receive/FIN/final-size fields before marking it connection-closing.

- [x] **Step 4: Enable diagnostics only in the temporary workflow harness**

Add `-DNEVERC_CI_HTTP3_TRACE=1` to the workflow's single-test `flags` array. Do not alter normal CMake or sanitizer-script flags.

- [x] **Step 5: Compile locally to reject warnings**

Run the single-test compile command from the workflow with Apple Clang and `-DNEVERC_CI_HTTP3_TRACE=1`.

Expected: compile succeeds with `-Wall -Wextra -Werror`; platform-dependent runtime behavior is not used as proof.

### Task 2: Reproduce on Native Ubuntu x64

**Files:**
- Test: `.github/workflows/debug-network-sanitizer-gates.yml`

- [x] **Step 1: Commit only the temporary plan and diagnostic files**

Run: `git diff --check && git status --short`

Expected: only the three diagnostic files and this plan are changed.

- [x] **Step 2: Push without starting the heavy matrix**

Commit message: `ci(debug): trace HTTP/3 shutdown response boundary [skip ci]`

Push branch: `codex/debug-http3-shutdown-20260803`

- [x] **Step 3: Dispatch 40 exact-test iterations**

Run: `gh workflow run debug-network-sanitizer-gates.yml --ref codex/debug-http3-shutdown-20260803 -f test=http3_e2e -f iterations=40`

Expected: at least one existing flaky slow-drain failure, with a stage-specific trace preceding `HTTP/3 response is invalid`.

If no failure occurs, dispatch another 40 iterations up to three additional times. If 160 serial iterations remain green, add a workflow input that launches 2, 4, and 8 independent test processes in parallel, recording each process separately. Commit and push that workflow-only fallback change to the same temporary branch with `[skip ci]`, then dispatch 40 parallel batches from the resulting SHA. Do not infer a root cause from an unreproduced run.

- [x] **Step 4: Compare successful and failed transitions**

Inspect the full failed-step log. Record the first divergent stage and all stream/connection state values; reject hypotheses that do not explain both success and failure.

- [x] **Step 5: State and minimally test one root-cause hypothesis**

Use one diagnostic-only variable change or one focused unit harness. Do not increase retry counts, shutdown limits, grace windows, or sleeps as a proposed fix.

Expected: the hypothesized invariant predicts whether the response parser sees complete HEADERS, DATA, trailers, and clean FIN.

- [x] **Step 6: Transfer evidence before cleanup**

Append an `Investigation Evidence` section to this plan containing the run URL/ID, full failing commit SHA, first divergent trace stage, successful-state comparison, relevant values, and the single confirmed root-cause hypothesis. Keep excerpts concise but sufficient to reconstruct the conclusion after the temporary branch is deleted.

Expected: `git diff` shows the evidence recorded in the plan before any cleanup command runs.

### Task 3: Specify and Review the Root Fix

**Files:**
- Modify: `docs/superpowers/plans/2026-08-03-http3-shutdown-ci-investigation.md`
- Review against: `docs/superpowers/plans/2026-08-03-http3-shutdown-ci-investigation-spec.md`
- Test: `tests/neverc/std/test_quic_loss.c`
- Test: `tests/neverc/std/test_quic_conn.c`
- Modify: `std/src/net/quic/quic_loss.c`
- Modify: `std/src/net/quic/quic_conn.c`
- Modify: `std/src/net/quic/quic_transport.c`
- Modify: `std/src/net/quic/_quic_internal.h`

- [x] **Step 1: Preserve the confirmed lifecycle invariant**

The violated invariant is: once the peer has completed address validation, a QUIC PTO timer is armed only while at least one ack-eliciting packet remains in flight. `neverc_quic_loss_get_timeout()` currently reads the persistent `time_of_last_ack_eliciting` field after every sent packet has been acknowledged and removed, and `neverc_quic_conn_tick()` treats that historical timestamp as a live timer. A server endpoint can assume its client peer implicitly validated the server. A client endpoint must retain the RFC anti-deadlock PTO only until a Handshake ACK or HANDSHAKE_DONE proves that its server peer validated the client's address. That exceptional timer starts from the transition's current time, not the historical send time. Separately, a server at its 3x anti-amplification limit cancels PTO because it cannot send a probe.

Add these deterministic connection-state regressions to `tests/neverc/std/test_quic_conn.c` and call all three from `main`:

```c
static void seed_idle_loss_history(struct neverc_quic_conn *conn) {
    neverc_quic_loss_on_sent(&conn->loss, QUIC_PNS_APPLICATION, 0, 1000,
                             1200, 1);
    neverc_quic_loss_mark_acked(&conn->loss, QUIC_PNS_APPLICATION, 0, 1100);
    neverc_quic_loss_on_ack(&conn->loss, QUIC_PNS_APPLICATION, 0, 0, 1100);
    neverc_quic_loss_cleanup(&conn->loss, QUIC_PNS_APPLICATION);
}

static void test_conn_loss_timeout_disarmed_after_address_validation(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->peer_completed_address_validation = 1;
    seed_idle_loss_history(conn);

    ASSERT_EQ(neverc_quic_loss_get_timeout(&conn->loss, 1), 0);
    ASSERT_EQ(neverc_quic_conn_loss_timeout(conn, 2000), 0);

    neverc_quic_conn_destroy(conn);
}

static void test_conn_loss_timeout_fresh_before_peer_validation(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_CLIENT, -1);
    conn->peer_completed_address_validation = 0;
    seed_idle_loss_history(conn);

    uint64_t expected = 2000 + neverc_quic_pto(&conn->loss.rtt, 0);
    ASSERT_EQ(neverc_quic_conn_loss_timeout(conn, 2000), expected);
    ASSERT_EQ(neverc_quic_conn_loss_timeout(conn, 2100), expected);

    neverc_quic_loss_on_sent(&conn->loss, QUIC_PNS_HANDSHAKE, 1, 2150,
                             1200, 1);
    ASSERT_TRUE(neverc_quic_conn_loss_timeout(conn, 2150) > 0);
    neverc_quic_loss_mark_acked(&conn->loss, QUIC_PNS_HANDSHAKE, 1, 2160);
    neverc_quic_loss_on_ack(&conn->loss, QUIC_PNS_HANDSHAKE, 1, 0, 2160);
    neverc_quic_loss_cleanup(&conn->loss, QUIC_PNS_HANDSHAKE);
    uint64_t reentered = 2200 + neverc_quic_pto(&conn->loss.rtt, 0);
    ASSERT_EQ(neverc_quic_conn_loss_timeout(conn, 2200), reentered);
    ASSERT_TRUE(reentered != expected);

    neverc_quic_conn_destroy(conn);
}

static void test_conn_loss_timeout_cancelled_at_amplification_limit(void) {
    struct neverc_quic_conn *conn =
        neverc_quic_conn_create(QUIC_SIDE_SERVER, -1);
    conn->address_validated = 0;
    conn->bytes_received_before_validation = 100;
    conn->bytes_sent_before_validation = 300;
    neverc_quic_loss_on_sent(&conn->loss, QUIC_PNS_INITIAL, 0, 1000,
                             1200, 1);

    ASSERT_EQ(neverc_quic_conn_loss_timeout(conn, 2000), 0);

    neverc_quic_conn_destroy(conn);
}
```

Also add this focused `test_quic_loss.c` regression and call it from `main`; it leaves an older Initial packet in flight while a newer Application Data space becomes empty but retains a later historical timestamp:

```c
static void test_loss_timeout_skips_empty_packet_number_space(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_INITIAL, 0, 1000, 1200, 1);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_APPLICATION, 0, 2000,
                             1200, 1);
    neverc_quic_loss_mark_acked(&ld, QUIC_PNS_APPLICATION, 0, 2100);
    neverc_quic_loss_on_ack(&ld, QUIC_PNS_APPLICATION, 0, 0, 2100);
    neverc_quic_loss_cleanup(&ld, QUIC_PNS_APPLICATION);

    uint64_t expected = 1000 + neverc_quic_pto(&ld.rtt, 0);
    ASSERT_EQ(neverc_quic_loss_get_timeout(&ld, 1), expected);

    neverc_quic_loss_destroy(&ld);
}
```

Add one more `test_quic_loss.c` regression and call it from `main`:

```c
static void test_loss_timeout_skips_app_before_handshake_confirmation(void) {
    quic_loss_detector_t ld;
    neverc_quic_loss_init(&ld);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_APPLICATION, 0, 1000,
                             1200, 1);
    neverc_quic_loss_on_sent(&ld, QUIC_PNS_INITIAL, 0, 2000, 1200, 1);

    uint64_t initial = 2000 + neverc_quic_pto(&ld.rtt, 0);
    uint64_t app = 1000 + neverc_quic_pto(&ld.rtt, 1);
    ASSERT_TRUE(app < initial);
    ASSERT_EQ(neverc_quic_loss_get_timeout(&ld, 0), initial);
    ASSERT_EQ(neverc_quic_loss_get_timeout(&ld, 1), app);

    neverc_quic_loss_destroy(&ld);
}
```

In `quic_loss.c`, change `neverc_quic_loss_get_timeout()` to accept `handshake_confirmed` and replace the global `last_ack_eliciting` PTO calculation with a per-space loop. A private predicate checks `ack_eliciting && in_flight && !acked && !lost` in that exact space; empty spaces are skipped, Application Data is skipped until `handshake_confirmed`, Initial/Handshake candidates exclude `max_ack_delay`, the confirmed Application Data candidate includes it, backoff applies saturatingly to each duration, and the earliest candidate is returned. Update existing focused test calls to pass confirmed state where they are not testing this distinction. Preserve the existing earliest time-threshold loss deadline priority.

Add `handshake_confirmed`, `peer_completed_address_validation`, and `validation_pto_deadline_ms` to `neverc_quic_conn`. Initialize handshake confirmation to false and peer-completed validation to true for `QUIC_SIDE_SERVER`. Set server handshake confirmation when TLS establishment completes; set client handshake confirmation and peer-completed validation on HANDSHAKE_DONE, and set client peer-completed validation after a valid Handshake-space ACK. The implementation in `quic_conn.c` adds two internal functions declared in `_quic_internal.h`: `neverc_quic_conn_has_ack_eliciting_in_flight()` scans every space using the same packet predicate; `neverc_quic_conn_loss_timeout(conn, now_ms)` passes `handshake_confirmed` into ordinary per-space selection, preserves any time-threshold loss deadline, cancels PTO at the server anti-amplification limit, returns the per-space raw PTO while a packet is in flight, cancels it once peer validation is complete, and otherwise stores/returns one fresh saturating `now_ms + (PTO << pto_count)` deadline without Application Data `max_ack_delay`. Clear `validation_pto_deadline_ms` whenever the exceptional zero-in-flight/unvalidated-client condition ceases to apply, as well as when that deadline fires, so later re-entry starts from its new current time. Replace tick's direct raw-timeout call with the connection-aware function. When the exceptional client timer fires with no packet in flight, choose Handshake keys for its probe when available, otherwise Initial keys. Do not clear historical timestamps, change retry limits, or add sleeps.

RED command:

```bash
clang -std=gnu11 -O1 -g -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-function \
  -fsanitize=address,undefined \
  -Istd/include -Istd/src/net -Istd/src/net/quic \
  tests/neverc/std/test_quic_loss.c -pthread -lm -lresolv \
  -o /tmp/neverc-quic-loss-asan-ubsan
ASAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  /tmp/neverc-quic-loss-asan-ubsan

clang -std=gnu11 -O1 -g -Wall -Wextra -Werror \
  -Wno-unused-parameter -Wno-unused-function \
  -fsanitize=address,undefined \
  -Istd/include -Istd/src/net -Istd/src/net/quic \
  tests/neverc/std/test_quic_conn.c std/src/crypto/rand/rand.c \
  -pthread -lm -lresolv -o /tmp/neverc-quic-conn-asan-ubsan
ASAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  /tmp/neverc-quic-conn-asan-ubsan
```

Run the per-space tests first and make only ordinary PTO selection GREEN: empty spaces are skipped, and Application Data is ignored before handshake confirmation but can win afterward. That low-level change also makes the peer-validated no-packet cancellation test GREEN with both raw/effective timeout `0`; do not manufacture a separate RED for behavior already covered. Next add the unvalidated-client test, which is RED because raw timeout `0` must become a fresh `now_ms + PTO` anti-deadlock deadline; a second call must remain non-sliding, and ordinary traffic followed by re-entry must produce a new deadline. Finally add the anti-amplification-limit test and require timeout `0` even with an Initial packet in flight. Expected final GREEN: all QUIC loss and connection checks pass with no sanitizer finding.

- [x] **Step 2: Review the updated end-to-end plan**

Dispatch one plan-document reviewer using `.agents/skills/writing-plans/plan-document-reviewer-prompt.md`. Fix every blocking issue and repeat whole-plan review, up to three review cycles.

Expected: reviewer status is `Approved` before production implementation begins.

### Task 4: Add Regression Coverage and Implement the Root Fix

**Files:**
- Test: exact file and test name inserted by Task 3, Step 1
- Modify: exact production files inserted by Task 3, Step 1

- [x] **Step 1: Read and follow `.agents/skills/tdd/SKILL.md`**

- [x] **Step 2: Add the smallest deterministic regression test**

The test must exercise the violated invariant directly, without relying on a probabilistic sleep race.

- [x] **Step 3: Run the focused test before the fix**

Expected: deterministic failure with the stage/invariant recorded in Task 2.

- [x] **Step 4: Implement one minimal root-cause fix**

Do not bundle timeout increases, retry increases, unrelated protocol changes, or refactors.

- [x] **Step 5: Run focused and sibling tests**

Run the exact commands inserted in Task 3, Step 1, followed by `test_http3_e2e`, QUIC connection/transport tests, and `git diff --check`.

Expected: all focused and sibling tests pass, with no sanitizer finding.

- [x] **Step 6: Cross-check analogous HTTP/3/QUIC paths**

Audit every use of the changed completion/state predicate. Record whether client-initiated close, peer-initiated close, timeout, reset, and endpoint shutdown preserve the same invariant.

### Task 5: Validate on Native Runner and Integrate into `dev`

**Files:**
- Modify on `dev`: only the reviewed production/test fix, this evidence-backed plan/spec record, plus deletion of temporary debug workflows
- Delete: `.github/workflows/debug-network-sanitizer-gates.yml`
- Delete if its original issue is already green: `.github/workflows/debug-x64-clang-lto-fastfail.yml`

- [x] **Step 1: Commit and push the root fix and regression test to the temporary branch**

Inspect the staged diff to ensure it contains the approved Task 4 production/test changes, diagnostic macro support, and updated evidence/plan files only. Push with `[skip ci]` so only the manually dispatched harness runs.

Expected: the temporary branch SHA used by the workflow contains the root fix and regression test.

- [x] **Step 2: Validate the root fix on the temporary native-runner harness**

Dispatch at least the same number of serial and parallel iterations needed to reproduce the issue. Require zero failures and no sanitizer finding; one green iteration is insufficient.

- [x] **Step 3: Reconcile with the developer worktree's pre-existing edits**

Re-read `git status` and both original diffs. Apply the reviewed fix with `apply_patch` without reverting or claiming unrelated edits; if a pre-existing edit directly conflicts with the proven fix, stop and request direction.

- [x] **Step 4: Remove diagnostics and temporary workflows**

Delete compile-time diagnostics and both temporary debug workflow files as applicable. Verify no `debug-*` workflow introduced for this or an already-resolved issue remains tracked.

- [x] **Step 5: Persist the evidence-backed plan and spec on `dev`**

Apply the finalized plan and investigation spec under `docs/superpowers/plans/` to `dev`. The plan's `Investigation Evidence` section must include the native run ID/URL, full SHA, first divergent stage, successful comparison, and confirmed invariant. This preserves the reasoning after the temporary branch/worktree is deleted.

- [x] **Step 6: Run local verification on the reconciled `dev` worktree**

Run focused regression tests, HTTP/3 end-to-end tests, relevant QUIC tests, sanitizer gates available on the host, `git diff --check`, and workflow YAML syntax checks.

- [ ] **Step 7: Commit and push only the root fix, tests, evidence-backed plan/spec, and cleanup**

Use a non-interactive commit. Inspect the staged diff and preserve all unrelated user changes.

- [ ] **Step 8: Monitor every current `dev` CI/CD run to completion**

Use `gh run list` and `gh run view --log-failed`. Treat cancelled superseded runs and intentionally failing historical debug runs as historical, not current failures. If a current job fails, return to Task 1 for that signal and follow `.agents/skills/github-ci-runtime-debugging/SKILL.md` before changing code.

Expected: every workflow triggered by the final `dev` SHA completes successfully or is legitimately skipped; no temporary workflow is left behind.

### Task 6: Clean Up Investigation Assets

**Files:**
- Delete from temporary branch/worktree: diagnostic-only edits and this plan after evidence is transferred to the main investigation notes

- [ ] **Step 1: Stop stale debug runs after logs are captured and the final native validation is green**

Cancel only runs of `debug-network-sanitizer-gates` on the temporary branch that remain queued or in progress.

- [ ] **Step 2: Delete the remote temporary branch after Task 5 is complete**

Verify the exact branch name before deleting `codex/debug-http3-shutdown-20260803`.

- [ ] **Step 3: Remove the exact temporary worktree after Task 5 is complete**

Verify `/tmp/neverc-ci-debug.B2s6Mz` is registered by `git worktree list`, then remove that worktree and prune its empty metadata.

Expected: the developer worktree remains on `dev` with its original two modified files unchanged.

## Investigation Evidence

- Native baseline run `30786401314` on commit `2310d3c4f443cadce2c863089eb306231ef5df0c` reproduced five failures in 50 Clang 22 ASan/UBSan iterations without a sanitizer finding.
- A local macOS arm64 diagnostic build reproduced the same slow-drain failure. The first divergent transition was `connection-close-received pn=35 error=16 reason=PTO limit exceeded`; request stream 0 had `recv_len=0`, `recv_offset=0`, `recv_final_known=0`, and `recv_fin=0`, followed by `frame-type-varint`. A successful request stream reached `recv_offset=53`, known final size 53, and clean FIN before parsing completed.
- The deterministic per-space regression is RED on the failing implementation: an empty Application Data space retains a newer historical timestamp and incorrectly overrides the live Initial-space PTO (`2325` instead of `1300`). A second RED test proves Application Data is incorrectly selected before handshake confirmation (`2022` instead of the Initial-space `2997`).
- RFC 9002 Appendix A.8 cancels the loss timer after address validation when there are no ack-eliciting packets in flight. The confirmed root-cause hypothesis is that the implementation omits that in-flight guard and repeatedly interprets a historical send timestamp as a live PTO source while the HTTP handler is idle.
- Native diagnostic run `30787237012` on temporary commit `2bf1df9e7ee6ae7ffb70c789e3bf126bdb7b2843` reproduced three failures in 40 iterations. Iterations 13 and 29 received/initiated `PTO limit exceeded` before the slow response became readable; iteration 16 exhausted response polling while still established. Before each failure, the first recovery divergence was a loss timeout on a connection/space with `outstanding=0`, followed by Initial/Handshake probe traffic and rapidly accumulating recovery work. Successful slow requests reached `response-complete`, body length 7, final size 47, and clean FIN.
- With the root fix applied, the focused Clang ASan/UBSan suites pass 52/52 loss checks and 89/89 connection checks. A local macOS arm64 Clang ASan/UBSan HTTP/3 build then passed 20/20 complete end-to-end iterations (four requests each), with no sanitizer finding, PTO-limit close, or loss timeout having `outstanding=0,0,0`.
- Native fixed-build attempt `30788519602` on `27e3b839139defa6b1eb89c4de983df9cf4cd015` still failed twice in 50 iterations. Empty-space firing was gone, but the first new divergence showed Application Data `outstanding` growing from 3 to 19 while `now`, `timeout`, and the time-threshold loss deadline remained equal in one millisecond. `detect_lost_packets()` was scheduling packets newer than `largest_acked`, then using strict `elapsed > threshold` against a `sent + threshold` deadline; equality therefore neither declared loss nor advanced the deadline, and tick misclassified each immediate re-entry as another PTO.
- Two additional deterministic RED checks captured those defects: the newer packet incorrectly scheduled `loss_time=1202` instead of `0`, and exact deadline `1009` left the packet live with the same expired deadline. After skipping packet numbers above `largest_acked` and using the inclusive deadline boundary, the focused ASan/UBSan suites pass 56/56 loss checks and 89/89 connection checks. A rebuilt local HTTP/3 harness again passed 20/20 complete iterations, with deadlines advancing under normal exponential backoff and no tight-loop signature.
- Native fixed-build run `30789229179` on temporary commit `100e63172cdf1863b70c3c53803f211b899cca87` completed successfully: all 50 Clang 22 ASan/UBSan iterations reported `46 checks, 0 failed`, `failures=0`, with no sanitizer finding, PTO-limit close, or zero-outstanding timeout. This directly contrasts with baseline `30786401314` (5/50 failures) and first fixed-build attempt `30788519602` (2/50 failures).
- On the clean production tree after removing diagnostics and both debug workflows, `utils/ci/run-network-core-sanitizers.sh` passed completely: thread/net tests, protocol corpus, and HTTP/RPC/gRPC/QUIC/network-simulation/HTTP3 full-stack tests under both ASan/UBSan and TSan. The exact final focused suites also pass 56/56 loss checks and 89/89 connection checks, `git diff --check` is clean, and all 31 remaining workflow YAML files parse successfully.
