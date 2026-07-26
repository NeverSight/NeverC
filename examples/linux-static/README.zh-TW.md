**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Linux 全靜態連結範例

使用 NeverC 建置的自包含全靜態連結 Linux 可執行檔。輸出二進位零執行階段相依性。

NeverC 在 `runtime/linux/` 中內建了 Linux sysroot（Ubuntu 22.04，glibc 2.35），單次呼叫即可完成預處理、編譯、最佳化（auto-LTO）以及透過內建連結器進行連結。

## 建置

```bash
cd examples/linux-static
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## 手動建置

```bash
neverc --target=x86_64-linux-gnu -Wall -static -lm -o static-demo main.c
```

## 執行

```bash
chmod +x static-demo
./static-demo
```

## 功能說明

- 系統資訊（指標大小、架構）
- 數學函數：`sqrt`、`sin`、`pow`、`log`
- 字串操作：`snprintf`、`strdup`
- 動態記憶體：`malloc`/`free`
