**Langues**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**Compilateur C23 orienté recherche en sécurité, construit sur LLVM**

Éditeur de liens intégré · Pipeline shellcode · Runtimes intégrés (`string` · `mimalloc`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#fonctionnalités)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#compilation-croisée-vers-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#fonctionnalités)

[Documentation](../README.fr.md) · [Guide shellcode](../shellcode-compiler/README.fr.md) · [Runtimes intégrés](../builtins/README.fr.md)

</div>

---

> **Note :** GitHub affiche toujours `README.md` (anglais) en page d'accueil du dépôt (pas de détection automatique). Utilisez les liens de langue ci-dessus ; dans la [documentation](../README.fr.md) et le [guide shellcode](../shellcode-compiler/README.fr.md), gardez la même locale via la barre de langue et le fil d'Ariane.

## Vue d'ensemble

NeverC compile du C standard en binaires hébergés, exécutables freestanding et shellcode indépendant de la position — le tout depuis une seule chaîne d'outils. Cible **x86_64** et **AArch64** (petit-boutien uniquement).

## Fonctionnalités

- **[Compilateur shellcode](../shellcode-compiler/README.fr.md)** — pipeline IR/MIR multi-étapes, extraction multiplateforme, résolution d'imports/syscalls, mode noyau, audit d'octets interdits, architecture de plugins
- **Éditeur de liens intégré** — COFF, ELF et Mach-O dans un seul binaire ; pas de `ld` ou `link.exe` externe
- **Compilation croisée** — PE Windows depuis macOS/Linux avec SDK MSVC fourni
- **[Runtimes intégrés](../builtins/README.fr.md)** — runtimes LLVM bitcode intégrés au compilateur : [`string`](../builtins/string/README.fr.md) (chaîne à sémantique de valeur, gestion mémoire automatique) et [`mimalloc`](../builtins/mimalloc/README.fr.md) (remplacement transparent d'allocateur haute performance)
- **[Extension `.nc`](../nc-extension/README.fr.md)** — utilisez `.nc` pour activer automatiquement toutes les fonctionnalités NeverC (`string`, types entiers style Rust) sans drapeaux supplémentaires
- **Build LLVM allégé** — backends x86_64 / AArch64 uniquement ; chemins C++/ObjC/OpenMP retirés

## Exemple rapide

```c
#include <stdio.h>

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());

    // Compile-time string encryption — plaintext never appears in the binary
    string secret = "API_KEY_12345".encrypt();
    if (secret == "API_KEY_12345".encrypt()) {   // zero-allocation decrypt-and-compare
        printf("authenticated: %s\n", secret.c_str());
    }
    return 0;
}
```

> **Note :** Le type **`string`** intégré nécessite **`-fbuiltin-string`** pour les fichiers `.c`. Il est activé automatiquement pour les [**fichiers `.nc`**](../nc-extension/README.fr.md) et en mode **`-fshellcode`**.

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

Voir l'**[index de documentation](../README.fr.md)** pour la conception détaillée, la matrice des plateformes, la référence CLI et les exemples.

## Binaires macOS pré-compilés

La release est signée en ad-hoc (pas d'Apple Developer ID, pas de notarisation). Si vous l'avez téléchargée via un navigateur, supprimez une fois l'attribut quarantine après extraction :

```bash
xattr -dr com.apple.quarantine /path/to/extracted/install
```

## Compilation

### Prérequis

- CMake 3.20+
- Ninja
- Compilateur hôte C++17 (GCC, Clang ou MSVC)

### Configuration

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
```

### Build

```bash
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` est détecté et activé automatiquement s'il est présent.

### Tests

```bash
cmake --build build-neverc --target neverc-tests
ctest --test-dir build-neverc --output-on-failure
```

### Vérification

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Compilation croisée vers Windows

Placer un splat SDK [xwin](https://github.com/Jake-Shadle/xwin) dans `build-neverc/sdk/msvc/`.

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Pour le shellcode Windows (`-fshellcode`, résolution PEB, etc.), voir la [documentation du compilateur shellcode](../shellcode-compiler/README.fr.md).

## Contribution

La branche de développement par défaut est **`dev`**. Clonez le dépôt, basculez sur `dev` avant de travailler, et ouvrez vos pull requests vers `dev`.

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## Licence

[AGPL-3.0](../../LICENSE)

Les composants LLVM conservent la licence [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT).
