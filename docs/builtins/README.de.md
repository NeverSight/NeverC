**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Dokumentation](../README.de.md)

# NeverC Integriertes Laufzeitsystem

NeverC erweitert Standard-C mit optionalen integrierten Laufzeiten, die als LLVM-Bitcode direkt in die Compiler-Binärdatei eingebettet sind. Nach Aktivierung über Compiler-Flags wird die entsprechende Laufzeit zur Kompilierzeit in das IR des Benutzers zusammengeführt — keine externen Header, Bibliotheken oder Link-Abhängigkeiten erforderlich.

## Verfügbare integrierte Funktionen

| Integriert | Flag | Standard | Beschreibung |
|------------|------|----------|-------------|
| [**`string`**](string/README.de.md) | `-fbuiltin-string` | Aus | Wertesemantischer String-Typ mit Punkt-Aufruf-Methoden, automatischer Speicherverwaltung und nativem UTF-8 |
| [**`mimalloc`**](mimalloc/README.de.md) | `-fbuiltin-mimalloc` | **An** | Hochleistungs-Speicherallokator, der `malloc`/`free`/`calloc`/`realloc` transparent ersetzt |

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## Architekturübersicht

Alle integrierten Funktionen teilen die gleiche Vier-Schichten-Architektur:

1. **Sprachoptionen und Treiber-Flags** — `LangOption` definiert in `LangOptions.def`
2. **Foundation API** — bietet `getEmbeddedBitcode()` und `isSupported()`
3. **CMake Bootstrap-Infrastruktur** — Zweistufige Bitcode-Generierung
4. **IR-Merge-Pass** — Bitcode-Zusammenführung in das Benutzermodul bei `PipelineStartEP`

---

## Designunterschiede zwischen integrierten Funktionen

| Aspekt | `string` | `mimalloc` |
|--------|----------|------------|
| **Merge-Strategie** | On-Demand (BFS-Aufrufgraph) | Gesamtarchiv (alle Symbole) |
| **Plattform-Bitcode** | Einzeln (architekturunabhängig) | Pro OS (Linux / Darwin / Windows) |
| **Symbolbehandlung** | Alle internalisiert | Override-Eintrittspunkte bleiben extern |
| **Präprozessor-Makro** | *(keine)* | `__NEVERC_MIMALLOC__` |
| **Shellcode-Modus** | Auto-aktiviert, Arena-Umschreibung | Unterdrückt (kein Heap in Shellcode) |
| **Optimierungsstufe** | `-O0` (Bitcode-Kompilierung) | `-O2` (leistungskritischer Allokator) |
| **DCE** | Pre-Merge-Pruning + Post-Merge-Mark-and-Sweep | Kein DCE (Gesamtarchiv-Semantik) |

---

## Sicherheitsverriegelungen

| Bedingung | Auswirkung | Grund |
|-----------|------------|-------|
| `-fno-builtin` | Unterdrückt mimalloc | Kein CRT-Override-Szenario |
| `-mkernel` | Unterdrückt mimalloc | Kein Userspace-Heap im Kernel |
| `-fshellcode-mode` | Unterdrückt mimalloc | Kein Heap in Shellcode |
| `-ffreestanding` | Unterdrückt mimalloc | Keine libc zum Überschreiben |

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
├── lib/Foundation/Builtin/
│   ├── BuiltinString.cpp / BuiltinMimalloc.cpp
│   ├── bin2c.py / gen_string_runtime.py / gen_mimalloc_source.py
├── lib/Emit/Backend/
│   ├── BackendUtil.cpp / StringRuntimeLinker.{h,cpp} / MimallocRuntimeLinker.{h,cpp}
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPreprocessor.cpp
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
