**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Ejemplo Linux POSIX API

Programación de sistemas POSIX compilada cruzadamente a Linux con NeverC: pthreads, mmap, pipe y señales.

NeverC incluye un sysroot de Linux (Ubuntu 22.04, glibc 2.35) en `runtime/linux/`.

## Compilación

```bash
cd examples/linux-posix
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Compilación manual

```bash
neverc --target=x86_64-linux-gnu -Wall -lpthread -o posix-demo main.c
```

## Ejecución

```bash
chmod +x posix-demo
./posix-demo
```

## Funcionalidades

- **pthreads**: crear 4 hilos de trabajo
- **mmap**: asignar página de memoria anónima
- **pipe**: enviar mensaje a través de pipe Unix
- **signals**: verificar manejador de `SIGUSR1`
