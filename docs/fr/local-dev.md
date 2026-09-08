**Langues**: [English](../local-dev.md) | [简体中文](../zh-CN/local-dev.md) | [繁體中文](../zh-TW/local-dev.md) | [日本語](../ja/local-dev.md) | [한국어](../ko/local-dev.md) | [Français](local-dev.md) | [Deutsch](../de/local-dev.md) | [Español](../es/local-dev.md) | [Italiano](../it/local-dev.md) | [Русский](../ru/local-dev.md) | [العربية](../ar/local-dev.md)

[← Index de la documentation](README.md)

# Développement local

Guide pour compiler NeverC à partir des sources et configurer un environnement de développement local.

---

## Prérequis

- CMake 3.20+
- Ninja
- Un compilateur C++17 hôte (GCC, Clang ou MSVC)

---

## Compilation

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
cmake --build build-neverc --target neverc
```

`ccache` / `sccache` est automatiquement détecté et activé s'il est présent.

`--target neverc` est la compilation quotidienne stage-1 (runtimes embarqués
vides). Cela suffit pour la plupart du travail local. Pour embarquer les runtimes
string / mimalloc / std / NVK dans le binaire (ou obtenir un compilateur proche
de la CI), lancez la cible parapluie stage-2 :

```bash
cmake --build build-neverc --target neverc-embed-runtime-bitcode
```

Le bootstrap en deux étapes est détaillé dans [Builtins](builtins/README.md).

### Compilation avec les tests

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

`check-neverc` dépend de `neverc-embed-runtime-bitcode` : le premier lancement
des tests bootstrappe et relie le compilateur automatiquement. Pas besoin
d'appeler la cible embed à la main.

---

## Configuration du PATH (macOS / Linux)

Après la compilation, le binaire `neverc` se trouve dans `build-neverc/bin/neverc`. Utilisez le script utilitaire pour l'ajouter à votre `PATH` sans avoir à saisir le chemin complet :

```bash
source ./utils/build/neverc-env.sh
```

Vous pouvez maintenant exécuter `neverc` directement :

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### Suppression du PATH

Pour retirer la compilation locale du `PATH` dans la session shell en cours :

```bash
source ./utils/build/neverc-env.sh --remove   # ou -r
```

### Configuration permanente

Écrire automatiquement la ligne `source` dans le fichier rc du shell (`~/.zshrc`, `~/.bashrc` ou `~/.profile`) :

```bash
source ./utils/build/neverc-env.sh --install
```

Annuler :

```bash
source ./utils/build/neverc-env.sh --uninstall
```

### Basculer entre développement local et release

Si vous avez à la fois une installation release (par défaut : `~/.neverc`) et une compilation dans l’arborescence source, utilisez `neverc-env.sh` pour changer le `neverc` actif dans le shell courant sans écraser l’une ou l’autre installation :

```bash
source ./utils/build/neverc-env.sh              # dev local (build-neverc/bin)
source ./utils/build/neverc-env.sh --local      # idem
source ./utils/build/neverc-env.sh --release    # release (~/.neverc/bin)
source ./utils/build/neverc-env.sh --status     # afficher le neverc actif
source ./utils/build/neverc-env.sh --remove     # retirer les deux du PATH
```

Le basculement définit `NEVERC_ENV` sur `local` ou `release` :

```bash
echo "$NEVERC_ENV"
neverc --version
which neverc
```

Si la release est installée dans un autre préfixe, indiquez le même répertoire que `install.sh` :

```bash
NEVERC_INSTALL_DIR=$HOME/.neverc-v3389.1.2 source ./utils/build/neverc-env.sh --release
```

Optionnel — alias dans la configuration du shell (remplacez par le chemin absolu de votre dépôt) :

```bash
alias neverc-dev='source /path/to/NeverC/utils/build/neverc-env.sh --local'
alias neverc-rel='source /path/to/NeverC/utils/build/neverc-env.sh --release'
```

---

## Windows (CMD)

Sous Windows, utilisez le script `.bat` (aucun privilège administrateur requis) :

```cmd
utils\build\neverc-env.bat             &REM ajouter au PATH (session courante)
utils\build\neverc-env.bat --remove    &REM supprimer du PATH (session courante)
utils\build\neverc-env.bat --global    &REM persister dans le PATH utilisateur via setx
utils\build\neverc-env.bat --global -r &REM supprimer du PATH utilisateur via setx
```

Contrairement au script Unix, pas besoin de `source` — le `.bat` modifie directement la session `cmd` en cours. `--global` écrit dans le registre utilisateur via `setx` (aucun privilège administrateur requis).

---

## Binaires macOS précompilés

Le package est signé avec un certificat Apple Developer ID et notarisé par Apple. Extrayez l'archive et utilisez directement.

---

## Compilation croisée vers Windows

NeverC intègre les SDK de chaque plateforme dans `runtime/` (Windows SDK/WDK, sysroot Linux, sysroot macOS, Android NDK) ; aucune configuration externe n'est nécessaire.

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Pour le dyncode Windows (`-fdyncode`, résolution d'imports PEB, etc.), voir la [documentation du compilateur dyncode](dyncode-compiler/README.md).

---

## Vérification

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```
