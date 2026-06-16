**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentazione](../README.it.md) · [← Progetto NeverC](../../docs/i18n/README.it.md)

# Esempi NeverC

Esempi completi compilabili che dimostrano le capacità di cross-compilazione di NeverC. Tutti compilano da macOS / Linux — nessun ambiente Windows richiesto.

---

## Esempi disponibili

| Esempio | Descrizione | Funzionalità chiave |
|---------|-------------|-------------------|
| [Driver kernel Windows](../../examples/windows-driver/README.it.md) | Driver WDM minimale | Cross-compilazione `.sys` da macOS/Linux, auto-LTO, linker integrato |
| [Driver Windows + CET](../../examples/windows-driver-cet/README.it.md) | Driver con Intel CET Shadow Stack | Codice kernel compatibile CET, `/guard:ehcont` |
| [Driver Windows + virgola mobile](../../examples/windows-driver-float/README.it.md) | Driver con virgola mobile/SIMD | Virgola mobile sicura in modalità kernel |
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
| [Kernel Inline Hook](../../examples/android-kernel-inline-hook/README.it.md) | Hook inline su `do_faccessat` | Patch sicuro BTI/PAC, modalità context hook, rilocazione PC-relativa |
| [Kernel Syscall Hook](../../examples/android-kernel-syscall-hook/README.it.md) | Tabella syscall / inline / context hook | Sostituzione `sys_call_table`, hook inline, context hook |
| [Kernel Stealth](../../examples/android-kernel-stealth/README.it.md) | Occultamento modulo | Occultamento list/sysfs/proc, concessione root, SELinux permissive |
| [Kernel Full SDK](../../examples/android-kernel-full/README.it.md) | Integrazione SDK completa | Netlink IPC, hook, credenziali, occultamento, SELinux, VMA, file I/O |
| [Kernel Chardev](../../examples/android-kernel-chardev/README.it.md) | Dispositivo carattere + ioctl | `misc_register`, dispatch ioctl, `/proc` seq_file |
| [Kernel Netlink](../../examples/android-kernel-netlink/README.it.md) | IPC netlink bidirezionale | Comandi PING/VERSION/ECHO, `nvk_nl_open`/`nvk_nl_reply` |

---

## Avvio rapido

```bash
cd examples/<nome-esempio>
neverc make
```

Specificare percorso compilatore: `neverc make NEVERC=/path/to/neverc`

Tutti gli esempi usano **neverc** e producono binari Windows PE (`.sys`) tramite il linker integrato.
