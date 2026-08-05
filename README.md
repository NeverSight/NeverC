**Languages**: [English](README.md) | [简体中文](docs/i18n/README.zh-CN.md) | [繁體中文](docs/i18n/README.zh-TW.md) | [日本語](docs/i18n/README.ja.md) | [한국어](docs/i18n/README.ko.md) | [Français](docs/i18n/README.fr.md) | [Deutsch](docs/i18n/README.de.md) | [Español](docs/i18n/README.es.md) | [Italiano](docs/i18n/README.it.md) | [Русский](docs/i18n/README.ru.md) | [العربية](docs/i18n/README.ar.md)

<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/assets/neverc-logo-dark.svg">
  <img src="docs/assets/neverc-logo-light.svg" width="72" alt="NeverC">
</picture>

# NeverC

**The AI-friendly C23 compiler for security research, built on LLVM**

Integrated linker · DynCode pipeline · Built-in runtimes (`string` · `mimalloc` · `xorstr` · `strhash`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#features)
![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#features)

[Documentation](docs/README.md) · [DynCode Guide](docs/dyncode-compiler/README.md) · [Built-in Runtimes](docs/builtins/README.md) · [Plugin API](docs/plugin-api/README.md) · [Roadmap](docs/roadmap/README.md)

</div>

---

> **Note:** GitHub always shows `README.md` as the repository homepage (no automatic locale detection). Use the language links above; follow in-page links and breadcrumbs to keep the same locale in [docs](docs/README.md) and the [dyncode guide](docs/dyncode-compiler/README.md).

## Overview

NeverC compiles standard C into hosted binaries, freestanding executables, and position-independent dyncode — all from a single toolchain. It targets **x86_64** and **AArch64** (little-endian only). Future releases will add **EVM** (Ethereum smart contracts) and **Solana eBPF** (on-chain programs) as compilation targets.

## Why NeverC?

C is already the simplest systems language. NeverC makes it even simpler:

- **Pure C23, nothing more** — No templates, no RAII, no operator overloading, no hidden control flow. What you read is what runs.
- **Built-in `string`** — Value-semantic strings with `+`, `==`, `.starts_with()` and automatic cleanup — no C++ required.
- **No exceptions** — Error handling stays explicit. No stack unwinding, no performance surprises.
- **Single binary** — Compiler + linker + runtimes ship as one executable. Zero external dependencies to set up.
- **LLM-friendly** — Minimal grammar and deterministic semantics mean AI-generated NeverC code compiles correctly more often than C++ alternatives.
- **True cross-compilation** — Build Windows PE, Linux ELF, macOS Mach-O, Android ELF, and dyncode from macOS or Linux — no VM, no dual boot, no SDK hunting. Platform SDKs ship inside the compiler.
- **Extensible with zero friction** — A single C header, 130 named compiler phases, and you have a [compiler plugin](docs/plugin-api/README.md) that can intercept any stage from IR optimization to final binary output — no LLVM knowledge needed.
- **Security research built in** — DynCode compilation, compile-time string encryption, and cross-platform PE generation are native to the toolchain — not afterthoughts bolted on with external scripts.

## Features

- **[DynCode compiler](docs/dyncode-compiler/README.md)** — multi-stage IR/MIR pipeline, cross-platform extraction, import/syscall lowering, kernel-mode support, bad-byte auditing, and a plugin architecture
- **Integrated linker** — COFF, ELF, and Mach-O in one binary; no external `ld` or `link.exe`
- **Cross-compilation** — build Windows PE, Linux ELF, macOS Mach-O, and Android ELF from any host with bundled platform SDKs
- **[Built-in runtimes](docs/builtins/README.md)** — LLVM bitcode runtimes embedded in the compiler: [`string`](docs/builtins/string/README.md) (value-semantic string with dot-call methods and automatic memory management), [`mimalloc`](docs/builtins/mimalloc/README.md) (transparent high-performance allocator override, on by default outside kernel and freestanding targets), [`xorstr`](docs/builtins/xorstr/README.md) (compile-time string encryption with anti-signature decryption), and [`strhash`](docs/builtins/strhash/README.md) (compile-time string hashing with matching runtime)
- **[Plugin API](docs/plugin-api/README.md)** — pure C ABI for out-of-tree plugins; single-header SDK with zero LLVM/CRT dependencies, spanning driver, preprocessor, AST, IR, MIR, MC, object, link, LTO, and dyncode phases
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

> **Note:** The built-in **`string`** type requires **`-fbuiltin-string`** for `.c` files. It is enabled automatically for [**`.nc` files**](docs/nc-extension/README.md) and in **`-fdyncode`** mode.

```bash
# macOS arm64 / x86_64
neverc -fdyncode -target arm64-apple-macos hello.c -o hello.bin
neverc -fdyncode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fdyncode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fdyncode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fdyncode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fdyncode -target aarch64-linux-android hello.c -o hello.bin
neverc -fdyncode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fdyncode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fdyncode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

See the **[documentation index](docs/README.md)** for detailed design notes, platform matrix, CLI reference, and examples. For complete buildable samples, see the **[examples](docs/examples/README.md)** directory.

## Installation

On **Linux x64/arm64** and **macOS arm64**, install the latest release with one command:

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/HEAD/install.sh | sh
```

This downloads the release archive for your platform, verifies it against `SHA256SUMS`, installs to `~/.neverc`, and prepends `~/.neverc/bin` to your shell `PATH`.

To pin a specific version:

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/v3389.1.2/install.sh | NEVERC_VERSION=v3389.1.2 sh
```

Verify the install:

```bash
neverc --version
neverc hello.c -o hello -fbuiltin-string
```

To run a program without keeping a binary, use
`neverc run -O2 -fbuiltin-string hello.c`.
Compiler flags precede the consecutive `.c`/`.nc` source list and following
arguments are passed to the program. For an advanced compiler invocation, use
an explicit separator, as in
`neverc run hello.c -O2 -fbuiltin-string --`.
The temporary executable inherits the current directory, environment, and
standard streams, returns the program's exit status, and is removed afterward;
use the normal compiler command when you need to keep the artifact.

**Windows x64/arm64** packages are on [GitHub Releases](https://github.com/NeverSight/NeverC/releases) for manual download. The macOS arm64 binary is Apple Developer ID signed and notarized.

Optional installer environment variables:

| Variable | Purpose |
|----------|---------|
| `NEVERC_INSTALL_DIR` | Install prefix (default: `~/.neverc`) |
| `NEVERC_VERSION` | Release tag, e.g. `v3389.1.2` (default: latest) |
| `NEVERC_NO_MODIFY_PATH=1` | Skip shell profile changes |

Cross-compilation sysroots (Windows SDK, Linux sysroot, etc.) are installed on demand after the compiler is on your `PATH`:

```bash
neverc runtime install all
neverc runtime install windows-x64
neverc runtime list
```

Update a release installation with the compiler and its already-installed
cross-compilation runtimes as one versioned unit:

```bash
neverc update                 # newest complete release
neverc update v3389.1.2       # exact release, including a downgrade
```

`neverc upgrade` is an alias. NeverC resolves one concrete release tag and
reinstalls only the runtimes that were already present, pinning every one to
the compiler's target tag. All required archives are downloaded, SHA256
verified, extracted, and validated before live files change; a staging or
checksum failure leaves the current installation untouched, and commit
failures are rolled back. If a runtime release is bad, run `neverc update`
with an earlier tag to roll the compiler and installed runtimes back together.

## Building from Source

See **[Local Development](docs/local-dev/README.md)** for build requirements, build commands, cross-compiling to Windows, PATH setup, and switching between a release install and an in-tree build.

## Contributing

NeverC is **C-only by design** (C23). C++, Objective-C, CUDA, and similar language
frontends are out of scope; pull requests that add them will be closed. If you need a
C++-oriented LLVM toolchain, consider [llvm-msvc](https://github.com/backengineering/llvm-msvc)
instead.

For large language, ABI, or runtime changes, open an issue first and discuss scope
before sending a pull request.

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
