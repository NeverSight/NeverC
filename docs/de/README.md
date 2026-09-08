**Sprachen**: [English](../README.md) | [简体中文](../zh-CN/README.md) | [繁體中文](../zh-TW/README.md) | [日本語](../ja/README.md) | [한국어](../ko/README.md) | [Français](../fr/README.md) | [Deutsch](README.md) | [Español](../es/README.md) | [Italiano](../it/README.md) | [Русский](../ru/README.md) | [العربية](../ar/README.md)

[← NeverC-Projekt](project.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (dyncode README and breadcrumbs).

# NeverC-Dokumentation

Designnotizen, API-Referenz und Leitfäden für jedes NeverC-Subsystem.

---

## DynCode-Compiler

Die DynCode-Kompilierungspipeline ist NeverCs Hauptforschungsschwerpunkt. Architektur, CLI-Optionen, Plattformmatrix und Beispiele:

**[DynCode-Compiler →](dyncode-compiler/README.md)**

| Dokument | Beschreibung |
|----------|--------------|
| [README](dyncode-compiler/README.md) | Überblick, Schnellstart, unterstützte Ziele |
| [Pipeline & PIC](dyncode-compiler/pipeline-and-pic.md) | Design IR → Objekt → Extraktion |
| [IR Pass Design](dyncode-compiler/ir-pass-design.md) | Begründung jeder IR-Pass |
| [MIR Pass Design](dyncode-compiler/mir-pass-design.md) | Backend-MIR-Passes |
| [Kernel-Mode DynCode](dyncode-compiler/kernel-mode-dyncode.md) | Ring-0-Kompilierung |
| [Cross-Platform Architecture](dyncode-compiler/cross-platform-architecture.md) | `TargetDesc` und Extraktoren |
| [Platform Extension Guide](dyncode-compiler/platform-extension-guide.md) | Neue Plattform hinzufügen |
| [ARM64 Assembly Tutorial](dyncode-compiler/arm64-assembly-tutorial.md) | ARM64-Befehle aus DynCode-Perspektive |
| [Roadmap](dyncode-compiler/roadmap.md) | Geplante Arbeit |
| [Progress](dyncode-compiler/progress.md) | Implementierungsstand |

---

## Die `.nc` Dateierweiterung

NeverC erkennt `.nc` als seine native Quelldateierweiterung. Mit `.nc` werden alle NeverC-Spracherweiterungen (`-fneverc-types`, `-fbuiltin-string`) automatisch aktiviert — keine zusätzlichen Flags erforderlich.

**[`.nc`-Erweiterung →](nc-extension.md)**

---

## Integrierte Laufzeiten

NeverC erweitert Standard-C mit integrierten Laufzeiten als LLVM-Bitcode. Jede wird über ein `-fbuiltin-<name>`-Flag gesteuert. `.nc`-Dateien aktivieren `string` automatisch.

**[Integriertes Laufzeitsystem →](builtins/README.md)**

| Integriert | Flag | Beschreibung |
|------------|------|-------------|
| [Integrierter String](builtins/string.md) | `-fbuiltin-string` | `string`-Werttyp mit Punkt-Aufruf-Methoden, automatischer Speicherverwaltung, nativem UTF-8 |
| [Integriertes mimalloc](builtins/mimalloc.md) | `-fbuiltin-mimalloc` | Transparenter `mimalloc` Hochleistungs-Allokator-Override für `malloc`/`free`/`calloc`/`realloc` |
| [Zeichenkettenverschlüsselung (xorstr)](builtins/xorstr.md) | `-fencrypt-call-strings` | Instanzbezogene Verschlüsselung, verpflichtende späte Versiegelung, Expansion pro Aufrufstelle und volatile Stack-Bereinigung |
| [Zeichenketten-Hashing (strhash)](builtins/strhash.md) | `-fstrhash-algo` / `-fstrhash-fold` | Kompilierzeit-Zeichenketten-Hashing, übereinstimmende Laufzeit, optionaler IR-Fold |

---

## Plugin-API

NeverC öffnet seine gesamte Toolchain über eine reine C-ABI. Ein Plugin ist ein gemeinsames Modul (`.dll` / `.so` / `.dylib`), das sich an jede der 130 benannten Compilerphasen hängen kann — von der Kommandozeilenanalyse bis zum fertig gelinkten Abbild — als Beobachter, als Interceptor oder als ersetzender Provider. Das SDK besteht nur aus Headern: keine LLVM-Header, keine Compiler-Anbindung.

**[Plugin-API →](plugin-api/README.md)**

| Dokument | Beschreibung |
|----------|--------------|
| [README](plugin-api/README.md) | Einstiegspunkt, Phasen, Schnittstellenaushandlung, Registrierung, ABI-Regeln |
| [Python-Plugins](plugin-api/python.md) | Optionales eingebettetes Python, Lebenszyklus, Optionen, schreibgeschützte Observer, Diagnosen und Grenzen |
| [Driver-API](plugin-api/driver.md) | Kommandozeile, Toolchain-Auswahl, Aktionsgraph, Job-Graph |
| [Source- und E/A-API](plugin-api/source.md) | VFS-Provider, Quellpositionen, Puffer, Ausgabesenken, Abhängigkeiten |
| [Präprozessor-API](plugin-api/prep.md) | Token, Makros, Pragmas, Includes, Feature-Abfragen, 39 Ereignisarten |
| [AST- und Semantik-API](plugin-api/ast-sema.md) | Parser-Erweiterung, AST-Mutation, Namensauflösung, Typen, Konstanten |
| [IR-API](plugin-api/ir.md) | LLVM-IR lesen, transaktionales Bauen, Analysen, Passes, Provider |
| [MIR-API](plugin-api/mir.md) | Maschinenfunktionen, Register, Stackframes, MIR-Passes und -Analysen |
| [Target, MC, Assembly, Objekt](plugin-api/target-mc-object.md) | Target-Registrierung, Aufrufkonventionen, MC-Kodierung, Objektgraphen |
| [Link- und LTO-API](plugin-api/link-lto.md) | Link-Graph, Symbolauflösung, GC/ICF, Linker- und LTO-Provider |
| [DynCode-API](plugin-api/dyncode.md) | Flache positionsunabhängige Images, Import-Lowering, Zeichensatzkodierung |
| [Eigene Aufrufkonventionen](plugin-api/custom-callconv.md) | Datengetriebene Aufrufkonventions-Plugins |

---

## Roadmap

Wichtigste geplante Richtungen des NeverC-Projekts: Standardbibliothek, EVM-Smart-Contract-Backend, Solana-eBPF-Backend.

**[Roadmap →](roadmap.md)**

| Funktion | Beschreibung |
|----------|-------------|
| Standardbibliothek (`std`) | Go-ähnliche Batterien-inklusive-Pakete: `fmt`, `os`, `io`, `net`, `crypto`, `encoding`, `sync` und mehr |
| Obfuskations-Plugin-Suite (`neverc-obfuscation`) | Erstanbieter VM, MBA, Kontrollflussverflachung, polymorphe Engine, Anti-Tamper-Plugins |
| UI-Komponentenbibliothek (`neverc-ui`) | Qt-inspirierte plattformübergreifende UI, HTML/JS/CSS-Renderer, Drag-and-Drop-Designer, KI-nativer Workflow |
| IDE & Sprachwerkzeuge (`neverc-ide`) | VSCode-Erweiterung + Standalone-IDE für `.nc`-Dateien, IntelliSense, Debugging, DynCode-Pipeline-Visualisierung |
| EVM-Smart-Contracts | C zu EVM-Bytecode kompilieren — Smart Contracts in C statt Solidity |
| Solana eBPF | C zu Solana-eBPF-Bytecode kompilieren — On-Chain-Programmentwicklung in C |

---

## CLI-Tools

Benutzerbefehle jenseits einer einzelnen Kompilierung.

| Dokument | Beschreibung |
|----------|--------------|
| [`neverc run`](run.md) | Temporäres Binary kompilieren, lokal ausführen und löschen (`go run`-Stil) |
| [`neverc translate`](translate.md) | C++ nach NeverC übersetzen |
| [`neverc update`](update.md) | Release-Installation up-/downgraden (Compiler + installierte Runtimes auf einen Tag) |
| [`neverc runtime`](runtime.md) | Cross-Compile-Sysroots installieren, auflisten, aktualisieren oder entfernen |
| [`neverc build` / `neverc make`](build.md) | GNU-Make-kompatibler Treiber für Beispiel- und Projekt-Makefiles |
| [Release-Binärdateien und `--strip`](release-builds.md) | Laufzeitunnötige Symbole und Quelldebug entfernen und `.ko`-Symbole kernelgerecht strukturell umbenennen (kein hash und keine encryption) |

---

## Windows-Sicherheitsziele

| Dokument | Beschreibung |
|----------|--------------|
| [VBS-Enklaven-DLLs](vbs-enclave.md) | Microsoft-kompatible VBS-Enklaven-Images linken, validieren, verarbeiten, signieren und laden |

---

## Lokale Entwicklung

NeverC aus dem Quellcode kompilieren und die lokale Entwicklungsumgebung einrichten, einschließlich PATH-Konfiguration.

**[Lokale Entwicklung →](local-dev.md)**

---

## Beispiele

Vollständig kompilierbare Beispiele für die plattformübergreifende Kompilierung mit NeverC. Cross-Kompilierung von macOS / Linux.

**[Beispiele →](examples.md)**

---

## Urheber- und Quellenangaben

**[Leitfaden zur Quellenangabe →](attribution.md)**
