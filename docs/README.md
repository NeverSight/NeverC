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

## Built-in String Type

NeverC ships a built-in `string` value type that brings `std::string` ergonomics and `QString`-parity Unicode support to C. Enabled via `-fbuiltin-string` (or automatically in `-fshellcode` mode).

**[Built-in String →](builtin-string/README.md)**
