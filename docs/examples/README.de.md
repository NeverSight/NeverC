**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md) · [← NeverC-Projekt](../../docs/i18n/README.de.md)

# NeverC Beispiele

Vollständig kompilierbare Beispiele für die plattformübergreifende Kompilierung mit NeverC. Alle Cross-Kompilierung von macOS / Linux — keine Windows-Umgebung erforderlich.

---

## Verfügbare Beispiele

| Beispiel | Beschreibung | Kernfunktionen |
|----------|--------------|---------------|
| [Windows Kerneltreiber](../../examples/windows-driver/README.de.md) | Minimaler WDM-Kerneltreiber | Cross-Kompilierung `.sys` von macOS/Linux, Auto-LTO, integrierter Linker |
| [Windows Treiber + CET](../../examples/windows-driver-cet/README.de.md) | Kerneltreiber mit Intel CET Shadow Stack | CET-kompatibler Kernelcode, `/guard:ehcont` |
| [Windows Treiber + Gleitkomma](../../examples/windows-driver-float/README.de.md) | Kerneltreiber mit Gleitkomma/SIMD | Sichere Gleitkommaoperationen im Kernelmodus |
| [Windows Ring3 EXE](../../examples/windows-exe/README.de.md) | Benutzermodus-Konsolenanwendung | GetSystemInfo, Prozessauflistung, VirtualAlloc |
| [Windows Ring3 DLL](../../examples/windows-dll/README.de.md) | Benutzermodus-DLL | ReadProcessMemory, VirtualAllocEx, Modulauflistung |

### Linux

| Beispiel | Beschreibung | Hauptmerkmale |
|----------|-------------|--------------|
| [Linux Hello World](../../examples/linux-hello/README.de.md) | Minimales C-Programm | Cross-Kompilierung von macOS/Windows |
| [Linux POSIX](../../examples/linux-posix/README.de.md) | POSIX-Systemprogrammierung | pthreads, mmap, pipe, Signale |
| [Linux Statisch](../../examples/linux-static/README.de.md) | Vollständig statisches Binary | `-static` Verlinkung |
| [Linux Netzwerk](../../examples/linux-network/README.de.md) | TCP-Socket-Demo | Client/Server |
| [Linux Math + zlib](../../examples/linux-math/README.de.md) | Math + Kompression | Trigonometrie, zlib, CRC32 |

### macOS

| Beispiel | Beschreibung | Hauptmerkmale |
|----------|-------------|--------------|
| [macOS-Anwendung](../../examples/macos-app/README.de.md) | Native Mach-O-Programmdatei | sysctl, uname, Mach host_info/task_info, Prozessabfrage |
| [macOS Dynamische Bibliothek](../../examples/macos-dylib/README.de.md) | Native `.dylib` Bibliothek | Mach vm_read/vm_write, vm_alloc/vm_dealloc, task_info, XOR |

### Android

| Beispiel | Beschreibung | Hauptmerkmale |
|----------|-------------|--------------|
| [Android ELF](../../examples/android-elf/README.de.md) | Natives ARM64-Binary für gerootete Geräte | Android-Cross-Kompilierung, dlopen/liblog, /proc, Root-Erkennung |
| [Android Shared Library](../../examples/android-so/README.de.md) | Native ARM64 `.so` Bibliothek | Shared Library, mmap RWX, XOR-Verschlüsselung |

### Android Kernel-Module (.ko)

Kein Kernel-Quellbaum erforderlich — NeverC kompiliert gegen die integrierte minimale Runtime. Eine Quelldatei deckt GKI 5.10–6.12 ab.

| Beispiel | Beschreibung | Hauptmerkmale |
|----------|-------------|--------------|
| [Kernel Hello](../../examples/android-kernel-hello/README.de.md) | Minimales `.ko`-Modul | kallsyms-Bootstrap via kprobe, einfachste insmod-Validierung |
| [Kernel-Treibervorlage](../../examples/android-kernel-driver/README.de.md) | Dynamische Symbolauflösung | `kallsyms_lookup_name`, GKI-stabiles ABI, 5.10–6.12 |
| [Kernel Inline Hook](../../examples/android-kernel-inline-hook/README.de.md) | Inline-Hook auf `do_faccessat` | BTI/PAC-sicherer Patch, Context-Hook-Modus, PC-relative Relokation |
| [Kernel Syscall Hook](../../examples/android-kernel-syscall-hook/README.de.md) | Syscall-Tabelle / Inline / Context Hook | `sys_call_table`-Austausch, Inline-Hook, Context-Hook |
| [Kernel Stealth](../../examples/android-kernel-stealth/README.de.md) | Modulverbergung | list/sysfs/proc-Verbergung, Root-Gewährung, SELinux permissive |
| [Kernel Full SDK](../../examples/android-kernel-full/README.de.md) | Vollständige SDK-Integration | Netlink IPC, Hooks, Anmeldedaten, Verbergung, SELinux, VMA, Datei-I/O |
| [Kernel Chardev](../../examples/android-kernel-chardev/README.de.md) | Zeichengerät + ioctl | `misc_register`, ioctl-Dispatch, `/proc` seq_file |
| [Kernel Netlink](../../examples/android-kernel-netlink/README.de.md) | Bidirektionales Netlink IPC | PING/VERSION/ECHO-Befehle, `nvk_nl_open`/`nvk_nl_reply` |

---

## Schnellstart

```bash
cd examples/<beispiel-name>
neverc make
```

Compilerpfad angeben: `neverc make NEVERC=/path/to/neverc`

Alle Beispiele verwenden **neverc** und erzeugen Windows-PE-Binärdateien (`.sys`) über den integrierten Linker.
