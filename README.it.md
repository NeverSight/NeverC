**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**Compilatore C23 orientato alla ricerca sulla sicurezza, costruito su LLVM**

Linker integrato · Pipeline shellcode · Tipo `string` integrato

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#funzionalità)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#cross-compilazione-verso-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#funzionalità)

[Documentazione](docs/README.it.md) · [Guida shellcode](docs/shellcode-compiler/README.it.md) · [String integrato](docs/builtin-string/README.it.md)

</div>

---

> **Nota:** GitHub mostra sempre `README.md` (inglese) come home del repository (nessuna rilevazione automatica della lingua). Usa i link lingua sopra; nella [documentazione](docs/README.it.md) e la [guida shellcode](docs/shellcode-compiler/README.it.md) mantieni la stessa lingua tramite barra lingua e breadcrumb.

## Panoramica

NeverC compila C standard in binari ospitati, eseguibili freestanding e shellcode indipendente dalla posizione — tutto da un'unica toolchain. Supporta **x86_64** e **AArch64** (solo little-endian).

## Funzionalità

- **[Compilatore shellcode](docs/shellcode-compiler/README.it.md)** — pipeline IR/MIR multistadio, estrazione multipiattaforma, risoluzione import/syscall, modalità kernel, audit byte vietati, architettura a plugin
- **Linker integrato** — COFF, ELF e Mach-O in un solo binario; nessun `ld` o `link.exe` esterno
- **Cross-compilazione** — PE Windows da macOS/Linux con SDK MSVC incluso
- **[Tipo `string` integrato](docs/builtin-string/README.it.md)** — string a semantica di valore con sintassi a metodi puntati, gestione automatica della memoria e supporto UTF-8 nativo
- **Build LLVM snella** — solo backend x86_64 / AArch64; percorsi C++/ObjC/OpenMP rimossi

## Esempio rapido

```c
#include <unistd.h>

int main(void) {
    string msg = "Hello " + "NeverC!";
    write(1, msg.c_str(), msg.len);
    return 0;
}
```

```bash
# Passare sempre -target: sceglie l'ABI OS/arch di output, non la macchina host.

# macOS arm64
neverc -fshellcode -target arm64-apple-macos -mshellcode-syscall hello.c -o hello.bin

# Linux x86_64 — stesso sorgente
neverc -fshellcode -target x86_64-linux-gnu -mshellcode-syscall hello.c -o hello.bin

# Windows x86_64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin
```

Vedi l'**[indice della documentazione](docs/README.it.md)** per design dettagliato, matrice piattaforme, riferimento CLI ed esempi.

## Compilazione

### Requisiti

- CMake 3.20+
- Ninja
- Compilatore host C++17 (GCC, Clang o MSVC)

### Configurazione

```bash
cmake -B build-neverc -G Ninja \
  -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  llvm
```

### Build

```bash
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` viene rilevato e abilitato automaticamente se presente.

### Test

```bash
cmake --build build-neverc --target neverc-tests
ctest --test-dir build-neverc --output-on-failure
```

### Verifica

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Cross-compilazione verso Windows

Dopo aver posizionato uno splat SDK [xwin](https://github.com/Jake-Shadle/xwin) in `build-neverc/sdk/msvc/`:

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -o hello.exe hello.c -lkernel32
```

Per shellcode Windows (`-fshellcode`, risoluzione PEB, ecc.), vedi la [documentazione del compilatore shellcode](docs/shellcode-compiler/README.it.md).

## Licenza

[AGPL-3.0](LICENSE)

I componenti LLVM mantengono la licenza [Apache-2.0 WITH LLVM-exception](llvm/LICENSE.TXT).
