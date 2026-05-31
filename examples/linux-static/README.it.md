**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Esempio Linux binario statico

Eseguibile Linux completamente statico costruito con NeverC. Zero dipendenze runtime.

NeverC include un sysroot Linux (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`.

## Compilazione

```bash
cd examples/linux-static
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Compilazione manuale

```bash
neverc --target=x86_64-linux-gnu -O2 -Wall -static -lm -o static-demo main.c
```

## Esecuzione

```bash
chmod +x static-demo
./static-demo
```

## Funzionalità

- Informazioni di sistema
- Funzioni matematiche: `sqrt`, `sin`, `pow`, `log`
- Operazioni su stringhe, gestione memoria dinamica
