**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Progetto NeverC](../README.it.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (shellcode README and breadcrumbs).

# Documentazione NeverC

Note di progettazione, riferimento API e guide per ogni sottosistema NeverC.

---

## Compilatore shellcode

La pipeline di compilazione shellcode è il focus principale di ricerca di NeverC. Architettura, opzioni CLI, matrice piattaforme ed esempi:

**[Compilatore shellcode →](shellcode-compiler/README.it.md)**

| Documento | Descrizione |
|-----------|-------------|
| [README](shellcode-compiler/README.it.md) | Panoramica, avvio rapido, target supportati |
| [Pipeline & PIC](shellcode-compiler/pipeline-and-pic/README.it.md) | Design IR → oggetto → estrazione |
| [IR Pass Design](shellcode-compiler/ir-pass-design/README.it.md) | Motivazione di ogni pass IR |
| [MIR Pass Design](shellcode-compiler/mir-pass-design/README.it.md) | Pass MIR backend |
| [Kernel-Mode Shellcode](shellcode-compiler/kernel-mode-shellcode/README.it.md) | Compilazione Ring-0 |
| [Plugin Interface](shellcode-compiler/plugin-interface/README.it.md) | Plugin offuscazione e codifica |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.it.md) | `TargetDesc` ed estrattori |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.it.md) | Aggiungere piattaforma |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.it.md) | Istruzioni ARM64 dalla prospettiva shellcode |
| [Roadmap](shellcode-compiler/roadmap/README.it.md) | Lavoro pianificato |
| [Progress](shellcode-compiler/progress/README.it.md) | Stato implementazione |

---

## Tipo `string` integrato

NeverC fornisce un tipo valore `string` integrato per il C, unendo l'ergonomia di `std::string` al supporto Unicode di `QString`. Attivazione tramite `-fbuiltin-string` (automatico in modalità `-fshellcode`).

**[String integrato →](builtin-string/README.it.md)**
