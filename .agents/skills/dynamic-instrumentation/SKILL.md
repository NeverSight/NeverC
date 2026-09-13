---
name: dynamic-instrumentation
description: Build dynamic instrumentation, runtime tracing, coverage, profiling, or monitoring with LLVM-related tooling. Use static-analysis when execution is not part of the analysis.
---

# Dynamic instrumentation

Use this skill when analysis depends on observing or modifying a running program.

## Topic routing

- [Instrumentation foundations](references/instrumentation-foundations.md) for DBI frameworks, compile-time probes, and runtime tracing.
- [Analysis applications](references/analysis-applications.md) for profiling, syscall monitoring, eBPF, and dynamic taint.
- [Integration and resources](references/integration-and-resources.md) for fuzzing, debugger integration, design guidance, and upstream projects.

Choose the least invasive mechanism that exposes the needed evidence, and account for runtime overhead and observer effects. Use `static-analysis` when execution is unnecessary and `binary-lifting` when translation into IR is the central problem.
