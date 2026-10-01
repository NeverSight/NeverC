# NeverC Progress

This document tracks repository issues, pull requests, static code-review findings, observed CI, and the next actionable work. The immediate priorities are to diagnose the Windows ARM64 COFF witness failure, finish documentation-navigation verification, and reconcile the translation roadmap with implementation evidence.

- Snapshot: **2026-10-01 09:06 Asia/Shanghai (UTC+08:00)** / **2026-10-01 01:06 UTC**
- Source branch: `dev`
- Reviewed source revision: [26c52fc7fad7f2f9218b488b4f34f9346927866d](https://github.com/NeverSight/NeverC/commit/26c52fc7fad7f2f9218b488b4f34f9346927866d), “Authenticate unique_ptr owner ordering result queries”
- Published static fix: [4a85206ec4b4457c2ef869cef40d4a8ac4decc6e](https://github.com/NeverSight/NeverC/commit/4a85206ec4b4457c2ef869cef40d4a8ac4decc6e), “fix(docs): ignore inline code in navigation links [skip ci]”
- Scope: bounded daily static review and progress reporting. No builds, tests, repository scripts, or program execution; no workflow dispatch or rerun. Four regression tests were added as source only. Priorities are recommendations, not assigned deadlines or release commitments.
- Related references: [product roadmap](docs/roadmap.md), [DynCode-specific progress](docs/dyncode-compiler-progress.md), [translation documentation](docs/translate.md). Those documents retain their existing scope.

## Current status

| Measure | Observed value | Qualification |
| --- | --- | --- |
| Open issues | 9 | #7–#12 and #16–#18; unchanged since September 30 |
| Open pull requests | 0 | #20 merged on September 30; no current PR review queue |
| Open issues with an assignee | 0 of 9 | Ownership needs an explicit decision |
| Open issues with a milestone | 0 of 9 | No delivery dates inferred |
| Priority labels on open issues | None | #16–#18 have enhancement/roadmap labels; #16 also has area:translate |
| Changes since prior inspected revision | 34 commits; 8 changed files | [04299d7…26c52fc](https://github.com/NeverSight/NeverC/compare/04299d7d284b27c3d1eb86495d75ab63243d80c3...26c52fc7fad7f2f9218b488b4f34f9346927866d); excludes today's fix/report commits |
| Workflows on reviewed source revision | 12 | 4 in progress, 7 queued, 1 failed; no successful workflow yet |
| Check runs on reviewed source revision | 23 | 4 in progress, 16 queued, 2 successful, 1 failed |
| Workflows on published fix | 1 queued | GitHub's dynamic Code Quality run; no validation pass inferred |

Pagination and review coverage: the all-state issue collection returned 18 issue/PR records and the pull-request collection returned 8 PRs, each with page size 100 and an empty second page. PRs are excluded from issue counts. All nine open issues report zero comments. Newly merged #20 has zero review submissions, inline review comments, and conversation comments in the checked collections. The exact-source workflow query returned 12 runs and an empty second page; the check-run query returned all 23 checks. Commit comparison returned all 34 commits and 8 files. No broad historical-CI completeness is claimed.

## Today's top priorities

### 1. Diagnose the Windows ARM64 COFF witness failure

**Evidence:** [Windows ARM64 Clang + LTO run](https://github.com/NeverSight/NeverC/actions/runs/36797991113) failed in the [COFF audit job](https://github.com/NeverSight/NeverC/actions/runs/36797991113/job/110165730634) on the reviewed source revision. The log records four failing configurations (MSVC/Clang at O0/O2) in `test_cmath_explicit_double_calls_preserve_values_and_remove_templates`: `CppFrontendToolchainTests.py:2785` expects one `_GENERIC_MATH2(pow)` match but finds zero in the runner's MSVC 14.51.36231 `cmath`. The compiler-build step was skipped; missing postbuild evidence is downstream, not proof of a compiler regression.

- Next action: inspect the exact installed SDK declaration and the fixture's provenance contract before choosing a replacement witness.
- Acceptance: preserve precise provenance checks, explain the SDK-shape difference, and obtain authorized exact-revision native evidence for all four configurations before declaring the platform gate restored.
- Dependency: the available failure establishes fixture/toolchain-shape incompatibility, not a safe replacement signature. No guard was weakened and no toolchain/dependency/security setting was changed.
- Owner: unassigned.

### 2. Finish documentation-navigation verification

**Evidence:** [PR #20](https://github.com/NeverSight/NeverC/pull/20) merged the literal-underscore heading fix. Today's [static fix](https://github.com/NeverSight/NeverC/commit/4a85206ec4b4457c2ef869cef40d4a8ac4decc6e) addresses a separate proven parser error: inline C++ signatures such as `operator new[](size_t, void *)` were treated as Markdown hrefs. The [historical September 30 log](https://github.com/NeverSight/NeverC/actions/runs/36665949091/job/109730522878) contains the exact false targets, and the corresponding signatures remain in the [current guide](https://github.com/NeverSight/NeverC/blob/26c52fc7fad7f2f9218b488b4f34f9346927866d/utils/translate-frontends/docs/cpp-core-v2.md#L1157-L1158).

- Next action: review the new ordered link/code tokenization and the remaining genuine translated-guide jump-parity gaps.
- Acceptance: literal code produces no navigation or path-filter target; real links, code labels, backticks in destinations, adjacent links, and escaped/unclosed delimiters retain their expected handling. A later authorized navigation run must pass both layout regressions and the full link checker.
- Dependency: four regression methods were written but not executed. The reviewed source revision's lint-docs workflow is queued, so yesterday's failed run is historical evidence, not today's CI result. The narrow patch does not claim to fix every navigation failure.
- Owner: unassigned.

### 3. Reconcile translation progress with issue #16

**Evidence:** [#16](https://github.com/NeverSight/NeverC/issues/16) remains open with its acceptance checklists unresolved. The 34-commit interval advances callback/record algorithm handling and vector/unique_ptr result queries. Static review found no confirmed bug in the reviewed MathSDK changes and their selected callers, but it does not prove whole-profile or cross-platform correctness.

- Next action: map each P0–P3 acceptance gate to exact commits, test source, platform execution, and installed-package evidence; separate implemented, verified, and remaining scope.
- Acceptance: completed claims have linked evidence; unresolved platform/runtime/install gates remain explicit; follow-on adapters retain bounded scope.
- Dependency: E Language follows the bounded C++ P3 baseline. Neither documentation nor author-reported focused tests certify that baseline.
- Owner: unassigned.

The previously recommended UI foundation slice remains open under #7: C23 core/draw contracts, a null HAL, one agreed real host/backend, and a triangle-list sample, with overlay builds excluding the web host. Select the platform/backend and owner before assigning implementation work.

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

## Static review coverage and findings

- Read the root `AGENTS.md` and relevant local-development instructions. No nested `AGENTS.md` was found along the reviewed utility or compiler/test paths. The root and `.github` locations did not expose a separate contributing guide.
- Reviewed all MathSDK diff hunks in the 34-commit interval: exact callback-pointer admission/type identity; transform, for_each, generate, predicate, filter, replacement, partition, remove, and extrema handling; mixed-record/record-result transforms; make_unique construction capture; and unique_ptr glvalue comparisons.
- Traced related storage/callback lowering in [Lowering.cpp](https://github.com/NeverSight/NeverC/blob/26c52fc7fad7f2f9218b488b4f34f9346927866d/neverc/lib/Translate/Cpp/Frontend/Lowering.cpp#L393-L468), [record callback conversion](https://github.com/NeverSight/NeverC/blob/26c52fc7fad7f2f9218b488b4f34f9346927866d/neverc/lib/Translate/Cpp/Frontend/Lowering.cpp#L647-L663), [transform lowering](https://github.com/NeverSight/NeverC/blob/26c52fc7fad7f2f9218b488b4f34f9346927866d/neverc/lib/Translate/Cpp/Frontend/Lowering.cpp#L9249-L9309), and [factory source validation](https://github.com/NeverSight/NeverC/blob/26c52fc7fad7f2f9218b488b4f34f9346927866d/neverc/lib/Translate/Cpp/Frontend/Frontend.cpp#L4400-L4430).
- Inspected selected positive/negative test source for callback-to-record transforms, transform objects, replacements, remove objects, extrema comparators, and unique_ptr xvalue/factory result queries. Examples: [record results/rejections](https://github.com/NeverSight/NeverC/blob/26c52fc7fad7f2f9218b488b4f34f9346927866d/tests/neverc/TranslateTests.cpp#L52943-L53066) and [extrema rejection cases](https://github.com/NeverSight/NeverC/blob/26c52fc7fad7f2f9218b488b4f34f9346927866d/tests/neverc/TranslateTests.cpp#L72106-L72168).
- Reviewed the documentation checker's heading/link parsing, `check_links` → `check_target`, `page_hrefs` → `jumps`/`linked_files`, locale mapping, existing layout-test source, and lint-docs workflow. Fixed only the confirmed inline-code false-link bug in [check-docs-links.py](https://github.com/NeverSight/NeverC/blob/4a85206ec4b4457c2ef869cef40d4a8ac4decc6e/utils/plugin-api/check-docs-links.py) with [four source-only regressions](https://github.com/NeverSight/NeverC/blob/4a85206ec4b4457c2ef869cef40d4a8ac4decc6e/utils/plugin-api/tests/test_docs_layout.py).
- The fix was independently inspected, published as a non-forced fast-forward on writable/unprotected `dev`, then read back at its immutable SHA; both changed files match the submitted contents. No PR was necessary for this permitted direct commit.
- Limits: this was not a whole-repository audit. The large Frontend.cpp/TranslateTests.cpp interval received targeted caller/test inspection, not exhaustive review. No code, builds, tests, scripts, or active CI actions were executed. Existing CI logs and commit-author test claims are separate evidence. No dependency/security changes, issue-state changes, merges, or deployments were performed.

## CI snapshot

Observed at **2026-10-01 01:05–01:06 UTC**. All rows in this table belong to reviewed source revision `26c52fc7fad7f2f9218b488b4f34f9346927866d`, not the later fix or report commit.

| Workflow | State |
| --- | --- |
| [macos-arm64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36797991263) | queued |
| [lint-docs](https://github.com/NeverSight/NeverC/actions/runs/36797991256) | queued |
| [cpp-frontend-tools](https://github.com/NeverSight/NeverC/actions/runs/36797991572) | queued |
| [linux-arm64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36797991299) | in progress |
| [linux-x64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36797992104) | in progress |
| [VBS enclave differential CI](https://github.com/NeverSight/NeverC/actions/runs/36797991231) | in progress |
| [windows-arm64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36797991198) | queued |
| [windows-x64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36797991115) | queued |
| [python-plugin-bindings](https://github.com/NeverSight/NeverC/actions/runs/36797991042) | queued |
| [windows-x64-neverc-build-clang-lto](https://github.com/NeverSight/NeverC/actions/runs/36797991112) | in progress |
| [windows-arm64-neverc-build-clang-lto](https://github.com/NeverSight/NeverC/actions/runs/36797991113) | failure |
| [Code Quality: Push on dev](https://github.com/NeverSight/NeverC/actions/runs/36797987725) | queued |

The source revision has 23 check runs (2 successful, 1 failed, 4 in progress, 16 queued) and zero legacy commit-status contexts. Success of an individual check is not success of its entire workflow.

The published fix `4a85206ec4b4457c2ef869cef40d4a8ac4decc6e` has one [dynamic Code Quality run](https://github.com/NeverSight/NeverC/actions/runs/36799464613), queued, and zero legacy statuses. The commit includes `[skip ci]`; GitHub still created this automatic dynamic run. No workflow was dispatched or rerun by this review, and no repository setting was changed to suppress checks. No passing execution validation exists for this fix at the snapshot.

Queued/running states have no final result. Branch-required checks and release readiness are not inferred. Refresh CI for the exact newest SHA before using this report as release evidence.

## Recently completed work

- **2026-10-01:** Published [4a85206](https://github.com/NeverSight/NeverC/commit/4a85206ec4b4457c2ef869cef40d4a8ac4decc6e), fixing inline-code false navigation links and adding four unexecuted regression tests. Remote commit and both file contents verified.
- **2026-09-30:** [PR #20](https://github.com/NeverSight/NeverC/pull/20) merged, preserving literal underscores in heading anchors. This is distinct from today's inline-code fix.
- **2026-09-25:** [PR #19](https://github.com/NeverSight/NeverC/pull/19) merged. It clarifies GKI source-layout alerts and retries transient probes. Its description reports 9 layout tests, 27 upstream tests, and a clean diff check; those local test counts were not rerun for this report.
- **2026-09-03:** [PR #13](https://github.com/NeverSight/NeverC/pull/13) merged, updating the Go gRPC dependency.
- **2026-08-06:** [Issue #3](https://github.com/NeverSight/NeverC/issues/3) was closed as completed for Python plugin bindings. Related [PR #4](https://github.com/NeverSight/NeverC/pull/4) and [PR #5](https://github.com/NeverSight/NeverC/pull/5) were merged. Historical closure does not replace current regression evidence.

## Reporting rules

- Refresh the timestamp, inspected SHA, issue/PR state, and exact-revision CI together.
- Keep completed history and append a dated daily entry; preserve contributor edits.
- Distinguish GitHub metadata, documented capabilities, author-reported test results, and independently observed CI.
- Record unknowns and blockers explicitly. Do not infer completion percentages, deadlines, owners, or universal platform support.
- Use the latest relevant evidence when a branch advances; do not carry forward an old failure as a new revision's result.
- This workflow is static-only: minimal evidence-backed fixes may be committed, but builds, tests, repository scripts, active CI actions, issue-state changes, merges, and deployments are outside this daily review.

## Daily log

### 2026-09-30

- Established the initial baseline: 9 open issues and 0 open PRs.
- Identified the documentation-navigation failure on revision 04299d7; confirmed docs-facts success and cpp-frontend-tools workflow success.
- Flagged the mismatch between #16's planned-status wording and current translation documentation.
- Proposed the three priorities above and recorded missing issue ownership/milestones.
- Verification scope: read-only GitHub issue/PR metadata, repository documentation, exact-revision workflow states, and the failed navigation job log. No compiler or test suite was executed for this report.

### 2026-10-01

- Kept issue counts at 9 open issues and 0 open PRs; confirmed #20 merged and no new discussion/review queue.
- Compared 04299d7 through 26c52fc: 34 commits, 8 files. Reviewed every MathSDK change plus selected source-validation/lowering callers and regression-test source; no confirmed compiler bug in that bounded scope.
- Statically proved and fixed inline-code allocation/operator signatures being parsed as documentation links. Published [4a85206](https://github.com/NeverSight/NeverC/commit/4a85206ec4b4457c2ef869cef40d4a8ac4decc6e); added four source-only regressions and verified remote contents.
- Recorded the current Windows ARM64 COFF witness mismatch separately from the historical navigation failure. Did not substitute speculative SDK signatures or weaken checks.
- CI snapshot: pre-fix source has 12 workflows (4 running, 7 queued, 1 failed); fix has 1 automatically created dynamic check queued. No test/build execution or active CI triggering by this review.
