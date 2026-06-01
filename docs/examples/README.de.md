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

### Android

| Beispiel | Beschreibung | Hauptmerkmale |
|----------|-------------|--------------|
| [Android ELF](../../examples/android-elf/README.de.md) | Natives ARM64-Binary für gerootete Geräte | Android-Cross-Kompilierung, dlopen/liblog, /proc, Root-Erkennung |
| [Android Shared Library](../../examples/android-so/README.de.md) | Native ARM64 `.so` Bibliothek | Shared Library, mmap RWX, XOR-Verschlüsselung |

---

## Schnellstart

```bash
cd examples/<beispiel-name>
neverc make
```

Compilerpfad angeben: `make NEVERC=/path/to/neverc`

Alle Beispiele verwenden **neverc** und erzeugen Windows-PE-Binärdateien (`.sys`) über den integrierten Linker.
