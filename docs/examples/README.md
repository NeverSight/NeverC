**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation index](../README.md) · [← NeverC project](../../README.md)

# NeverC Examples

Complete, buildable examples demonstrating NeverC's cross-platform compilation capabilities. All examples cross-compile from any host OS — no target build environment required.

---

## Available Examples

### Windows

| Example | Description | Key Features |
|---------|-------------|-------------|
| [Windows Kernel Driver](../../examples/windows-driver/README.md) | Minimal WDM kernel driver | Cross-compile `.sys` from macOS/Linux, auto-LTO, built-in linker, `DbgPrint` device I/O |
| [Windows Driver + CET](../../examples/windows-driver-cet/README.md) | Kernel driver with Intel CET Shadow Stack | CET-compatible kernel code, `/guard:ehcont`, Shadow Stack enforcement |
| [Windows Driver + Float](../../examples/windows-driver-float/README.md) | Kernel driver with floating-point/SIMD | Safe FP in kernel mode, `KeSaveExtendedProcessorState` / `KeRestoreExtendedProcessorState` |

### Linux

| Example | Description | Key Features |
|---------|-------------|-------------|
| [Linux Hello World](../../examples/linux-hello/README.md) | Minimal C program | Cross-compile ELF from macOS/Windows, printf, string ops |
| [Linux POSIX](../../examples/linux-posix/README.md) | POSIX system programming | pthreads, mmap, pipe, signal handling |
| [Linux Static](../../examples/linux-static/README.md) | Fully-static binary | `-static` linking, zero runtime dependencies, math functions |
| [Linux Network](../../examples/linux-network/README.md) | TCP socket demo | Client/server, socket API, loopback communication |
| [Linux Math + zlib](../../examples/linux-math/README.md) | Math + compression | Trigonometry, special functions, zlib compress/decompress, CRC32 |

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

Linux examples support architecture selection:

```bash
make TARGET=aarch64-linux-gnu   # Build for ARM64
make TARGET=x86_64-linux-gnu    # Build for x86_64 (default)
```

---

## Cross-Platform Highlights

- **Single toolchain**: NeverC handles preprocessing, compilation, optimization (auto-LTO), and linking in one invocation
- **Bundled SDK**: Windows SDK/WDK and Linux sysroot (Ubuntu 22.04) headers/libraries are bundled in `runtime/` — zero external dependencies
- **Host-agnostic**: Build from macOS (arm64/x86_64), Linux (x86_64/aarch64), or Windows with identical commands
- **Multi-target**: Cross-compile to Windows PE (`.sys`/`.exe`) and Linux ELF from any host
- **Debug support**: Pass `-g` for DWARF debug info; inspect with `llvm-dwarfdump`
