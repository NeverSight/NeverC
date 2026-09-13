---
name: compiler-development
description: Build NeverC or other LLVM compilers across frontend, IR, optimization, code generation, ABI, and target support. Use when the compiler pipeline is the subject; use narrower LLVM skills for isolated tooling or analysis.
---

# Compiler development

Use this skill when a change crosses compiler stages or target boundaries. Keep source-language semantics, LLVM IR, ABI lowering, object format, linker behavior, and runtime assumptions owned by the narrowest authoritative layer.

## Topic routing

Read only the reference that matches the work:

- [Targets and ABI](references/targets-and-abi.md) for platform matrices, calling conventions, build-system target handling, and module metadata.
- [DynCode and testing](references/dyncode-and-testing.md) for cross-platform DynCode invariants and target-specific verification.
- [Compiler pipeline](references/compiler-pipeline.md) for frontend, AST, IR generation, optimization, JIT, and language implementation patterns.
- [Workflow and resources](references/workflow-and-resources.md) for broader compiler-development workflow and upstream references.

For an isolated pass use `llvm-optimization`; for Clang/LLDB/LSP developer tooling use `llvm-tooling`; for static or dynamic analyses use their narrower skills. Prefer current NeverC source and repository documentation over examples in these references.
