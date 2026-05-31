**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Esempio Linux POSIX API

Programmazione di sistema POSIX cross-compilata su Linux con NeverC: pthreads, mmap, pipe e segnali.

NeverC include un sysroot Linux (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`.

## Compilazione

```bash
cd examples/linux-posix
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Compilazione manuale

```bash
neverc --target=x86_64-linux-gnu -Wall -lpthread -o posix-demo main.c
```

## Esecuzione

```bash
chmod +x posix-demo
./posix-demo
```

## Funzionalità

- **pthreads**: creazione di 4 thread di lavoro
- **mmap**: allocazione pagina di memoria anonima
- **pipe**: invio messaggio tramite pipe Unix
- **signals**: verifica handler `SIGUSR1`
