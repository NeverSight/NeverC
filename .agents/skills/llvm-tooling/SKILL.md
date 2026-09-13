---
name: llvm-tooling
description: Build Clang plugins, LibTooling tools, LLDB extensions, or clangd/LSP integrations. Use for developer tooling around LLVM, not compiler pipeline implementation.
---

# LLVM tooling

Choose the LLVM interface that matches the user's integration surface and deployment model.

## Topic routing

- [Clang tools](references/clang-tools.md) for plugins, AST matchers, and standalone LibTooling applications.
- [Debugger and language server](references/debugger-and-language-server.md) for LLDB and clangd/LSP extensions.
- [Transformation and ecosystem](references/transformation-and-ecosystem.md) for source rewriting, refactoring, and comparable projects.
- [Resources](references/resources.md) for upstream references.

Preserve source locations, diagnostics, and compilation database behavior when they are part of the tool's contract. Use `compiler-development` when changing compilation semantics rather than building an external developer tool.
