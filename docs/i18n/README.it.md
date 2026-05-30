**Lingue**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**Il compilatore C23 AI-friendly per la ricerca sulla sicurezza, costruito su LLVM**

Linker integrato · Pipeline shellcode · Runtime integrati (`string` · `mimalloc` · `xorstr`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#funzionalità)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#cross-compilazione-verso-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#funzionalità)

[Documentazione](../README.it.md) · [Guida shellcode](../shellcode-compiler/README.it.md) · [Runtime integrati](../builtins/README.it.md) · [API Plugin](../plugin-api/README.it.md)

</div>

---

> **Nota:** GitHub mostra sempre `README.md` (inglese) come home del repository (nessuna rilevazione automatica della lingua). Usa i link lingua sopra; nella [documentazione](../README.it.md) e la [guida shellcode](../shellcode-compiler/README.it.md) mantieni la stessa lingua tramite barra lingua e breadcrumb.

## Panoramica

NeverC compila C standard in binari ospitati, eseguibili freestanding e shellcode indipendente dalla posizione — tutto da un'unica toolchain. Supporta **x86_64** e **AArch64** (solo little-endian).

## Perché NeverC?

C è già il linguaggio di sistema più semplice. NeverC lo rende ancora più semplice:

- **Puro C23, nient'altro** — Nessun template, nessun RAII, nessun overloading di operatori, nessun flusso di controllo nascosto. Ciò che leggi è ciò che viene eseguito.
- **`string` integrato** — Tipo stringa a semantica di valore con `+`, `==`, `.starts_with()` e rilascio automatico — senza C++.
- **Nessuna eccezione** — La gestione degli errori resta esplicita. Nessuno stack unwinding, nessuna sorpresa sulle prestazioni.
- **Singolo binario** — Compilatore + linker + runtime in un unico eseguibile. Zero dipendenze esterne.
- **Compatibile con LLM** — Grammatica minimale e semantica deterministica: il codice NeverC generato dall'IA compila correttamente più spesso delle alternative C++.
- **Vera cross-compilazione** — Compilare eseguibili Windows e shellcode da macOS o Linux — nessuna VM, nessun dual boot, nessuna ricerca di SDK. Il Windows SDK è integrato nel compilatore.
- **[Sistema plugin](../plugin-api/README.it.md), un solo header** — API C pura con 20+ hook point su IR, MIR, binary e linker. Scrivi un plugin in qualsiasi linguaggio e intervieni in praticamente ogni punto della pipeline di compilazione — senza header LLVM.
- **Ricerca sulla sicurezza integrata** — Compilazione shellcode, cifratura stringhe a tempo di compilazione e generazione PE multipiattaforma sono nativamente integrati nel compilatore — non aggiunte posticce con script esterni.

## Funzionalità

- **[Compilatore shellcode](../shellcode-compiler/README.it.md)** — pipeline IR/MIR multistadio, estrazione multipiattaforma, risoluzione import/syscall, modalità kernel, audit byte vietati, architettura a plugin
- **Linker integrato** — COFF, ELF e Mach-O in un solo binario; nessun `ld` o `link.exe` esterno
- **Cross-compilazione** — PE Windows da macOS/Linux con SDK MSVC incluso
- **[Runtime integrati](../builtins/README.it.md)** — runtime LLVM bitcode integrati nel compilatore: [`string`](../builtins/string/README.it.md) (stringa a semantica di valore, gestione automatica della memoria), [`mimalloc`](../builtins/mimalloc/README.it.md) (sostituzione trasparente allocatore ad alte prestazioni) e [`xorstr`](../builtins/xorstr/README.it.md) (cifratura di stringhe a tempo di compilazione con decifratura anti-firma)
- **[API Plugin](../plugin-api/README.it.md)** — ABI C pura per plugin di pass fuori dall'albero; SDK a singolo header, zero dipendenze LLVM/CRT, hook point IR, MIR, Binary e Linker
- **[Estensione `.nc`](../nc-extension/README.it.md)** — usa `.nc` per abilitare automaticamente tutte le funzionalità NeverC (`string`, tipi interi stile Rust) senza flag aggiuntivi
- **Build LLVM snella** — solo backend x86_64 / AArch64; percorsi C++/ObjC/OpenMP rimossi

## Esempio rapido

```c
#include <stdio.h>

typedef struct { string user; string pass; } creds;

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());

    // Compile-time encryption — `strings ./bin` cannot find these literals
    creds login = {.user = "admin".encrypt(), .pass = "s3cret".encrypt()};
    string paths[] = {"/api/v1".encrypt(), "/api/v2".encrypt()};

    // Zero-allocation decrypt-and-compare (plaintext never fully in memory)
    if (login.user == "admin".encrypt() && login.pass == "s3cret".encrypt()) {
        for (int i = 0; i < 2; i++)
            if (msg.starts_with(paths[i]))
                printf("route matched: %s\n", paths[i].c_str());
    }
    return 0;
}
```

> **Nota:** Il tipo **`string`** integrato richiede **`-fbuiltin-string`** per i file `.c`. Viene abilitato automaticamente per i [**file `.nc`**](../nc-extension/README.it.md) e in modalità **`-fshellcode`**.

```bash
# macOS arm64 / x86_64
neverc -fshellcode -target arm64-apple-macos hello.c -o hello.bin
neverc -fshellcode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fshellcode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fshellcode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fshellcode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fshellcode -target aarch64-linux-android hello.c -o hello.bin
neverc -fshellcode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fshellcode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

Vedi l'**[indice della documentazione](../README.it.md)** per design dettagliato, matrice piattaforme, riferimento CLI ed esempi. Esempi completi compilabili: **[examples/](../../examples/)**.

## Binari macOS precompilati

La release è firmata in ad-hoc (nessun Apple Developer ID, non notarizzata). Se l'hai scaricata tramite browser, rimuovi una sola volta l'attributo quarantine dopo l'estrazione:

```bash
xattr -dr com.apple.quarantine /path/to/extracted/install
```

## Compilazione

### Requisiti

- CMake 3.20+
- Ninja
- Compilatore host C++17 (GCC, Clang o MSVC)

### Configurazione

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
```

### Build

```bash
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` viene rilevato e abilitato automaticamente se presente.

### Test

```bash
cmake --build build-neverc --target check-neverc
```

### Verifica

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Cross-compilazione verso Windows

NeverC include un Windows SDK e WDK in `runtime/`; non è necessaria alcuna configurazione esterna.

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Per shellcode Windows (`-fshellcode`, risoluzione PEB, ecc.), vedi la [documentazione del compilatore shellcode](../shellcode-compiler/README.it.md).

## Contribuire

Il branch di sviluppo predefinito è **`dev`**. Clonare il repository, eseguire checkout di `dev` prima di iniziare e aprire pull request verso `dev`.

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## Licenza

[AGPL-3.0](../../LICENSE)

I componenti LLVM mantengono la licenza [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT).
