---
name: llvm-optimization
description: Implement or analyze LLVM optimization passes, pipelines, and generated-code quality. Use for IR transformation and pass behavior, including NeverC compiler performance, not general application tuning.
---

# LLVM optimization

Preserve semantics first, then measure the effect at the boundary the change intends to improve.

## Topic routing

- [Pass development](references/pass-development.md) for pipelines, built-in passes, custom passes, instruction patterns, and dominance.
- [Loops, vectorization, and LTO](references/loops-vectorization-and-lto.md) for specialized optimization modes.
- [Debugging and correctness](references/debugging-and-correctness.md) for missed transforms, invalid IR, and semantic verification.
- [NeverC performance](references/neverc-performance.md) for repository-specific compile-time and memory optimization evidence.
- [Resources](references/resources.md) for upstream references.

Use current pass-manager APIs and repository pipelines as authority. Do not infer success from IR shape alone when runtime behavior, code size, or compile-time performance is the intended outcome.
