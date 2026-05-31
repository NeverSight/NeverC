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

### Linux

| Esempio | Descrizione | Caratteristiche |
|---------|-------------|----------------|
| [Linux Hello World](../../examples/linux-hello/README.it.md) | Programma C minimale | Cross-compilazione da macOS/Windows |
| [Linux POSIX](../../examples/linux-posix/README.it.md) | Programmazione di sistema POSIX | pthreads, mmap, pipe, segnali |
| [Linux Statico](../../examples/linux-static/README.it.md) | Binario completamente statico | Linking `-static` |
| [Linux Rete](../../examples/linux-network/README.it.md) | Demo socket TCP | Client/server |
| [Linux Math + zlib](../../examples/linux-math/README.it.md) | Math + compressione | Trigonometria, zlib, CRC32 |

### Android

| Esempio | Descrizione | Caratteristiche |
|---------|-------------|----------------|
| [Android ELF](../../examples/android-elf/README.it.md) | Binario ARM64 nativo per dispositivi rootati | Cross-compilazione Android, dlopen/liblog, /proc, rilevamento root |

---

## Avvio rapido

```bash
cd examples/<nome-esempio>
make
```

Specificare percorso compilatore: `make NEVERC=/path/to/neverc`

Tutti gli esempi usano **neverc** e producono binari Windows PE (`.sys`) tramite il linker integrato.
