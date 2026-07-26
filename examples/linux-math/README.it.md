**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Esempio Linux math + zlib

Funzioni matematiche e compressione zlib. Usa `-lm` e `-lz`.

NeverC include un sysroot Linux (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`.

## Compilazione

```bash
cd examples/linux-math
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## Compilazione manuale

```bash
neverc --target=x86_64-linux-gnu -Wall -lm -lz -o math-demo main.c
```

## Esecuzione

```bash
chmod +x math-demo
./math-demo
```

## Funzionalità

- Trigonometria: sin/cos/tan
- Funzioni speciali: `exp`, `tgamma`, `erf`
- Compressione/decompressione zlib, CRC32
