**Languages**: [English](README.md) | [简体中文](docs/i18n/README.zh-CN.md) | [繁體中文](docs/i18n/README.zh-TW.md) | [日本語](docs/i18n/README.ja.md) | [한국어](docs/i18n/README.ko.md) | [Français](docs/i18n/README.fr.md) | [Deutsch](docs/i18n/README.de.md) | [Español](docs/i18n/README.es.md) | [Italiano](docs/i18n/README.it.md) | [Русский](docs/i18n/README.ru.md) | [العربية](docs/i18n/README.ar.md)

<div align="center">

# NeverC

**The AI-friendly C23 compiler for security research, built on LLVM**

Integrated linker · Shellcode pipeline · Built-in runtimes (`string` · `mimalloc` · `xorstr`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#features)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#cross-compiling-to-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#features)

[Documentation](docs/README.md) · [Shellcode Guide](docs/shellcode-compiler/README.md) · [Built-in Runtimes](docs/builtins/README.md) · [Plugin API](docs/plugin-api/README.md)

</div>

---

> **Note:** GitHub always shows `README.md` as the repository homepage (no automatic locale detection). Use the language links above; follow in-page links and breadcrumbs to keep the same locale in [docs](docs/README.md) and the [shellcode guide](docs/shellcode-compiler/README.md).

## Overview

NeverC compiles standard C into hosted binaries, freestanding executables, and position-independent shellcode — all from a single toolchain. It targets **x86_64** and **AArch64** (little-endian only).

## Why NeverC?

C is already the simplest systems language. NeverC makes it even simpler:

- **Pure C23, nothing more** — No templates, no RAII, no operator overloading, no hidden control flow. What you read is what runs.
- **Built-in `string`** — Value-semantic strings with `+`, `==`, `.starts_with()` and automatic cleanup — no C++ required.
- **No exceptions** — Error handling stays explicit. No stack unwinding, no performance surprises.
- **Single binary** — Compiler + linker + runtimes ship as one executable. Zero external dependencies to set up.
- **LLM-friendly** — Minimal grammar and deterministic semantics mean AI-generated NeverC code compiles correctly more often than C++ alternatives.
- **True cross-compilation** — Build Windows executables and shellcode from macOS or Linux — no VM, no dual boot, no SDK hunting. The Windows SDK ships inside the compiler.
- **Security research built in** — Shellcode compilation, compile-time string encryption, and cross-platform PE generation are native to the toolchain — not afterthoughts bolted on with external scripts.

## Features

- **[Shellcode compiler](docs/shellcode-compiler/README.md)** — multi-stage IR/MIR pipeline, cross-platform extraction, import/syscall lowering, kernel-mode support, bad-byte auditing, and a plugin architecture
- **Integrated linker** — COFF, ELF, and Mach-O in one binary; no external `ld` or `link.exe`
- **Cross-compilation** — build Windows PE from macOS/Linux with bundled MSVC SDK support
- **[Built-in runtimes](docs/builtins/README.md)** — opt-in LLVM bitcode runtimes embedded in the compiler: [`string`](docs/builtins/string/README.md) (value-semantic string with dot-call methods and automatic memory management), [`mimalloc`](docs/builtins/mimalloc/README.md) (transparent high-performance allocator override), and [`xorstr`](docs/builtins/xorstr/README.md) (compile-time string encryption with anti-signature decryption)
- **[Plugin API](docs/plugin-api/README.md)** — pure C ABI for out-of-tree pass plugins; single-header SDK with zero LLVM/CRT dependencies, supporting IR, MIR, binary, and linker hook points
- **[`.nc` extension](docs/nc-extension/README.md)** — use `.nc` as file extension to auto-enable all NeverC features (`string`, Rust-style integer types) without extra flags
- **Lean LLVM build** — only x86_64 and AArch64 backends; C++/ObjC/OpenMP paths stripped

## Quick Example

```c
#include <stdio.h>

typedef struct { string user; string pass; } creds;

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());

    // Compile-time encryption — `strings ./bin` cannot find these literals
    creds login = {.user = "admin".encrypt(), .pass = "s3cret".encrypt()};
    string paths[] = {"/api/v1".encrypt(), "/api/v2".encrypt()};

    // Zero-allocation decrypt-and-compare (plaintext never fully in memory)
    if (login.user == "admin".encrypt() && login.pass == "s3cret".encrypt()) {
        for (int i = 0; i < 2; i++)
            if (msg.starts_with(paths[i]))
                printf("route matched: %s\n", paths[i].c_str());
    }
    return 0;
}
```

> **Note:** The built-in **`string`** type requires **`-fbuiltin-string`** for `.c` files. It is enabled automatically for [**`.nc` files**](docs/nc-extension/README.md) and in **`-fshellcode`** mode.

```bash
# macOS arm64 / x86_64
neverc -fshellcode -target arm64-apple-macos hello.c -o hello.bin
neverc -fshellcode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fshellcode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fshellcode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fshellcode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fshellcode -target aarch64-linux-android hello.c -o hello.bin
neverc -fshellcode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fshellcode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

See the **[documentation index](docs/README.md)** for detailed design notes, platform matrix, CLI reference, and examples. For complete buildable samples, see the **[examples/](examples/)** directory.

## Prebuilt macOS binaries

The release is ad-hoc signed (no Apple Developer ID, not notarized). If you downloaded it via a browser, clear the quarantine attribute once after extracting:

```bash
xattr -dr com.apple.quarantine /path/to/extracted/install
```

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
cmake --build build-neverc --target check-neverc
```

### Verify

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Cross-Compiling to Windows

NeverC bundles a Windows SDK and WDK in `runtime/`; no external SDK setup is needed.

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

See [shellcode compiler docs](docs/shellcode-compiler/README.md) for Windows shellcode (`-fshellcode`, PEB import resolution, etc.).

## Contributing

The default development branch is **`dev`**. Clone and check it out before
you start work; open pull requests against `dev`.

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## License

[AGPL-3.0](LICENSE)

LLVM components retain their [Apache-2.0 WITH LLVM-exception](llvm/LICENSE.TXT) license.
