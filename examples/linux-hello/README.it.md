**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Esempio Linux Hello World

Un programma C minimale cross-compilato in Linux ELF usando NeverC. Compilazione da macOS, Windows o Linux — nessuna toolchain del sistema di destinazione richiesta.

NeverC include un sysroot Linux (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`, cosicché una singola invocazione gestisce preelaborazione, compilazione, ottimizzazione (auto-LTO) e linking tramite il linker integrato.

## Compilazione

Dal repository (target predefinito: `x86_64-linux-gnu`):

```bash
cd examples/linux-hello
neverc make          # debug: -g (predefinito alla prima build)
neverc make release  # release: -O2 --strip
neverc make debug    # torna a debug
```

Il Makefile memorizza `PROFILE`, quindi i successivi `neverc make`
mantengono la stessa scelta debug/release. Release usa `--strip` integrato
in NeverC: rimuove metadati di debug e nomi di simboli statici non
necessari, preservando i nomi ABI dinamici/del loader richiesti.
Vedi [Build di rilascio](../../docs/release-builds/README.it.md).


Compilare per AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

Con una versione standalone di NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

## Compilazione manuale (senza Make)

```bash
neverc --target=x86_64-linux-gnu -Wall -o hello main.c
```

## Esecuzione

Copiare `hello` su una macchina Linux (o container Docker) ed eseguire:

```bash
chmod +x hello
./hello
```

## Funzionalità

- Stampa un saluto con gli argomenti della riga di comando
- Dimostra `printf`, `strncpy`, `strlen`, `atoi` dalla libc integrata
- Trasformazione XOR di una stringa per verificare operazioni base su interi/caratteri
