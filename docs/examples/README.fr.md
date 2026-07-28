**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation](../README.fr.md) · [← Projet NeverC](../../docs/i18n/README.fr.md)

# Exemples NeverC

Exemples compilables démontrant les capacités de compilation croisée de NeverC. Tous compilent depuis macOS / Linux — aucun environnement Windows requis.

---

## Exemples disponibles

### Windows

| Exemple | Description | Fonctionnalités clés |
|---------|-------------|---------------------|
| [Pilote noyau Windows](../../examples/windows-driver/README.fr.md) | Pilote WDM minimal | Compilation croisée `.sys` pour **x64** (défaut) et **ARM64**, auto-LTO, éditeur de liens intégré |
| [Pilote Windows + CET](../../examples/windows-driver-cet/README.fr.md) | Pilote avec Intel CET Shadow Stack | Code noyau compatible CET (**x64 uniquement**), `/guard:ehcont` |
| [Pilote Windows + virgule flottante](../../examples/windows-driver-float/README.fr.md) | Pilote avec virgule flottante/SIMD | Virgule flottante sécurisée en mode noyau sur **x64** et **ARM64** |
| [Windows Ring3 EXE](../../examples/windows-exe/README.fr.md) | Application console mode utilisateur | GetSystemInfo, énumération processus, VirtualAlloc |
| [Windows Ring3 DLL](../../examples/windows-dll/README.fr.md) | DLL mode utilisateur | ReadProcessMemory, VirtualAllocEx, énumération modules |

### Linux

| Exemple | Description | Caractéristiques |
|---------|-------------|-----------------|
| [Linux Hello World](../../examples/linux-hello/README.fr.md) | Programme C minimal | Cross-compilation depuis macOS/Windows |
| [Linux POSIX](../../examples/linux-posix/README.fr.md) | Programmation système POSIX | pthreads, mmap, pipe, signaux |
| [Linux Statique](../../examples/linux-static/README.fr.md) | Binaire entièrement statique | Liaison `-static` |
| [Linux Réseau](../../examples/linux-network/README.fr.md) | Démo socket TCP | Client/serveur |
| [Linux Math + zlib](../../examples/linux-math/README.fr.md) | Math + compression | Trigonométrie, zlib, CRC32 |

### macOS

| Exemple | Description | Caractéristiques |
|---------|-------------|-----------------|
| [Application macOS](../../examples/macos-app/README.fr.md) | Exécutable natif Mach-O | sysctl, uname, Mach host_info/task_info, introspection processus |
| [Bibliothèque dynamique macOS](../../examples/macos-dylib/README.fr.md) | Bibliothèque native `.dylib` | Mach vm_read/vm_write, vm_alloc/vm_dealloc, task_info, XOR |

### Android

| Exemple | Description | Caractéristiques |
|---------|-------------|-----------------|
| [Android ELF](../../examples/android-elf/README.fr.md) | Binaire ARM64 natif pour appareils rootés | Cross-compilation Android, dlopen/liblog, /proc, détection root |
| [Bibliothèque partagée Android](../../examples/android-so/README.fr.md) | Bibliothèque `.so` native ARM64 | Bibliothèque partagée, mmap RWX, chiffrement XOR |

### Modules noyau Android (.ko)

Aucun arbre source noyau requis — NeverC compile contre le runtime minimal intégré. Source unique couvrant GKI 5.10–6.12.

| Exemple | Description | Caractéristiques |
|---------|-------------|-----------------|
| [Kernel Hello](../../examples/android-kernel-hello/README.fr.md) | Module `.ko` minimal | Bootstrap kallsyms via kprobe, validation insmod minimale |
| [Template pilote noyau](../../examples/android-kernel-driver/README.fr.md) | Template de résolution dynamique de symboles | `kallsyms_lookup_name`, ABI stable GKI, 5.10–6.12 |
| [Kernel Inline Interpose](../../examples/android-kernel-inline-interpose/README.fr.md) | Interpose inline sur `do_faccessat` | Patch BTI/PAC sûr, mode context interpose, relocation PC-relative |
| [Kernel Syscall Interpose](../../examples/android-kernel-syscall-interpose/README.fr.md) | Table syscall / inline / context interpose | Remplacement `sys_call_table`, interpose inline, context interpose |
| [Kernel Lowvis](../../examples/android-kernel-lowvis/README.fr.md) | Gestion de visibilité de module | Visibilité list/sysfs/proc, wrappers d'identifiants, état d'application SELinux |
| [Kernel Full SDK](../../examples/android-kernel-full/README.fr.md) | Intégration SDK complète | Netlink IPC, interposes, wrappers d'identifiants, visibilité de module, contrôle de politique SELinux, VMA, fichiers |
| [Kernel Chardev](../../examples/android-kernel-chardev/README.fr.md) | Périphérique caractère + ioctl | `misc_register`, dispatch ioctl, `/proc` seq_file |
| [Kernel Netlink](../../examples/android-kernel-netlink/README.fr.md) | IPC netlink bidirectionnel | Commandes PING/VERSION/ECHO, `nvk_nl_open`/`nvk_nl_reply` |
| [Kernel Probe](../../examples/android-kernel-probe/README.fr.md) | Sonder une instruction arbitraire | `neverc_krt_probe_register`, contexte de registres complet, chaînage par priorité, saut/redirection |
| [Kernel Multi-Fichiers](../../examples/android-kernel-multifile/README.fr.md) | Module noyau multi-fichiers | Un seul `NEVERC_KRT_BOOTSTRAP()`, état partagé `weak_odr`, découpage init/interpose/utilitaires |

---

## Démarrage rapide

Chaque exemple suit le même modèle :

```bash
cd examples/example-name
neverc make
```

Remplacez le chemin du compilateur si nécessaire :

```bash
neverc make NEVERC=/path/to/neverc
```

Les exemples de pilotes Windows sélectionnent l'architecture avec `ARCH`
(x64 par défaut). L'exemple CET est réservé à x64 — CET est une fonctionnalité
x86 :

```bash
neverc make ARCH=x64        # Build for x64 (default)
neverc make ARCH=arm64      # Build for ARM64
neverc make all-arch        # Build every architecture the example supports
neverc make TESTSIGN=1      # Attach an Authenticode test signature
```

Les exemples Linux prennent en charge la sélection d'architecture :

```bash
neverc make TARGET=aarch64-linux-gnu   # Build for ARM64
neverc make TARGET=x86_64-linux-gnu    # Build for x86_64 (default)
```

Les exemples macOS prennent en charge la sélection d'architecture :

```bash
neverc make TARGET=arm64-apple-macos     # Build for Apple Silicon (default)
neverc make TARGET=x86_64-apple-macos    # Build for Intel
```

Les exemples Android ciblent ARM64 par défaut :

```bash
cd examples/android-elf
neverc make            # Build
neverc make run        # Build + push to device + run via adb
```

---

## Points forts multiplateformes

- **Chaîne d'outils unique** : NeverC gère le préprocessing, la compilation, l'optimisation (auto-LTO) et l'édition de liens en une seule invocation
- **SDK intégré** : Windows SDK/WDK, sysroot Linux (Ubuntu 22.04), sysroot macOS (macOS 14) et sysroot Android (NDK r26c, API 21+) sont intégrés dans `runtime/` — zéro dépendance externe
- **Indépendant de l'hôte** : Compilez depuis macOS (arm64/x86_64), Linux (x86_64/aarch64) ou Windows avec des commandes identiques
- **Multi-cible** : Compilation croisée vers Windows PE (`.sys`/`.exe`/`.dll`), Linux ELF, macOS Mach-O (`.dylib`) et Android ELF depuis n'importe quel hôte
- **Support du débogage** : Passez `-g` pour les infos de débogage DWARF ; inspectez avec `llvm-dwarfdump`
