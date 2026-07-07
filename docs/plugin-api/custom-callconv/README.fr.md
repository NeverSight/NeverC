**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Conventions d'appel personnalisées

NeverC prend en charge les **conventions d'appel personnalisées pilotées par les données** — vous pouvez assigner des registres physiques arbitraires aux arguments et valeurs de retour de n'importe quelle fonction, entièrement depuis un plugin externe ou des attributs au niveau du code source, sans modifier le compilateur ni aucune définition TableGen.

## Présentation

Les conventions d'appel LLVM traditionnelles sont codées en dur dans le backend via les fichiers `.td` / `.inc`. NeverC remplace cela par une approche **pilotée par les données à l'exécution** :

- Un **spec d'assignation de registres** (chaîne de caractères) est attaché à chaque fonction comme attribut.
- Le backend lit ce spec et assigne les paramètres/valeurs de retour aux registres physiques spécifiés.
- Le spec peut provenir d'un **plugin externe** (passe IR), d'**attributs source** (`__attribute__` / `__declspec`), ou des deux.

## Format du spec

Chaîne délimitée par des points-virgules. Chaque segment a une clé et une liste de noms de registres séparés par des virgules (insensible à la casse, tolérant aux espaces) :

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| Segment | Alias | Signification |
|---|---|---|
| `args` | | **Mode positionnel** : chaque jeton est un nom de registre ou `stack`/`mem` |
| `gpr` | `arg_gpr` | **Mode pool** : registres d'arguments entiers/pointeurs |
| `xmm` | `arg_xmm` | **Mode pool** : registres d'arguments flottants/vecteurs |
| `ret_gpr` | `ret` | Registres de valeur de retour entiers/pointeurs |
| `csr` | | Ensemble callee-saved personnalisé (défaut : ensemble ABI standard) |

### Architectures supportées

| Architecture | GPR | SIMD | Sélection de largeur |
|---|---|---|---|
| **x86-64** | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | `xmm0`–`xmm15` | i32→32 bits, i64→64 bits |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f32→`s`, f64→`d` |

## Utilisation

### 1. Piloté par plugin (recommandé)

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib
# Mode attribut (par défaut)
neverc -fplugin-pass=./CustomCallConvPlugin.dylib input.c -o output.o
# Mode global
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

### 2. Attributs source

```c
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

## Support LTO

Le plugin s'enregistre à `NEVERC_INTERPOSE_POST_OPT` et `NEVERC_INTERPOSE_LTO_POST_OPT`, garantissant l'application des conventions personnalisées après la fusion LTO des unités de traduction.

## API plugin

```c
API->FunctionSetCustomCallConv(F, "gpr:r10,r11,rsi;ret:rdx");
```

Définit `CallingConv::NeverC_Custom` (CC 1000), écrit l'attribut et **synchronise tous les sites d'appel directs**. Passer `NULL` ou `""` efface la convention.

## Tests

Suite GoogleTest (22 tests, tous PASS) :

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```
