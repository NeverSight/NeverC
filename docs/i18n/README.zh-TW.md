**語言**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

# NeverC

**基於 LLVM 的安全研究向 C23 編譯器**

整合連結器 · Shellcode 流水線 · 內建執行時（`string` · `mimalloc`）

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#特色)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#交叉編譯到-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#特色)

[文件索引](../README.zh-TW.md) · [Shellcode 指南](../shellcode-compiler/README.zh-TW.md) · [內建執行時](../builtins/README.zh-TW.md)

</div>

---

> **說明：** GitHub 儲存庫首頁固定展示英文 `README.md`，不會依據瀏覽器語言自動切換。請用上方語言連結進入對應版本；進入 [文件](../README.zh-TW.md) 或 [shellcode 指南](../shellcode-compiler/README.zh-TW.md) 後，請繼續透過頁內語言列與導覽列保持同一語言。

## 概述

NeverC 將標準 C 編譯為宿主二進位、獨立可執行檔以及位置無關 shellcode——全部來自同一工具鏈。目標架構為 **x86_64** 與 **AArch64**（僅小端序）。

## 特色

- **[Shellcode 編譯器](../shellcode-compiler/README.zh-TW.md)** — 多階段 IR/MIR 流水線、跨平台提取、匯入/系統呼叫降階、核心模式、壞位元組稽核與外掛架構
- **整合連結器** — 單一二進位內完成 COFF、ELF、Mach-O 連結，無需外部 `ld` 或 `link.exe`
- **交叉編譯** — 在 macOS/Linux 上建置 Windows PE，支援捆綁 MSVC SDK
- **[內建執行時](../builtins/README.zh-TW.md)** — 嵌入編譯器的 LLVM bitcode 執行時：[`string`](../builtins/string/README.zh-TW.md)（值語意字串，自動記憶體管理）和 [`mimalloc`](../builtins/mimalloc/README.zh-TW.md)（透明高效能配置器覆蓋）
- **精簡 LLVM 建置** — 僅 x86_64 / AArch64 後端；剝離 C++/ObjC/OpenMP 等路徑

## 快速範例

```c
#include <stdio.h>

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());
    return 0;
}
```

> **說明：** 內建 **`string`** 在一般宿主程式裡需加 **`-fbuiltin-string`**；**`-fshellcode`** 會自動啟用。

```bash
# macOS arm64 / x86_64
neverc -fshellcode -target arm64-apple-macos hello.c -o hello.bin
neverc -fshellcode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fshellcode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fshellcode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fshellcode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fshellcode -target aarch64-linux-android hello.c -o hello.bin
neverc -fshellcode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fshellcode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

詳細設計說明、平台矩陣、CLI 參考與範例見 **[文件索引](../README.zh-TW.md)**。

## 建置

### 需求

- CMake 3.20+
- Ninja
- 支援 C++17 的宿主編譯器（GCC、Clang 或 MSVC）

### 設定

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
```

### 編譯

```bash
cmake --build build-neverc --target neverc
```

若存在 `ccache` / `sccache` 將自動偵測並啟用。

### 測試

```bash
cmake --build build-neverc --target neverc-tests
ctest --test-dir build-neverc --output-on-failure
```

### 驗證

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## 交叉編譯到 Windows

將 [xwin](https://github.com/Jake-Shadle/xwin) SDK splat 置於 `build-neverc/sdk/msvc/`。

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Windows shellcode（`-fshellcode`、PEB 匯入解析等）詳見 [shellcode 編譯器文件](../shellcode-compiler/README.zh-TW.md)。

## 貢獻

預設開發分支為 **`dev`**。開始工作前請 clone 並 checkout 該分支；請向 `dev` 提交 Pull Request。

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## 授權條款

[AGPL-3.0](../../LICENSE)

LLVM 元件保留 [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT) 授權。
