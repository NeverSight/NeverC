**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux 網路 Socket 範例

使用 NeverC 交叉編譯的 TCP 客戶端/伺服器演示。

NeverC 在 `runtime/linux/` 中內建了 Linux sysroot（Ubuntu 22.04，glibc 2.35），單次呼叫即可完成預處理、編譯、最佳化（auto-LTO）以及透過內建連結器進行連結。

## 建置

```bash
cd examples/linux-network
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## 手動建置

```bash
neverc --target=x86_64-linux-gnu -Wall -o network-demo main.c
```

## 執行

```bash
chmod +x network-demo
./network-demo
```

## 功能說明

- TCP 伺服器（127.0.0.1）
- 客戶端連線
- 傳送 3 條訊息
- 演示 `socket`、`bind`、`listen`、`accept`
