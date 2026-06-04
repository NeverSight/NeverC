**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Indice della documentazione](../README.it.md)

# Roadmap di NeverC

Questo documento delinea le principali direzioni pianificate per il progetto NeverC oltre l'attuale compilatore shellcode e i runtime integrati.

---

## 1. Libreria standard (`std`)

NeverC fornirà una libreria standard completa ispirata a quella di Go — pacchetti pronti all'uso che coprono le esigenze comuni della programmazione di sistema senza dipendenze esterne.

### Pacchetti pianificati

| Pacchetto | Descrizione |
|-----------|-------------|
| `fmt` | I/O formattato (famiglia printf + estensioni type-safe) |
| `os` | Interazione con l'OS: variabili d'ambiente, gestione processi, permessi file |
| `io` | Interfacce Reader/Writer, I/O bufferizzato, utilità pipe |
| `fs` | Operazioni sul filesystem: walk, glob, file temporanei, scrittura atomica |
| `net` | Socket TCP/UDP, risoluzione DNS, client/server HTTP |
| `net/http` | Client e server HTTP/1.1 e HTTP/2 |
| `crypto` | Hashing (SHA-256, SHA-512, BLAKE3), HMAC, AES, ChaCha20, RSA, Ed25519 |
| `encoding` | JSON, Base64, Hex, CSV, binario (little/big endian) |
| `sync` | Mutex, RWLock, WaitGroup, Once, operazioni atomiche |
| `time` | Orologio monotono/a muro, durata, timer, formattazione |
| `bytes` | Manipolazione di slice di byte, buffer |
| `math` | Costanti, funzioni elementari, generazione numeri casuali |
| `sort` | Ordinamento e ricerca generici |
| `container` | Lista concatenata, heap, buffer circolare |
| `log` | Logging strutturato con livelli |
| `flag` | Parsing dei flag da riga di comando |
| `path` | Manipolazione percorsi (POSIX e Windows) |
| `regexp` | Matching espressioni regolari (sintassi RE2) |
| `compress` | gzip, zlib, zstd, lz4 |
| `hash` | CRC32, CRC64, FNV, xxHash |
| `unicode` | Tabelle Unicode, case folding, conversione UTF-8/UTF-16 |

### Principi di progettazione

- **C23 puro** — ogni pacchetto compila come NeverC/C23 standard; nessun C++ nascosto né assembler specifico per piattaforma
- **Zero dipendenze esterne** — la libreria standard è incorporata come bitcode LLVM nel compilatore, come i built-in `string` e `mimalloc` esistenti
- **Multipiattaforma** — tutti i pacchetti funzionano su macOS, Linux e Windows (x86_64 / AArch64)
- **Compatibile con shellcode** — i pacchetti utili in modalità freestanding (es.: `crypto`, `encoding`, `bytes`) funzionano con `-fshellcode`

---

## 2. Libreria di componenti UI (`neverc-ui`)

NeverC fornirà una libreria di componenti UI multipiattaforma ispirata a Qt — con un motore di rendering frontend HTML/JS/CSS, intrinsecamente adatto alla progettazione di interfacce tramite IA.

### Obiettivi

- **Architettura basata su componenti** — finestre, pulsanti, campi di testo, liste, alberi, tabelle, menu, finestre di dialogo, schede e contenitori di layout come tipi C di prima classe
- **Renderer HTML/JS/CSS** — l'UI è renderizzata tramite un motore browser leggero integrato; gli sviluppatori scrivono la logica in C, il livello visivo usa tecnologie web standard
- **Designer visuale drag-and-drop** — un GUI builder che genera codice C compatibile NeverC, consentendo la prototipazione rapida senza scrivere manualmente il codice di layout
- **Workflow di design nativo IA** — gli LLM possono generare la logica di business C e il layout HTML/CSS in un singolo passaggio
- **Aspetto nativo** — temi adattativi per piattaforma (macOS, Windows, Linux) tramite variabili CSS e rilevamento di font/colori di sistema
- **Embedding leggero** — il renderer è fornito come runtime integrato (come `string` / `mimalloc`); nessun overhead a scala Electron
- **Sistema di eventi** — funzioni callback C per le interazioni utente (clic, input, ridimensionamento, trascinamento, tastiera, eventi personalizzati)
- **Data binding** — binding dichiarativo tra struct C e stato dell'UI; le modifiche si propagano automaticamente
- **Rendering personalizzato** — accesso diretto a canvas/WebGL per UI di gioco, visualizzazione dati o widget personalizzati

### Perché HTML/CSS per una libreria UI C?

- Ogni modello IA conosce già HTML/CSS — generare codice UI non richiede addestramento specializzato
- Le tecnologie web sono il sistema di layout più collaudato; nessun bisogno di reinventare flexbox, grid o il rendering del testo
- Gli strumenti di ricerca sulla sicurezza (dashboard, viewer esadecimali, ispettori di pacchetti) beneficiano di interfacce ricche senza imparare un'API widget proprietaria
- Il designer visuale esporta template HTML che funzionano sia nell'app NeverC che in un browser standalone

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


## 4. Backend EVM per smart contract

NeverC supporterà la compilazione di codice sorgente C in bytecode EVM (Ethereum Virtual Machine) — consentendo agli sviluppatori di scrivere smart contract in C invece di Solidity.

### Obiettivi

- **Nuovo backend LLVM** — target triple `evm` (es.: `neverc --target=evm hello.c -o contract.bin`)
- **Compatibilità ABI** — generazione di descrittori ABI compatibili Solidity per interagire con gli strumenti Ethereum (Hardhat, Foundry, ethers.js)
- **Layout di archiviazione** — mappatura di struct C su slot di archiviazione EVM con layout deterministico
- **Primitive EVM integrate** — `msg.sender`, `msg.value`, `block.number`, `tx.origin` come variabili integrate o intrinsics
- **Modificatori payable / view / pure** — attributi di funzione mappati alla semantica di visibilità di Solidity
- **Emissione di eventi** — generazione di opcode `LOG0`–`LOG4` da chiamate a funzioni annotate
- **Ottimizzazione del gas** — pass IR che minimizzano il costo del gas (scheduling dello stack, constant folding, eliminazione di storage morto)
- **revert / require** — primitive di gestione errori con messaggi personalizzati

### Perché C per EVM?

- La sintassi di Solidity è familiare per gli sviluppatori JavaScript ma estranea ai programmatori di sistema; C è universale
- La pipeline di ottimizzazione IR esistente di NeverC può produrre bytecode più compatto di `solc` in molti casi
- I ricercatori di sicurezza già pensano in C — scrivere strumenti di audit e fuzzer in C per contratti C è naturale
- L'API dei plugin permette pass personalizzati di analisi del gas e rilevamento vulnerabilità a tempo di compilazione

---

## 5. Backend Solana eBPF

NeverC supporterà la compilazione di codice sorgente C in bytecode eBPF di Solana — abilitando lo sviluppo di programmi on-chain in C.

### Obiettivi

- **Target eBPF** — target triple `sbf` (Solana BPF) (es.: `neverc --target=sbf-solana hello.c -o program.so`)
- **Binding runtime Solana** — header integrati per le chiamate di sistema Solana: `sol_invoke_signed`, `sol_log`, `sol_memcpy`, struct di informazioni account
- **Modello di account** — overlay di struct C sui dati degli account Solana con serializzazione/deserializzazione automatica
- **CPI (Cross-Program Invocation)** — wrapper type-safe per chiamare altri programmi on-chain
- **PDA (Program Derived Address)** — funzioni integrate per derivazione e verifica PDA
- **Consapevolezza del budget di calcolo** — avvisi del compilatore quando le unità di calcolo stimate superano i limiti del programma
- **Compatibilità Anchor** — generazione IDL opzionale per interoperabilità con frontend basati su Anchor

### Perché C per Solana?

- Il runtime di Solana esegue già eBPF — C è il linguaggio sorgente più naturale per target BPF
- Le toolchain C-BPF esistenti (clang + solana-bpf) richiedono configurazione complessa; NeverC raggruppa tutto in un singolo binario
- I programmi critici per le prestazioni beneficiano dell'astrazione a zero overhead di C e dei pass di ottimizzazione di NeverC
- L'esperienza di compilazione shellcode (indipendente dalla posizione, runtime minimo) si applica direttamente ai vincoli dei programmi on-chain

---

## Tempistica

Queste funzionalità sono in fase di ricerca e progettazione. Non sono impegnate date di rilascio specifiche. I progressi saranno aggiornati in questo documento e annunciati sulla pagina dei rilasci del progetto.

| Funzionalità | Stato |
|-------------|-------|
| Libreria standard (`std`) | Ricerca / Progettazione |
| Libreria di componenti UI (`neverc-ui`) | Ricerca / Progettazione |
| IDE e strumenti linguistici (`neverc-ide`) | Ricerca / Progettazione |
| Backend EVM per smart contract | Ricerca / Progettazione |
| Backend Solana eBPF | Ricerca / Progettazione |
