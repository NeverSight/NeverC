**Languages**: [English](README.md) | [简体中文](docs/i18n/README.zh-CN.md) | [繁體中文](docs/i18n/README.zh-TW.md) | [日本語](docs/i18n/README.ja.md) | [한국어](docs/i18n/README.ko.md) | [Français](docs/i18n/README.fr.md) | [Deutsch](docs/i18n/README.de.md) | [Español](docs/i18n/README.es.md) | [Italiano](docs/i18n/README.it.md) | [Русский](docs/i18n/README.ru.md) | [العربية](docs/i18n/README.ar.md)

<div align="center">

# NeverC

**A security-research-oriented C23 compiler built on LLVM**

Integrated linker · Shellcode pipeline · Built-in runtimes (`string` · mimalloc)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#features)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#cross-compiling-to-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#features)

[Documentation](docs/README.md) · [Shellcode Guide](docs/shellcode-compiler/README.md) · [Built-in Runtimes](docs/builtins/README.md)

</div>

---

> **Note:** GitHub always shows `README.md` as the repository homepage (no automatic locale detection). Use the language links above; follow in-page links and breadcrumbs to keep the same locale in [docs](docs/README.md) and the [shellcode guide](docs/shellcode-compiler/README.md).

## Overview

NeverC compiles standard C into hosted binaries, freestanding executables, and position-independent shellcode — all from a single toolchain. It targets **x86_64** and **AArch64** (little-endian only).

## Features

- **[Shellcode compiler](docs/shellcode-compiler/README.md)** — multi-stage IR/MIR pipeline, cross-platform extraction, import/syscall lowering, kernel-mode support, bad-byte auditing, and a plugin architecture
- **Integrated linker** — COFF, ELF, and Mach-O in one binary; no external `ld` or `link.exe`
- **Cross-compilation** — build Windows PE from macOS/Linux with bundled MSVC SDK support
- **[Built-in runtimes](docs/builtins/README.md)** — opt-in LLVM bitcode runtimes embedded in the compiler: [`string`](docs/builtin-string/README.md) (value-semantic string with dot-call methods and automatic memory management) and [`mimalloc`](docs/builtins/mimalloc/README.md) (transparent high-performance allocator override)
- **Lean LLVM build** — only x86_64 and AArch64 backends; C++/ObjC/OpenMP paths stripped

## Quick Example

```c
#include <stdio.h>

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());
    return 0;
}
```

> **Note:** The built-in **`string`** type requires **`-fbuiltin-string`** for normal hosted binaries. **`-fshellcode`** enables it automatically.

```bash
# macOS arm64
neverc -fshellcode -target arm64-apple-macos -mshellcode-syscall hello.c -o hello.bin

# Linux x86_64
neverc -fshellcode -target x86_64-linux-gnu -mshellcode-syscall hello.c -o hello.bin

# Windows x86_64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin
```

See the **[documentation index](docs/README.md)** for detailed design notes, platform matrix, CLI reference, and examples.

## Building

### Requirements

- CMake 3.20+
- Ninja
- A C++17 host compiler (GCC, Clang, or MSVC)

### Configure

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
```

### Build

```bash
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` is auto-detected and enabled if present.

### Test

```bash
cmake --build build-neverc --target neverc-tests
ctest --test-dir build-neverc --output-on-failure
```

### Verify

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Cross-Compiling to Windows

Place an [xwin](https://github.com/Jake-Shadle/xwin) SDK splat at `build-neverc/sdk/msvc/`.

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

See [shellcode compiler docs](docs/shellcode-compiler/README.md) for Windows shellcode (`-fshellcode`, PEB import resolution, etc.).

## License

[AGPL-3.0](LICENSE)

LLVM components retain their [Apache-2.0 WITH LLVM-exception](llvm/LICENSE.TXT) license.
