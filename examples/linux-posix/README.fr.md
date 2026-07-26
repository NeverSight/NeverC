**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Exemple Linux POSIX API

Programmation système POSIX cross-compilée vers Linux avec NeverC : pthreads, mmap, pipe et signaux.

NeverC embarque un sysroot Linux (Ubuntu 22.04, glibc 2.35) dans `runtime/linux/`.

## Compilation

```bash
cd examples/linux-posix
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## Compilation manuelle

```bash
neverc --target=x86_64-linux-gnu -Wall -lpthread -o posix-demo main.c
```

## Exécution

```bash
chmod +x posix-demo
./posix-demo
```

## Fonctionnalités

- **pthreads** : création de 4 threads
- **mmap** : allocation de page mémoire anonyme
- **pipe** : envoi de message via un pipe Unix
- **signals** : vérification du gestionnaire `SIGUSR1`
