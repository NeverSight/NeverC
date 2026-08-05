**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC-Projekt](i18n/README.de.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (dyncode README and breadcrumbs).

# NeverC-Dokumentation

Designnotizen, API-Referenz und Leitfäden für jedes NeverC-Subsystem.

---

## DynCode-Compiler

Die DynCode-Kompilierungspipeline ist NeverCs Hauptforschungsschwerpunkt. Architektur, CLI-Optionen, Plattformmatrix und Beispiele:

**[DynCode-Compiler →](dyncode-compiler/README.de.md)**

| Dokument | Beschreibung |
|----------|--------------|
| [README](dyncode-compiler/README.de.md) | Überblick, Schnellstart, unterstützte Ziele |
| [Pipeline & PIC](dyncode-compiler/pipeline-and-pic/README.de.md) | Design IR → Objekt → Extraktion |
| [IR Pass Design](dyncode-compiler/ir-pass-design/README.de.md) | Begründung jeder IR-Pass |
| [MIR Pass Design](dyncode-compiler/mir-pass-design/README.de.md) | Backend-MIR-Passes |
| [Kernel-Mode DynCode](dyncode-compiler/kernel-mode-dyncode/README.de.md) | Ring-0-Kompilierung |
| [Cross-Platform Architecture](dyncode-compiler/cross-platform-architecture/README.de.md) | `TargetDesc` und Extraktoren |
| [Platform Extension Guide](dyncode-compiler/platform-extension-guide/README.de.md) | Neue Plattform hinzufügen |
| [ARM64 Assembly Tutorial](dyncode-compiler/arm64-assembly-tutorial/README.de.md) | ARM64-Befehle aus DynCode-Perspektive |
| [Roadmap](dyncode-compiler/roadmap/README.de.md) | Geplante Arbeit |
| [Progress](dyncode-compiler/progress/README.de.md) | Implementierungsstand |

---

## Die `.nc` Dateierweiterung

NeverC erkennt `.nc` als seine native Quelldateierweiterung. Mit `.nc` werden alle NeverC-Spracherweiterungen (`-fneverc-types`, `-fbuiltin-string`) automatisch aktiviert — keine zusätzlichen Flags erforderlich.

**[`.nc`-Erweiterung →](nc-extension/README.de.md)**

---

## Integrierte Laufzeiten

NeverC erweitert Standard-C mit integrierten Laufzeiten als LLVM-Bitcode. Jede wird über ein `-fbuiltin-<name>`-Flag gesteuert. `.nc`-Dateien aktivieren `string` automatisch.

**[Integriertes Laufzeitsystem →](builtins/README.de.md)**

| Integriert | Flag | Beschreibung |
|------------|------|-------------|
| [Integrierter String](builtins/string/README.de.md) | `-fbuiltin-string` | `string`-Werttyp mit Punkt-Aufruf-Methoden, automatischer Speicherverwaltung, nativem UTF-8 |
| [Integriertes mimalloc](builtins/mimalloc/README.de.md) | `-fbuiltin-mimalloc` | Transparenter `mimalloc` Hochleistungs-Allokator-Override für `malloc`/`free`/`calloc`/`realloc` |
| [Zeichenkettenverschlüsselung (xorstr)](builtins/xorstr/README.de.md) | `-fencrypt-call-strings` | Kompilierzeit-Zeichenkettenverschlüsselung, Stack-XOR-Entschlüsselung, Anti-Signatur |
| [Zeichenketten-Hashing (strhash)](builtins/strhash/README.de.md) | `-fstrhash-algo` / `-fstrhash-fold` | Kompilierzeit-Zeichenketten-Hashing, übereinstimmende Laufzeit, optionaler IR-Fold |

---

## Plugin-API

NeverC öffnet seine gesamte Toolchain über eine reine C-ABI. Ein Plugin ist ein gemeinsames Modul (`.dll` / `.so` / `.dylib`), das sich an jede der 130 benannten Compilerphasen hängen kann — von der Kommandozeilenanalyse bis zum fertig gelinkten Abbild — als Beobachter, als Interceptor oder als ersetzender Provider. Das SDK besteht nur aus Headern: keine LLVM-Header, keine Compiler-Anbindung.

**[Plugin-API →](plugin-api/README.de.md)**

| Dokument | Beschreibung |
|----------|--------------|
| [README](plugin-api/README.de.md) | Einstiegspunkt, Phasen, Schnittstellenaushandlung, Registrierung, ABI-Regeln |
| [Driver-API](plugin-api/driver.de.md) | Kommandozeile, Toolchain-Auswahl, Aktionsgraph, Job-Graph |
| [Source- und E/A-API](plugin-api/source.de.md) | VFS-Provider, Quellpositionen, Puffer, Ausgabesenken, Abhängigkeiten |
| [Präprozessor-API](plugin-api/prep.de.md) | Token, Makros, Pragmas, Includes, Feature-Abfragen, 39 Ereignisarten |
| [AST- und Semantik-API](plugin-api/ast-sema.de.md) | Parser-Erweiterung, AST-Mutation, Namensauflösung, Typen, Konstanten |
| [IR-API](plugin-api/ir.de.md) | LLVM-IR lesen, transaktionales Bauen, Analysen, Passes, Provider |
| [MIR-API](plugin-api/mir.de.md) | Maschinenfunktionen, Register, Stackframes, MIR-Passes und -Analysen |
| [Target, MC, Assembly, Objekt](plugin-api/target-mc-object.de.md) | Target-Registrierung, Aufrufkonventionen, MC-Kodierung, Objektgraphen |
| [Link- und LTO-API](plugin-api/link-lto.de.md) | Link-Graph, Symbolauflösung, GC/ICF, Linker- und LTO-Provider |
| [DynCode-API](plugin-api/dyncode.de.md) | Flache positionsunabhängige Images, Import-Lowering, Zeichensatzkodierung |
| [Eigene Aufrufkonventionen](plugin-api/custom-callconv/README.de.md) | Datengetriebene Aufrufkonventions-Plugins |

---

## Roadmap

Wichtigste geplante Richtungen des NeverC-Projekts: Standardbibliothek, EVM-Smart-Contract-Backend, Solana-eBPF-Backend.

**[Roadmap →](roadmap/README.de.md)**

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
| [`neverc run`](run/README.de.md) | Temporäres Binary kompilieren, lokal ausführen und löschen (`go run`-Stil) |

---

## Lokale Entwicklung

NeverC aus dem Quellcode kompilieren und die lokale Entwicklungsumgebung einrichten, einschließlich PATH-Konfiguration.

**[Lokale Entwicklung →](local-dev/README.de.md)**

---

## Beispiele

Vollständig kompilierbare Beispiele für die plattformübergreifende Kompilierung mit NeverC. Cross-Kompilierung von macOS / Linux.

**[Beispiele →](examples/README.de.md)**
