# NeverC agent guide

Keep repository-wide context small. Read the document that matches the task:

- Use `docs/local-dev.md` for build profiles, local setup, and the `check-neverc` test entrypoint.
- Use `docs/dyncode-compiler.md` and its focused design documents for DynCode pipeline changes.
- Use `docs/plugin-api.md` for public plugin ABI or phase-hook work.
- Use `docs/release-builds.md` and `release.md` when changing packaging or release workflows.
- Use `development.md` only for x86 privileged-intrinsic lowering and generated intrinsic or instruction tables.
- Use `docs/roadmap.md` only when scoping planned product work.

NeverC's source language is C23-only. Do not expand the frontend to C++ or other source languages as incidental work. Keep target-specific ABI, object-format, linker, runtime, and DynCode behavior at their owning boundary; generated LLVM tables must remain synchronized with their source definitions or generators.

Local build and test fixtures are disposable and have no production access. Run the smallest check that can disprove the change, fix failures caused by the requested work, and rerun affected checks without asking after each step. Use the stage-1 `neverc` target for ordinary compiler work and `check-neverc` or focused tests when behavior changes; broaden to stage-2 runtime embedding, sanitizers, cross-target, or native CI only when the affected boundary requires that evidence.

For requested implementation work, continue through implementation and proportionate verification. Stop earlier only for diagnosis or review requests, or when completion requires a product decision, credentials, external mutation, or authority not already provided.

Preserve unrelated working-tree changes, ignored build trees, profiling data, and local diagnostics. Do not reformat untouched files or modify third-party sources unless the task requires it.
