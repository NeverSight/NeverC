**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 內建執行時系統](../README.zh-TW.md)

# 內建 mimalloc 配置器

## 概述

NeverC 可以將 [mimalloc](https://github.com/microsoft/mimalloc) — 微軟的高效能通用記憶體配置器 — 透過 LLVM bitcode 合併直接嵌入編譯產物中。啟用後，`malloc`、`free`、`calloc` 和 `realloc` 在編譯時被透明替換為 mimalloc 的實作。

**啟用方式：**

```bash
neverc -fbuiltin-mimalloc main.c -o main
```

---

## 平台支援

| 平台 | Triple | 狀態 |
|------|--------|------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | 支援 |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | 支援 |
| Android | `aarch64-linux-android` | 支援 |
| macOS x86_64 | `x86_64-apple-macosx` | 支援 |
| macOS AArch64 | `arm64-apple-macosx` | 支援 |
| iOS | `arm64-apple-ios` | 支援 |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | 支援 |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | 支援 |

---

## 自動抑制

| 旗標 / 模式 | 原因 |
|-------------|------|
| `-fno-builtin` | 無 CRT 函式覆蓋場景 |
| `-mkernel` | 核心模式無使用者空間堆積 |
| `-fshellcode-mode` | shellcode 無堆積 |
| `-ffreestanding` | 無 libc 可覆蓋 |

---

## 引導建置流程

```bash
ninja neverc                         # 階段 1：空 bitcode 佔位符
ninja neverc-bootstrap-mimalloc-bc   # 階段 2：編譯按 OS 的 bitcode
ninja neverc                         # 階段 3：嵌入真實 bitcode
```

---

## 編譯器旗標參考

| 旗標 | 描述 |
|------|------|
| `-fbuiltin-mimalloc` | 啟用 mimalloc 覆蓋注入（hosted 建置預設開啟） |
| `-fno-builtin-mimalloc` | 明確停用 mimalloc 注入 |

| 巨集 | 值 | 何時定義 |
|------|----|---------| 
| `__NEVERC_MIMALLOC__` | `1` | 當 `-fbuiltin-mimalloc` 啟動時 |
