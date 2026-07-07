**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation Index](../README.md)

# NeverC Roadmap

This document outlines the major planned directions for the NeverC project beyond its current dyncode compiler and built-in runtime capabilities.

---

## 1. Standard Library (`std`)

NeverC will ship a comprehensive standard library modeled after Go's standard library — providing batteries-included packages that cover common systems programming needs without external dependencies.

### Planned Packages


| Package     | Description                                                                 |
| ----------- | --------------------------------------------------------------------------- |
| `fmt`       | Formatted I/O (printf-family with type-safe extensions)                     |
| `os`        | OS interaction: environment variables, process management, file permissions |
| `io`        | Reader/Writer interfaces, buffered I/O, pipe utilities                      |
| `fs`        | Filesystem operations: walk, glob, temp files, atomic writes                |
| `net`       | TCP/UDP sockets, DNS resolution, HTTP client/server                         |
| `net/http`  | HTTP/1.1 and HTTP/2 client and server                                       |
| `crypto`    | Hashing (SHA-256, SHA-512, BLAKE3), HMAC, AES, ChaCha20, RSA, Ed25519       |
| `encoding`  | JSON, Base64, Hex, CSV, binary (little/big endian)                          |
| `sync`      | Mutex, RWLock, WaitGroup, Once, atomic operations                           |
| `time`      | Monotonic/wall clocks, duration, timers, formatting                         |
| `bytes`     | Byte slice manipulation, buffer                                             |
| `math`      | Constants, elementary functions, random number generation                   |
| `sort`      | Generic sorting and searching                                               |
| `container` | Linked list, heap, ring buffer                                              |
| `log`       | Structured logging with levels                                              |
| `flag`      | Command-line flag parsing                                                   |
| `path`      | Path manipulation (POSIX and Windows)                                       |
| `regexp`    | Regular expression matching (RE2 syntax)                                    |
| `compress`  | gzip, zlib, zstd, lz4                                                       |
| `hash`      | CRC32, CRC64, FNV, xxHash                                                   |
| `unicode`   | Unicode tables, case folding, UTF-8/UTF-16 conversion                       |


### Design Principles

- **Pure C23** — every package compiles as standard NeverC/C23; no hidden C++ or platform-specific assembly
- **Zero external dependencies** — the standard library ships as LLVM bitcode embedded in the compiler, just like the existing `string` and `mimalloc` built-ins
- **Cross-platform** — all packages work on macOS, Linux, and Windows (x86_64 / AArch64)
- **DynCode-compatible** — packages that make sense in freestanding mode (e.g., `crypto`, `encoding`, `bytes`) work under `-fdyncode`

---

## 2. Obfuscation Plugin Suite (`neverc-obfuscation`)

NeverC will ship a first-party suite of code obfuscation plugins — reference implementations that demonstrate the Plugin API's full capabilities while providing production-grade code protection out of the box.

### Planned Plugins

| Plugin | Interpose Point | Description |
|--------|-----------|-------------|
| Junk Code Insertion | `RunAfterFinalMIR` | Insert semantically dead but syntactically valid instruction sequences between real basic blocks |
| Opaque Predicates | `RunBeforePreEmit` | Insert always-true/always-false branches guarded by number-theoretic invariants; adds dead paths that confuse analysis |
| Control Flow Flattening | `RunAfterStackify` | Scatter basic blocks into a switch-dispatched loop; destroys natural CFG structure for decompilers |
| Anti-Tamper | `RunPostFinalize` | Embed self-integrity checks (CRC/hash of code sections) that trigger failure on patching |
| Polymorphic Engine | `RunPostExtract` | Seed-based output variation — each compilation produces functionally equivalent but structurally different code; defeats signature-based detection |
| MBA (Mixed Boolean Arithmetic) | `RunAfterInlining` | Replace arithmetic/boolean expressions with equivalent but opaque MBA forms (e.g., `x + y` → `(x ^ y) + 2 * (x & y)` chains); resists symbolic execution |
| VM (Code Virtualization) | `RunAfterFinalIR` | Convert functions into custom bytecode executed by an embedded interpreter; defeats static disassembly and signature matching |

### Design Principles

- **Pure Plugin API** — every obfuscation ships as a `.dll` / `.so` / `.dylib` plugin; no compiler fork required
- **Composable** — plugins stack: apply MBA first, then flatten, then virtualize — each pass is independent
- **Configurable** — per-function annotations (`__attribute__((obfuscate("vm")))`) to selectively protect hot paths without whole-program overhead
- **Auditable** — each plugin logs its transformations for security review; before/after IR diff output available via `-fdyncode-dump-ir`
- **DynCode-compatible** — all plugins work in `-fdyncode` mode; generated code remains position-independent

---

## 3. UI Component Library (`neverc-ui`)

NeverC will provide a cross-platform UI component library inspired by Qt — but with an HTML/JS/CSS frontend rendering engine, making it inherently AI-friendly for interface design.

### Goals

- **Component-based architecture** — windows, buttons, text inputs, lists, trees, tables, menus, dialogs, tabs, and layout containers as first-class C types
- **HTML/JS/CSS renderer** — UI is rendered via an embedded lightweight browser engine; developers write C logic while the visual layer uses standard web technologies
- **Drag-and-drop visual designer** — a companion GUI builder that generates NeverC-compatible C code, enabling rapid prototyping without hand-writing layout code
- **AI-native design workflow** — LLMs can generate both the C business logic and the HTML/CSS layout in a single pass, because the visual layer uses the most widely-understood UI language on earth
- **Native look and feel** — platform-adaptive themes (macOS, Windows, Linux) via CSS variables and system font/color detection
- **Lightweight embedding** — the renderer ships as a built-in runtime (like `string` / `mimalloc`); no Electron-scale overhead
- **Event system** — C callback functions for user interactions (click, input, resize, drag, keyboard, custom events)
- **Data binding** — declarative binding between C structs and UI state; changes propagate automatically
- **Custom rendering** — escape hatch to raw canvas/WebGL for game UIs, data visualization, or custom widgets

### Why HTML/CSS for a C UI Library?

- Every AI model already knows HTML/CSS — generating UI code requires zero specialized training
- Web technologies are the most battle-tested layout system; no need to reinvent flexbox, grid, or text rendering
- Security research tools (dashboards, hex viewers, packet inspectors) benefit from rich, styled interfaces without learning a proprietary widget API
- The visual designer can export HTML templates that work both in the NeverC app and in a standalone browser for rapid iteration

---

## 4. IDE & Language Tooling (`neverc-ide`)

NeverC will provide first-class IDE support for the `.nc` language extension — a VSCode extension for immediate productivity and a standalone NeverC IDE for a fully integrated development experience.

### VSCode Extension

- **Syntax highlighting** — full `.nc` grammar with semantic token support for NeverC-specific types (`string`, `u8`–`u64`, `i8`–`i64`, `f32`, `f64`)
- **IntelliSense** — auto-completion for built-in types, dot-call methods (`.c_str()`, `.len()`, `.starts_with()`), and `#include` paths
- **Diagnostics** — real-time error and warning display from `neverc` compiler output
- **Go to definition** — jump to function, struct, and macro definitions across translation units
- **Hover documentation** — inline docs for built-in functions, compiler intrinsics, and standard library packages
- **Code actions** — quick-fix suggestions for common errors, auto-import for `std` packages
- **Debugging** — integrated LLDB/GDB debug adapter with breakpoint, step, and variable inspection support
- **DynCode mode** — syntax-aware features for `-fdyncode` pipelines: bad-byte highlighting, dyncode size display, target-specific completions
- **Plugin API integration** — plugin interpose point visualization and scaffolding

### Standalone IDE

- **Built on NeverC UI (`neverc-ui`)** — the IDE is itself a showcase of the HTML/JS/CSS component library, dogfooding the UI framework
- **Integrated terminal** — build, run, and debug without leaving the IDE
- **Visual dyncode pipeline** — graphical view of the IR → MIR → extraction pipeline with pass-by-pass output inspection
- **Project templates** — one-click scaffolding for hosted binaries, dyncode, EVM contracts, and Solana programs
- **AI-assisted coding** — built-in LLM integration that understands NeverC semantics, generates `.nc` code, and explains compiler diagnostics
- **Cross-compilation dashboard** — visual target selector with platform matrix and build status

### Why Both VSCode and Standalone?

- VSCode captures the majority of developers who already live in that ecosystem
- The standalone IDE provides a deeper, purpose-built experience for security researchers who want dyncode pipeline visualization and integrated binary analysis
- Both share the same language server backend — improvements benefit both simultaneously

---

## 5. EVM Smart Contract Backend

NeverC will support compiling C source code into EVM (Ethereum Virtual Machine) bytecode — enabling developers to write smart contracts in C instead of Solidity.

### Goals

- **New LLVM backend target** — `evm` target triple (e.g., `neverc --target=evm hello.c -o contract.bin`)
- **ABI compatibility** — generate Solidity-compatible ABI descriptors so contracts can interact with existing Ethereum tooling (Hardhat, Foundry, ethers.js)
- **Storage layout** — map C structs to EVM storage slots with deterministic layout
- **Built-in EVM primitives** — `msg.sender`, `msg.value`, `block.number`, `tx.origin` as built-in variables or intrinsics
- **Payable / view / pure modifiers** — function attributes that map to Solidity visibility semantics
- **Event emission** — `LOG0`–`LOG4` opcode generation from annotated function calls
- **Gas optimization** — IR passes that minimize gas cost (stack scheduling, constant folding, dead storage elimination)
- **Revert / require** — error handling primitives with custom error messages

### Why C for EVM?

- Solidity's syntax is familiar to JavaScript developers but foreign to systems programmers; C is universal
- NeverC's existing IR optimization pipeline can produce tighter bytecode than `solc` in many cases
- Security researchers already think in C — writing audit tools and fuzzers against C contracts is natural
- The plugin API allows custom gas-analysis and vulnerability-detection passes at compile time

---

## 6. Solana eBPF Backend

NeverC will support compiling C source code into Solana's eBPF bytecode — enabling on-chain program development in C.

### Goals

- **eBPF target** — `sbf` (Solana BPF) target triple (e.g., `neverc --target=sbf-solana hello.c -o program.so`)
- **Solana runtime bindings** — built-in headers for Solana system calls: `sol_invoke_signed`, `sol_log`, `sol_memcpy`, account info structs
- **Account model** — C struct overlays for Solana account data with automatic serialization/deserialization
- **CPI (Cross-Program Invocation)** — type-safe wrappers for calling other on-chain programs
- **PDAs (Program Derived Addresses)** — built-in functions for PDA derivation and verification
- **Compute budget awareness** — compiler warnings when estimated compute units exceed program limits
- **Anchor compatibility** — optional IDL generation for interoperability with Anchor-based frontends

### Why C for Solana?

- Solana's runtime already executes eBPF — C is the most natural source language for BPF targets
- Existing C-based BPF toolchains (clang + solana-bpf) require complex setup; NeverC bundles everything in one binary
- Performance-critical programs benefit from C's zero-overhead abstraction and NeverC's optimization passes
- The dyncode compilation experience (position-independent, minimal-runtime code) maps directly to on-chain program constraints

---

## Timeline

These features are in the research and design phase. No specific release dates are committed. Progress will be tracked in this document and announced on the project's release page.

| Feature | Status |
|---------|--------|
| Standard Library (`std`) | Research / Design |
| Obfuscation Plugin Suite (`neverc-obfuscation`) | Research / Design |
| UI Component Library (`neverc-ui`) | Research / Design |
| IDE & Language Tooling (`neverc-ide`) | Research / Design |
| EVM Smart Contract Backend | Research / Design |
| Solana eBPF Backend | Research / Design |


