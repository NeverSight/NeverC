**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md) · [← NeverC-Projekt](../../docs/i18n/README.de.md)

# NeverC Beispiele

Vollständig kompilierbare Beispiele für die plattformübergreifende Kompilierung mit NeverC. Alle Cross-Kompilierung von macOS / Linux — keine Windows-Umgebung erforderlich.

---

## Verfügbare Beispiele

### Windows

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
| [Kernel Inline Interpose](../../examples/android-kernel-inline-interpose/README.de.md) | Inline-Interpose auf `do_faccessat` | BTI/PAC-sicherer Patch, Context-Interpose-Modus, PC-relative Relokation |
| [Kernel Syscall Interpose](../../examples/android-kernel-syscall-interpose/README.de.md) | Syscall-Tabelle / Inline / Context Interpose | `sys_call_table`-Austausch, Inline-Interpose, Context-Interpose |
| [Kernel Lowvis](../../examples/android-kernel-lowvis/README.de.md) | Modul-Sichtbarkeitsverwaltung | list/sysfs/proc-Sichtbarkeit, Credential-Wrapper, SELinux-Enforcement-State |
| [Kernel Full SDK](../../examples/android-kernel-full/README.de.md) | Vollständige SDK-Integration | Netlink IPC, Interposes, Credential-Wrapper, Modul-Sichtbarkeit, SELinux-Richtliniensteuerung, VMA, Datei-I/O |
| [Kernel Chardev](../../examples/android-kernel-chardev/README.de.md) | Zeichengerät + ioctl | `misc_register`, ioctl-Dispatch, `/proc` seq_file |
| [Kernel Netlink](../../examples/android-kernel-netlink/README.de.md) | Bidirektionales Netlink IPC | PING/VERSION/ECHO-Befehle, `nvk_nl_open`/`nvk_nl_reply` |
| [Kernel Probe](../../examples/android-kernel-probe/README.de.md) | Beliebige Instruktion sondieren | `neverc_krt_probe_register`, vollständiger Registerkontext, Prioritätsverkettung, Überspringen/Umleiten |
| [Kernel Mehrdatei-Modul](../../examples/android-kernel-multifile/README.de.md) | Kernelmodul aus mehreren Dateien | Nur ein `NEVERC_KRT_BOOTSTRAP()`, `weak_odr`-Zustand geteilt, Aufteilung in Init/Interpose/Hilfsdateien |

---

## Schnellstart

Jedes Beispiel folgt demselben Muster:

```bash
cd examples/beispiel-name
neverc make
```

Bei Bedarf den Compilerpfad überschreiben:

```bash
neverc make NEVERC=/path/to/neverc
```

Linux-Beispiele unterstützen die Architekturauswahl:

```bash
neverc make TARGET=aarch64-linux-gnu   # Build for ARM64
neverc make TARGET=x86_64-linux-gnu    # Build for x86_64 (default)
```

macOS-Beispiele unterstützen die Architekturauswahl:

```bash
neverc make TARGET=arm64-apple-macos     # Build for Apple Silicon (default)
neverc make TARGET=x86_64-apple-macos    # Build for Intel
```

Android-Beispiele zielen standardmäßig auf ARM64:

```bash
cd examples/android-elf
neverc make            # Build
neverc make run        # Build + push to device + run via adb
```

---

## Plattformübergreifende Highlights

- **Einzige Toolchain**: NeverC übernimmt Präprozessierung, Kompilierung, Optimierung (Auto-LTO) und Linken in einem Aufruf
- **Integriertes SDK**: Windows SDK/WDK, Linux-Sysroot (Ubuntu 22.04), macOS-Sysroot (macOS 14) und Android-Sysroot (NDK r26c, API 21+) sind in `runtime/` integriert — keine externen Abhängigkeiten
- **Host-unabhängig**: Build von macOS (arm64/x86_64), Linux (x86_64/aarch64) oder Windows mit identischen Befehlen
- **Multi-Target**: Cross-Kompilierung zu Windows PE (`.sys`/`.exe`/`.dll`), Linux ELF, macOS Mach-O (`.dylib`) und Android ELF von jedem Host
- **Debug-Unterstützung**: `-g` für DWARF-Debug-Infos übergeben; mit `llvm-dwarfdump` inspizieren
