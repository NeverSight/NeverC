**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 內建運行時系統](../README.zh-TW.md)

# 編譯期字串雜湊 (`strhash`)

## 概述

NeverC 為純 C 提供編譯期與執行時字串雜湊，適合以整數雜湊相等做快速字串分派——例如比對 API 名稱、物品 ID、命令詞——而無需在二進位中保留明文對照表。

- **第 1 層 — 顯式編譯期巨集**：`NC_STRHASH("string")` / `NEVERC_STRHASH("string")` 在 Sema 階段摺疊為整數常數
- **第 2 層 — 執行時 + 可選 IR 摺疊**：`neverc_strhash_rt` / `NC_STRHASH_AUTO`，搭配 `-fstrhash-fold` 將參數為字串字面量的執行時呼叫摺疊為常數

兩層共用 `-fstrhash-algo` 選定的演算法（預設：FNV-1a 64-bit），保證編譯期與執行時雜湊始終一致。

---

## 快速上手

### 第 1 層：編譯期巨集

```c
#include <neverc/strhash/strhash.h>

static const uint64_t kApi = NC_STRHASH("NtQuerySystemInformation");

int is_api(const char *name) {
    return neverc_strhash_rt(name, strlen(name)) == kApi;
}
```

### 第 2 層：自動分派 + 摺疊

```c
#include <neverc/strhash/strhash.h>
uint64_t h = NC_STRHASH_AUTO(name);
```

```bash
neverc -fstrhash-fold -fstrhash-algo=fnv64a main.c -o main
```

---

## 第 1 層：`NC_STRHASH` / `NEVERC_STRHASH` 巨集

支援所有字串字面量類型（普通、UTF-8、寬字元、UTF-16、UTF-32）。非字面量參數會產生編譯期錯誤；變數請使用 `NC_STRHASH_AUTO` 或 `neverc_strhash_rt`。

### 演算法

| 旗標值 | 說明 | 預設 |
|--------|------|------|
| `fnv32a` | FNV-1a 32-bit | |
| `fnv64a` | FNV-1a 64-bit | **是** |
| `xxhash64` | XXHash64（seed 0） | |

---

## 第 2 層：`-fstrhash-fold`

將對 `neverc_fnv_sum32a` / `neverc_fnv_sum64a` / `neverc_xxhash64` 且參數為常數字串的呼叫摺疊為整數常數。

| 旗標 | 說明 | 預設 |
|------|------|------|
| `-fstrhash-fold` | 啟用 IR 摺疊 | 關閉 |
| `-fno-strhash-fold` | 停用 | — |
| `-fstrhash-algo=<algo>` | 選擇演算法 | `fnv64a` |

---

## 自訂執行時雜湊

```c
#define NC_STRHASH_HASH_FN(data, len) my_hash(data, len)
#include <neverc/strhash/strhash.h>
```

僅覆寫執行時路徑；`NC_STRHASH()` 仍使用 builtin / `-fstrhash-algo`。

---

## 編譯器旗標參考

| 旗標 | 說明 |
|------|------|
| `-fstrhash-algo=fnv32a` | 使用 FNV-1a 32-bit |
| `-fstrhash-algo=fnv64a` | 使用 FNV-1a 64-bit（預設） |
| `-fstrhash-algo=xxhash64` | 使用 XXHash64（seed 0） |
| `-fstrhash-fold` | 摺疊常數字串參數的執行時雜湊呼叫 |
| `-fno-strhash-fold` | 停用 IR 摺疊 |
