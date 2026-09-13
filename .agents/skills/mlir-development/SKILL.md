---
name: mlir-development
description: Build MLIR or CIR dialects, conversions, passes, and multi-level compiler pipelines. Use when MLIR/CIR is the implementation substrate, including ML and domain-specific compilers.
---

# MLIR development

Define the source and target invariants for each dialect boundary before implementing rewrites or lowering.

## Topic routing

- [Foundations](references/foundations.md) for MLIR's IR model, dialects, operations, types, and attributes.
- [Passes and conversion](references/passes-and-conversion.md) for transformations, patterns, legality, and lowering.
- [Dialects and compilers](references/dialects-and-compilers.md) for built-in dialects, CIR, and ML compiler ecosystems.
- [Testing, tools, and resources](references/testing-tools-and-resources.md) for `mlir-opt`, translation, verification, and upstream references.

Use current dialect definitions and verifier contracts as authority. Test transformations at the narrowest textual or executable boundary that proves legality and semantics.
