**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentazione](../README.it.md) · [← Progetto NeverC](../../docs/i18n/README.it.md)

# Esempi NeverC

Esempi completi compilabili che dimostrano le capacità di cross-compilazione di NeverC. Tutti compilano da macOS / Linux — nessun ambiente Windows richiesto.

---

## Esempi disponibili

### Backend server

| Esempio | Descrizione | Funzionalità chiave |
|---------|-------------|-------------------|
| [Server di gioco autoritativo](../../examples/network-authoritative-server/README.it.md) | Backend di gioco cross-platform | Tick fisso 60 Hz, sessioni TCP, input UDP/QUIC, protezione replay |
| [Collettore anti-cheat](../../examples/network-anticheat-collector/README.it.md) | Ingestione telemetria rafforzata | mTLS, NRPC streaming, telemetria HMAC, pipeline di audit limitata |

### Windows

| Esempio | Descrizione | Funzionalità chiave |
|---------|-------------|-------------------|
| [Driver kernel Windows](../../examples/windows-driver/README.it.md) | Driver WDM minimale | Cross-compilazione `.sys` per **x64** (predefinito) e **ARM64**, auto-LTO, linker integrato |
| [Driver Windows + CET](../../examples/windows-driver-cet/README.it.md) | Driver con Intel CET Shadow Stack | Codice kernel compatibile CET (**solo x64**), `/guard:ehcont` |
| [Driver Windows + virgola mobile](../../examples/windows-driver-float/README.it.md) | Driver con virgola mobile/SIMD | Virgola mobile sicura in modalità kernel su **x64** e **ARM64** |
| [Windows Ring3 EXE](../../examples/windows-exe/README.it.md) | App console user-mode | GetSystemInfo, enumerazione processi, VirtualAlloc |
| [Windows Ring3 DLL](../../examples/windows-dll/README.it.md) | DLL user-mode | ReadProcessMemory, VirtualAllocEx, enumerazione moduli |

### Linux

| Esempio | Descrizione | Caratteristiche |
|---------|-------------|----------------|
| [Linux Hello World](../../examples/linux-hello/README.it.md) | Programma C minimale | Cross-compilazione da macOS/Windows |
| [Linux POSIX](../../examples/linux-posix/README.it.md) | Programmazione di sistema POSIX | pthreads, mmap, pipe, segnali |
| [Linux Statico](../../examples/linux-static/README.it.md) | Binario completamente statico | Linking `-static` |
| [Linux Rete](../../examples/linux-network/README.it.md) | Demo socket TCP | Client/server |
| [Linux Math + zlib](../../examples/linux-math/README.it.md) | Math + compressione | Trigonometria, zlib, CRC32 |

### macOS

| Esempio | Descrizione | Caratteristiche |
|---------|-------------|----------------|
| [Applicazione macOS](../../examples/macos-app/README.it.md) | Eseguibile nativo Mach-O | sysctl, uname, Mach host_info/task_info, introspezione processi |
| [Libreria dinamica macOS](../../examples/macos-dylib/README.it.md) | Libreria nativa `.dylib` | Mach vm_read/vm_write, vm_alloc/vm_dealloc, task_info, XOR |

### Android

| Esempio | Descrizione | Caratteristiche |
|---------|-------------|----------------|
| [Android ELF](../../examples/android-elf/README.it.md) | Binario ARM64 nativo per dispositivi rootati | Cross-compilazione Android, dlopen/liblog, /proc, rilevamento root |
| [Libreria condivisa Android](../../examples/android-so/README.it.md) | Libreria `.so` nativa ARM64 | Libreria condivisa, mmap RWX, crittografia XOR |

### Moduli kernel Android (.ko)

Nessun albero sorgente kernel richiesto — NeverC compila contro il runtime minimale integrato. Sorgente unica per GKI 5.10–6.12.

| Esempio | Descrizione | Caratteristiche |
|---------|-------------|----------------|
| [Kernel Hello](../../examples/android-kernel-hello/README.it.md) | Modulo `.ko` minimale | Bootstrap kallsyms via kprobe, validazione insmod minimale |
| [Template driver kernel](../../examples/android-kernel-driver/README.it.md) | Template risoluzione dinamica simboli | `kallsyms_lookup_name`, ABI stabile GKI, 5.10–6.12 |
| [Kernel Inline Interpose](../../examples/android-kernel-inline-interpose/README.it.md) | Interpose inline su `do_faccessat` | Patch sicuro BTI/PAC, modalità context interpose, rilocazione PC-relativa |
| [Kernel Syscall Interpose](../../examples/android-kernel-syscall-interpose/README.it.md) | Tabella syscall / inline / context interpose | Sostituzione `sys_call_table`, interpose inline, context interpose |
| [Kernel Lowvis](../../examples/android-kernel-lowvis/README.it.md) | Gestione visibilità modulo | Visibilità list/sysfs/proc, wrapper credenziali, stato di enforcement SELinux |
| [Kernel Full SDK](../../examples/android-kernel-full/README.it.md) | Integrazione SDK completa | Netlink IPC, interpose, wrapper credenziali, visibilità moduli, controllo policy SELinux, VMA, file I/O |
| [Kernel Chardev](../../examples/android-kernel-chardev/README.it.md) | Dispositivo carattere + ioctl | `misc_register`, dispatch ioctl, `/proc` seq_file |
| [Kernel Netlink](../../examples/android-kernel-netlink/README.it.md) | IPC netlink bidirezionale | Comandi PING/VERSION/ECHO, `nvk_nl_open`/`nvk_nl_reply` |
| [Kernel Probe](../../examples/android-kernel-probe/README.it.md) | Sondare un'istruzione arbitraria | `neverc_krt_probe_register`, contesto completo dei registri, concatenamento per priorità, salto/reindirizzamento |
| [Kernel Multi-File](../../examples/android-kernel-multifile/README.it.md) | Modulo kernel su più file | Un solo `NEVERC_KRT_BOOTSTRAP()`, stato condiviso `weak_odr`, suddivisione init/interpose/utilità |

---

## Avvio rapido

Ogni esempio segue lo stesso schema:

```bash
cd examples/example-name
neverc make
```

Per il driver Makefile vero e proprio, vedi [`neverc build` / `make` →](../build/README.it.md).

Sovrascrivi il percorso del compilatore se necessario:

```bash
neverc make NEVERC=/path/to/neverc
```

Gli esempi di driver Windows selezionano l'architettura con `ARCH` (predefinito:
x64). L'esempio CET è solo per x64 — CET è una funzionalità x86:

```bash
neverc make ARCH=x64        # Build for x64 (default)
neverc make ARCH=arm64      # Build for ARM64
neverc make all-arch        # Build every architecture the example supports
neverc make TESTSIGN=1      # Attach an Authenticode test signature
```

Gli esempi Linux supportano la selezione dell'architettura:

```bash
neverc make TARGET=aarch64-linux-gnu   # Build for ARM64
neverc make TARGET=x86_64-linux-gnu    # Build for x86_64 (default)
```

Gli esempi macOS supportano la selezione dell'architettura:

```bash
neverc make TARGET=arm64-apple-macos     # Build for Apple Silicon (default)
neverc make TARGET=x86_64-apple-macos    # Build for Intel
```

Gli esempi Android puntano a ARM64 per impostazione predefinita:

```bash
cd examples/android-elf
neverc make            # Build
neverc make run        # Build + push to device + run via adb
```

---

## Punti salienti multipiattaforma

- **Toolchain unica**: NeverC gestisce preprocessing, compilazione, ottimizzazione (auto-LTO) e linking in un'unica invocazione
- **SDK integrato**: Windows SDK/WDK, sysroot Linux (Ubuntu 22.04), sysroot macOS (macOS 14) e sysroot Android (NDK r26c, API 21+) sono integrati in `runtime/` — zero dipendenze esterne
- **Indipendente dall'host**: Compila da macOS (arm64/x86_64), Linux (x86_64/aarch64) o Windows con comandi identici
- **Multi-target**: Compilazione incrociata verso Windows PE (`.sys`/`.exe`/`.dll`), Linux ELF, macOS Mach-O (`.dylib`) e Android ELF da qualsiasi host
- **Supporto debug**: Passa `-g` per le informazioni di debug DWARF; ispeziona con `llvm-dwarfdump`
