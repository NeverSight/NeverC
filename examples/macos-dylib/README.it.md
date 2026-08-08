**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Esempio libreria dinamica macOS

Una libreria dinamica nativa macOS `.dylib` compilata in modo incrociato con NeverC. Incapsula le interfacce del kernel Mach per l'introspezione dei task e le operazioni di memoria virtuale — progettata per la ricerca sulla sicurezza. Compilazione da macOS, Windows o Linux — senza Xcode.

## Compilazione

Dal repository (target predefinito: `arm64-apple-macos`):

```bash
cd examples/macos-dylib
neverc make          # debug: -g (predefinito alla prima build)
neverc make release  # release: -O2 --strip
neverc make debug    # torna a debug
```

Il Makefile memorizza `PROFILE`, quindi i successivi `neverc make`
mantengono la stessa scelta debug/release. Release usa `--strip` integrato
in NeverC: rimuove metadati di debug e nomi di simboli statici non
necessari, preservando i nomi ABI dinamici/del loader richiesti.
Vedi [Build di rilascio](../../docs/release-builds/README.it.md).


Compilare per Intel:

```bash
neverc make TARGET=x86_64-apple-macos
```

Con una versione standalone di NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

## Compilazione manuale (senza Make)

```bash
neverc --target=arm64-apple-macos -Wall -dynamiclib -o libneverc.dylib lib.c
```

## Funzionalità

- Esporta il wrapper `nc_task_basic_info` per le query Mach `task_info`
- Fornisce `nc_vm_read`/`nc_vm_write` per lettura/scrittura di memoria virtuale Mach
- `nc_vm_alloc`/`nc_vm_dealloc` per allocazione e deallocazione di memoria VM Mach
- Funzione helper di crittografia XOR e query PID/task
