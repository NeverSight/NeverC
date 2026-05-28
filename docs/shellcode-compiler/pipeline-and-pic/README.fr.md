**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Compilateur shellcode](../README.fr.md)

# Pipeline Shellcode, MIR et Stratégie PIC (Notes de conception)

Ce document décrit les compromis de conception dans le mode shellcode de NeverC à travers la chaîne **IR → optimisation LLVM → backend MIR → fichier objet → extraction/correction**, et sa relation avec la politique de **PIC par défaut au niveau compilateur**. Les détails d'implémentation font foi dans le code source et les commentaires en anglais.

## 1. Pourquoi forcer PIC par défaut (y compris la compilation non-shellcode)

L'extracteur de shellcode suppose que les références aux symboles externes dans le fragment exécutable sont des relocalisations **relatives au PC** ou résolubles intra-`.text`, et non des adresses absolues codées en dur ou des pools de constantes nécessitant un chargeur pour remplir `.data`.

NeverC retourne **true** dans `Generic_GCC::isPICDefaultForced()`, `MachO::isPICDefaultForced()` et `MSVCToolChain::isPICDefaultForced()`, se distinguant du comportement Clang amont « PIC par défaut optionnel » : **toute compilation multiplateforme utilise toujours PIC comme seul modèle**. Cela signifie :

- La compilation C normale et la compilation `-fshellcode` partagent les mêmes habitudes de relocalisation, réduisant la charge cognitive « fonctionne normalement, casse sous shellcode ».
- Les backends Linux / Android / macOS / Windows partagent les mêmes hypothèses sous les descripteurs pilotés par table (`TargetDesc` + `Options.td.h`), évitant le codage dur `if (linux)` dans le driver.

Cette politique ne distingue pas si `-fshellcode` est activé ou si le contexte est user/kernel. Même si l'utilisateur passe `-fno-pic` / `-static` / `-mkernel` / `-mdynamic-no-pic`, `ParsePICArgs()` maintient `Reloc::PIC_`, utilisant les mêmes hypothèses relatives au PC pour la compilation normale, le shellcode mode utilisateur et le shellcode mode noyau.

## 2. Division du travail IR et MIR en deux phases

### 2.1 Couche IR (`registerShellcodePasses`)

Responsable de la compression de la sémantique « C normal » en une forme **entrée unique, pas de section données indépendante, pas de globales problématiques** : `ZeroRelocPass`, `IndirectBrPass`, `MemIntrinPass`, `StringRuntimePass`, `HeapArenaPass`, `CompilerRtPass`, `SyscallStubPass`, `WinPEBImportPass`, `KernelImportPass` (noyau uniquement), `Data2TextPass`, etc.

**Principe** : Les problèmes résolubles en IR avec des approches structurées sont corrigés d'abord en IR (pools de constantes, BlockAddress, passage de `memcpy` à libc, passage de `__int128 /` à `__udivti3`, etc.), simplifiant le flux d'octets vu par le backend et l'extracteur. Pour les scénarios à charge cognitive élevée mais internalisables en toute sécurité, le driver injecte proactivement des règles (ex. `long double` d'AArch64 Linux / Android / Windows dégradé en binary64 en mode shellcode). Seules les constructions impossibles à supporter sans runtime déclenchent les diagnostics MIR/extracteur.

### 2.2 Couche MIR (`registerShellcodeMachinePasses`)

Enregistre des callbacks dans le `TargetPassConfig` hérité de LLVM **après l'allocation de registres, avant `addPreEmitPass`**, dans cet ordre :

1. Utilisateur/bibliothèque d'obfuscation : `RunBeforePreEmit` (pseudos CFI / EH encore présents ; utile pour les transformations dépendant des métadonnées).
2. **`ShellcodeMIRPrepPass`** : Supprime les pseudos qui généreraient des sections latérales `.eh_frame` / `.pdata` / `.xray_*`, rendant le flux d'instructions aussi proche que possible de « code pur » avant AsmPrinter.
3. Utilisateur/bibliothèque d'obfuscation : `RunAfterPreEmit` (adapté à la substitution d'instructions, au renommage de registres et à l'obfuscation similaire de « forme finale du code machine »).

**Principe** : Si les séquences d'instructions natives ont encore des problèmes, corriger en MIR (surtout autour de `ShellcodeMIRPrepPass`) ; **l'extraction et le patching sont le dernier filet de sécurité**, évitant de dupliquer la même logique dans les couches COFF/ELF/Mach-O.

Les noms d'opcode MIR ne sont pas dispersés dans le flux de contrôle de la passe ; `ShellcodeMIRPrepPass` utilise la table `(pattern, role, opcode)` de `Tables/MIRRewriteOpcodes.def` via `TargetInstrInfo::getName()`. Pour les substitutions d'instructions amicales avec shellcode, préférer l'ajout d'entrées de table et de petites réécritures MIR ; ne recourir aux modifications de sélection d'instructions backend `.td` que si nécessaire, le repli au niveau extracteur étant le dernier recours.

> Note : `ShellcodeMIRPrepPass` n'est enregistré que lorsque `-fshellcode` est activé. Les programmes normaux ne doivent pas supprimer globalement CFI/EH, cela briserait la gestion normale des exceptions et les informations de débogage.

Les callbacks globaux IR et MIR utilisent tous deux un modèle **enregistrer une fois, lire l'instantané `ShellcodeOptions` courant à l'exécution**. Cela supporte les processus compilateur embarqué de longue durée : quand le même processus compile d'abord du shellcode puis du C normal, la compilation C normale n'hérite pas des passes IR/MIR précédentes ; lors de compilations consécutives de plusieurs TUs shellcode, les enregistrements en double ne s'empilent pas.

## 3. Différences de plateforme pilotées par table

- **Triple → comportement** : Centralisé dans `describeTriple()` de `TargetDesc.cpp` et les champs `TargetDesc` (nom de section, ABI syscall, template assembleur en ligne, flags d'injection driver, etc.). Pour un nouvel OS/Arch, préférer l'**ajout d'entrées de table** plutôt que l'écriture de longues branches dans les extracteurs ou passes.
- **Options CLI** : Définies dans `neverc/include/neverc/Invoke/Options.td.h` ; consommées par `DriverIntegration.cpp` via les enums `OPT_*`, évitant la magie des chaînes.

## 4. Chaîne d'outils Windows MSVC et disposition du SDK

Lors de la compilation croisée vers des cibles Windows, NeverC supporte deux sources de SDK **sans chemins absolus codés en dur** :

1. **SDK groupé avec l'arbre de build** (recommandé) : Les utilisateurs et scripts de test traitent `runtime/windows/<arch>/msvc/` pour les fichiers MSVC CRT/SDK. NeverC détecte automatiquement ce chemin relatif au répertoire d'installation et injecte les chemins include/lib dans `MSVCToolChain::AddNeverCSystemIncludeArgs` / `Linker::ConstructJob`. Disposition typique :

   ```
   build-neverc/bin/neverc
   build-neverc/runtime/windows/x64/msvc/
     crt/include, crt/lib/<arch>
     sdk/include/{ucrt,um,shared}, sdk/lib/{ucrt,um}/<arch>
   ```

2. **Vrai sysroot style VS** (optionnel) : Si vous avez un arbre `VC/Tools/MSVC/<version>/...` + `Windows Kits/10/...`, pointer via `-winsysroot=<path>` ou la variable d'environnement `NEVERC_WIN_SYSROOT`.

Les deux sources fonctionnent sans registre ni variables d'environnement VS fournies par l'OS, permettant la compilation croisée de shellcode Windows depuis macOS / Linux.

## 5. Points d'obfuscation et d'extension

- **Obfuscation IR** : Via `setShellcodeObfuscationHooks` avec plusieurs hooks d'étape IR ; `-fshellcode-obfuscate=` passe la chaîne spec à la bibliothèque externe. Chaque couche fournit des hooks **pré** (avant optimisation) et **post** (après optimisation). `RunAfterFinalIR` est le vrai dernier point d'injection IR — aucune passe après. 11 points de hook total (6 IR + 3 MIR + 2 flux d'octets).
- **Obfuscation MIR** : `RunBeforePreEmit` / `RunAfterPreEmit` sont des hooks MIR de granularité moyenne ; `RunAfterFinalMIR` est le **vrai dernier** hook MIR. `-fshellcode-mir-obfuscate=` spécifie le spec MIR séparément ; utilise le spec IR par défaut si non défini.
- **Hooks de flux d'octets** : `RunPostExtract` est le hook **pré**-finalize ; `RunPostFinalize` est le hook **post**-finalize (dernier moment avant écriture sur disque).
- **SDK plugin Finalize** : `Plugin.h` expose `registerBadByteRewriteStrategy` et `registerCharsetEncoder`. Voir [plugin-interface.md §2–§3](../plugin-interface/README.fr.md#2-bad-byte-rewriter-badbyterewritestrategy).
- **Taille / alignement / remplissage** : `-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=` s'exécutent en fin de finalize ; le driver rejette les configurations contradictoires.
- **Choix de conception** : L'obfuscation, le polymorphisme, les encodeurs par étapes, les syscalls indirects et fonctionnalités similaires de couche stratégie sont **intentionnellement non intégrés**, disponibles uniquement comme plugins optionnels.

## 6. Dimension mode noyau (Ring-0)

Le mode shellcode introduit `-mshellcode-context=user|kernel` comme seconde dimension du pipeline, superposée au triple :

- **Mode utilisateur** : Pipeline PEB walk / syscall stub.
- **Mode noyau** :
  - `SyscallStubPass` / `WinPEBImportPass` retournent tôt au niveau passe.
  - `TargetDesc::KernelInjectFlags` ajoute les flags backend appropriés OS/arch.
  - `KernelImportPass` réécrit les appels directs extern non résolus en appels indirects via résolveur.
  - `<neverc/kernel.h>` expose `neverc_kern_resolve_t`, `neverc_kern_hash()` ; les shims mode utilisateur rejettent via `#error`.

Voir [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.fr.md) pour les détails.

## 7. Couche de compatibilité Windows POSIX

### 7.1 Problème

Le code C multiplateforme utilise couramment `write(fd, buf, n)`, `read(fd, buf, n)`, `exit(code)`, etc. Sous Unix, `SyscallStubPass` les remplace par des syscalls en ligne. Sous Windows, ces noms POSIX n'ont pas d'API Win32 correspondante, causant des erreurs « relocalisation non résolue ».

### 7.2 Objectif

**Zéro conscience utilisateur** : Le même source C compile sur les 8 triples cibles sans `#ifdef _WIN32` ni appels manuels Win32.

### 7.3 Implémentation

`WinPEBImportPass` implémente un traitement en trois phases :

1. **Phase 1 — Scan POSIX** : Scanne les déclarations extern non correspondantes contre une table de compatibilité POSIX.
2. **Phase 2 — Génération de wrappers pont** : `Win32PosixCompat.def` dispatch les noms POSIX vers des constructeurs de wrappers `always_inline` (ex. `write` → `GetStdHandle` + `WriteFile`, `mmap` → `VirtualAlloc`, `exit` → `ExitProcess`, etc.). 13 groupes de fonctions POSIX couverts.
3. **Phase 3 — Résolution PEB** : Les APIs Win32 référencées par les wrappers sont résolues via le résolveur PEB walk normal.

### 7.4 Extensibilité

Ajout de nouvelles fonctions de compatibilité POSIX : les ajouts alias-seul modifient uniquement `Win32PosixCompat.def` ; une nouvelle sémantique nécessite un petit constructeur IR + une entrée de table. Les opérations à état comme `open→CreateFileA` (nécessitant des tables de durée de vie fd/handle) ne sont intentionnellement pas émulées.

## 8. Auto-correction de déclaration implicite K&R

Quand les utilisateurs appellent des fonctions POSIX sans `#include`, C89 génère des déclarations implicites K&R avec 0 paramètre formel. `SyscallStubPass` maintient une table `getCanonicalSyscallType()` avec les types de fonction LLVM IR canoniques pour 50+ fonctions POSIX courantes. La signature canonique est automatiquement substituée lors de la détection d'une déclaration K&R à 0 paramètre.

## 9. Résumé

| Sujet | Approche |
|-------|----------|
| PIC par défaut | Toutes toolchains `isPICDefaultForced()==true`, aligné avec les hypothèses shellcode |
| Corriger d'abord en IR | Constantes, sauts indirects, intrinsèques mémoire éliminés en IR si possible |
| Filet de sécurité MIR | `ShellcodeMIRPrepPass` + hooks pré/post, puis extraction/patching comme dernier recours |
| Minimiser le codage dur | `TargetDesc` + `Options.td.h` piloté par table |
| Deux dimensions user/kernel | `-fshellcode` × `-mshellcode-context={user,kernel}` ; chaque (OS, arch, level) est une ligne dans `describeTriple()` |
| Compatibilité Windows POSIX | `WinPEBImportPass` ponte 13 groupes POSIX (write→WriteFile, mmap→VirtualAlloc, etc.) |
| Auto-correction K&R | `SyscallStubPass` se replie sur les signatures POSIX canoniques pour les déclarations 0 paramètre |

## 10. Constantes multiplateforme des en-têtes shim

Les en-têtes shim (`sys/mman.h`, `fcntl.h`, etc.) exposent des constantes devant correspondre à l'ABI du noyau cible, car les stubs syscall de shellcode passent ces valeurs directement au noyau sans traduction libc.

Différences clés :

| Constante | Darwin | Linux/Android |
|-----------|--------|---------------|
| `AT_FDCWD` | `-2` | `-100` |
| `MAP_ANONYMOUS` | `0x1000` | `0x20` |
| `O_CREAT` | `0x0200` | `0x0040` |
| `O_TRUNC` | `0x0400` | `0x0200` |
| `O_CLOEXEC` | `0x1000000` | `0x80000` |

Implémentation : gardes `#if defined(__APPLE__)` dans les en-têtes shim. La table de compatibilité POSIX de `SyscallTables.cpp` utilise les valeurs Linux (`AT_FDCWD = -100`), active uniquement sur les chemins `SyscallABI::LinuxSvc0` / `LinuxSyscall`. Les cibles Windows n'utilisent pas ces en-têtes POSIX ; le pont POSIX→Win32 est géré par les wrappers de compatibilité de `WinPEBImportPass`.
