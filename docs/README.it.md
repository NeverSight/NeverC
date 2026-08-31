**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Progetto NeverC](i18n/README.it.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (dyncode README and breadcrumbs).

# Documentazione NeverC

Note di progettazione, riferimento API e guide per ogni sottosistema NeverC.

---

## Compilatore dyncode

La pipeline di compilazione dyncode è il focus principale di ricerca di NeverC. Architettura, opzioni CLI, matrice piattaforme ed esempi:

**[Compilatore dyncode →](dyncode-compiler/README.it.md)**

| Documento | Descrizione |
|-----------|-------------|
| [README](dyncode-compiler/README.it.md) | Panoramica, avvio rapido, target supportati |
| [Pipeline & PIC](dyncode-compiler/pipeline-and-pic/README.it.md) | Design IR → oggetto → estrazione |
| [IR Pass Design](dyncode-compiler/ir-pass-design/README.it.md) | Motivazione di ogni pass IR |
| [MIR Pass Design](dyncode-compiler/mir-pass-design/README.it.md) | Pass MIR backend |
| [Kernel-Mode DynCode](dyncode-compiler/kernel-mode-dyncode/README.it.md) | Compilazione Ring-0 |
| [Cross-Platform Architecture](dyncode-compiler/cross-platform-architecture/README.it.md) | `TargetDesc` ed estrattori |
| [Platform Extension Guide](dyncode-compiler/platform-extension-guide/README.it.md) | Aggiungere piattaforma |
| [ARM64 Assembly Tutorial](dyncode-compiler/arm64-assembly-tutorial/README.it.md) | Istruzioni ARM64 dalla prospettiva dyncode |
| [Roadmap](dyncode-compiler/roadmap/README.it.md) | Lavoro pianificato |
| [Progress](dyncode-compiler/progress/README.it.md) | Stato implementazione |

---

## L'estensione file `.nc`

NeverC riconosce `.nc` come estensione nativa per i file sorgente. Con `.nc`, tutte le estensioni del linguaggio NeverC (`-fneverc-types`, `-fbuiltin-string`) vengono abilitate automaticamente — nessun flag aggiuntivo richiesto.

**[Estensione `.nc` →](nc-extension/README.it.md)**

---

## Runtime Integrati

NeverC estende il C standard con runtime integrati come bitcode LLVM. Ciascuno è controllato da un flag `-fbuiltin-<name>`. I file `.nc` abilitano `string` automaticamente.

**[Sistema Runtime Integrato →](builtins/README.it.md)**

| Integrato | Flag | Descrizione |
|-----------|------|-------------|
| [String integrato](builtins/string/README.it.md) | `-fbuiltin-string` | Tipo `string` a semantica di valore, metodi con punto, gestione automatica della memoria, UTF-8 nativo |
| [mimalloc integrato](builtins/mimalloc/README.it.md) | `-fbuiltin-mimalloc` | Sostituzione trasparente allocatore `mimalloc` ad alte prestazioni `malloc`/`free`/`calloc`/`realloc` |
| [Crittografia stringhe (xorstr)](builtins/xorstr/README.it.md) | `-fencrypt-call-strings` | Crittografia per istanza, sigillatura tardiva obbligatoria, espansione per call site e pulizia volatile dello stack |
| [Hash di stringhe (strhash)](builtins/strhash/README.it.md) | `-fstrhash-algo` / `-fstrhash-fold` | Hash di stringhe a tempo di compilazione, stesso algoritmo a runtime, fold IR opzionale |

---

## API Plugin

NeverC apre l'intera toolchain attraverso una ABI C pura. Un plugin è un modulo condiviso (`.dll` / `.so` / `.dylib`) che si aggancia a una qualsiasi delle 130 fasi di compilazione con nome — dall'analisi della riga di comando fino all'immagine collegata finale — come osservatore, come intercettore o come provider sostitutivo. L'SDK è di soli header: nessun header LLVM e nessun collegamento al compilatore.

**[API Plugin →](plugin-api/README.it.md)**

| Documento | Descrizione |
|-----------|-------------|
| [README](plugin-api/README.it.md) | Punto di ingresso, fasi, negoziazione delle interfacce, registrazione, regole ABI |
| [Plugin Python](plugin-api/python.it.md) | Python embedded opzionale, ciclo di vita, opzioni, observer read-only, diagnostica e limiti |
| [API Driver](plugin-api/driver.it.md) | Riga di comando, scelta della toolchain, grafo delle azioni, grafo dei job |
| [API Source e I/O](plugin-api/source.it.md) | Provider VFS, posizioni sorgente, buffer, sink di output, dipendenze |
| [API Preprocessore](plugin-api/prep.it.md) | Token, macro, pragma, inclusioni, interrogazioni sulle funzionalità, 39 tipi di evento |
| [API AST e semantica](plugin-api/ast-sema.it.md) | Estensione del parser, mutazione dell'AST, ricerca dei nomi, tipi, costanti |
| [API IR](plugin-api/ir.it.md) | Lettura dell'IR LLVM, costruzione transazionale, analisi, pass, provider |
| [API MIR](plugin-api/mir.it.md) | Funzioni macchina, registri, stack frame, pass e analisi MIR |
| [Target, MC, assembly, oggetto](plugin-api/target-mc-object.it.md) | Registrazione di target, convenzioni di chiamata, codifica MC, grafi oggetto |
| [API Link e LTO](plugin-api/link-lto.it.md) | Grafo di collegamento, risoluzione dei simboli, GC/ICF, provider di linker e LTO |
| [API DynCode](plugin-api/dyncode.it.md) | Immagini piatte indipendenti dalla posizione, abbassamento degli import, codifica del set di caratteri |
| [Convenzioni di chiamata personalizzate](plugin-api/custom-callconv/README.it.md) | Plugin di convenzione di chiamata guidati dai dati |

---

## Roadmap

Principali direzioni pianificate del progetto NeverC: libreria standard, backend EVM per smart contract, backend Solana eBPF.

**[Roadmap →](roadmap/README.it.md)**

| Funzionalità | Descrizione |
|-------------|-------------|
| Libreria standard (`std`) | Pacchetti in stile Go: `fmt`, `os`, `io`, `net`, `crypto`, `encoding`, `sync` e altro |
| Suite di plugin di offuscamento (`neverc-obfuscation`) | VM, MBA, appiattimento flusso di controllo, motore polimorfico, anti-manomissione — plugin di prima parte |
| Libreria di componenti UI (`neverc-ui`) | UI multipiattaforma tipo Qt, renderer HTML/JS/CSS, designer drag-and-drop, workflow nativo IA |
| IDE e strumenti linguistici (`neverc-ide`) | Estensione VSCode + IDE autonomo per file `.nc`, IntelliSense, debug, visualizzazione pipeline dyncode |
| Smart contract EVM | Compilare C in bytecode EVM — scrivere contratti in C invece di Solidity |
| Solana eBPF | Compilare C in bytecode eBPF di Solana — sviluppo di programmi on-chain in C |

---

## Strumenti CLI

Comandi utente oltre a una singola compilazione.

| Documento | Descrizione |
|-----------|-------------|
| [`neverc run`](run/README.it.md) | Compila, esegue localmente ed elimina un binario temporaneo (stile `go run`) |
| [`neverc update`](update/README.it.md) | Aggiornare o declassare un'installazione release (compilatore + runtime installati a un tag) |
| [`neverc runtime`](runtime/README.it.md) | Installare, elencare, aggiornare o rimuovere sysroot di cross-compilazione |
| [`neverc build` / `neverc make`](build/README.it.md) | Driver compatibile GNU Make per Makefile di esempi e progetti |
| [Binari di rilascio e `--strip`](release-builds/README.it.md) | Rimuove simboli non necessari e debug sorgente, con rinomina strutturale dei simboli `.ko` consapevole del kernel (non è hash né encryption) |

---

## Target di sicurezza Windows

| Documento | Descrizione |
|-----------|-------------|
| [DLL di enclave VBS](vbs-enclave/README.it.md) | Collegare, convalidare, elaborare, firmare e caricare immagini di enclave VBS compatibili con Microsoft |

---

## Sviluppo locale

Compilare NeverC dal codice sorgente e configurare l'ambiente di sviluppo locale, inclusa la configurazione del PATH.

**[Sviluppo locale →](local-dev/README.it.md)**

---

## Esempi

Esempi compilabili che dimostrano le capacità di cross-compilazione di NeverC. Tutti compilano da macOS / Linux.

**[Esempi →](examples/README.it.md)**
