**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux 數學 + zlib 範例

演示數學庫函數和 zlib 壓縮。使用 `-lm` 和 `-lz`。

NeverC 在 `runtime/linux/` 中內建了 Linux sysroot（Ubuntu 22.04，glibc 2.35），單次呼叫即可完成預處理、編譯、最佳化（auto-LTO）以及透過內建連結器進行連結。

## 建置

```bash
cd examples/linux-math
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## 手動建置

```bash
neverc --target=x86_64-linux-gnu -Wall -lm -lz -o math-demo main.c
```

## 執行

```bash
chmod +x math-demo
./math-demo
```

## 功能說明

- 三角函數：sin/cos/tan
- 特殊函數：`exp`、`tgamma`、`erf`
- zlib 壓縮 + 解壓縮 + CRC32
