**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Projet NeverC](i18n/README.fr.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (shellcode README and breadcrumbs).

# Documentation NeverC

Notes de conception, référence API et guides pour chaque sous-système NeverC.

---

## Compilateur shellcode

Le pipeline de compilation shellcode est le cœur de la recherche NeverC. Architecture, options CLI, matrice des plateformes et exemples :

**[Compilateur shellcode →](shellcode-compiler/README.fr.md)**

| Document | Description |
|----------|-------------|
| [README](shellcode-compiler/README.fr.md) | Vue d'ensemble, démarrage rapide, cibles supportées |
| [Pipeline & PIC](shellcode-compiler/pipeline-and-pic/README.fr.md) | Conception IR → objet → extraction |
| [IR Pass Design](shellcode-compiler/ir-pass-design/README.fr.md) | Raison d'être de chaque passe IR |
| [MIR Pass Design](shellcode-compiler/mir-pass-design/README.fr.md) | Passes MIR backend |
| [Kernel-Mode Shellcode](shellcode-compiler/kernel-mode-shellcode/README.fr.md) | Compilation Ring-0 |
| [Plugin Interface](shellcode-compiler/plugin-interface/README.fr.md) | Plugins d'obfuscation et d'encodage |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.fr.md) | `TargetDesc` et extracteurs |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.fr.md) | Ajouter une plateforme |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.fr.md) | Instructions ARM64 du point de vue shellcode |
| [Roadmap](shellcode-compiler/roadmap/README.fr.md) | Travail planifié |
| [Progress](shellcode-compiler/progress/README.fr.md) | État d'implémentation |

---

## L'extension de fichier `.nc`

NeverC reconnaît `.nc` comme son extension de fichier source native. Avec `.nc`, toutes les extensions du langage NeverC (`-fneverc-types`, `-fbuiltin-string`) sont activées automatiquement — aucun drapeau supplémentaire requis.

**[Extension `.nc` →](nc-extension/README.fr.md)**

---

## Runtimes Intégrés

NeverC étend le C standard avec des runtimes intégrés sous forme de bitcode LLVM. Chacun est contrôlé par un drapeau `-fbuiltin-<name>`. Les fichiers `.nc` activent `string` automatiquement.

**[Système de Runtime Intégré →](builtins/README.fr.md)**

| Intégré | Drapeau | Description |
|---------|---------|-------------|
| [String intégré](builtins/string/README.fr.md) | `-fbuiltin-string` | Type `string` à sémantique de valeur, méthodes par appel pointé, gestion mémoire automatique, UTF-8 natif |
| [mimalloc intégré](builtins/mimalloc/README.fr.md) | `-fbuiltin-mimalloc` | Remplacement transparent de l'allocateur `mimalloc` haute performance `malloc`/`free`/`calloc`/`realloc` |
| [Chiffrement de chaînes (xorstr)](builtins/xorstr/README.fr.md) | `-fencrypt-call-strings` | Chiffrement de chaînes à la compilation, déchiffrement XOR sur pile, anti-signature |
