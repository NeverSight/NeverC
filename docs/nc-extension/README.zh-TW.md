**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 文件](../README.zh-TW.md)

# `.nc` 檔案副檔名

## 概述

NeverC 將 `.nc` 作為原生原始檔副檔名。當編譯器偵測到 `.nc` 輸入檔案時，會**自動啟用**所有 NeverC 語言擴充 — 無需額外旗標。

## 自動啟用的功能

| 旗標 | 效果 |
|------|------|
| `-fneverc-types` | Rust 風格的整數別名（`u8`、`u16`、`u32`、`u64`、`i8`、`i16`、`i32`、`i64`、`usize`、`isize`） |
| `-fbuiltin-string` | 內建 `string` 值型別，具備自動記憶體管理、dot-call 語法和 UTF-8 支援 |

## 使用方法

只需將原始檔命名為 `.nc` 副檔名：

```bash
# 自動啟用 — 無需額外旗標
neverc hello.nc -o hello

# 等價於：
neverc -fneverc-types -fbuiltin-string hello.c -o hello
```

```c
// hello.nc
#include <stdio.h>

int main(void) {
    string greeting = "你好，NeverC！";
    printf("%s (len=%zu)\n", greeting.c_str(), greeting.len);

    u32 x = 42;
    i64 y = -100;

    string msg = greeting + " x=%u, y=%lld".format(x, y);
    printf("%s\n", msg.c_str());
    return 0;
}
```

## 運作原理

偵測在編譯器管線的兩個層級進行：

### 1. Driver / Toolchain 層

Driver 在建構編譯器呼叫之前檢查每個輸入檔案的副檔名。對於 `.nc` 檔案，`-fneverc-types` 和 `-fbuiltin-string` 被無條件注入命令列 — 使用者不需要手動傳遞。

對於 `.c` 檔案，這些旗標保持可選：按需明確加上所需旗標（`-fneverc-types`、`-fbuiltin-string`）。

### 2. CompilerInvocation 層

作為安全兜底，前端在解析呼叫時也會檢查輸入檔案副檔名。如果任何輸入具有 `.nc` 副檔名，`LangOpts.NeverCTypes` 和 `LangOpts.BuiltinString` 將被設為 `1`，確保即使繞過 Driver 層（例如直接呼叫 `-cc1`）功能也處於活動狀態。

## 相容性

- `.nc` 檔案被視為 C 原始碼 — 語言仍然是 C（預設 C23），不是新語言
- 所有標準 C 旗標（`-std=c11`、`-O2`、`-g`、`-Wall` 等）的行為完全相同
- `-fshellcode` 與 `.nc` 自然結合：shellcode 模式本身已啟用 `string`，`.nc` 確保 `neverc-types` 也處於活動狀態
- 交叉編譯（`-target aarch64-linux-gnu` 等）以相同方式運作
- `.c` 檔案不受影響 — 除非明確傳遞旗標，否則行為與之前完全相同

## 何時使用 `.nc` vs `.c`

| 情境 | 建議 |
|------|------|
| 使用 `string` 和 Rust 風格型別的新 NeverC 專案 | 使用 `.nc` |
| 希望保持與其他編譯器相容的現有 C 程式碼庫 | 使用 `.c` + 明確旗標 |
| Shellcode 專案 | 兩者均可 — `-fshellcode` 無論如何都會啟用 `string` |
| 混合程式碼庫 | NeverC 專用檔案使用 `.nc`，可攜式程式碼使用 `.c` |
