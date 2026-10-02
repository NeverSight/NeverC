# NeverC Progress

This document tracks repository issues, pull requests, static code-review findings, observed CI, and the next actionable work. The immediate priorities are to diagnose the Windows ARM64 COFF witness failure, synchronize translated-guide navigation, and reconcile the translation roadmap with implementation evidence.

- Snapshot: **2026-10-02 09:08 Asia/Shanghai (UTC+08:00)** / **2026-10-02 01:08 UTC**
- Source branch: `dev`
- Reviewed source revision: [8d48d8baa12a8c97a071ad5baa65d466bece3437](https://github.com/NeverSight/NeverC/commit/8d48d8baa12a8c97a071ad5baa65d466bece3437), “translate: support nested member-pointer use adapters”
- Latest previously published static fix: [4a85206](https://github.com/NeverSight/NeverC/commit/4a85206ec4b4457c2ef869cef40d4a8ac4decc6e), “fix(docs): ignore inline code in navigation links [skip ci]”
- Today's result: no new confirmed bug in the bounded reviewed scope; progress-document update only.
- Scope: static source, diff, caller, test-source, configuration and existing-log review only. No builds, tests, repository scripts, program execution, workflow dispatch or rerun by this review. Existing automatic CI is reported separately. Priorities are recommendations, not assigned deadlines or release commitments.
- Related references: [product roadmap](docs/roadmap.md), [DynCode-specific progress](docs/dyncode-compiler-progress.md), [translation documentation](docs/translate.md). Those documents retain their existing scope.

## Current status

| Measure | Observed value | Qualification |
| --- | --- | --- |
| Open issues | 9; delta 0 | #7–#12 and #16–#18 |
| Open pull requests | 0; delta 0 | No new PR/review queue |
| Open issues with an assignee | 0 of 9 | Ownership needs an explicit decision |
| Open issues with a milestone | 0 of 9 | No delivery dates inferred |
| Priority labels on open issues | None | #16–#18 retain enhancement/roadmap labels; #16 also has area:translate |
| Changes since prior inspected revision | 48 commits; 9 changed files | [26c52fc…8d48d8b](https://github.com/NeverSight/NeverC/compare/26c52fc7fad7f2f9218b488b4f34f9346927866d...8d48d8baa12a8c97a071ad5baa65d466bece3437); includes October 1's fix and progress commits, excludes today's report |
| Workflows on reviewed source revision | 12 | 2 successful, 2 failed, 7 in progress, 1 queued |
| Check runs on reviewed source revision | 23 | 7 successful, 2 failed, 12 in progress, 2 queued |
| Documentation-layout regressions | 16 passed in existing CI | Includes the four inline-code regressions; full navigation still fails |
| New fixes from today's static review | 0 | No speculative compiler or fixture change |

Pagination and inventory: the all-state issue collection returned 18 issue/PR records and the pull-request collection returned 8 PRs, each at page size 100 with an empty second page. PRs are excluded from issue counts. All nine open issues report zero comments. Latest PR #20 remains merged, with zero review submissions, inline review comments and conversation comments in the checked collections. The exact-source workflow and check-run queries returned all 12 runs and 23 checks, with empty second pages. The comparison enumerated all 48 commits and 9 files. Large aggregate patches for Frontend.cpp and TranslateTests.cpp were omitted by GitHub, so the source review used selected individual-commit patches and exact-revision source instead. A whole-repository recursive tree fetch failed; scoped directory inventories and available subtree inspection were used for instructions. No whole-repository or broad historical-CI completeness is claimed.

## Today's top priorities

### 1. Diagnose the Windows ARM64 COFF witness failure

**Evidence:** the current [Windows ARM64 Clang + LTO run](https://github.com/NeverSight/NeverC/actions/runs/36947984286) failed in the [COFF audit job](https://github.com/NeverSight/NeverC/actions/runs/36947984286/job/110654304374). Its existing log reports 24 tests with four failures: MSVC/Clang at O0/O2 all stop in `test_cmath_explicit_double_calls_preserve_values_and_remove_templates`. [The fixture](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/tests/neverc/CppFrontendToolchainTests.py#L2760-L2788) expects exactly one `_GENERIC_MATH2(pow)` line, but the runner's MSVC 14.51.36231 `cmath` provides zero matches. This confirms the same blocker on today's exact revision, rather than carrying yesterday's status forward.

- Next action: inspect the actual installed SDK's pow declaration and select a provenance-preserving witness for that declaration shape.
- Acceptance: explain the SDK difference without weakening the source/provenance checks, then obtain authorized exact-revision native evidence for all four configurations.
- Dependency: the log exposes the generic macros and log10 instantiation but not a sufficient replacement pow declaration. The compiler build and compiler tests were skipped. The failed postbuild-evidence step is downstream, not proof of a compiler regression.
- Owner: unassigned.

### 2. Synchronize translated-guide navigation

**Evidence:** [the current navigation log](https://github.com/NeverSight/NeverC/actions/runs/36947984267/job/110654304422) reports all 16 documentation-layout tests passing. The October 1 inline-code false links and September 30 literal-underscore regression no longer appear in this run's failures. The full checker still reports **90 jump-parity diagnostics across 10 translated translate.md pages**; all reported navigation failures are these parity differences. [The docs-facts job](https://github.com/NeverSight/NeverC/actions/runs/36947984267/job/110654304491) passed.

- Next action: reconcile each translated page with the English guide's actual linked content. The English/zh-CN source comparison confirms missing new/memory/tuple/array and structured-binding jumps, plus the string-header versus string-literal destination mismatch. Do not add filler links merely to satisfy counts.
- Acceptance: translated content and link multiplicities/destinations agree with the English source; an authorized later full navigation run passes.
- Dependency: passing layout regressions validate the narrow parser fixes on this descendant revision, not complete multilingual documentation or all current compiler features.
- Owner: unassigned.

### 3. Reconcile translation progress with issue #16

**Evidence:** [#16](https://github.com/NeverSight/NeverC/issues/16) remains open with unresolved acceptance checklists. The 48-commit interval adds unique_ptr comparison/query handling, SDK imports, tuple/reference-wrapper and invoke/mem_fn result sources, pointer-layout metadata, hash queries and noexcept-sensitive function-template identity. Selected high-risk paths received static review; this does not certify the full feature list.

- Next action: map P0–P3 gates to implementing SHAs, positive/negative fixture source, completed platform execution and installed-package evidence. Keep implemented, statically reviewed and natively verified states separate.
- Acceptance: completion claims have exact-revision evidence; unreviewed source and unfinished platform/runtime/install gates remain explicit.
- Dependency: E Language follows the bounded C++ P3 baseline. The successful cpp-frontend-tools workflow does not replace all compiler, relocation or runtime gates.
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

- Read root `AGENTS.md`, `docs/local-dev.md` and the relevant compiler-development guidance. Scoped root, .github, compiler, utility and test directory inventories found no applicable nested AGENTS or separate contributing guide on the reviewed paths.
- Reviewed all **29 MathSDK.cpp diff hunks** across [26c52fc…8d48d8b](https://github.com/NeverSight/NeverC/compare/26c52fc7fad7f2f9218b488b4f34f9346927866d...8d48d8baa12a8c97a071ad5baa65d466bece3437): [lazy wrapper metadata](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/neverc/lib/Translate/Cpp/Frontend/MathSDK.cpp#L864-L923), [ref/cref and wrapper access source proofs](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/neverc/lib/Translate/Cpp/Frontend/MathSDK.cpp#L8766-L9028), [nested member-pointer/mem_fn adapters and temporary initializers](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/neverc/lib/Translate/Cpp/Frontend/MathSDK.cpp#L10880-L11068), invoke/apply descriptor propagation, function-pointer hash conversions, explicit unique_ptr operator calls, and tuple-get forward-declaration authentication.
- Reviewed both Project.cpp changed hunks: [canonical noexcept type-argument identity](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/neverc/lib/Translate/Cpp/Frontend/Project.cpp#L144-L244), pack positions and propagation into instance-local declaration identities. Inspected the [identity/storage fixture](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/tests/neverc/TranslateTests.cpp#L21267-L21381).
- Reviewed all Frontend.cpp patches in the latest **seven commits, c7dd294 through 8d48d8b**. Traced [SDK value-adapter proof](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/neverc/lib/Translate/Cpp/Frontend/Frontend.cpp#L2872-L3036), [mem_fn query and carrier-source retention](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/neverc/lib/Translate/Cpp/Frontend/Frontend.cpp#L3577-L3778), [member-pointer written-argument retention](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/neverc/lib/Translate/Cpp/Frontend/Frontend.cpp#L11130-L11318), [copy/factory declaration traversal](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/neverc/lib/Translate/Cpp/Frontend/Frontend.cpp#L12007-L12054), [written template traversal](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/neverc/lib/Translate/Cpp/Frontend/Frontend.cpp#L14857-L14898), and [native/invoke/apply admission consumers](https://github.com/NeverSight/NeverC/blob/8d48d8baa12a8c97a071ad5baa65d466bece3437/neverc/lib/Translate/Cpp/Frontend/Frontend.cpp#L17721-L17940). Also checked the changed Frontend.h descriptor fields and selected earlier metadata/query callers.
- Inspected focused positive/negative test source for reference-wrapper metadata/access/factories, pointer hashing, callback identity/storage, nested and temporary mem_fn initialization, and nested member-pointer uses. The latest nested-pointer fixture pair and temporary-factory fixture pair received full patch inspection, including unevaluated effects, written type arguments, SDK replacements, receiver categories, missing bodies, reassignment, source identity and temporary lifetime. Fixture source is not execution evidence.
- Rechecked the documentation checker's ordered inline-link/code handling, its four regression methods, the lint-docs workflow and exact-source failure logs. Compared English and zh-CN translate.md links to ground the reported parity debt.
- **Finding:** no new statically proven defect in the reviewed scope. No code fix or new test was authored today. The narrow existing hash-wrapper argument gate still rejects top-level-const function-pointer lvalues; this is a boundary clarification opportunity, not a demonstrated regression or a reason to broaden the supported profile.
- **Limits:** this is not a whole-repository audit. Earlier Frontend.cpp changes and the 100,868-line TranslateTests.cpp received targeted rather than exhaustive review; numerous hash/invoke/query additions remain outside a full end-to-end audit. No project code, builds, tests or scripts were executed and no CI was dispatched/rerun. Existing automatic CI and author-reported validation are separate evidence. No dependencies, security settings, issue states, merges or deployments were changed.

## CI snapshot

Observed at **2026-10-02 01:07–01:08 UTC** for reviewed source revision `8d48d8baa12a8c97a071ad5baa65d466bece3437`, not the later report-only commit.

| Workflow | State |
| --- | --- |
| [windows-x64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36947984227) | in progress |
| [windows-arm64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36947984304) | in progress |
| [windows-x64-neverc-build-clang-lto](https://github.com/NeverSight/NeverC/actions/runs/36947984196) | in progress |
| [VBS enclave differential CI](https://github.com/NeverSight/NeverC/actions/runs/36947984250) | in progress |
| [linux-x64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36947984747) | in progress |
| [linux-arm64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36947984167) | in progress |
| [macos-arm64-neverc-build](https://github.com/NeverSight/NeverC/actions/runs/36947984231) | in progress |
| [python-plugin-bindings](https://github.com/NeverSight/NeverC/actions/runs/36947984220) | queued |
| [windows-arm64-neverc-build-clang-lto](https://github.com/NeverSight/NeverC/actions/runs/36947984286) | failure |
| [lint-docs](https://github.com/NeverSight/NeverC/actions/runs/36947984267) | failure |
| [cpp-frontend-tools](https://github.com/NeverSight/NeverC/actions/runs/36947984288) | success |
| [Code Quality: Push on dev](https://github.com/NeverSight/NeverC/actions/runs/36947982050) | success |

The exact source has 23 check runs: 7 successful, 2 failed, 12 in progress and 2 queued, with zero legacy commit-status contexts. cpp-frontend-tools completed successfully across its three archive-audit jobs. Individual setup/policy/facts checks are not success of the larger compiler workflow.

The navigation job ran all 16 layout tests successfully, then failed the full checker on the 90 translation-parity diagnostics described above. The Windows ARM64 Clang + LTO job failed the existing SDK witness before the compiler build; its missing postbuild evidence is downstream. These are observed failures on this SHA, not failures produced by this review.

Running/queued states have no final result. Branch `dev` was writable and unprotected at the publication precheck; the visible repository ruleset was disabled. No release-readiness or universal-platform claim is made. The report-only commit uses `[skip ci]`; automatically created checks, if any, are not manually triggered validation. Refresh exact-SHA CI before using this report as release evidence.

## Recently completed work

- **2026-10-02:** Completed the bounded static review with no confirmed new defect. Independently observed all 16 documentation-layout regressions passing on 8d48d8b; recorded remaining translation-parity and Windows ARM64 fixture blockers.

- **2026-10-01:** Published [4a85206](https://github.com/NeverSight/NeverC/commit/4a85206ec4b4457c2ef869cef40d4a8ac4decc6e), fixing inline-code false navigation links and adding four unexecuted regression tests. Remote commit and both file contents verified.
- **2026-09-30:** [PR #20](https://github.com/NeverSight/NeverC/pull/20) merged, preserving literal underscores in heading anchors. This is distinct from the October 1 inline-code fix.
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

### 2026-10-02

- Counts remain 9 open issues and 0 open PRs, with no new issue comments or PR review queue.
- Compared 26c52fc through 8d48d8b: 48 commits and 9 files, including the prior fix/report commits. Reviewed every MathSDK diff hunk and both Project identity hunks, all Frontend.cpp patches in the latest seven commits, relevant callers/descriptors, and focused fixture source.
- No new statically proven defect; no source code or tests changed. Kept the documented narrow admission boundaries rather than making a speculative widening.
- Existing CI now confirms all 16 docs-layout regressions pass. Full navigation still fails on 90 parity diagnostics across 10 translated guides; English/zh-CN source confirms the missing or differing destinations.
- Current Windows ARM64 Clang + LTO still fails all four cmath witness configurations on the expected pow macro line; compiler build/tests were skipped. No witness was weakened or guessed.
- Exact-source snapshot: 12 workflows (2 successful, 2 failed, 7 running, 1 queued); 23 checks (7 successful, 2 failed, 12 running, 2 queued). No build/test/script execution or active CI action by this review.
