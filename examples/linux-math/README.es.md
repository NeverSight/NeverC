**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Ejemplo Linux math + zlib

Funciones de biblioteca matemática y compresión zlib. Usa `-lm` y `-lz`.

NeverC incluye un sysroot de Linux (Ubuntu 22.04, glibc 2.35) en `runtime/linux/`.

## Compilación

```bash
cd examples/linux-math
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## Compilación manual

```bash
neverc --target=x86_64-linux-gnu -Wall -lm -lz -o math-demo main.c
```

## Ejecución

```bash
chmod +x math-demo
./math-demo
```

## Funcionalidades

- Trigonometría: sin/cos/tan
- Funciones especiales: `exp`, `tgamma`, `erf`
- Compresión/descompresión zlib, CRC32
