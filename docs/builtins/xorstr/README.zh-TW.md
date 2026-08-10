**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 內建運行時系統](../README.zh-TW.md)

# 編譯期字串加密 (`xorstr`)

## 概述

NeverC 提供兩層編譯期字串加密機制，專為安全場景設計——確保 API 名稱、登錄檔路徑、除錯訊息等敏感字串在編譯後的二進位檔案中不以明文出現。

- **第 1 層 — 顯式巨集**：`NC_XORSTR("string")` / `NEVERC_XORSTR("string")`，逐字串精確控制
- **第 2 層 — 自動 IR Pass**：`-fencrypt-call-strings`，自動加密函式呼叫中的所有字串參數

兩層機制均使用堆疊配置緩衝區（無 heap 配置）、逐實例金鑰流以及 volatile 清零。到達原生機器碼邊界時，顯式 `NC_XORSTR` 的解碼呼叫會重新加密並直接展開至各自呼叫點；最終物件不保留共享解碼函式。

---

## 快速上手

### 第 1 層：顯式巨集

```c
#include <neverc/xorstr/xorstr.h>

FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

### 第 2 層：自動加密

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## 第 1 層：`NC_XORSTR` / `NEVERC_XORSTR` 巨集

### 用法

```c
#include <neverc/xorstr/xorstr.h>

const char *api = NC_XORSTR("GetProcAddress");
const char *api = NEVERC_XORSTR("GetProcAddress");
```

支援所有字串字面量類型（普通、UTF-8、寬字元、UTF-16、UTF-32），非字面量參數會產生編譯期錯誤。

### 保護流程

1. **Sema** 使用逐實例金鑰加密每個字面值。seed `0` 由編譯器取得新的作業系統熵；`-fstring-encrypt-key=` 可指定具決定性的完整 64 位元輸出。
2. **中間 IR / LTO 輸入**保留不透明、不可特化的解碼呼叫，避免一般最佳化或 LTO 將明文重新摺疊進 IR。
3. **最終機器碼邊界**解開並重新加密編譯器側 ciphertext，為每個呼叫點選擇不同迴圈形態並就地展開；接著移除解碼器、輔助函式圖、ABI anchor、route state 與語意名稱。
4. **清零**會在最佳化/provider 交接前與最終 tail 各執行一次；後者具冪等性，能修復 CFG 變更後的位置。

### 解碼器多樣化

狀態排程、常數、ciphertext 與等價的逐位元組運算會隨 seed 和呼叫點改變；`a + b − 2 × (a & b)` 只是其中一種形式。volatile 狀態/ciphertext 讀取抑制常數摺疊，`nooutline` 阻止 Machine Outliner 在 IR finalization 後重新抽取共享解碼器。

這會消除供 IDA 一次識別或統一模擬的穩定獨立函式，但不表示執行中的程式所需明文無法透過動態 instrumentation 觀察。

---

## 第 2 層：`-fencrypt-call-strings`

| 標誌 | 說明 | 預設值 |
|------|------|--------|
| `-fencrypt-call-strings` | 啟用自動加密 | 關閉 |
| `-fno-encrypt-call-strings` | 停用 | — |
| `-fencrypt-call-strings-max-len=N` | 跳過超過 N 位元組的字串 | 1024 |

此轉換會在 IPO 前、一般最佳化後，以及每個一般或 plugin 提供的 late IR 階段後執行。LTO 也會在 provider hook 與 pre-codegen hook 後套用相同的強制封口。

Pass 會處理源自編譯器私有 `unnamed_addr` 字面值儲存的直接與間接 `CallBase` 參數，並保留 GEP、cast、`freeze`、`select`、PHI 與可提升區域指標槽的語意。LLVM intrinsic、inline asm、對外可見或使用者定義陣列，以及超過長度上限的字面值會略過。受保護字面值若透過 `musttail` 傳遞，編譯會安全失敗。

---

## 堆疊清零（`XorStrCleanupPass`）

在每個可達的 `ret`、`resume`、unwind 至呼叫端的 `cleanupret`，以及未攔截的 `catchswitch` unwind 前，以 volatile `memset` 清除完整緩衝區。無法完整追蹤或不安全的儲存形式會被拒絕，而不會只清除其中一部分。

---

## 與 `.encrypt()` 的比較

| 方面 | `NC_XORSTR()` | `.encrypt()` |
|------|---------------|--------------|
| **可用性** | 純 C（透過標頭檔） | 僅 NeverC 語法擴充 |
| **記憶體** | 堆疊（`alloca`） | 堆積（`NEVERC_STRING_ALLOC`） |
| **回傳類型** | `const char*` | `string`（值類型） |
| **適用場景** | Win32 API、FFI | 通用字串操作 |

---

## 編譯器標誌參考

| 標誌 | 說明 |
|------|------|
| `-fencrypt-call-strings` | 啟用函式呼叫參數的自動字串加密 |
| `-fno-encrypt-call-strings` | 停用自動加密 |
| `-fencrypt-call-strings-max-len=N` | 自動加密的最大位元組長度（預設：1024） |
| `-fstring-encrypt-key=0xHEX` | 覆寫完整 64 位元 seed；`0` 使用新的隨機熵 |

## 輸出邊界與可重現性

- `-fno-lto` 在 frontend 產生原生機器碼時完成 finalization。
- Auto-LTO 與 Full LTO 在 pre-link bitcode 保留不透明解碼器，待全程式與 plugin IR 最佳化後再重新加密並逐呼叫點展開。
- provider 取代的 pipeline 與 late plugin pass 後，都有強制的加密、清零與 finalization tail。
- 使用預設 seed 時，各次獨立原生建置會產生不同結果；可能重播舊受保護程式碼的 whole-link 與 partition cache 會被繞過。
- 非零 seed 刻意提供決定性且可使用 cache：相同輸入與相同完整 64 位元 seed 會產生相同受保護程式碼。
- `-emit-llvm` 與 pre-link bitcode 是中間產物，因此刻意保留不透明 decoder ABI；「無共享解碼器」保證適用於成功產生的最終機器碼。
