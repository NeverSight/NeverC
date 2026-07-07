**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Index de la documentation](../README.fr.md)

# Feuille de route NeverC

Ce document présente les grandes orientations prévues pour le projet NeverC au-delà du compilateur dyncode et des runtimes intégrés actuels.

---

## 1. Bibliothèque standard (`std`)

NeverC fournira une bibliothèque standard complète inspirée de celle de Go — des paquets prêts à l'emploi couvrant les besoins courants de la programmation système, sans dépendance externe.

### Paquets prévus

| Paquet | Description |
|--------|-------------|
| `fmt` | E/S formatées (famille printf + extensions type-safe) |
| `os` | Interaction OS : variables d'environnement, gestion de processus, permissions de fichiers |
| `io` | Interfaces Reader/Writer, E/S tamponnées, utilitaires de pipe |
| `fs` | Opérations sur le système de fichiers : parcours, glob, fichiers temporaires, écriture atomique |
| `net` | Sockets TCP/UDP, résolution DNS, client/serveur HTTP |
| `net/http` | Client et serveur HTTP/1.1 et HTTP/2 |
| `crypto` | Hachage (SHA-256, SHA-512, BLAKE3), HMAC, AES, ChaCha20, RSA, Ed25519 |
| `encoding` | JSON, Base64, Hex, CSV, binaire (petit/gros boutiste) |
| `sync` | Mutex, RWLock, WaitGroup, Once, opérations atomiques |
| `time` | Horloge monotone/murale, durée, minuteurs, formatage |
| `bytes` | Manipulation de tranches d'octets, tampon |
| `math` | Constantes, fonctions élémentaires, génération de nombres aléatoires |
| `sort` | Tri et recherche génériques |
| `container` | Liste chaînée, tas, tampon circulaire |
| `log` | Journalisation structurée avec niveaux |
| `flag` | Analyse des drapeaux de ligne de commande |
| `path` | Manipulation de chemins (POSIX et Windows) |
| `regexp` | Correspondance d'expressions régulières (syntaxe RE2) |
| `compress` | gzip, zlib, zstd, lz4 |
| `hash` | CRC32, CRC64, FNV, xxHash |
| `unicode` | Tables Unicode, pliage de casse, conversion UTF-8/UTF-16 |

### Principes de conception

- **C23 pur** — chaque paquet compile en NeverC/C23 standard ; pas de C++ caché ni d'assembleur spécifique à une plateforme
- **Zéro dépendance externe** — la bibliothèque standard est embarquée en tant que bitcode LLVM dans le compilateur, comme les built-ins `string` et `mimalloc` existants
- **Multiplateforme** — tous les paquets fonctionnent sur macOS, Linux et Windows (x86_64 / AArch64)
- **Compatible dyncode** — les paquets pertinents en mode freestanding (ex. : `crypto`, `encoding`, `bytes`) fonctionnent avec `-fdyncode`

---

## 2. Obfuscation Plugin Suite (`neverc-obfuscation`)

NeverC will ship a first-party suite of code obfuscation plugins — reference implementations that demonstrate the Plugin API's full capabilities while providing production-grade code protection out of the box.

### Planned Plugins

| Plugin | Interpose Point | Description |
|--------|-----------|-------------|
| Junk Code Insertion | `RunAfterFinalMIR` | Insert semantically dead but syntactically valid instruction sequences between real basic blocks |
| Opaque Predicates | `RunBeforePreEmit` | Insert always-true/always-false branches guarded by number-theoretic invariants; adds dead paths that confuse analysis |
| Control Flow Flattening | `RunAfterStackify` | Scatter basic blocks into a switch-dispatched loop; destroys natural CFG structure for decompilers |
| Anti-Tamper | `RunPostFinalize` | Embed self-integrity checks (CRC/hash of code sections) that trigger failure on patching |
| Polymorphic Engine | `RunPostExtract` | Seed-based output variation — each compilation produces functionally equivalent but structurally different code; defeats signature-based detection |
| MBA (Mixed Boolean Arithmetic) | `RunAfterInlining` | Replace arithmetic/boolean expressions with equivalent but opaque MBA forms (e.g., `x + y` → `(x ^ y) + 2 * (x & y)` chains); resists symbolic execution |
| VM (Code Virtualization) | `RunAfterFinalIR` | Convert functions into custom bytecode executed by an embedded interpreter; defeats static disassembly and signature matching |

### Design Principles

- **Pure Plugin API** — every obfuscation ships as a `.dll` / `.so` / `.dylib` plugin; no compiler fork required
- **Composable** — plugins stack: apply MBA first, then flatten, then virtualize — each pass is independent
- **Configurable** — per-function annotations (`__attribute__((obfuscate("vm")))`) to selectively protect hot paths without whole-program overhead
- **Auditable** — each plugin logs its transformations for security review; before/after IR diff output available via `-fdyncode-dump-ir`
- **DynCode-compatible** — all plugins work in `-fdyncode` mode; generated code remains position-independent

---

## 3. Bibliothèque de composants UI (`neverc-ui`)

NeverC fournira une bibliothèque de composants UI multiplateforme inspirée de Qt — avec un moteur de rendu frontend HTML/JS/CSS, intrinsèquement adapté à la conception d'interfaces par IA.

### Objectifs

- **Architecture basée sur les composants** — fenêtres, boutons, champs de texte, listes, arbres, tableaux, menus, dialogues, onglets et conteneurs de mise en page comme types C de premier ordre
- **Moteur de rendu HTML/JS/CSS** — l'UI est rendue via un moteur de navigateur léger intégré ; les développeurs écrivent la logique en C, la couche visuelle utilise les technologies web standards
- **Concepteur visuel glisser-déposer** — un constructeur GUI compagnon qui génère du code C compatible NeverC, permettant le prototypage rapide sans écrire manuellement le code de mise en page
- **Flux de travail de conception natif IA** — les LLM peuvent générer la logique métier C et la mise en page HTML/CSS en une seule passe
- **Apparence native** — thèmes adaptatifs par plateforme (macOS, Windows, Linux) via variables CSS et détection de polices/couleurs système
- **Intégration légère** — le moteur de rendu est fourni comme runtime intégré (comme `string` / `mimalloc`) ; pas de surcharge à l'échelle d'Electron
- **Système d'événements** — fonctions de rappel C pour les interactions utilisateur (clic, saisie, redimensionnement, glissement, clavier, événements personnalisés)
- **Liaison de données** — liaison déclarative entre les structs C et l'état de l'UI ; les changements se propagent automatiquement
- **Rendu personnalisé** — accès brut au canvas/WebGL pour les UI de jeu, la visualisation de données ou les widgets personnalisés

### Pourquoi HTML/CSS pour une bibliothèque UI C ?

- Tous les modèles d'IA connaissent déjà HTML/CSS — générer du code UI ne nécessite aucune formation spécialisée
- Les technologies web sont le système de mise en page le plus éprouvé ; pas besoin de réinventer flexbox, grid ou le rendu de texte
- Les outils de recherche en sécurité (tableaux de bord, visualiseurs hexadécimaux, inspecteurs de paquets) bénéficient d'interfaces riches sans apprendre une API de widgets propriétaire
- Le concepteur visuel exporte des modèles HTML fonctionnant dans l'application NeverC et dans un navigateur autonome

---

## 4. IDE & Language Tooling (`neverc-ide`)

NeverC will provide first-class IDE support for the `.nc` language extension — a VSCode extension for immediate productivity and a standalone NeverC IDE for a fully integrated development experience.

### VSCode Extension

- **Syntax highlighting** — full `.nc` grammar with semantic token support for NeverC-specific types (`string`, `u8`–`u64`, `i8`–`i64`, `f32`, `f64`)
- **IntelliSense** — auto-completion for built-in types, dot-call methods (`.c_str()`, `.len()`, `.starts_with()`), and `#include` paths
- **Diagnostics** — real-time error and warning display from `neverc` compiler output
- **Go to definition** — jump to function, struct, and macro definitions across translation units
- **Hover documentation** — inline docs for built-in functions, compiler intrinsics, and standard library packages
- **Code actions** — quick-fix suggestions for common errors, auto-import for `std` packages
- **Debugging** — integrated LLDB/GDB debug adapter with breakpoint, step, and variable inspection support
- **DynCode mode** — syntax-aware features for `-fdyncode` pipelines: bad-byte highlighting, dyncode size display, target-specific completions
- **Plugin API integration** — plugin interpose point visualization and scaffolding

### Standalone IDE

- **Built on NeverC UI (`neverc-ui`)** — the IDE is itself a showcase of the HTML/JS/CSS component library, dogfooding the UI framework
- **Integrated terminal** — build, run, and debug without leaving the IDE
- **Visual dyncode pipeline** — graphical view of the IR → MIR → extraction pipeline with pass-by-pass output inspection
- **Project templates** — one-click scaffolding for hosted binaries, dyncode, EVM contracts, and Solana programs
- **AI-assisted coding** — built-in LLM integration that understands NeverC semantics, generates `.nc` code, and explains compiler diagnostics
- **Cross-compilation dashboard** — visual target selector with platform matrix and build status

### Why Both VSCode and Standalone?

- VSCode captures the majority of developers who already live in that ecosystem
- The standalone IDE provides a deeper, purpose-built experience for security researchers who want dyncode pipeline visualization and integrated binary analysis
- Both share the same language server backend — improvements benefit both simultaneously

---


## 5. Backend EVM pour contrats intelligents

NeverC supportera la compilation de code source C en bytecode EVM (Ethereum Virtual Machine) — permettant aux développeurs d'écrire des contrats intelligents en C au lieu de Solidity.

### Objectifs

- **Nouveau backend LLVM** — triple cible `evm` (ex. : `neverc --target=evm hello.c -o contract.bin`)
- **Compatibilité ABI** — génération de descripteurs ABI compatibles Solidity pour interagir avec les outils Ethereum (Hardhat, Foundry, ethers.js)
- **Disposition du stockage** — mappage de structs C vers des slots de stockage EVM avec disposition déterministe
- **Primitives EVM intégrées** — `msg.sender`, `msg.value`, `block.number`, `tx.origin` comme variables intégrées ou intrinsèques
- **Modificateurs payable / view / pure** — attributs de fonction mappés aux sémantiques de visibilité Solidity
- **Émission d'événements** — génération d'opcodes `LOG0`–`LOG4` à partir d'appels de fonctions annotés
- **Optimisation du gas** — passes IR minimisant le coût en gas (ordonnancement de pile, propagation de constantes, élimination de stockage mort)
- **revert / require** — primitives de gestion d'erreurs avec messages personnalisés

### Pourquoi C pour EVM ?

- La syntaxe de Solidity est familière aux développeurs JavaScript mais étrangère aux programmeurs système ; C est universel
- Le pipeline d'optimisation IR existant de NeverC peut produire un bytecode plus compact que `solc` dans de nombreux cas
- Les chercheurs en sécurité pensent déjà en C — écrire des outils d'audit et des fuzzers en C pour des contrats C est naturel
- L'API de plugins permet des passes personnalisées d'analyse de gas et de détection de vulnérabilités à la compilation

---

## 6. Backend Solana eBPF

NeverC supportera la compilation de code source C en bytecode eBPF de Solana — permettant le développement de programmes on-chain en C.

### Objectifs

- **Cible eBPF** — triple cible `sbf` (Solana BPF) (ex. : `neverc --target=sbf-solana hello.c -o program.so`)
- **Bindings runtime Solana** — en-têtes intégrés pour les appels système Solana : `sol_invoke_signed`, `sol_log`, `sol_memcpy`, structs d'information de compte
- **Modèle de compte** — overlays de structs C sur les données de comptes Solana avec sérialisation/désérialisation automatique
- **CPI (Cross-Program Invocation)** — wrappers type-safe pour l'appel d'autres programmes on-chain
- **PDA (Program Derived Address)** — fonctions intégrées pour la dérivation et la vérification de PDA
- **Conscience du budget de calcul** — avertissements du compilateur lorsque les unités de calcul estimées dépassent les limites du programme
- **Compatibilité Anchor** — génération IDL optionnelle pour l'interopérabilité avec les frontends basés sur Anchor

### Pourquoi C pour Solana ?

- Le runtime Solana exécute déjà de l'eBPF — C est le langage source le plus naturel pour les cibles BPF
- Les chaînes d'outils C-BPF existantes (clang + solana-bpf) nécessitent une configuration complexe ; NeverC regroupe tout en un seul binaire
- Les programmes critiques en performance bénéficient de l'abstraction sans surcoût de C et des passes d'optimisation de NeverC
- L'expérience de compilation dyncode (code indépendant de la position, runtime minimal) s'applique directement aux contraintes des programmes on-chain

---

## Calendrier

Ces fonctionnalités sont en phase de recherche et de conception. Aucune date de sortie spécifique n'est engagée. Les progrès seront suivis dans ce document et annoncés sur la page de publication du projet.

| Fonctionnalité | Statut |
|----------------|--------|
| Bibliothèque standard (`std`) | Recherche / Conception |
| Obfuscation Plugin Suite (`neverc-obfuscation`) | Recherche / Conception |
| Bibliothèque de composants UI (`neverc-ui`) | Recherche / Conception |
| IDE & outils linguistiques (`neverc-ide`) | Recherche / Conception |
| Backend EVM pour contrats intelligents | Recherche / Conception |
| Backend Solana eBPF | Recherche / Conception |
