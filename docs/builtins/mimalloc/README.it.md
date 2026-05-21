**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Sistema Runtime Integrato di NeverC](../README.it.md)

# Allocatore mimalloc Integrato

## Panoramica

NeverC può integrare [mimalloc](https://github.com/microsoft/mimalloc) — l'allocatore di memoria ad alte prestazioni di Microsoft — direttamente nei binari compilati tramite fusione di bitcode LLVM. Una volta attivato, `malloc`, `free`, `calloc` e `realloc` vengono sostituiti in modo trasparente dalle implementazioni di mimalloc al momento della compilazione.

**Attivazione:**

```bash
neverc -fbuiltin-mimalloc main.c -o main
```

---

## Utilizzo

```bash
neverc -fbuiltin-mimalloc hello.c -o hello                     # base
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main      # combinato con string
neverc -fno-builtin-mimalloc main.c -o main                    # disattivare
```

```c
#ifdef __NEVERC_MIMALLOC__
    printf("Utilizzo dell'allocatore mimalloc\n");
#endif
```

---

## Supporto Piattaforme

| Piattaforma | Triple | Stato |
|------------|--------|-------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | Supportato |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | Supportato |
| Android | `aarch64-linux-android` | Supportato |
| macOS x86_64 | `x86_64-apple-macosx` | Supportato |
| macOS AArch64 | `arm64-apple-macosx` | Supportato |
| iOS | `arm64-apple-ios` | Supportato |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | Supportato |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | Supportato |

---

## Soppressione Automatica

| Flag / Modalità | Motivo |
|----------------|--------|
| `-fno-builtin` | Nessuno scenario di override CRT |
| `-mkernel` | Nessun heap userspace nel kernel |
| `-fshellcode-mode` | Nessun heap in shellcode |
| `-ffreestanding` | Nessuna libc da sovrascrivere |

---

## Processo di Bootstrap

```bash
ninja neverc                         # Fase 1: Placeholder bitcode vuoti
ninja neverc-bootstrap-mimalloc-bc   # Fase 2: Compilare bitcode per SO
ninja neverc                         # Fase 3: Incorporare bitcode reale
```

---

## Architettura

mimalloc è incorporato come bitcode LLVM nel binario del compilatore. Durante la compilazione utente, un Module Pass fonde il bitcode nell'IR prima della pipeline di ottimizzazione. Compilato separatamente per SO (Linux `mmap`, macOS `vm_allocate`, Windows `VirtualAlloc`), selezionato tramite target triple. Semantica **archivio completo** — tutte le funzioni vengono collegate.

---

## Struttura dei File

```
neverc/
├── include/neverc/Foundation/Builtin/BuiltinMimalloc.h
├── lib/Foundation/Builtin/
│   ├── BuiltinMimalloc.cpp / gen_mimalloc_source.py / bin2c.py
├── lib/Emit/Backend/
│   ├── MimallocRuntimeLinker.{h,cpp} / BackendUtil.cpp
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPreprocessor.cpp
```

---

## Riferimento Flag del Compilatore

| Flag | Descrizione |
|------|-------------|
| `-fbuiltin-mimalloc` | Attivare l'iniezione dell'override mimalloc (attivato per default per build hosted) |
| `-fno-builtin-mimalloc` | Disattivare esplicitamente l'iniezione mimalloc |

| Macro | Valore | Quando definita |
|-------|--------|----------------|
| `__NEVERC_MIMALLOC__` | `1` | Quando `-fbuiltin-mimalloc` è attivo |
