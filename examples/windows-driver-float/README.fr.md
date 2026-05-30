**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Pilote noyau Windows avec virgule flottante

Un pilote noyau WDM construit avec NeverC qui démontre **l'utilisation sûre
des opérations en virgule flottante / SIMD en mode noyau**. Compilation
croisée depuis macOS / Linux.

## Compilation

```bash
cd examples/windows-driver-float
make
```

Depuis une version autonome de NeverC :

```bash
make NEVERC=/path/to/neverc
```

Le résultat est `FloatDriver.sys` (optimisé auto-LTO).
La compilation par défaut inclut `-g` pour le débogage ; supprimez `-g`
pour les versions de production.

---

## Deux problèmes à gérer

La virgule flottante en mode noyau présente deux problèmes distincts :

### Problème 1 — le marqueur ABI `_fltused` (compilation/édition de liens)

Le compilateur MSVC émet une référence non définie au symbole `_fltused`
chaque fois qu'une unité de traduction effectue une opération en virgule
flottante. Dans les programmes en mode utilisateur, `libcmt.lib` fournit
ce symbole, satisfaisant l'éditeur de liens et entraînant l'inclusion de
quelques composants CRT spécifiques au FP.

Les pilotes noyau ne sont **pas** liés à `libcmt` (nous passons `-nostdlib`
et `-Xlinker --nodefaultlib`), donc un `_fltused` non résolu provoquerait
une erreur d'édition de liens.

**Comment NeverC le résout** : avec `-fms-kernel`, le backend X86 de LLVM
définit `_fltused` localement comme 0. Vous pouvez le voir dans l'assembleur
généré :

```asm
# Cible mode utilisateur :
    .globl  _fltused              # référence externe -- nécessite libcmt
```

```asm
# Cible -fms-kernel :
    .globl  _fltused
    .set    _fltused, 0           # définition locale ! aucun symbole externe requis
```

Vous **n'avez donc jamais à écrire manuellement `int _fltused = 0;`** dans votre pilote.

### Problème 2 — le noyau NE préserve PAS les registres FP/SIMD (exécution)

Le noyau Windows ne sauvegarde/restaure **pas** les registres x87 / XMM / YMM / ZMM
lors des changements de contexte par défaut. Si un pilote touche à ces
registres depuis du code noyau arbitraire, il corrompra silencieusement
l'état SIMD du thread mode utilisateur qui se trouve sur le CPU.

**Solution** : encadrez chaque région virgule flottante / SIMD avec
[`KeSaveExtendedProcessorState`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesaveextendedprocessorstate)
et `KeRestoreExtendedProcessorState` :

```c
XSTATE_SAVE save;
NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
if (!NT_SUCCESS(status))
    return status;

// ... votre code FP / SIMD ici ...

KeRestoreExtendedProcessorState(&save);
```

### Masques XSTATE

| Masque | Couverture |
|--------|-----------|
| `XSTATE_MASK_LEGACY_FLOATING_POINT` (bit 0) | pile x87 |
| `XSTATE_MASK_LEGACY_SSE` (bit 1) | XMM0–15 |
| `XSTATE_MASK_LEGACY` | bit 0 \| bit 1 (couvre la plupart du code `double` / SSE simple) |
| `XSTATE_MASK_GSSE` / AVX (bit 2) | moitiés supérieures de YMM0–15 |
| `XSTATE_MASK_AVX512` | registres ZMM AVX-512 |

Passez le masque combiné par OU correspondant aux registres les plus larges utilisés.

---

## Ce que fait ce pilote

- Crée un objet périphérique à `\Device\FloatDriver` et un lien symbolique à `\DosDevices\FloatDriver`
- Dans `DriverEntry`, appelle `ComputeAreaSafe()` (qui enveloppe `ComputeArea()` avec
  sauvegarde/restauration de l'état FP) deux fois avec `radius=1.0` et `radius=5.0`
- Affiche les bits bruts du double via `DbgPrint` (car `%f` n'est pas pris en charge
  par `DbgPrint` — nous utilisons `RtlCopyMemory` pour extraire le motif 64 bits)
- Définit implicitement `_fltused` via `-fms-kernel`

## Vérification de l'émission de `_fltused`

Comparez la sortie du compilateur avec et sans `-fms-kernel` :

```bash
# Mode utilisateur (nécessiterait libcmt) :
neverc --target=x86_64-pc-windows-msvc -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused

# Noyau (défini localement comme 0) :
neverc --target=x86_64-pc-windows-msvc -fms-kernel -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused
#  .set    _fltused, 0
```

## Chargement (sur une machine de test Windows)

```cmd
sc create FloatDriver type= kernel binPath= C:\path\to\FloatDriver.sys
sc start FloatDriver
sc stop FloatDriver
sc delete FloatDriver
```

Activez la signature de test ou utilisez un certificat de signature de code pour la production.

## Mises en garde

- **`%f` ne fonctionne pas avec `DbgPrint`** — la routine d'impression de
  débogage du noyau n'a pas de formatage en virgule flottante. Convertissez
  votre double en entier à virgule fixe pour l'affichage, ou imprimez les
  bits bruts comme dans cet exemple.
- **N'utilisez pas la virgule flottante à IRQL ≥ DISPATCH_LEVEL** sauf si
  absolument nécessaire. `KeSaveExtendedProcessorState` documente les
  contraintes IRQL.
- **Performance** : la sauvegarde/restauration d'état n'est pas gratuite ;
  pour les chemins critiques, envisagez de regrouper le travail FP dans
  une seule région encadrée.
