# HTTP/3 Shutdown CI Investigation Spec

## User Requirements

- Resolve the latest GitHub CI/CD failures completely, and stop when current CI/CD is all green.
- Treat the developer worktree's existing diff as prior work; do not overwrite it or claim it as the new fix.
- Find and fix the root cause rather than continuing a sequence of timing mitigations.
- Follow `.agents/skills/github-ci-runtime-debugging/SKILL.md` for CI-only/platform-specific failures.
- A temporary manually dispatched Action is allowed when native-runner evidence is required.
- Avoid rebuilding the full NeverC binary/toolchain merely to reproduce the std network sanitizer gate.
- Delete temporary debug workflow files after the issue is resolved.

## Current Evidence

- Failing commit: `2310d3c4f443cadce2c863089eb306231ef5df0c` on `dev`.
- Failing run: `30784654381`, `debug-network-sanitizer-gates`, native `ubuntu-22.04`, Clang 22 with ASan/UBSan.
- Failure rate in that run: one of 20 `http3_e2e` iterations.
- A 50-iteration native baseline reproduced five failures; diagnostic run `30787237012` reproduced three failures in 40 iterations.
- Failure signal: slow-drain request returns `HTTP/3 response is invalid`; no ASan or UBSan finding appears.
- First divergent diagnostic state: a stale QUIC loss timeout fires with no ack-eliciting packet in flight, creates Initial/Handshake recovery traffic, and can reach `PTO limit exceeded` before the handler response is readable.
- Deterministic invariant: ordinary PTO selection must skip every packet-number space without an ack-eliciting packet in flight; after peer validation, no-in-flight PTO is cancelled, while an unvalidated client uses one fresh anti-deadlock deadline and an amplification-blocked server cancels PTO.
- Local fixed-build evidence: Clang ASan/UBSan focused suites pass 52/52 loss checks and 89/89 connection checks, followed by 20/20 complete HTTP/3 end-to-end iterations with no zero-outstanding loss timeout.
- The first native fixed-build attempt exposed a second deterministic invariant: time-threshold loss only applies to packet numbers at or below `largest_acked`, and a packet is lost when its deadline is reached, not only after it. Focused fixed-build evidence is now 56/56 loss checks and 89/89 connection checks, followed by another 20/20 local HTTP/3 iterations.
- Native fixed-build run `30789229179` then passed 50/50 Clang 22 ASan/UBSan HTTP/3 iterations (`46 checks, 0 failed` each, `failures=0`), with no sanitizer or PTO-limit signal.
- The diagnostic-free production tree subsequently passed the complete local network-core ASan/UBSan and TSan gate, the exact 56/56 and 89/89 focused suites, diff checks, and parsing of all 31 remaining workflow YAML files.
- Test boundary: `tests/neverc/std/test_http3_e2e.c`, unified-server shutdown starts after the `/slow` handler enters and before its 250 ms sleep completes.
- More than ten recent commits have increased grace periods/retries or changed draining-read behavior without eliminating the failure.
- Developer worktree before investigation: modified `std/src/net/http3/http3_server.c` and `std/src/net/quic/quic_conn.c`.
- Local host is macOS arm64 and its Docker daemon is unavailable, so native Ubuntu x64 evidence must come from GitHub Actions.

## Investigation Deliverable

Produce one stage-specific native-runner trace that identifies the first invalid response-read invariant. The investigation must not present increased sleeps, retries, grace windows, or absolute timeout limits as a root-cause fix.
