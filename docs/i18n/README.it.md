**Lingue**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../assets/neverc-logo-dark.svg">
  <img src="../assets/neverc-logo-light.svg" width="72" alt="NeverC">
</picture>

# NeverC

**Il compilatore C23 AI-friendly per la ricerca sulla sicurezza, costruito su LLVM**

Linker integrato · Pipeline dyncode · Runtime integrati (`string` · `mimalloc` · `xorstr` · `strhash`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#funzionalità)
![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#funzionalità)

[Documentazione](../README.it.md) · [Guida dyncode](../dyncode-compiler/README.it.md) · [Runtime integrati](../builtins/README.it.md) · [API Plugin](../plugin-api/README.it.md) · [Roadmap](../roadmap/README.it.md)

</div>

---

> **Nota:** GitHub mostra sempre `README.md` (inglese) come home del repository (nessuna rilevazione automatica della lingua). Usa i link lingua sopra; nella [documentazione](../README.it.md) e la [guida dyncode](../dyncode-compiler/README.it.md) mantieni la stessa lingua tramite barra lingua e breadcrumb.

## Panoramica

NeverC compila C standard in binari ospitati, eseguibili freestanding e dyncode indipendente dalla posizione — tutto da un'unica toolchain. Supporta **x86_64** e **AArch64** (solo little-endian). Le versioni future aggiungeranno **EVM** (smart contract Ethereum) e **Solana eBPF** (programmi on-chain) come target di compilazione.

## Perché NeverC?

C è già il linguaggio di sistema più semplice. NeverC lo rende ancora più semplice:

- **Puro C23, nient'altro** — Nessun template, nessun RAII, nessun overloading di operatori, nessun flusso di controllo nascosto. Ciò che leggi è ciò che viene eseguito.
- **`string` integrato** — Tipo stringa a semantica di valore con `+`, `==`, `.starts_with()` e rilascio automatico — senza C++.
- **Nessuna eccezione** — La gestione degli errori resta esplicita. Nessuno stack unwinding, nessuna sorpresa sulle prestazioni.
- **Singolo binario** — Compilatore + linker + runtime in un unico eseguibile. Zero dipendenze esterne.
- **Compatibile con LLM** — Grammatica minimale e semantica deterministica: il codice NeverC generato dall'IA compila correttamente più spesso delle alternative C++.
- **Vera cross-compilazione** — Compilare Windows PE, Linux ELF, macOS Mach-O, Android ELF e dyncode da macOS o Linux — nessuna VM, nessun dual boot, nessuna ricerca di SDK. Gli SDK di piattaforma sono integrati nel compilatore.
- **Estensibile senza frizioni** — Un singolo header C, 130 fasi di compilazione con nome, e hai un [plugin del compilatore](../plugin-api/README.it.md) capace di intervenire in qualsiasi fase — dall'ottimizzazione IR all'output binario finale — senza conoscere LLVM.
- **Ricerca sulla sicurezza integrata** — Compilazione dyncode, cifratura stringhe a tempo di compilazione e generazione PE multipiattaforma sono nativamente integrati nel compilatore — non aggiunte posticce con script esterni.

## Funzionalità

- **[Compilatore dyncode](../dyncode-compiler/README.it.md)** — pipeline IR/MIR multistadio, estrazione multipiattaforma, risoluzione import/syscall, modalità kernel, audit byte vietati, architettura a plugin
- **Linker integrato** — COFF, ELF e Mach-O in un solo binario; nessun `ld` o `link.exe` esterno
- **[Stripping di rilascio](../release-builds/README.it.md)** — `--strip` / `-s` integrato rimuove simboli non necessari a runtime e debug sorgente dalle immagini ELF, Mach-O e PE/COFF finali
- **Cross-compilazione** — Windows PE, Linux ELF, macOS Mach-O e Android ELF da qualsiasi host con SDK di piattaforma integrati
- **[Runtime integrati](../builtins/README.it.md)** — runtime LLVM bitcode integrati nel compilatore: [`string`](../builtins/string/README.it.md) (stringa a semantica di valore, gestione automatica della memoria), [`mimalloc`](../builtins/mimalloc/README.it.md) (sostituzione trasparente allocatore ad alte prestazioni, attiva per impostazione predefinita fuori dai target kernel e freestanding), [`xorstr`](../builtins/xorstr/README.it.md) (cifratura di stringhe a tempo di compilazione con decifratura anti-firma) e [`strhash`](../builtins/strhash/README.it.md) (hash di stringhe a tempo di compilazione con lo stesso algoritmo a runtime)
- **[API Plugin](../plugin-api/README.it.md)** — ABI C pura per plugin fuori dall'albero; SDK a singolo header, zero dipendenze LLVM/CRT, che copre le fasi di driver, preprocessore, AST, IR, MIR, MC, oggetto, collegamento, LTO e dyncode
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

> **Nota:** Il tipo **`string`** integrato richiede **`-fbuiltin-string`** per i file `.c`. Viene abilitato automaticamente per i [**file `.nc`**](../nc-extension/README.it.md) e in modalità **`-fdyncode`**.

```bash
# macOS arm64 / x86_64
neverc -fdyncode -target arm64-apple-macos hello.c -o hello.bin
neverc -fdyncode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fdyncode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fdyncode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fdyncode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fdyncode -target aarch64-linux-android hello.c -o hello.bin
neverc -fdyncode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fdyncode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fdyncode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

Vedi l'**[indice della documentazione](../README.it.md)** per design dettagliato, matrice piattaforme, riferimento CLI ed esempi. Esempi completi compilabili: **[examples](../examples/README.it.md)**.

## Installazione

Su **Linux x64/arm64** e **macOS arm64**, installate l’ultima release con un solo comando:

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/HEAD/install.sh | sh
```

L’installer scarica l’archivio release per la vostra piattaforma, lo verifica con `SHA256SUMS`, installa in `~/.neverc` e antepone `~/.neverc/bin` al `PATH` della shell.

Per fissare una versione specifica:

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/v3389.1.2/install.sh | NEVERC_VERSION=v3389.1.2 sh
```

Verificare l’installazione:

```bash
neverc --version
neverc hello.c -o hello -fbuiltin-string
```

### `neverc run`

Compila in un eseguibile temporaneo, lo esegue sull'**host locale** e lo elimina — simile a `go run`. Per conservare il binario usa `neverc ... -o out`.

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -O2 main.c helper.nc -- --verbose two words
neverc run hello.c -O1 -- program-arg
```

| Argomento | Comportamento |
|-----------|---------------|
| Divisione predefinita | Flag compilatore prima del primo `.c`/`.nc`; sorgenti consecutive compilate insieme; il resto va a `main` |
| `--` esplicito | Prima di `--` al compilatore, dopo al programma (flag di link dopo le sorgenti) |
| Directory di lavoro | Esegue nella directory corrente — percorsi relativi come un binario normale |
| Ambiente e I/O | Eredita l'ambiente; stdin/stdout/stderr collegati al processo temporaneo |
| Codice di uscita | Restituisce il codice del programma; se la compilazione fallisce, quello del compilatore senza avviare il programma |
| Artefatto | Salvato in `neverc-run-*` e rimosso dopo l'esecuzione |

I flag di cross-compilazione possono compilare, ma il binario temporaneo viene sempre eseguito sull'host. Regole, esempi e limiti: **[`neverc run` →](../run/README.it.md)**.

I pacchetti **Windows x64/arm64** sono su [GitHub Releases](https://github.com/NeverSight/NeverC/releases) per download manuale. Il binario macOS arm64 è firmato con Apple Developer ID e notarizzato.

Variabili d’ambiente opzionali:

| Variabile | Scopo |
|-----------|-------|
| `NEVERC_INSTALL_DIR` | Prefisso di installazione (predefinito: `~/.neverc`) |
| `NEVERC_VERSION` | Tag release, es. `v3389.1.2` (predefinito: latest) |
| `NEVERC_NO_MODIFY_PATH=1` | Non modificare il profilo shell |

I sysroot per cross-compilazione (Windows SDK, sysroot Linux, ecc.) si installano on demand dopo che il compilatore è nel `PATH`:

```bash
neverc runtime install all
neverc runtime install windows-x64
neverc runtime list
```

Un’installazione release può aggiornare il compilatore e i runtime di cross-compilazione già installati come un’unica unità versionata:

```bash
neverc update                 # release completa più recente
neverc update v3389.1.2       # versione esatta, incluso il downgrade
```

`neverc upgrade` è un alias. NeverC risolve un solo tag release concreto e reinstalla soltanto
i runtime già presenti, fissandoli tutti alla versione di destinazione del compilatore. Prima di
modificare i file attivi, tutti gli archivi necessari vengono scaricati, verificati con SHA256,
estratti e validati. Un errore di preparazione o verifica lascia invariata l’installazione corrente,
mentre un errore di commit avvia il rollback automatico. Se una release del runtime è difettosa,
`neverc update <versione precedente>` riporta indietro insieme compilatore e runtime installati.

Riferimento completo: [`neverc runtime` →](../runtime/README.it.md) · [`neverc update` →](../update/README.it.md) · [`neverc build` / `make` →](../build/README.it.md).

## Compilazione dal sorgente

Requisiti, comandi di compilazione, cross-compilazione verso Windows, configurazione PATH e passaggio tra release installata e build in-tree — consultate **[Sviluppo locale](../local-dev/README.it.md)**.

## Contribuire

NeverC è **solo C per design** (C23). Frontend C++, Objective-C, CUDA e linguaggi simili
sono fuori scope; le pull request che li aggiungono verranno chiuse. Per una toolchain
LLVM orientata al C++, considerare
[llvm-msvc](https://github.com/backengineering/llvm-msvc).

Per modifiche ampie a linguaggio, ABI o runtime, aprire prima una issue e discutere lo
scope prima di inviare una pull request.

Il branch di sviluppo predefinito è **`dev`**. Clonare il repository, eseguire checkout di `dev` prima di iniziare e aprire pull request verso `dev`.

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## Licenza

[AGPL-3.0](../../LICENSE)

I componenti LLVM mantengono la licenza [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT).
