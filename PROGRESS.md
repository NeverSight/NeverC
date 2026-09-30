# NeverC Progress

This document tracks repository issues, pull requests, verified CI observations, and the next actionable work. The immediate priorities are to resolve the documentation-navigation failure, reconcile the translation roadmap with implementation evidence, and define a small UI foundation milestone.

- Snapshot: **2026-09-30 11:51 Asia/Shanghai (UTC+08:00)** / **2026-09-30 03:51 UTC**
- Source branch: `dev`
- Inspected revision: [04299d7d284b27c3d1eb86495d75ab63243d80c3](https://github.com/NeverSight/NeverC/commit/04299d7d284b27c3d1eb86495d75ab63243d80c3), “Support callback results in C++ transform”
- Scope: daily planning and progress reporting. Priorities below are recommendations, not assigned deadlines or approved release commitments.
- Related references: [product roadmap](docs/roadmap.md), [DynCode-specific progress](docs/dyncode-compiler-progress.md), [translation documentation](docs/translate.md). Those documents retain their existing scope.

## Current status

| Measure | Observed value | Qualification |
| --- | --- | --- |
| Open issues | 9 | #7–#12 and #16–#18 |
| Open pull requests | 0 | No current PR review queue was found |
| Open issues with an assignee | 0 of 9 | Ownership needs an explicit decision |
| Open issues with a milestone | 0 of 9 | No delivery dates inferred |
| Priority labels on open issues | None | #16–#18 have enhancement/roadmap labels; #16 also has area:translate |
| Workflows for the inspected revision | 12 | 9 running, 1 queued, 1 successful, 1 failed |

Counts were checked through the repository issue collection, with pull requests excluded from issue counts. The collection returned 17 total issue/PR records with a 100-record limit. CI was queried for the exact inspected SHA, also below its 100-record limit.

## Today's top priorities

### 1. Resolve the documentation-navigation failure

**Evidence:** [lint-docs](https://github.com/NeverSight/NeverC/actions/runs/36665949091) failed in [check-docs-navigation](https://github.com/NeverSight/NeverC/actions/runs/36665949091/job/109730522878). The docs-facts job succeeded, and the navigation job's 10 layout unit tests passed before its link checker failed.

The failure log reports unresolved `type_traits`, `initializer_list`, and `string_view` anchors; mismatched links across translated guides; and allocation-signature text interpreted as links. Inspect the documents and checker before deciding which findings are document defects and which may be parser false positives.

- Next action: reproduce the reported checks and correct only confirmed defects.
- Acceptance: the layout unit tests and `utils/plugin-api/check-docs-links.py` pass on the correcting revision, with a linked successful navigation job.
- Dependency: no product decision is needed for diagnosis; a passing compiler build alone does not resolve this documentation failure.
- Owner: unassigned.

### 2. Reconcile translation progress with issue #16

**Evidence:** [#16](https://github.com/NeverSight/NeverC/issues/16) still describes the feature as planned and leaves its acceptance checklists open. [Translation documentation at the inspected revision](https://github.com/NeverSight/NeverC/blob/04299d7d284b27c3d1eb86495d75ab63243d80c3/docs/translate.md) describes `cpp-core-v2`, bounded project translation, and selected standard-library support. The current [cpp-frontend-tools workflow](https://github.com/NeverSight/NeverC/actions/runs/36665949155) succeeded.

- Next action: map each P0–P3 acceptance gate to exact commits, tests, platform execution, and installed-package evidence; separate implemented, verified, and remaining scope.
- Acceptance: each completed claim has linked evidence; unresolved platform/runtime/install gates remain explicit; follow-on adapter work has bounded scope.
- Dependency: the issue places the E Language adapter after the bounded C++ P3 baseline. Documentation or one successful workflow does not prove that entire baseline complete.
- Owner: unassigned.

### 3. Define the smallest UI foundation deliverable

**Evidence:** [#7](https://github.com/NeverSight/NeverC/issues/7) defines the core/draw/HAL/host split. [#8](https://github.com/NeverSight/NeverC/issues/8), [#9](https://github.com/NeverSight/NeverC/issues/9), [#11](https://github.com/NeverSight/NeverC/issues/11), and [#12](https://github.com/NeverSight/NeverC/issues/12) explicitly depend on it; [#10](https://github.com/NeverSight/NeverC/issues/10) also depends on #8 and #9.

- Next action: scope the first independently verifiable slice around the C23 contracts, a null HAL, one real host/backend, and a triangle-list window.
- Acceptance: core/draw compile without OS/GPU headers; a null backend records commands for tests; one real host/backend presents the agreed sample; overlay builds exclude the web host.
- Dependency: select the initial platform/backend and owner before assigning implementation work.
- Owner: unassigned.

## Open work and dependencies

| Issue | Workstream | Evidence-backed tracking state | Next gate or dependency |
| --- | --- | --- | --- |
| [#7](https://github.com/NeverSight/NeverC/issues/7) | UI architecture | Open architecture proposal | Core/draw and HAL/host contracts with minimal verified implementation |
| [#8](https://github.com/NeverSight/NeverC/issues/8) | Immediate-mode UI | Open proposal | #7; widget/demo and input-capture validation |
| [#9](https://github.com/NeverSight/NeverC/issues/9) | Renderer HAL | Open proposal | #7; null tests and first real renderer |
| [#10](https://github.com/NeverSight/NeverC/issues/10) | Owned-process overlay host | Open proposal | #7, #8, #9; frame/input integration |
| [#11](https://github.com/NeverSight/NeverC/issues/11) | Retained NXML UI | Open proposal | #7; stable tree identity and inspector contract |
| [#12](https://github.com/NeverSight/NeverC/issues/12) | Desktop HTML/CSS host | Open proposal | #7; desktop-only runtime boundary |
| [#16](https://github.com/NeverSight/NeverC/issues/16) | Source translation | Open roadmap; implementation documentation has advanced beyond issue wording | Reconcile acceptance evidence before declaring phases complete |
| [#17](https://github.com/NeverSight/NeverC/issues/17) | NeverC Studio | Open RFC | Bounded shell prototype and shared language/build/diagnostic services |
| [#18](https://github.com/NeverSight/NeverC/issues/18) | NeverC Engine | Open RFC | M0: one platform/backend and an animated, controllable sprite with debug panel |

The UI, Studio, and Engine entries describe issue/RFC status; this report does not independently certify whether every proposed component exists in the source tree. Studio's basic editing/building need not wait for the complete UI runtime. Engine translation is optional, and its 2D milestone need not wait for 3D.

## CI snapshot

All rows below belong to the inspected revision. Running and queued jobs have no final result.

| Workflow | State |
| --- | --- |
| [Code Quality: Push on dev](https://github.com/NeverSight/NeverC/actions/runs/36665948382) | in progress |
| [VBS enclave differential CI](https://github.com/NeverSight/NeverC/actions/runs/36665949128) | in progress |
| [windows-arm64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36665949171) | in progress |
| [windows-x64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36665949111) | in progress |
| [windows-x64-neverc-build-clang-lto](https://github.com/NeverSight/NeverC/actions/runs/36665949129) | in progress |
| [linux-arm64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36665949272) | in progress |
| [windows-arm64-neverc-build-clang-lto](https://github.com/NeverSight/NeverC/actions/runs/36665949114) | in progress |
| [linux-x64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36665949421) | in progress |
| [lint-docs](https://github.com/NeverSight/NeverC/actions/runs/36665949091) | failure |
| [macos-arm64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36665949059) | in progress |
| [cpp-frontend-tools](https://github.com/NeverSight/NeverC/actions/runs/36665949155) | success |
| [python-plugin-bindings](https://github.com/NeverSight/NeverC/actions/runs/36665949092) | queued |

The failed navigation check is confirmed by its job log. No claim is made that every workflow here is a branch-required check. Workflow success does not establish broader platform or release support beyond the tests actually run.

## Recently completed work

- **2026-09-25:** [PR #19](https://github.com/NeverSight/NeverC/pull/19) merged. It clarifies GKI source-layout alerts and retries transient probes. Its description reports 9 layout tests, 27 upstream tests, and a clean diff check; those local test counts were not rerun for this report.
- **2026-09-03:** [PR #13](https://github.com/NeverSight/NeverC/pull/13) merged, updating the Go gRPC dependency.
- **2026-08-06:** [Issue #3](https://github.com/NeverSight/NeverC/issues/3) was closed as completed for Python plugin bindings. Related [PR #4](https://github.com/NeverSight/NeverC/pull/4) and [PR #5](https://github.com/NeverSight/NeverC/pull/5) were merged. Historical closure does not replace current regression evidence.

## Reporting rules

- Refresh the timestamp, inspected SHA, issue/PR state, and exact-revision CI together.
- Keep completed history and append a dated daily entry; preserve contributor edits.
- Distinguish GitHub metadata, documented capabilities, author-reported test results, and independently observed CI.
- Record unknowns and blockers explicitly. Do not infer completion percentages, deadlines, owners, or universal platform support.
- Use the latest relevant evidence when a branch advances; do not carry forward an old failure as a new revision's result.
- This snapshot does not run builds, repair code, change issue states, or prove release readiness.

## Daily log

### 2026-09-30

- Established the initial baseline: 9 open issues and 0 open PRs.
- Identified the documentation-navigation failure on revision 04299d7; confirmed docs-facts success and cpp-frontend-tools workflow success.
- Flagged the mismatch between #16's planned-status wording and current translation documentation.
- Proposed the three priorities above and recorded missing issue ownership/milestones.
- Verification scope: read-only GitHub issue/PR metadata, repository documentation, exact-revision workflow states, and the failed navigation job log. No compiler or test suite was executed for this report.
