**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← DynCode Compiler](../README.md)

# Roadmap

This document tracks features that are planned, in progress, or deferred by design.

## Current State

NeverC's dyncode pipeline covers:

- Full LLVM IR pipeline with 11+ dedicated passes
- COFF / ELF / Mach-O extractors
- Win32 PEB-walk import resolution (ROR-13 hash, 6 DLL buckets)
- Direct syscall lowering (Darwin `svc #0x80`, Linux `svc #0` / `syscall`)
- Kernel-mode support (Windows, Linux)
- Bad-byte auditing with configurable profiles
- Out-of-tree C Plugin API with 11 interpose points across IR, MIR, and byte-stream layers
- Size / alignment / padding constraints (`-fdyncode-max-length=`, `-fdyncode-align=`, `-fdyncode-pad=`)
- 11 obfuscation interposes across IR, MIR, and byte-stream layers

## Completed (2026-04)

1. **Size / alignment / padding constraints** — Built-in. `-fdyncode-max-length=`, `-fdyncode-align=`, `-fdyncode-pad=` execute at the end of `finalizeDynCodeBytes`. The driver rejects contradictory configurations (e.g., pad byte in the bad-byte set, or pad without align/max-length).

2. **Out-of-tree C Plugin API** — Pure C ABI plugin interface ([`NevercPluginAPI.h`]) for custom IR, MIR, binary, and linker passes. Plugins register at 11 dyncode interpose points (`NEVERC_INTERPOSE_SC_*`). Single-header SDK with zero LLVM/CRT dependencies. See the [Plugin API documentation](../../plugin-api/README.md).

## Planned — Plugin Layer (via interposes)

These capabilities are **intentionally not built-in**. They belong to the strategy/obfuscation layer and are designed to be provided by third-party plugins through the interpose and plugin interfaces.

| Feature | Interpose Point | Notes |
|---------|-----------|-------|
| Anti-disassembly | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | Instruction prefix interference, jump reordering, junk insertion |
| Polymorphism | `RunAfterFinalMIR` / `RunPostExtract` | Seed-based output variation per compilation |
| Staged encoder (XOR / RC4 / self-decrypting) | `RunPostExtract` / `RunPostFinalize` | Compile-time stub generation + payload encryption |
| Indirect syscalls (Halos / Tartarus / Recycled Gate) | IR-level plugin or `RunPostExtract` | Runtime ntdll gadget scanning |
| Sleep mask / call stack spoofing | IR pass plugin | Ekko / FOLIAGE / Cronos patterns |
| ETW / AMSI patching | IR pass plugin | Runtime patch sequences |
| Module stomping / uninterposing | IR pass plugin | Memory manipulation patterns |

## Plugin Interpose Summary

11 interposes in three layers:

**IR layer (6 interposes, receive `ModulePassManager &`)**:
- `RunBeforePrep` — Before any dyncode pass
- `RunAfterPrep` — After linkage unification
- `RunBeforeInlining` — Last chance before AlwaysInliner
- `RunAfterInlining` — IR fully flattened into one function
- `RunAfterStackify` — Final IR shape before codegen
- `RunAfterFinalIR` — After `AllBlrPass`, the absolute last IR interpose

**MIR layer (3 interposes, receive `TargetPassConfig &`)**:
- `RunBeforePreEmit` — Registers allocated, CFI/EH pseudos still present
- `RunAfterPreEmit` — After `MIRPrepPass` cleanup, closest to final bytes
- `RunAfterFinalMIR` — After LLVM `addPreEmitPass2()`, just before AsmPrinter

**Byte-stream layer (2 interposes, receive `SmallVectorImpl<uint8_t> &`)**:
- `RunPostExtract` — Pre-finalize, still processed by rewriter/encoder/audit/sizing
- `RunPostFinalize` — Post-finalize, last moment before writing to disk; NeverC performs no further auditing

## Finalize Pipeline

Each extractor calls `finalizeDynCodeBytes` before writing the `.bin`:

```
applyPostExtractObfuscationInterpose       (C Plugin API: NEVERC_INTERPOSE_SC_POST_EXTRACT)
        |
auditFinalBadBytes                    (built-in hard audit)
        |
applyDynCodeSizing                  (-fdyncode-align/-max-length/-pad)
        |
applyPostFinalizeObfuscationInterpose      (C Plugin API: NEVERC_INTERPOSE_SC_POST_FINALIZE)
```

See the [Plugin API documentation](../../plugin-api/README.md) for usage and code examples.

## Not Planned

- **Cross-language frontend** — NeverC accepts only its own C23 frontend. The IR pipeline is decoupled from the frontend, but accepting external bitcode (e.g., from `rustc` or `zig`) is not a project goal.

[`NevercPluginAPI.h`]: ../../../neverc/include/neverc/Plugin/NevercPluginAPI.h
