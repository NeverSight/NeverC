**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**A security-research-oriented C23 compiler built on LLVM**

Integrated linker · Shellcode pipeline · Built-in `string` type

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#features)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#cross-compiling-to-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#features)

[Documentation](docs/README.md) · [Shellcode Guide](docs/shellcode-compiler/README.md) · [Built-in String](docs/builtin-string/README.md)

</div>

---

> **Note:** GitHub always shows `README.md` as the repository homepage (no automatic locale detection). Use the language links above; follow in-page links and breadcrumbs to keep the same locale in [docs](docs/README.md) and the [shellcode guide](docs/shellcode-compiler/README.md).

## Overview

NeverC compiles standard C into hosted binaries, freestanding executables, and position-independent shellcode — all from a single toolchain. It targets **x86_64** and **AArch64** (little-endian only).

## Features

- **[Shellcode compiler](docs/shellcode-compiler/README.md)** — multi-stage IR/MIR pipeline, cross-platform extraction, import/syscall lowering, kernel-mode support, bad-byte auditing, and a plugin architecture
- **Integrated linker** — COFF, ELF, and Mach-O in one binary; no external `ld` or `link.exe`
- **Cross-compilation** — build Windows PE from macOS/Linux with bundled MSVC SDK support
- **[Built-in `string` type](docs/builtin-string/README.md)** — value-semantic string with dotted method syntax, automatic memory management, and native UTF-8 backing
- **Lean LLVM build** — only x86_64 and AArch64 backends; C++/ObjC/OpenMP paths stripped

## Quick Example

```c
#include <unistd.h>

int main(void) {
    string msg = "Hello " + "NeverC!";
    write(1, msg.c_str(), msg.len);
    return 0;
}
```

```bash
# macOS arm64 shellcode
neverc -fshellcode -mshellcode-syscall hello.c -o hello.bin

# Cross-compile to Linux x86_64 — same source
neverc -fshellcode -target x86_64-linux-gnu -mshellcode-syscall hello.c -o hello.bin
```

See the **[documentation index](docs/README.md)** for detailed design notes, platform matrix, CLI reference, and examples.

## Building

### Requirements

- CMake 3.20+
- Ninja
- A C++17 host compiler (GCC, Clang, or MSVC)

### Configure

```bash
cmake -B build-neverc -G Ninja \
  -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  llvm
```

### Build

```bash
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` is auto-detected and enabled if present.

### Test

```bash
tests/neverc/run_tests.sh build-neverc
```

### Verify

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Cross-Compiling to Windows

Place an [xwin](https://github.com/Jake-Shadle/xwin) SDK splat at `build-neverc/sdk/msvc/`, then:

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -o hello.exe hello.c -lkernel32
```

See [shellcode compiler docs](docs/shellcode-compiler/README.md) for Windows shellcode (`-fshellcode`, PEB import resolution, etc.).

## License

[AGPL-3.0](LICENSE)

LLVM components retain their [Apache-2.0 WITH LLVM-exception](llvm/LICENSE.TXT) license.
