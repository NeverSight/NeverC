**語言**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../assets/neverc-logo-dark.svg">
  <img src="../assets/neverc-logo-light.svg" width="72" alt="NeverC">
</picture>

# NeverC

**AI 友好的安全研究 C23 編譯器，基於 LLVM 建構**

整合連結器 · DynCode 流水線 · 內建執行時（`string` · `mimalloc` · `xorstr` · `strhash`）

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#特色)
![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#特色)

[文件索引](../README.zh-TW.md) · [DynCode 指南](../dyncode-compiler/README.zh-TW.md) · [內建執行時](../builtins/README.zh-TW.md) · [外掛 API](../plugin-api/README.zh-TW.md) · [路線圖](../roadmap/README.zh-TW.md)

</div>

---

> **說明：** GitHub 儲存庫首頁固定展示英文 `README.md`，不會依據瀏覽器語言自動切換。請用上方語言連結進入對應版本；進入 [文件](../README.zh-TW.md) 或 [dyncode 指南](../dyncode-compiler/README.zh-TW.md) 後，請繼續透過頁內語言列與導覽列保持同一語言。

## 概述

NeverC 將標準 C 編譯為宿主二進位、獨立可執行檔以及位置無關 dyncode——全部來自同一工具鏈。目標架構為 **x86_64** 與 **AArch64**（僅小端序）。未來版本將新增 **EVM**（以太坊智慧合約）和 **Solana eBPF**（鏈上程式）編譯目標。

## 為什麼選擇 NeverC？

C 已經是最簡單的系統程式語言。NeverC 讓它更簡單：

- **純 C23，僅此而已** — 沒有模板、沒有 RAII、沒有運算子多載、沒有隱式控制流。你讀到的就是機器執行的。
- **內建 `string`** — 值語意字串，支援 `+`、`==`、`.starts_with()` 與自動釋放——不需要 C++。
- **無例外處理** — 錯誤處理始終顯式。沒有堆疊展開、沒有效能意外。
- **單一二進位** — 編譯器 + 連結器 + 執行時打包成一個可執行檔，零外部相依性。
- **LLM 友好** — 極簡語法與確定性語意，讓 AI 生成的 NeverC 程式碼比 C++ 更容易編譯正確。
- **真正的跨平台編譯** — 在 macOS 或 Linux 上直接編譯 Windows PE、Linux ELF、macOS Mach-O、Android ELF 和 dyncode——不需要虛擬機、不需要雙系統、不需要找 SDK。各平台 SDK 已內建在編譯器裡。
- **零門檻可擴展** — 單個 C 標頭檔、130 個具名編譯階段，就能寫出[編譯器外掛](../plugin-api/README.zh-TW.md)，介入從 IR 最佳化到最終產物輸出的任何階段——不需要懂 LLVM。
- **安全研究開箱即用** — DynCode 編譯、編譯期字串加密、跨平台 PE 生成全部原生整合在編譯器中——不需要靠外部腳本拼湊。

## 特色

- **[DynCode 編譯器](../dyncode-compiler/README.zh-TW.md)** — 多階段 IR/MIR 流水線、跨平台提取、匯入/系統呼叫降階、核心模式、壞位元組稽核與外掛架構
- **整合連結器** — 單一二進位內完成 COFF、ELF、Mach-O 連結，無需外部 `ld` 或 `link.exe`
- **交叉編譯** — 從任意宿主建置 Windows PE、Linux ELF、macOS Mach-O 和 Android ELF，內建各平台 SDK
- **[內建執行時](../builtins/README.zh-TW.md)** — 嵌入編譯器的 LLVM bitcode 執行時：[`string`](../builtins/string/README.zh-TW.md)（值語意字串，自動記憶體管理）、[`mimalloc`](../builtins/mimalloc/README.zh-TW.md)（透明高效能配置器覆蓋）、[`xorstr`](../builtins/xorstr/README.zh-TW.md)（編譯期字串加密，反特徵碼解密）和 [`strhash`](../builtins/strhash/README.zh-TW.md)（編譯期字串雜湊，與執行時演算法一致）
- **[外掛 API](../plugin-api/README.zh-TW.md)** — 純 C ABI 的樹外外掛介面；單一標頭檔 SDK，零 LLVM/CRT 相依性，涵蓋驅動、預處理、AST、IR、MIR、MC、目的檔、連結、LTO、dyncode 各階段
- **[`.nc` 副檔名](../nc-extension/README.zh-TW.md)** — 使用 `.nc` 檔案副檔名自動啟用所有 NeverC 功能（`string`、Rust 風格整數型別），無需額外旗標
- **精簡 LLVM 建置** — 僅 x86_64 / AArch64 後端；剝離 C++/ObjC/OpenMP 等路徑

## 快速範例

```c
#include <stdio.h>

typedef struct { string user; string pass; } creds;

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());

    // 編譯期加密 — `strings ./bin` 搜不到這些字面量
    creds login = {.user = "admin".encrypt(), .pass = "s3cret".encrypt()};
    string paths[] = {"/api/v1".encrypt(), "/api/v2".encrypt()};

    // 零分配解密比較（明文不會完整出現在記憶體裡）
    if (login.user == "admin".encrypt() && login.pass == "s3cret".encrypt()) {
        for (int i = 0; i < 2; i++)
            if (msg.starts_with(paths[i]))
                printf("route matched: %s\n", paths[i].c_str());
    }
    return 0;
}
```

> **說明：** 內建 **`string`** 在 `.c` 檔案中需加 **`-fbuiltin-string`**。使用 [**`.nc` 檔案**](../nc-extension/README.zh-TW.md) 或 **`-fdyncode`** 模式時自動啟用。

```bash
# macOS arm64 / x86_64
neverc -fdyncode -target arm64-apple-macos hello.c -o hello.bin
neverc -fdyncode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fdyncode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fdyncode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fdyncode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fdyncode -target aarch64-linux-android hello.c -o hello.bin
neverc -fdyncode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fdyncode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fdyncode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

詳細設計說明、平台矩陣、CLI 參考與範例見 **[文件索引](../README.zh-TW.md)**。更多完整可建置範例見 **[examples](../examples/README.zh-TW.md)**。

## 建置

建置需求、建置命令、macOS 預編譯產物、Windows 交叉編譯、PATH 設定和環境配置詳見 **[本地開發](../local-dev/README.zh-TW.md)**。

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
