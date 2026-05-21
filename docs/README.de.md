**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC-Projekt](../README.de.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (shellcode README and breadcrumbs).

# NeverC-Dokumentation

Designnotizen, API-Referenz und Leitfäden für jedes NeverC-Subsystem.

---

## Shellcode-Compiler

Die Shellcode-Kompilierungspipeline ist NeverCs Hauptforschungsschwerpunkt. Architektur, CLI-Optionen, Plattformmatrix und Beispiele:

**[Shellcode-Compiler →](shellcode-compiler/README.de.md)**

| Dokument | Beschreibung |
|----------|--------------|
| [README](shellcode-compiler/README.de.md) | Überblick, Schnellstart, unterstützte Ziele |
| [Pipeline & PIC](shellcode-compiler/pipeline-and-pic/README.de.md) | Design IR → Objekt → Extraktion |
| [IR Pass Design](shellcode-compiler/ir-pass-design/README.de.md) | Begründung jeder IR-Pass |
| [MIR Pass Design](shellcode-compiler/mir-pass-design/README.de.md) | Backend-MIR-Passes |
| [Kernel-Mode Shellcode](shellcode-compiler/kernel-mode-shellcode/README.de.md) | Ring-0-Kompilierung |
| [Plugin Interface](shellcode-compiler/plugin-interface/README.de.md) | Obfuskations- und Encoder-Plugins |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.de.md) | `TargetDesc` und Extraktoren |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.de.md) | Neue Plattform hinzufügen |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.de.md) | ARM64-Befehle aus Shellcode-Perspektive |
| [Roadmap](shellcode-compiler/roadmap/README.de.md) | Geplante Arbeit |
| [Progress](shellcode-compiler/progress/README.de.md) | Implementierungsstand |

---

## Eingebauter `string`-Typ

NeverC bietet einen eingebauten `string`-Werttyp für C, der `std::string`-Ergonomie und `QString`-Unicode-Unterstützung vereint. Aktivierung über `-fbuiltin-string` (automatisch im `-fshellcode`-Modus).

**[Eingebauter String →](builtins/string/README.de.md)**
