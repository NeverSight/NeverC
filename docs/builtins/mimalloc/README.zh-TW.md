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

## 用法

### 基本用法

```bash
neverc -fbuiltin-mimalloc hello.c -o hello
```

### 與內建字串組合

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

### 停用

```bash
neverc -fno-builtin-mimalloc main.c -o main
```

### 編譯時偵測

```c
#ifdef __NEVERC_MIMALLOC__
    printf("使用 mimalloc 配置器\n");
#endif
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

## 架構

mimalloc 以 LLVM bitcode 形式嵌入編譯器二進位檔。使用者編譯時，Module Pass 在最佳化管線前將 bitcode 合併到使用者 IR。按 OS 分別編譯（Linux `mmap`、macOS `vm_allocate`、Windows `VirtualAlloc`），合併時根據 target triple 選擇。使用 **whole-archive** 語義——所有函式都被連結。覆蓋入口保持外部連結，內部輔助函式內部化。

---

## 檔案結構

```
neverc/
├── include/neverc/Foundation/Builtin/BuiltinMimalloc.h
├── lib/Foundation/Builtin/
│   ├── BuiltinMimalloc.cpp / gen_mimalloc_source.py / bin2c.py
├── lib/Emit/Backend/
│   ├── MimallocRuntimeLinker.{h,cpp} / BackendUtil.cpp
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPreprocessor.cpp
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
