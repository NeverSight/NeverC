**Langues**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**Le compilateur C23 compatible IA pour la recherche en sécurité, construit sur LLVM**

Éditeur de liens intégré · Pipeline shellcode · Runtimes intégrés (`string` · `mimalloc` · `xorstr`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#fonctionnalités)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#compilation-croisée-vers-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#fonctionnalités)

[Documentation](../README.fr.md) · [Guide shellcode](../shellcode-compiler/README.fr.md) · [Runtimes intégrés](../builtins/README.fr.md) · [API Plugin](../plugin-api/README.fr.md)

</div>

---

> **Note :** GitHub affiche toujours `README.md` (anglais) en page d'accueil du dépôt (pas de détection automatique). Utilisez les liens de langue ci-dessus ; dans la [documentation](../README.fr.md) et le [guide shellcode](../shellcode-compiler/README.fr.md), gardez la même locale via la barre de langue et le fil d'Ariane.

## Vue d'ensemble

NeverC compile du C standard en binaires hébergés, exécutables freestanding et shellcode indépendant de la position — le tout depuis une seule chaîne d'outils. Cible **x86_64** et **AArch64** (petit-boutien uniquement).

## Pourquoi NeverC ?

C est déjà le langage système le plus simple. NeverC le rend encore plus simple :

- **C23 pur, rien de plus** — Pas de templates, pas de RAII, pas de surcharge d'opérateurs, pas de flux de contrôle caché. Ce que vous lisez est ce qui s'exécute.
- **`string` intégré** — Type chaîne à sémantique de valeur avec `+`, `==`, `.starts_with()` et libération automatique — sans C++.
- **Pas d'exceptions** — La gestion d'erreurs reste explicite. Pas de déroulement de pile, pas de surprises de performance.
- **Binaire unique** — Compilateur + éditeur de liens + runtimes dans un seul exécutable. Zéro dépendance externe.
- **Compatible LLM** — Grammaire minimale et sémantique déterministe : le code NeverC généré par IA compile correctement plus souvent que les alternatives C++.
- **Véritable compilation croisée** — Compilez des exécutables Windows et du shellcode depuis macOS ou Linux — pas de VM, pas de dual boot, pas de SDK à chercher. Le Windows SDK est intégré au compilateur.
- **Extensible sans friction** — Écrivez des [plugins compilateur](../plugin-api/README.fr.md) dans n'importe quel langage avec un seul en-tête C. 20+ points d'accroche pour intervenir à presque toute étape — de l'optimisation IR à la sortie binaire finale — sans toucher aux internes LLVM.
- **Recherche en sécurité intégrée** — Compilation shellcode, chiffrement de chaînes à la compilation et génération PE multiplateforme sont nativement intégrés au compilateur — pas des ajouts bricolés avec des scripts externes.

## Fonctionnalités

- **[Compilateur shellcode](../shellcode-compiler/README.fr.md)** — pipeline IR/MIR multi-étapes, extraction multiplateforme, résolution d'imports/syscalls, mode noyau, audit d'octets interdits, architecture de plugins
- **Éditeur de liens intégré** — COFF, ELF et Mach-O dans un seul binaire ; pas de `ld` ou `link.exe` externe
- **Compilation croisée** — PE Windows depuis macOS/Linux avec SDK MSVC fourni
- **[Runtimes intégrés](../builtins/README.fr.md)** — runtimes LLVM bitcode intégrés au compilateur : [`string`](../builtins/string/README.fr.md) (chaîne à sémantique de valeur, gestion mémoire automatique), [`mimalloc`](../builtins/mimalloc/README.fr.md) (remplacement transparent d'allocateur haute performance) et [`xorstr`](../builtins/xorstr/README.fr.md) (chiffrement de chaînes à la compilation avec déchiffrement anti-signature)
- **[API Plugin](../plugin-api/README.fr.md)** — ABI C pure pour les plugins de passes hors arbre ; SDK à en-tête unique, zéro dépendance LLVM/CRT, points d'accroche IR, MIR, Binary et Linker
- **[Extension `.nc`](../nc-extension/README.fr.md)** — utilisez `.nc` pour activer automatiquement toutes les fonctionnalités NeverC (`string`, types entiers style Rust) sans drapeaux supplémentaires
- **Build LLVM allégé** — backends x86_64 / AArch64 uniquement ; chemins C++/ObjC/OpenMP retirés

## Exemple rapide

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

Voir l'**[index de documentation](../README.fr.md)** pour la conception détaillée, la matrice des plateformes, la référence CLI et les exemples. Exemples compilables complets : **[examples/](../../examples/)**.

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
cmake --build build-neverc --target check-neverc
```

### Vérification

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## Compilation croisée vers Windows

NeverC embarque un Windows SDK et WDK dans `runtime/` ; aucune configuration externe n'est requise.

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
