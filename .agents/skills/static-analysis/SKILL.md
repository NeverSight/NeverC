---
name: static-analysis
description: Implement LLVM-based dataflow, pointer, taint, dependency, or verification analyses. Use for static program-analysis algorithms and tools, not runtime instrumentation.
---

# Static analysis

State the analysis lattice, transfer behavior, merge rule, scope, and soundness tradeoff before optimizing implementation details.

## Topic routing

- [Dataflow and pointers](references/dataflow-and-pointers.md) for control/data flow, LLVM analysis infrastructure, and points-to techniques.
- [Taint and dependencies](references/taint-and-dependencies.md) for source-sink models, interprocedural taint, dependency graphs, and slicing.
- [Security tools and integration](references/security-tools-and-integration.md) for vulnerability applications, existing frameworks, IDE/CI integration, and resources.

Use `dynamic-instrumentation` when runtime evidence is required. Make false-positive, false-negative, path-sensitivity, aliasing, and interprocedural limits explicit in the result.
