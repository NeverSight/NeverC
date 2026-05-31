**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux Math + zlib Beispiel

Mathematische Bibliotheksfunktionen und zlib-Kompression. Verwendet `-lm` und `-lz`.

NeverC bündelt ein Linux-Sysroot (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`.

## Erstellung

```bash
cd examples/linux-math
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Manuelle Erstellung

```bash
neverc --target=x86_64-linux-gnu -O2 -Wall -lm -lz -o math-demo main.c
```

## Ausführung

```bash
chmod +x math-demo
./math-demo
```

## Funktionen

- Trigonometrie: sin/cos/tan
- Spezielle Funktionen: `exp`, `tgamma`, `erf`
- zlib Kompression/Dekompression, CRC32
