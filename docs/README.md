**Languages**: [English](README.md) | [简体中文](zh-CN/README.md) | [繁體中文](zh-TW/README.md) | [日本語](ja/README.md) | [한국어](ko/README.md) | [Français](fr/README.md) | [Deutsch](de/README.md) | [Español](es/README.md) | [Italiano](it/README.md) | [Русский](ru/README.md) | [العربية](ar/README.md)

[← NeverC project](../README.md)

> Same locale: use the language bar above; sub-pages (dyncode guide) are linked in that language from this index.

# NeverC Documentation

Design notes, API reference, and guides for every NeverC subsystem.

---

## DynCode Compiler

The dyncode compilation pipeline is NeverC's primary research focus. For architecture, CLI options, platform matrix, and examples, see:

**[DynCode Compiler →](dyncode-compiler.md)**

| Document | Description |
|----------|-------------|
| [README](dyncode-compiler.md) | Overview, quick start, supported targets |
| [Pipeline & PIC](dyncode-compiler-pipeline-and-pic.md) | IR → object → extraction design |
| [IR Pass Design](dyncode-compiler-ir-pass-design.md) | Rationale for each IR pass |
| [MIR Pass Design](dyncode-compiler-mir-pass-design.md) | Backend MIR passes |
| [Kernel-Mode DynCode](dyncode-compiler-kernel-mode-dyncode.md) | Ring-0 compilation |
| [Cross-Platform Architecture](dyncode-compiler-cross-platform-architecture.md) | `TargetDesc` and extractors |
| [Platform Extension Guide](dyncode-compiler-platform-extension-guide.md) | Adding new targets |
| [ARM64 Assembly Tutorial](dyncode-compiler-arm64-assembly-tutorial.md) | ARM64 instructions from a dyncode perspective |
| [Roadmap](dyncode-compiler-roadmap.md) | Planned work |
| [Progress](dyncode-compiler-progress.md) | Implementation status |

---

## The `.nc` File Extension

NeverC recognizes `.nc` as its native source file extension. When you use `.nc`, the compiler automatically enables all NeverC language extensions (`-fneverc-types`, `-fbuiltin-string`) — no extra flags needed.

**[`.nc` Extension →](nc-extension.md)**

---

## Built-in Runtimes

NeverC extends standard C with opt-in built-in runtimes embedded as LLVM bitcode. Each is controlled by a `-fbuiltin-<name>` flag. For `.nc` files, `string` is enabled automatically.

**[Built-in Runtime System →](builtins.md)**

| Built-in | Flag | Description |
|----------|------|-------------|
| [Built-in String](builtins-string.md) | `-fbuiltin-string` | Value-semantic `string` type with dot-call methods, automatic memory management, and native UTF-8 |
| [Built-in mimalloc](builtins-mimalloc.md) | `-fbuiltin-mimalloc` | Transparent high-performance `mimalloc` allocator override for `malloc`/`free`/`calloc`/`realloc` |
| [String Encryption (xorstr)](builtins-xorstr.md) | `-fencrypt-call-strings` | Per-instance encryption, mandatory late sealing, per-call final expansion, and volatile stack cleanup |
| [String Hashing (strhash)](builtins-strhash.md) | `-fstrhash-algo` / `-fstrhash-fold` | Compile-time string hashing with matching runtime and optional IR constant folding |

---

## Plugin API

NeverC exposes its whole toolchain through a pure C ABI. A plugin is a shared module (`.dll` / `.so` / `.dylib`) that attaches to any of 130 named compiler phases — from command-line parsing to the final linked image — as an observer, an interceptor, or a replacement provider. The SDK is header-only: no LLVM headers, no compiler linkage.

**[Plugin API →](plugin-api.md)**

| Document | Description |
|----------|-------------|
| [README](plugin-api.md) | Entry point, phases, interface negotiation, registration, ABI rules |
| [Python plugins](plugin-api-python.md) | Optional embedded Python, lifecycle, options, read-only observers, diagnostics, limitations |
| [Driver API](plugin-api-driver.md) | Command line, toolchain selection, action graph, job graph |
| [Source and I/O API](plugin-api-source.md) | VFS providers, source locations, buffers, output sinks, dependencies |
| [Preprocessor API](plugin-api-prep.md) | Tokens, macros, pragmas, includes, feature queries, 39 event kinds |
| [AST and semantic API](plugin-api-ast-sema.md) | Parser extension, AST mutation, name lookup, types, constants |
| [IR API](plugin-api-ir.md) | LLVM IR reading, transactional building, analyses, passes, providers |
| [MIR API](plugin-api-mir.md) | Machine functions, registers, frames, MIR passes and analyses |
| [Target, MC, assembly, object](plugin-api-target-mc-object.md) | Target registration, calling conventions, MC encoding, object graphs |
| [Link and LTO API](plugin-api-link-lto.md) | Link graph, symbol resolution, GC/ICF, linker and LTO providers |
| [DynCode API](plugin-api-dyncode.md) | Flat position-independent images, import lowering, charset encoding |
| [Custom calling conventions](plugin-api-custom-callconv.md) | Data-driven calling-convention plugins |

---

## Roadmap

Major planned directions for the NeverC project: standard library, EVM smart contract backend, and Solana eBPF backend.

**[Roadmap →](roadmap.md)**

| Feature | Description |
|---------|-------------|
| Standard Library (`std`) | Go-style batteries-included packages: `fmt`, `os`, `io`, `net`, `crypto`, `encoding`, `sync`, and more |
| Obfuscation Plugin Suite (`neverc-obfuscation`) | First-party VM, MBA, control flow flattening, polymorphic engine, and anti-tamper plugins |
| UI Component Library (`neverc-ui`) | Qt-inspired cross-platform UI with HTML/JS/CSS renderer, drag-and-drop designer, AI-native workflow |
| IDE & Language Tooling (`neverc-ide`) | VSCode extension + standalone IDE for `.nc` files with IntelliSense, debugging, and dyncode pipeline visualization |
| EVM Smart Contracts | Compile C to EVM bytecode — write smart contracts in C instead of Solidity |
| Solana eBPF | Compile C to Solana eBPF bytecode — on-chain program development in C |

---

## CLI Tools

User-facing commands beyond a single compile invocation.

| Document | Description |
|----------|-------------|
| [`neverc run`](run.md) | Compile, execute locally, and discard a temporary binary (`go run`-style workflow) |
| [`neverc translate`](translate.md) | Translate C++ to NeverC |
| [`neverc update`](update.md) | Upgrade or downgrade a release install (compiler + installed runtimes as one tag) |
| [`neverc runtime`](runtime.md) | Install, list, update, or remove cross-compilation sysroots |
| [`neverc build` / `neverc make`](build.md) | GNU Make–compatible driver for example and project Makefiles |
| [Release binaries and `--strip`](release-builds.md) | Remove non-runtime symbols and source debug, including kernel-aware `.ko` structural symbol renaming (not a hash or encryption) |

---

## Windows security targets

| Document | Description |
|----------|-------------|
| [VBS enclave DLLs](vbs-enclave.md) | Link, validate, process, sign, and load Microsoft-compatible VBS enclave images |

---

## Local Development

Build NeverC from source and set up the local development environment with PATH configuration.

**[Local Development →](local-dev.md)**

---

## Examples

Complete buildable samples demonstrating NeverC's cross-platform compilation capabilities. All examples cross-compile from macOS / Linux.

**[Examples →](examples.md)**

---

## Source attribution

**[attribution guide →](attribution.md)**
