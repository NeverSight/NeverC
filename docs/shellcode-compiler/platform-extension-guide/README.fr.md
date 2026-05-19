**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilateur shellcode](../README.fr.md)

# Guide d'extension de plateforme

Ce document explique comment étendre le compilateur shellcode à de nouvelles plateformes cibles. Actuellement supporté : **arm64 / x86_64 sur macOS / Linux / Android / Windows** (8 triples), chacun avec des contextes indépendants **User** / **Kernel** (16 variantes au total). L'ajout d'une nouvelle plateforme nécessite typiquement quelques centaines de lignes de code.

## Philosophie de conception : Piloté par table, pas par branchement

Toutes les passes sont indépendantes de la cible. Les différences de plateforme sont concentrées dans **deux endroits** :

1. Entrées de table `describeTriple()` dans `TargetDesc.cpp`
2. Switches d'architecture des trois extracteurs (Mach-O / ELF / COFF)

Ajout nouvelle plateforme = une ligne dans (1) + un case dans (2).

## Étapes

### 1. Ajouter une ligne dans `TargetDesc`

Ajouter la branche OS correspondante dans `describeTriple()` :

```cpp
if (TT.isOSFreeBSD()) {
  D.OS = ShellcodeOS::FreeBSD;
  D.Format = ObjectFormat::ELF;
  D.TextSectionName = ".text";
  if (D.Arch == ShellcodeArch::X86_64) {
    D.Syscall = SyscallABI::FreeBSDSyscall;
    D.AsmTemplate = "syscall";
    D.SyscallNumberReg = "rax";
    D.SyscallRetReg = "rax";
    D.ArgRegs = kX86_64FreeBSDArgRegs;
    D.NumArgRegs = 6;
    D.DriverInjectFlags = kX86_64UnixInjectFlags;
  }
  return D;
}
```

**Champs requis** (tous définis dans `TargetDesc.h`) :

| Champ | Objectif | Si manquant |
|-------|---------|-------------|
| `OS` / `Arch` / `Format` | Clé de dispatch | `describeTriple` retourne Unknown → driver rejette tôt |
| `TextSectionName` | L'extracteur cherche la section d'entrée | `.text` non trouvé → rejet |
| `Syscall` | Décision de remplacement SyscallStubPass | `None` → SyscallStubPass no-op |
| `AsmTemplate` / `SyscallNumberReg` / `SyscallRetReg` / `ArgRegs` | Génération InlineAsm SyscallStubPass | Un champ vide → SyscallStubPass no-op |
| `TCBReadAsm` / `TCBReadConstraint` | InlineAsm lecture TEB WinPEBImportPass | Vide → PEB walk génère InlineAsm vide (Windows : requis) |
| `DriverInjectFlags` | Flags spécifiques plateforme en mode shellcode | null → pas d'injection |

### 2. Étendre `SyscallStub` / `SyscallTables` (si l'OS a des traps kernel)

- Ajouter valeur enum à `SyscallABI` dans `TargetDesc.h`
- Ajouter `kXxxTable` dans `SyscallTables.cpp`
- Ajouter case dans switch de `lookupSyscall`
- `SyscallStubPass` inchangé — templates/contraintes InlineAsm viennent de `TargetDesc`

### 2.5 Étendre la liste blanche Win32 API Windows

Windows n'a pas d'ABI syscall stable ; les appels utilisateur à `WriteFile` / `CreateThread` / `VirtualAlloc` passent par `WinPEBImportPass`. La liste blanche est une table multi-DLL :

- Définie dans `Tables/Win32Apis.def`
- Chaque ligne : `NEVERC_WIN32_API(ApiName, "dll.dll")`
- Le résolveur supporte déjà les DLL arbitraires via `__neverc_win_resolve(dll_hash, api_hash)`

**Ajouter nouvelle API** (ex. `DeviceIoControl`) : 1 ligne dans `Win32Apis.def` + 1 déclaration dans `lib/Headers/windows.h`.

**Ajouter nouveau bucket DLL** (ex. `winhttp.dll`) : Ajouter des lignes avec le nouveau nom DLL.

### 3. Étendre l'extracteur correspondant

1. Identifier les types de reloc → patcher bytes ou rejeter
2. Mettre à jour la liste de noms de sections de données interdites
3. Mettre à jour la validation de plage de cible reloc entrée-à-offset-0

Pour un format objet entièrement nouveau (ex. modules WASM) :
1. Ajouter valeur enum `ObjectFormat`
2. Ajouter case dans switch dispatch de `ShellcodeExtractor.cpp`
3. Écrire `<Format>Extractor.cpp` (suivant la structure de `ELFExtractor.cpp`)

### 4. Ajouter Loader (outil de test uniquement)

Référence `loader_linux.c` et `loader_windows.c`. Typiquement : `mmap(RWX) → memcpy → icache flush → call`.

### 5. Mettre à jour les tests

Ajouter un test de compilation croisée dans `tests/neverc/ShellcodeCrossTargetTests.cpp`.

---

## Pièges multiplateformes connus

- **Endianness** : NeverC ne supporte que little-endian (LE).
- **Différences ABI** : Win64 vs System V AMD64 ont des registres d'arguments complètement différents. Géré au niveau frontend NeverC.
- **Numéros syscall** : Différents par architecture sous Linux, Android identique à Linux, Darwin a ses propres numéros BSD, Windows sans numéros stables (PEB walk).
- **Cohérence de cache** : ARM nécessite un flush i-cache explicite ; x86 non.
- **SELinux / W^X** : Android contraint par SELinux `execmem` ; iOS non-jailbreaké rejette complètement `mmap(RWX)`.

## Feuille de route des extensions futures

| Cible | Effort estimé | Dépendances |
|-------|--------------|-------------|
| **iOS arm64** (jailbreak / `MAP_JIT`) | 1 jour | Réutiliser extracteur Mach-O |
| **FreeBSD / OpenBSD x86_64** | Demi-journée | Réutiliser extracteur ELF + nouvelle table syscall |
| **RISC-V64 Linux** | 2 jours | Nécessite RISC-V TargetDesc + nouvelle variante AllBlr + patching reloc RISC-V |

## Interface d'extension de passe d'obfuscation

La pipeline shellcode expose 11 hooks via `Pipeline.h::ObfuscationHooks` pour les bibliothèques d'obfuscation tierces. Le patching MIR intégré est aussi piloté par table : `Tables/MIRRewritePatterns.def` et `Tables/MIRRewriteOpcodes.def`. Préférer les entrées de table et helpers étroits plutôt que disperser des branches spécifiques à la cible dans le corps de la passe.
