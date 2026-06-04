**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md)

# NeverC-Roadmap

Dieses Dokument skizziert die wichtigsten geplanten Richtungen des NeverC-Projekts jenseits des bestehenden Shellcode-Compilers und der integrierten Laufzeiten.

---

## 1. Standardbibliothek (`std`)

NeverC wird eine umfassende Standardbibliothek nach dem Vorbild von Gos Standardbibliothek liefern — Batterien-inklusive-Pakete, die gängige Systemprogrammierungsanforderungen ohne externe Abhängigkeiten abdecken.

### Geplante Pakete

| Paket | Beschreibung |
|-------|-------------|
| `fmt` | Formatierte E/A (printf-Familie + typsichere Erweiterungen) |
| `os` | OS-Interaktion: Umgebungsvariablen, Prozessverwaltung, Dateiberechtigungen |
| `io` | Reader/Writer-Schnittstellen, gepufferte E/A, Pipe-Utilities |
| `fs` | Dateisystemoperationen: Walk, Glob, temporäre Dateien, atomare Schreibvorgänge |
| `net` | TCP/UDP-Sockets, DNS-Auflösung, HTTP-Client/Server |
| `net/http` | HTTP/1.1- und HTTP/2-Client und -Server |
| `crypto` | Hashing (SHA-256, SHA-512, BLAKE3), HMAC, AES, ChaCha20, RSA, Ed25519 |
| `encoding` | JSON, Base64, Hex, CSV, Binär (Little/Big Endian) |
| `sync` | Mutex, RWLock, WaitGroup, Once, atomare Operationen |
| `time` | Monotone/Wanduhr, Dauer, Timer, Formatierung |
| `bytes` | Byte-Slice-Manipulation, Puffer |
| `math` | Konstanten, elementare Funktionen, Zufallszahlengenerierung |
| `sort` | Generisches Sortieren und Suchen |
| `container` | Verkettete Liste, Heap, Ringpuffer |
| `log` | Strukturiertes Logging mit Ebenen |
| `flag` | Befehlszeilen-Flag-Parsing |
| `path` | Pfadmanipulation (POSIX und Windows) |
| `regexp` | Regulärer-Ausdruck-Matching (RE2-Syntax) |
| `compress` | gzip, zlib, zstd, lz4 |
| `hash` | CRC32, CRC64, FNV, xxHash |
| `unicode` | Unicode-Tabellen, Groß-/Kleinschreibungsumwandlung, UTF-8/UTF-16-Konvertierung |

### Designprinzipien

- **Reines C23** — jedes Paket kompiliert als Standard-NeverC/C23; kein verstecktes C++ oder plattformspezifischer Assembler
- **Null externe Abhängigkeiten** — die Standardbibliothek wird als LLVM-Bitcode in den Compiler eingebettet, genau wie die bestehenden `string`- und `mimalloc`-Built-ins
- **Plattformübergreifend** — alle Pakete funktionieren auf macOS, Linux und Windows (x86_64 / AArch64)
- **Shellcode-kompatibel** — Pakete, die im Freestanding-Modus sinnvoll sind (z. B. `crypto`, `encoding`, `bytes`), funktionieren mit `-fshellcode`

---

## 2. UI-Komponentenbibliothek (`neverc-ui`)

NeverC wird eine plattformübergreifende UI-Komponentenbibliothek nach Qt-Vorbild bereitstellen — jedoch mit einer HTML/JS/CSS-Frontend-Rendering-Engine, die inhärent KI-freundlich für Interface-Design ist.

### Ziele

- **Komponentenbasierte Architektur** — Fenster, Buttons, Texteingaben, Listen, Bäume, Tabellen, Menüs, Dialoge, Tabs und Layout-Container als erstklassige C-Typen
- **HTML/JS/CSS-Renderer** — UI wird über eine eingebettete leichte Browser-Engine gerendert; Entwickler schreiben C-Logik, die visuelle Schicht nutzt Standard-Web-Technologien
- **Drag-and-Drop visueller Designer** — ein begleitender GUI-Builder, der NeverC-kompatiblen C-Code generiert; schnelles Prototyping ohne manuelles Layout-Schreiben
- **KI-nativer Design-Workflow** — LLMs können C-Geschäftslogik und HTML/CSS-Layout in einem Durchgang generieren
- **Natives Erscheinungsbild** — plattformadaptive Themes (macOS, Windows, Linux) über CSS-Variablen und Systemschrift-/Farberkennung
- **Leichte Einbettung** — der Renderer wird als integrierte Laufzeit bereitgestellt (wie `string` / `mimalloc`); kein Electron-Overhead
- **Eventsystem** — C-Callback-Funktionen für Benutzerinteraktionen (Klick, Eingabe, Größenänderung, Drag, Tastatur, benutzerdefinierte Events)
- **Datenbindung** — deklarative Bindung zwischen C-Structs und UI-Zustand; Änderungen propagieren automatisch
- **Benutzerdefiniertes Rendering** — Zugang zu rohem Canvas/WebGL für Spiel-UIs, Datenvisualisierung oder benutzerdefinierte Widgets

### Warum HTML/CSS für eine C-UI-Bibliothek?

- Jedes KI-Modell kennt bereits HTML/CSS — UI-Code-Generierung erfordert kein spezialisiertes Training
- Web-Technologien sind das bewährteste Layout-System; kein Bedarf, Flexbox, Grid oder Textrendering neu zu erfinden
- Sicherheitsforschungstools (Dashboards, Hex-Viewer, Paketinspektoren) profitieren von reichhaltigen, gestylten Interfaces ohne proprietäre Widget-API
- Der visuelle Designer exportiert HTML-Templates, die sowohl in der NeverC-App als auch im Standalone-Browser funktionieren

---

## 3. IDE & Language Tooling (`neverc-ide`)

NeverC will provide first-class IDE support for the `.nc` language extension — a VSCode extension for immediate productivity and a standalone NeverC IDE for a fully integrated development experience.

### VSCode Extension

- **Syntax highlighting** — full `.nc` grammar with semantic token support for NeverC-specific types (`string`, `u8`–`u64`, `i8`–`i64`, `f32`, `f64`)
- **IntelliSense** — auto-completion for built-in types, dot-call methods (`.c_str()`, `.len()`, `.starts_with()`), and `#include` paths
- **Diagnostics** — real-time error and warning display from `neverc` compiler output
- **Go to definition** — jump to function, struct, and macro definitions across translation units
- **Hover documentation** — inline docs for built-in functions, compiler intrinsics, and standard library packages
- **Code actions** — quick-fix suggestions for common errors, auto-import for `std` packages
- **Debugging** — integrated LLDB/GDB debug adapter with breakpoint, step, and variable inspection support
- **Shellcode mode** — syntax-aware features for `-fshellcode` pipelines: bad-byte highlighting, shellcode size display, target-specific completions
- **Plugin API integration** — plugin hook point visualization and scaffolding

### Standalone IDE

- **Built on NeverC UI (`neverc-ui`)** — the IDE is itself a showcase of the HTML/JS/CSS component library, dogfooding the UI framework
- **Integrated terminal** — build, run, and debug without leaving the IDE
- **Visual shellcode pipeline** — graphical view of the IR → MIR → extraction pipeline with pass-by-pass output inspection
- **Project templates** — one-click scaffolding for hosted binaries, shellcode, EVM contracts, and Solana programs
- **AI-assisted coding** — built-in LLM integration that understands NeverC semantics, generates `.nc` code, and explains compiler diagnostics
- **Cross-compilation dashboard** — visual target selector with platform matrix and build status

### Why Both VSCode and Standalone?

- VSCode captures the majority of developers who already live in that ecosystem
- The standalone IDE provides a deeper, purpose-built experience for security researchers who want shellcode pipeline visualization and integrated binary analysis
- Both share the same language server backend — improvements benefit both simultaneously

---


## 4. EVM-Smart-Contract-Backend

NeverC wird die Kompilierung von C-Quellcode in EVM-Bytecode (Ethereum Virtual Machine) unterstützen — damit Entwickler Smart Contracts in C statt in Solidity schreiben können.

### Ziele

- **Neues LLVM-Backend-Target** — `evm`-Target-Triple (z. B. `neverc --target=evm hello.c -o contract.bin`)
- **ABI-Kompatibilität** — Generierung Solidity-kompatibler ABI-Deskriptoren für die Interaktion mit bestehenden Ethereum-Tools (Hardhat, Foundry, ethers.js)
- **Speicherlayout** — Abbildung von C-Structs auf EVM-Speicherslots mit deterministischem Layout
- **Eingebaute EVM-Primitive** — `msg.sender`, `msg.value`, `block.number`, `tx.origin` als eingebaute Variablen oder Intrinsics
- **payable / view / pure Modifizierer** — Funktionsattribute, die auf Solidity-Sichtbarkeitssemantik abgebildet werden
- **Ereignisausgabe** — Generierung von `LOG0`–`LOG4`-Opcodes aus annotierten Funktionsaufrufen
- **Gas-Optimierung** — IR-Passes zur Minimierung der Gas-Kosten (Stack-Scheduling, Konstantenfaltung, Tote-Speicher-Elimination)
- **revert / require** — Fehlerbehandlungsprimitive mit benutzerdefinierten Fehlermeldungen

### Warum C für EVM?

- Soliditys Syntax ist JavaScript-Entwicklern vertraut, aber Systemprogrammierern fremd; C ist universell
- NeverCs bestehende IR-Optimierungspipeline kann in vielen Fällen kompakteren Bytecode als `solc` erzeugen
- Sicherheitsforscher denken bereits in C — Audit-Tools und Fuzzer in C für C-Contracts zu schreiben ist natürlich
- Die Plugin-API ermöglicht benutzerdefinierte Gas-Analyse- und Schwachstellenerkennungspasses zur Kompilierzeit

---

## 5. Solana-eBPF-Backend

NeverC wird die Kompilierung von C-Quellcode in Solanas eBPF-Bytecode unterstützen — On-Chain-Programmentwicklung in C.

### Ziele

- **eBPF-Target** — `sbf` (Solana BPF) Target-Triple (z. B. `neverc --target=sbf-solana hello.c -o program.so`)
- **Solana-Laufzeit-Bindings** — eingebaute Header für Solana-Systemaufrufe: `sol_invoke_signed`, `sol_log`, `sol_memcpy`, Kontoinformationsstrukturen
- **Kontomodell** — C-Struct-Overlays für Solana-Kontodaten mit automatischer Serialisierung/Deserialisierung
- **CPI (Cross-Program Invocation)** — typsichere Wrapper zum Aufrufen anderer On-Chain-Programme
- **PDA (Program Derived Address)** — eingebaute Funktionen zur PDA-Ableitung und -Verifizierung
- **Rechenbudget-Bewusstsein** — Compiler-Warnungen bei Überschreitung der geschätzten Recheneinheiten
- **Anchor-Kompatibilität** — optionale IDL-Generierung für Interoperabilität mit Anchor-basierten Frontends

### Warum C für Solana?

- Solanas Laufzeit führt bereits eBPF aus — C ist die natürlichste Quellsprache für BPF-Targets
- Bestehende C-basierte BPF-Toolchains (clang + solana-bpf) erfordern komplexes Setup; NeverC bündelt alles in einer einzigen Binärdatei
- Leistungskritische Programme profitieren von Cs Zero-Overhead-Abstraktion und NeverCs Optimierungspasses
- Die Shellcode-Kompilierungserfahrung (positionsunabhängig, minimale Laufzeit) lässt sich direkt auf On-Chain-Programmeinschränkungen übertragen

---

## Zeitplan

Diese Funktionen befinden sich in der Forschungs- und Designphase. Konkrete Veröffentlichungstermine stehen nicht fest. Der Fortschritt wird in diesem Dokument aktualisiert und auf der Projektveröffentlichungsseite bekanntgegeben.

| Funktion | Status |
|----------|--------|
| Standardbibliothek (`std`) | Forschung / Design |
| UI-Komponentenbibliothek (`neverc-ui`) | Forschung / Design |
| IDE & Sprachwerkzeuge (`neverc-ide`) | Forschung / Design |
| EVM-Smart-Contract-Backend | Forschung / Design |
| Solana-eBPF-Backend | Forschung / Design |
