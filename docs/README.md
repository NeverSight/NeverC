**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC project](../README.md)

> Same locale: use the language bar above; sub-pages (shellcode guide) are linked in that language from this index.

# NeverC Documentation

Design notes, API reference, and guides for every NeverC subsystem.

---

## Shellcode Compiler

The shellcode compilation pipeline is NeverC's primary research focus. For architecture, CLI options, platform matrix, and examples, see:

**[Shellcode Compiler →](shellcode-compiler/README.md)**

| Document | Description |
|----------|-------------|
| [README](shellcode-compiler/README.md) | Overview, quick start, supported targets |
| [Pipeline & PIC](shellcode-compiler/pipeline-and-pic/README.md) | IR → object → extraction design |
| [IR Pass Design](shellcode-compiler/ir-pass-design/README.md) | Rationale for each IR pass |
| [MIR Pass Design](shellcode-compiler/mir-pass-design/README.md) | Backend MIR passes |
| [Kernel-Mode Shellcode](shellcode-compiler/kernel-mode-shellcode/README.md) | Ring-0 compilation |
| [Plugin Interface](shellcode-compiler/plugin-interface/README.md) | Obfuscation and encoding plugins |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.md) | `TargetDesc` and extractors |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.md) | Adding new targets |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.md) | ARM64 instructions from a shellcode perspective |
| [Roadmap](shellcode-compiler/roadmap/README.md) | Planned work |
| [Progress](shellcode-compiler/progress/README.md) | Implementation status |

---

## The `.nc` File Extension

NeverC recognizes `.nc` as its native source file extension. When you use `.nc`, the compiler automatically enables all NeverC language extensions (`-fneverc-types`, `-fbuiltin-string`) — no extra flags needed.

**[`.nc` Extension →](nc-extension/README.md)**

---

## Built-in Runtimes

NeverC extends standard C with opt-in built-in runtimes embedded as LLVM bitcode. Each is controlled by a `-fbuiltin-<name>` flag. For `.nc` files, `string` is enabled automatically.

**[Built-in Runtime System →](builtins/README.md)**

| Built-in | Flag | Description |
|----------|------|-------------|
| [Built-in String](builtins/string/README.md) | `-fbuiltin-string` | Value-semantic `string` type with dot-call methods, automatic memory management, and native UTF-8 |
| [Built-in mimalloc](builtins/mimalloc/README.md) | `-fbuiltin-mimalloc` | Transparent high-performance `mimalloc` allocator override for `malloc`/`free`/`calloc`/`realloc` |
| [String Encryption (xorstr)](builtins/xorstr/README.md) | `-fencrypt-call-strings` | Compile-time string encryption with stack-allocated XOR decryption and anti-signature algorithm |
