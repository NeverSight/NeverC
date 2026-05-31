**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Пример Linux math + zlib

Математические функции и сжатие zlib. Использует `-lm` и `-lz`.

NeverC включает Linux sysroot (Ubuntu 22.04, glibc 2.35) в `runtime/linux/`.

## Сборка

```bash
cd examples/linux-math
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Ручная сборка

```bash
neverc --target=x86_64-linux-gnu -O2 -Wall -lm -lz -o math-demo main.c
```

## Запуск

```bash
chmod +x math-demo
./math-demo
```

## Возможности

- Тригонометрия: sin/cos/tan
- Специальные функции: `exp`, `tgamma`, `erf`
- Сжатие/распаковка zlib, CRC32
