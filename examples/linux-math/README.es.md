**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Ejemplo Linux math + zlib

Funciones de biblioteca matemática y compresión zlib. Usa `-lm` y `-lz`.

NeverC incluye un sysroot de Linux (Ubuntu 22.04, glibc 2.35) en `runtime/linux/`.

## Compilación

```bash
cd examples/linux-math
neverc make          # debug: -g (predeterminado en la primera compilación)
neverc make release  # release: -O2 --strip
neverc make debug    # volver a debug
```

El Makefile guarda `PROFILE`, así que los siguientes `neverc make`
conservan la misma selección debug/release. Release usa el `--strip`
integrado de NeverC: quita metadatos de depuración y nombres de símbolos
estáticos innecesarios, y conserva los nombres ABI dinámicos/del cargador
necesarios. Véase [Compilaciones de publicación](../../docs/release-builds/README.es.md).


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
