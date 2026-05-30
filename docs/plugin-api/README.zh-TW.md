**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC Out-of-Tree 外掛 API

NeverC 提供一套**純 C ABI** 用於 out-of-tree pass 外掛。外掛是一個共享函式庫（`.dll` / `.so` / `.dylib`），在編譯管線的指定位置註冊自訂 pass。外掛只需編譯依賴**一個標頭檔**（`NevercPluginAPI.h`），**零** LLVM 或 CRT 依賴 — 所有功能透過宿主提供的 vtable 路由。

## 1. 快速開始

### 最小外掛

```c
#include "NevercPluginAPI.h"

static int myPass(NevercModuleRef M, const NevercHostAPI *API, void *UD) {
    (void)UD;
    unsigned Count = 0;
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
        (void)F;
        Count++;
    }
    API->DiagNoteF("[my-plugin] %u defined functions", Count);
    return 0;
}

static void registerPasses(const NevercHostAPI *API, void *Reg) {
    API->RegisterModulePass(Reg, NEVERC_HOOK_PRE_OPT, myPass, NULL, "my-pass");
}

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
    NevercPluginInfo Info;
    Info.APIVersion     = NEVERC_PLUGIN_API_VERSION;
    Info.PluginName     = "my-plugin";
    Info.PluginVersion  = "1.0.0";
    Info.RegisterPasses = registerPasses;
    Info.Destroy        = NULL;
    return Info;
}
```

### 建置

```bash
cc -shared -o MyPlugin.dll MyPlugin.c -I/path/to/pluginsdk/include

make -C /path/to/pluginsdk/examples
```

### 執行

```bash
neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
```

## 2. 架構

**核心特性：**

- **單標頭檔 SDK**：編譯外掛只需 `NevercPluginAPI.h`。
- **零依賴**：不需要 LLVM 標頭檔，不連結 CRT。所有操作透過 vtable 完成。
- **純 C ABI**：外掛可用 C、C++、Zig、Rust（FFI）或任何能產生 C 連結共享函式庫的語言撰寫。
- **版本安全**：使用 `NEVERC_API_FN(api, Field)` 在呼叫前檢查可選 vtable 項目。

## 3. 外掛進入點

每個外掛必須匯出一個函式：

```c
NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
```

| 欄位 | 類型 | 說明 |
|------|------|------|
| `APIVersion` | `uint32_t` | 必須為 `NEVERC_PLUGIN_API_VERSION` |
| `PluginName` | `const char *` | 可讀名稱 |
| `PluginVersion` | `const char *` | 語意化版本字串 |
| `RegisterPasses` | 函式指標 | 呼叫一次以註冊所有 pass |
| `Destroy` | 函式指標 | 選用清理，可為 `NULL` |

## 4. Pass 類型

### 4.1 Module Pass（IR）

操作 LLVM IR 模組。可讀取和修改 IR。回傳非零值表示模組被修改。

### 4.2 Machine Pass（MIR）

操作機器級 IR（指令選擇之後）。

### 4.3 Binary Pass

操作原始位元組（shellcode 提取、二進位修補）。可透過 `API->BinaryResize()` 調整緩衝區大小。

### 4.4 Linker Pass

在連結時操作，可存取符號和區段。

## 5. 勾點

勾點決定 pass **何時**在管線中執行。

### 常規流程

| 勾點 | 層級 | 說明 |
|------|------|------|
| `NEVERC_HOOK_PRE_OPT` | IR | LLVM 最佳化 pass 之前 |
| `NEVERC_HOOK_POST_OPT` | IR | LLVM 最佳化 pass 之後 |
| `NEVERC_HOOK_PIPELINE_START` | IR | 管線最開始 |
| `NEVERC_HOOK_PIPELINE_LAST` | IR | IR 管線最後 |
| `NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT` | MIR | pre-emit 機器 pass 之前 |
| `NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR` | MIR | 所有機器 pass 之後 |

### Shellcode 流程

| 勾點 | 層級 | 說明 |
|------|------|------|
| `NEVERC_HOOK_SC_BEFORE_PREP` ~ `SC_AFTER_FINAL_IR` | IR | shellcode IR 階段勾點 |
| `NEVERC_HOOK_SC_BEFORE_PREEMIT` ~ `SC_AFTER_FINAL_MIR` | MIR | shellcode MIR 階段勾點 |
| `NEVERC_HOOK_SC_POST_EXTRACT` / `SC_POST_FINALIZE` | 二進位 | 位元組提取後/後處理完成後 |

### LTO / 連結器流程

| 勾點 | 層級 | 說明 |
|------|------|------|
| `NEVERC_HOOK_LTO_PRE_OPT` / `LTO_POST_OPT` | IR | LTO 最佳化前後 |
| `NEVERC_HOOK_LINK_PRE_LAYOUT` / `POST_LAYOUT` / `POST_EMIT` | 連結器 | 區段佈局前後 / 發射後 |

## 6. 不透明控制代碼類型

所有 IR/MIR 物件透過不透明控制代碼存取。控制代碼**僅在接收它們的 pass 回呼作用域內有效**。

| 控制代碼 | 表示 |
|---------|------|
| `NevercModuleRef` | LLVM Module |
| `NevercValueRef` | LLVM Value（函式、指令、全域變數） |
| `NevercBasicBlockRef` | LLVM BasicBlock |
| `NevercTypeRef` | LLVM Type |
| `NevercBuilderRef` | IR Builder |
| `NevercMachineFuncRef` / `MBBRef` / `MInstrRef` | 機器函式 / 基本區塊 / 指令 |
| `NevercLinkerSymbolRef` / `SectionRef` | 連結器符號 / 區段 |

## 7. 資料結構

宿主透過 vtable 提供多種高效能資料結構：**Arena**（bump-pointer 配置器）、**StrMap**（字串鍵雜湊表）、**IntMap**（整數鍵雜湊表）、**StrBuilder**（增量字串建構）、**ValueSet**（值雜湊集合）。所有都是不透明的，必須在 pass 回傳前釋放。

## 8. 版本相容性

```c
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

vtable 單調增長。舊標頭檔編譯的外掛仍與新版宿主相容。

## 9. 外掛引數

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       input.c -o output.obj
```

```c
int verbose   = API->PluginGetArgBool("verbose", 0);
int64_t limit = API->PluginGetArgInt64("max-fns", -1);
```

## 10. 生命週期規則

| 資源 | 生命週期 | 清理 |
|------|---------|------|
| 不透明控制代碼 | 在 pass 回呼內有效 | 無需釋放 |
| `NevercBuilderRef` | 由 `BuilderCreate` 建立 | `BuilderDispose` |
| 堆積字串（`StrDup` / `StrFormat`） | 呼叫者擁有 | `Free` |
| Arena 配置 | 歸 Arena 所有 | `ArenaDestroy` |
| 資料結構（Map / Builder / Set） | 由 `*Create` 建立 | 對應 `*Destroy` |

## 11. 最佳實踐

1. **Arena 優先配置**：臨時資料使用 `NEVERC_TRY_ARENA`。一個 `ArenaDestroy` 取代 N 個 `Free`。
2. **版本保護**：始終用 `NEVERC_API_FN` 包裹新版 vtable 呼叫。
3. **優先使用回呼疊代**：`ModuleForEachDefinedFunction` 比巨集疊代更快。
4. **無 CRT 依賴**：所有操作透過 vtable。不要直接呼叫 `malloc` / `printf` / `qsort`。
5. **乾淨回傳**：pass 回傳前釋放所有 Builder、銷毀所有資料結構和 Arena。

## 12. Plugin SDK 內容

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h    # 唯一需要的標頭檔
└── examples/
    ├── Makefile             # 獨立建置範本
    ├── ExamplePlugin.c      # 綜合示範
    ├── CrtShimPlugin.c      # 零 CRT 依賴概念驗證
    └── BenchPlugin.c        # HostAPI 吞吐量微基準測試
```

## 13. 相關文件

- [Shellcode 外掛介面](../shellcode-compiler/plugin-interface/README.zh-TW.md) — shellcode 管線的 in-tree C++ 擴充點。
