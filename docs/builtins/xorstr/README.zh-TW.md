**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 內建運行時系統](../README.zh-TW.md)

# 編譯期字串加密 (`xorstr`)

## 概述

NeverC 提供兩層編譯期字串加密機制，專為安全場景設計——確保 API 名稱、登錄檔路徑、除錯訊息等敏感字串在編譯後的二進位檔案中不以明文出現。

- **第 1 層 — 顯式巨集**：`NC_XORSTR("string")` / `NEVERC_XORSTR("string")`，逐字串精確控制
- **第 2 層 — 自動 IR Pass**：`-fencrypt-call-strings`，自動加密函式呼叫中的所有字串參數

兩層機制均使用堆疊分配緩衝區（無堆積分配），使用無 XOR 指令特徵的解密演算法（反簽名偵測），並在函式返回前透過 volatile `memset` 清零堆疊上的明文。

---

## 快速上手

### 第 1 層：顯式巨集

```c
#include <neverc/xorstr.h>

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
#include <neverc/xorstr.h>

const char *api = NC_XORSTR("GetProcAddress");
const char *api = NEVERC_XORSTR("GetProcAddress");
```

支援所有字串字面量類型（普通、UTF-8、寬字元、UTF-16、UTF-32），非字面量參數會產生編譯期錯誤。

### 反簽名解密

解密操作完全避免使用 XOR 指令，使用等價計算 `a + b − 2 × (a & b)`，結合 `volatile` 防止優化器常量摺疊。

---

## 第 2 層：`-fencrypt-call-strings`

| 標誌 | 說明 | 預設值 |
|------|------|--------|
| `-fencrypt-call-strings` | 啟用自動加密 | 關閉 |
| `-fno-encrypt-call-strings` | 停用 | — |
| `-fencrypt-call-strings-max-len=N` | 跳過超過 N 位元組的字串 | 1024 |

---

## 堆疊清零（`XorStrCleanupPass`）

在每個 `ReturnInst` 前插入 `volatile memset` 清零所有 xorstr 緩衝區，防止明文殘留在堆疊上。

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
| `-fstring-encrypt-key=0xHEX` | 覆寫 XOR 金鑰種子 |
