**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation index](../README.md) · [← NeverC project](../../README.md)

# NeverC Examples

Complete, buildable examples demonstrating NeverC's cross-platform compilation capabilities. All examples cross-compile from macOS / Linux — no Windows build environment required.

---

## Available Examples

| Example | Description | Key Features |
|---------|-------------|-------------|
| [Windows Kernel Driver](../../examples/windows-driver/README.md) | Minimal WDM kernel driver | Cross-compile `.sys` from macOS/Linux, auto-LTO, built-in linker, `DbgPrint` device I/O |
| [Windows Driver + CET](../../examples/windows-driver-cet/README.md) | Kernel driver with Intel CET Shadow Stack | CET-compatible kernel code, `/guard:ehcont`, Shadow Stack enforcement |
| [Windows Driver + Float](../../examples/windows-driver-float/README.md) | Kernel driver with floating-point/SIMD | Safe FP in kernel mode, `KeSaveExtendedProcessorState` / `KeRestoreExtendedProcessorState` |

---

## Quick Start

Every example follows the same pattern:

```bash
cd examples/<example-name>
make
```

Override the compiler path if needed:

```bash
make NEVERC=/path/to/neverc
```

All examples use **neverc** as the compiler and produce Windows PE binaries (`.sys` drivers) via NeverC's built-in linker — no external `link.exe` or Windows SDK installation required.

---

## Cross-Platform Highlights

- **Single toolchain**: NeverC handles preprocessing, compilation, optimization (auto-LTO), and linking in one invocation
- **Bundled SDK**: Windows SDK and WDK headers/libraries are bundled in `runtime/` — zero external dependencies
- **Host-agnostic**: Build from macOS (arm64/x86_64) or Linux (x86_64) with identical commands
- **Debug support**: Pass `-g` for DWARF debug info embedded in PE; inspect with `llvm-dwarfdump`
