**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Esempio applicazione macOS

Un eseguibile nativo macOS Mach-O compilato in modo incrociato con NeverC. Dimostra sysctl, uname e le API del kernel Mach per l'introspezione del sistema e dei processi. Compilazione da macOS, Windows o Linux — senza Xcode.

## Compilazione

Dal repository (target predefinito: `arm64-apple-macos`):

```bash
cd examples/macos-app
neverc make
```

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
neverc --target=arm64-apple-macos -Wall -o macos-app main.c
```

## Esecuzione

```bash
./macos-app
```

## Funzionalità

- Interrogazione delle informazioni del kernel tramite `uname`
- Lettura dei dettagli hardware tramite `sysctl` (modello, numero CPU, dimensione memoria, dimensione pagina)
- Informazioni sull'identità del processo (`getpid`, `getppid`, `getuid`)
- Recupero informazioni host Mach (`host_info`) e statistiche memoria del task (`task_info`)
