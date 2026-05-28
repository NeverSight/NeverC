**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Dokumentation](../README.de.md)

# NeverC Integriertes Laufzeitsystem

NeverC erweitert Standard-C mit optionalen integrierten Laufzeiten, die als LLVM-Bitcode direkt in die Compiler-Binärdatei eingebettet sind. Nach Aktivierung über Compiler-Flags wird die entsprechende Laufzeit zur Kompilierzeit in das IR des Benutzers zusammengeführt — keine externen Header, Bibliotheken oder Link-Abhängigkeiten erforderlich.

## Verfügbare integrierte Funktionen

| Integriert | Flag | Standard | Beschreibung |
|------------|------|----------|-------------|
| [**`string`**](string/README.de.md) | `-fbuiltin-string` | Aus | Wertesemantischer String-Typ mit Punkt-Aufruf-Methoden, automatischer Speicherverwaltung und nativem UTF-8 |
| [**`mimalloc`**](mimalloc/README.de.md) | `-fbuiltin-mimalloc` | **An** | Hochleistungs-Speicherallokator, der `malloc`/`free`/`calloc`/`realloc` transparent ersetzt |
| [**`xorstr`**](xorstr/README.de.md) | `-fencrypt-call-strings` | Aus | Kompilierzeit-Zeichenkettenverschlüsselung, Stack-allozierte XOR-Entschlüsselung, Anti-Signatur-Algorithmus |

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## Architekturübersicht

`string` und `mimalloc` teilen die gleiche Vier-Schichten-Architektur:

1. **Sprachoptionen und Treiber-Flags** — `LangOption` definiert in `LangOptions.def`
2. **Foundation API** — bietet `getEmbeddedBitcode()` und `isSupported()`
3. **CMake Bootstrap-Infrastruktur** — Zweistufige Bitcode-Generierung
4. **IR-Merge-Pass** — Bitcode-Zusammenführung in das Benutzermodul bei `PipelineStartEP`

Beispiel für `LangOptions.def`-Registrierung:

```cpp
LANGOPT(BuiltinString,      1, 0, "inject NeverC builtin string prelude")
LANGOPT(BuiltinMimalloc,    1, 1, "inject mimalloc allocator override")
LANGOPT(EncryptCallStrings, 1, 0, "auto-encrypt string literals in call arguments")
VALUE_LANGOPT(EncryptCallStringsMaxLen, 32, 1024,
              "maximum string length for auto-encryption (0 = no limit)")
```

> **Hinweis:** `xorstr` verwendet nicht das Embedded-Bitcode-Modell. Das explizite Makro [`NC_XORSTR(s)` / `NEVERC_XORSTR(s)`](xorstr/README.de.md) wird von der Sema-Schicht (Handler `semaBuiltinNeverCXorstr` in `SemaChecking.cpp`) abgesenkt, und die optionale `-fencrypt-call-strings`-Autoverschlüsselung erfolgt durch die IR-Transform-Pass `EncryptCallStringsPass`, registriert an **OptimizerLast** (mit `XorStrCleanupPass`, das Klartext-Stack-Puffer per `volatile memset` nullt). Siehe [xorstr-Dokumentation](xorstr/README.de.md) für Details.

---

## Designunterschiede zwischen integrierten Funktionen

| Aspekt | `string` | `mimalloc` |
|--------|----------|------------|
| **Merge-Strategie** | On-Demand (BFS-Aufrufgraph) | Gesamtarchiv (alle Symbole) |
| **Plattform-Bitcode** | Einzeln (architekturunabhängig) | Pro OS (Linux / Darwin / Windows) |
| **Symbolbehandlung** | Alle internalisiert | Override-Eintrittspunkte bleiben extern |
| **Präprozessor-Makro** | *(keine)* | `__NEVERC_MIMALLOC__` |
| **Shellcode-Modus** | Auto-aktiviert, Arena-Umschreibung | Unterdrückt (HeapArenaPass übernimmt Heap) |
| **Optimierungsstufe** | `-O0` (Bitcode-Kompilierung) | `-O2` (leistungskritischer Allokator) |
| **DCE** | Pre-Merge-Pruning + Post-Merge-Mark-and-Sweep | Kein DCE (Gesamtarchiv-Semantik) |

---

## Sicherheitsverriegelungen

| Bedingung | Auswirkung | Grund |
|-----------|------------|-------|
| `-fno-builtin` | Unterdrückt mimalloc | Kein CRT-Override-Szenario |
| `-mkernel` | Unterdrückt mimalloc | Kein Userspace-Heap im Kernel |
| `-fshellcode-mode` | Unterdrückt mimalloc | Ersetzt durch HeapArenaPass (Arena-basiert) |
| `-ffreestanding` | Unterdrückt mimalloc | Keine libc zum Überschreiben |

Das `string`-Built-in hat eine eigene Unterdrückungslogik (Arena-Umschreibung in der Shellcode-Pipeline ersetzt Heap-Allokation).

### HeapArenaPass (Shellcode-Heap-Allokation)

Wenn `-fshellcode-mode` aktiv ist, wird `mimalloc` unterdrückt, aber `malloc`/`free`/`calloc`/`realloc`-Aufrufe werden automatisch von `HeapArenaPass` umgeschrieben (standardmäßig aktiviert). Der Pass verwendet eine hybride Strategie:

- **Kleine Allokationen (≤ 64 KB)**: Bedient aus einer stack-residenten Arena, die mit dem `string`-Built-in-Runtime geteilt wird (Bump-Allocator + Free-List-Wiederverwendung).
- **Große Allokationen (> 64 KB) oder Arena-OOM**: Fallback zum OS-Allokator:
  - **Windows**: `malloc`/`free` über PEB-Walk aus `msvcrt.dll` aufgelöst (`-mshellcode-win-peb-import`).
  - **Linux / macOS / Android**: `mmap`/`munmap` als native Systemaufrufe inlined (`-mshellcode-syscall`).
  - **Kein Import-Pass aktiviert**: Nur Arena; OOM gibt `NULL` zurück.

Steuerung über Treiber-Flags:

```bash
neverc -fshellcode test.c -o test.bin                     # HeapArenaPass AN (Standard)
neverc -fshellcode -fno-shellcode-heap-arena test.c       # HeapArenaPass AUS (ursprüngliches Verhalten)
```

---

## Präprozessor-Makros

```c
#ifdef __NEVERC_MIMALLOC__
// mimalloc ist aktiv — malloc/free werden transparent überschrieben
#endif
```

---

## Dateistruktur

```
neverc/
├── include/neverc/Foundation/Builtin/
│   ├── BuiltinString.h / BuiltinMimalloc.h
│   └── Builtins.def                      # __builtin_neverc_xorstr
├── include/neverc/Transforms/XorStr/
│   └── EncryptCallStringsPass.h / XorStrCleanupPass.h
├── lib/Foundation/Builtin/
│   ├── BuiltinString.cpp / BuiltinMimalloc.cpp
│   └── bin2c.py / gen_string_runtime.py / gen_mimalloc_source.py
├── lib/Headers/neverc/
│   └── xorstr.h / xorstr_impl.inc        # Makros NC_XORSTR / NEVERC_XORSTR
├── lib/Analyze/Checking/SemaChecking.cpp # semaBuiltinNeverCXorstr
├── lib/Transforms/XorStr/
│   └── EncryptCallStringsPass.cpp / XorStrCleanupPass.cpp
├── lib/Emit/Backend/
│   └── BackendUtil.cpp / StringRuntimeLinker.{h,cpp} / MimallocRuntimeLinker.{h,cpp}
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPredefinedMacros.cpp
```

---

## Neue integrierte Funktion hinzufügen

1. `LANGOPT` in `LangOptions.def` hinzufügen
2. Treiber-Flags in `Options.td.h` hinzufügen
3. Foundation API erstellen (`BuiltinFoo.h` + `.cpp`)
4. Quellgenerator erstellen
5. CMake Bootstrap-Targets hinzufügen
6. IR-Pass erstellen und bei `PipelineStartEP` registrieren
7. Präprozessor-Makro definieren
8. Sicherheitsprüfungen hinzufügen
9. Tests hinzufügen
10. Dokumentation und i18n-Übersetzungen hinzufügen
