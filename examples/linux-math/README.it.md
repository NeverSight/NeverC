**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Esempio Linux math + zlib

Funzioni matematiche e compressione zlib. Usa `-lm` e `-lz`.

NeverC include un sysroot Linux (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`.

## Compilazione

```bash
cd examples/linux-math
neverc make          # debug: -g (predefinito alla prima build)
neverc make release  # release: -O2 --strip
neverc make debug    # torna a debug
```

Il Makefile memorizza `PROFILE`, quindi i successivi `neverc make`
mantengono la stessa scelta debug/release. Release usa `--strip` integrato
in NeverC: rimuove metadati di debug e nomi di simboli statici non
necessari, preservando i nomi ABI dinamici/del loader richiesti.
Vedi [Build di rilascio](../../docs/release-builds/README.it.md).


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
