---
name: github-ci-runtime-debugging
description: Diagnose NeverC runtime tests that fail only or intermittently in CI. Use for platform-specific crashes, signals, nondeterminism, or local/runner divergence; not ordinary build errors or workflow configuration failures.
---

# NeverC CI runtime debugging

Identify the exact failing workflow, runner, step, process, signal, and revision before changing code. Match the relevant CI environment and distinguish compiler crashes from failures in generated programs.

A read-only diagnosis does not authorize reruns, pushes, temporary workflows, or artifact uploads. Inspect existing logs and prepare the smallest useful external experiment before requesting any required authorization.

## Topic routing

- [Triage](references/triage.md) for log classification, failure frequency, platform scope, and compiler-versus-generated-program boundaries.
- [Local reproduction](references/local-reproduction.md) for matching architecture and environment, reusing artifacts when they exist, determinism checks, sanitizers, and emulation limits.
- [Native runner escalation](references/native-runner.md) only when local evidence cannot reproduce the native runner condition and an authorized temporary workflow is justified.
- [Fix and validation](references/fix-and-validation.md) for backtrace interpretation, concurrency and allocator failures, source fixes, native repetition, and sibling audits.

Use current workflow files as authority; the references contain patterns, not guarantees that a workflow uploads a reusable artifact or uses a particular runner image. Use `systematic-debugging` for ordinary local failures.

## Completion

For diagnosis, report the failing layer and evidence without implementing a fix unless requested. For a requested fix, continue through a focused regression and proportionate runner validation, and state any untested platform or optional-tool boundary.
