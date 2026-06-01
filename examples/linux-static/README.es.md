**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Ejemplo Linux binario estático

Ejecutable Linux estático autónomo construido con NeverC. Cero dependencias de ejecución.

NeverC incluye un sysroot de Linux (Ubuntu 22.04, glibc 2.35) en `runtime/linux/`.

## Compilación

```bash
cd examples/linux-static
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## Compilación manual

```bash
neverc --target=x86_64-linux-gnu -Wall -static -lm -o static-demo main.c
```

## Ejecución

```bash
chmod +x static-demo
./static-demo
```

## Funcionalidades

- Información del sistema
- Funciones matemáticas: `sqrt`, `sin`, `pow`, `log`
- Operaciones de cadenas, gestión de memoria dinámica
