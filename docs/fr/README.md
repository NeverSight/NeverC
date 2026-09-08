**Langues**: [English](../README.md) | [简体中文](../zh-CN/README.md) | [繁體中文](../zh-TW/README.md) | [日本語](../ja/README.md) | [한국어](../ko/README.md) | [Français](README.md) | [Deutsch](../de/README.md) | [Español](../es/README.md) | [Italiano](../it/README.md) | [Русский](../ru/README.md) | [العربية](../ar/README.md)

[← Projet NeverC](project.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (dyncode README and breadcrumbs).

# Documentation NeverC

Notes de conception, référence API et guides pour chaque sous-système NeverC.

---

## Compilateur dyncode

Le pipeline de compilation dyncode est le cœur de la recherche NeverC. Architecture, options CLI, matrice des plateformes et exemples :

**[Compilateur dyncode →](dyncode-compiler/README.md)**

| Document | Description |
|----------|-------------|
| [README](dyncode-compiler/README.md) | Vue d'ensemble, démarrage rapide, cibles supportées |
| [Pipeline & PIC](dyncode-compiler/pipeline-and-pic.md) | Conception IR → objet → extraction |
| [IR Pass Design](dyncode-compiler/ir-pass-design.md) | Raison d'être de chaque passe IR |
| [MIR Pass Design](dyncode-compiler/mir-pass-design.md) | Passes MIR backend |
| [Kernel-Mode DynCode](dyncode-compiler/kernel-mode-dyncode.md) | Compilation Ring-0 |
| [Cross-Platform Architecture](dyncode-compiler/cross-platform-architecture.md) | `TargetDesc` et extracteurs |
| [Platform Extension Guide](dyncode-compiler/platform-extension-guide.md) | Ajouter une plateforme |
| [ARM64 Assembly Tutorial](dyncode-compiler/arm64-assembly-tutorial.md) | Instructions ARM64 du point de vue dyncode |
| [Roadmap](dyncode-compiler/roadmap.md) | Travail planifié |
| [Progress](dyncode-compiler/progress.md) | État d'implémentation |

---

## L'extension de fichier `.nc`

NeverC reconnaît `.nc` comme son extension de fichier source native. Avec `.nc`, toutes les extensions du langage NeverC (`-fneverc-types`, `-fbuiltin-string`) sont activées automatiquement — aucun drapeau supplémentaire requis.

**[Extension `.nc` →](nc-extension.md)**

---

## Runtimes Intégrés

NeverC étend le C standard avec des runtimes intégrés sous forme de bitcode LLVM. Chacun est contrôlé par un drapeau `-fbuiltin-<name>`. Les fichiers `.nc` activent `string` automatiquement.

**[Système de Runtime Intégré →](builtins/README.md)**

| Intégré | Drapeau | Description |
|---------|---------|-------------|
| [String intégré](builtins/string.md) | `-fbuiltin-string` | Type `string` à sémantique de valeur, méthodes par appel pointé, gestion mémoire automatique, UTF-8 natif |
| [mimalloc intégré](builtins/mimalloc.md) | `-fbuiltin-mimalloc` | Remplacement transparent de l'allocateur `mimalloc` haute performance `malloc`/`free`/`calloc`/`realloc` |
| [Chiffrement de chaînes (xorstr)](builtins/xorstr.md) | `-fencrypt-call-strings` | Chiffrement par instance, scellement tardif obligatoire, développement par site d'appel et nettoyage volatile de pile |
| [Hachage de chaînes (strhash)](builtins/strhash.md) | `-fstrhash-algo` / `-fstrhash-fold` | Hachage de chaînes à la compilation, même algorithme à l'exécution, pliage IR optionnel |

---

## API Plugin

NeverC expose l'intégralité de sa chaîne d'outils via une ABI C pure. Un greffon est un module partagé (`.dll` / `.so` / `.dylib`) qui se rattache à n'importe laquelle des 130 phases de compilation nommées — de l'analyse de la ligne de commande à l'image liée finale — en tant qu'observateur, intercepteur ou fournisseur de remplacement. Le SDK se limite à des en-têtes : aucun en-tête LLVM, aucune liaison avec le compilateur.

**[API Plugin →](plugin-api/README.md)**

| Document | Description |
|----------|-------------|
| [README](plugin-api/README.md) | Point d'entrée, phases, négociation d'interface, enregistrement, règles ABI |
| [Plugins Python](plugin-api/python.md) | Python embarqué facultatif, cycle de vie, options, observers en lecture seule, diagnostics et limites |
| [API Driver](plugin-api/driver.md) | Ligne de commande, choix de la chaîne d'outils, graphe d'actions, graphe de jobs |
| [API Source et E/S](plugin-api/source.md) | Fournisseurs VFS, positions source, tampons, puits de sortie, dépendances |
| [API Préprocesseur](plugin-api/prep.md) | Jetons, macros, pragmas, inclusions, requêtes de fonctionnalités, 39 types d'événements |
| [API AST et sémantique](plugin-api/ast-sema.md) | Extension du parseur, mutation de l'AST, recherche de noms, types, constantes |
| [API IR](plugin-api/ir.md) | Lecture de l'IR LLVM, construction transactionnelle, analyses, passes, fournisseurs |
| [API MIR](plugin-api/mir.md) | Fonctions machine, registres, cadres de pile, passes et analyses MIR |
| [Cible, MC, assembleur, objet](plugin-api/target-mc-object.md) | Enregistrement de cible, conventions d'appel, encodage MC, graphes objet |
| [API Link et LTO](plugin-api/link-lto.md) | Graphe de liaison, résolution de symboles, GC/ICF, fournisseurs de lieur et de LTO |
| [API DynCode](plugin-api/dyncode.md) | Images plates indépendantes de la position, abaissement des imports, encodage de jeu de caractères |
| [Conventions d'appel personnalisées](plugin-api/custom-callconv.md) | Plugins de convention d'appel pilotés par les données |

---

## Feuille de route

Grandes orientations du projet NeverC : bibliothèque standard, backend EVM pour contrats intelligents, backend Solana eBPF.

**[Feuille de route →](roadmap.md)**

| Fonctionnalité | Description |
|----------------|-------------|
| Bibliothèque standard (`std`) | Paquets à la Go : `fmt`, `os`, `io`, `net`, `crypto`, `encoding`, `sync`, et plus |
| Suite de plugins d'obfuscation (`neverc-obfuscation`) | VM, MBA, aplatissement de flux, moteur polymorphe, anti-altération — plugins de première partie |
| Bibliothèque de composants UI (`neverc-ui`) | UI multiplateforme à la Qt, moteur HTML/JS/CSS, concepteur glisser-déposer, flux IA natif |
| IDE & outils linguistiques (`neverc-ide`) | Extension VSCode + IDE autonome pour fichiers `.nc`, IntelliSense, débogage, visualisation pipeline dyncode |
| Contrats intelligents EVM | Compiler du C en bytecode EVM — écrire des contrats en C au lieu de Solidity |
| Solana eBPF | Compiler du C en bytecode eBPF Solana — développement de programmes on-chain en C |

---

## Outils CLI

Commandes utilisateur au-delà d'une simple compilation.

| Document | Description |
|----------|-------------|
| [`neverc run`](run.md) | Compiler, exécuter localement et supprimer un binaire temporaire (style `go run`) |
| [`neverc translate`](translate.md) | Traduire C++ vers NeverC |
| [`neverc update`](update.md) | Mettre à niveau ou rétrograder une install release (compilateur + runtimes installés, une balise) |
| [`neverc runtime`](runtime.md) | Installer, lister, mettre à jour ou retirer les sysroots de cross-compilation |
| [`neverc build` / `neverc make`](build.md) | Pilote compatible GNU Make pour les Makefile d'exemples et de projets |
| [Binaires de publication et `--strip`](release-builds.md) | Retirer symboles non requis et débogage source, avec renommage structurel des symboles `.ko` adapté au noyau (ni hash ni encryption) |

---

## Cibles de sécurité Windows

| Document | Description |
|----------|-------------|
| [DLL d’enclave VBS](vbs-enclave.md) | Lier, valider, traiter, signer et charger des images d’enclave VBS compatibles avec Microsoft |

---

## Développement local

Compiler NeverC à partir des sources et configurer l'environnement de développement local, y compris le PATH.

**[Développement local →](local-dev.md)**

---

## Exemples

Exemples compilables démontrant les capacités de compilation croisée de NeverC. Tous compilent depuis macOS / Linux.

**[Exemples →](examples.md)**

---

## Attribution et citation des sources

**[guide d’attribution →](attribution.md)**
