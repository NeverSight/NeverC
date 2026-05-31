**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Exemple Linux math + zlib

Fonctions mathématiques et compression zlib. Utilise `-lm` et `-lz`.

NeverC embarque un sysroot Linux (Ubuntu 22.04, glibc 2.35) dans `runtime/linux/`.

## Compilation

```bash
cd examples/linux-math
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Compilation manuelle

```bash
neverc --target=x86_64-linux-gnu -Wall -lm -lz -o math-demo main.c
```

## Exécution

```bash
chmod +x math-demo
./math-demo
```

## Fonctionnalités

- Trigonométrie : sin/cos/tan
- Fonctions spéciales : `exp`, `tgamma`, `erf`
- Compression/décompression zlib, CRC32
