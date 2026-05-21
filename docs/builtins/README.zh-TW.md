**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 文件](../README.zh-TW.md)

# NeverC 內建執行時系統

NeverC 透過可選的內建執行時擴展標準 C，這些執行時以 LLVM bitcode 形式直接嵌入編譯器二進位檔中。透過編譯器旗標啟用後，相應的執行時會在編譯時合併到使用者的 IR 中——無需外部標頭檔、函式庫或連結時依賴。

## 可用內建功能

| 內建功能 | 旗標 | 預設 | 描述 |
|---------|------|------|------|
| [**`string`**](string/README.zh-TW.md) | `-fbuiltin-string` | 關閉 | 值語義字串型別，支援點呼叫方法、自動記憶體管理和原生 UTF-8 |
| [**mimalloc**](mimalloc/README.zh-TW.md) | `-fbuiltin-mimalloc` | **開啟** | 高效能記憶體配置器，透明替換 `malloc`/`free`/`calloc`/`realloc` |

`string` 內建需要明確啟用；mimalloc 對所有 hosted 建置預設開啟（核心、shellcode 和 freestanding 模式下自動抑制）。可以組合使用：

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## 架構總覽

所有內建功能共享相同的四層架構：

```
┌─────────────────────────────────────────────────────────────────┐
│                       編譯器建置時                                │
│                                                                 │
│  原始碼 ──→ neverc -c -emit-llvm ──→ .bc ──→ bin2c.py          │
│                                       │                         │
│                              嵌入編譯器二進位檔                   │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                       使用者編譯時                                │
│                                                                 │
│  user.c ──→ 詞法/語法/語意/程式碼產生 ──→ IR 模組                │
│                                          │                      │
│                      PipelineStartEP: RuntimeLinkerPass          │
│                         │                                       │
│                         ├─ 解析嵌入的 bitcode                    │
│                         ├─ 合併到使用者 Module                   │
│                         ├─ 內部化輔助符號                        │
│                         └─ 清理 llvm.used                       │
│                                          │                      │
│                      最佳化管線                                  │
│                                          │                      │
│                                       輸出 .o                   │
└─────────────────────────────────────────────────────────────────┘
```

### 第一層：語言選項與驅動器旗標

每個內建功能由 `LangOptions.def` 中定義的 `LangOption` 控制：

```cpp
LANGOPT(BuiltinString,   1, 0, "inject NeverC builtin string prelude")
LANGOPT(BuiltinMimalloc, 1, 0, "inject mimalloc allocator override")
```

### 第二層：Foundation API

每個內建功能在 `neverc/Foundation/Builtin/` 中有一對標頭檔和實作檔。API 提供 `getEmbeddedBitcode()` 和 `isSupported()`。

### 第三層：CMake 引導基礎設施

```bash
ninja neverc                         # 階段 1：空 bitcode 佔位符
ninja neverc-bootstrap-string-bc     # 使用 neverc 編譯字串執行時
ninja neverc-bootstrap-mimalloc-bc   # 為所有目標 OS 編譯 mimalloc
ninja neverc                         # 階段 2：嵌入真實 bitcode
```

### 第四層：IR 合併 Pass（PipelineStartEP）

每個內建功能在 `BackendUtil.cpp` 的 `PipelineStartEP` 註冊一個 `ModulePass`，解析嵌入的 bitcode 並透過 `llvm::Linker::linkModules()` 合併。

---

## 內建功能之間的設計差異

| 方面 | `string` | `mimalloc` |
|------|----------|------------|
| **合併策略** | 按需（BFS 呼叫圖，裁剪未使用） | 全量合併（whole-archive） |
| **平台 bitcode** | 單一（架構中性） | 按 OS 分（Linux / Darwin / Windows） |
| **符號處理** | 全部內部化 | 覆蓋入口保持外部連結 |
| **預處理器巨集** | *（無）* | `__NEVERC_MIMALLOC__` |
| **Shellcode 模式** | 自動啟用，arena 重寫 | 被抑制（shellcode 無堆積） |
| **最佳化層級** | `-O0`（bitcode 編譯） | `-O2`（效能關鍵的配置器） |
| **DCE** | 預合併裁剪 + 後合併標記清掃 | 無 DCE（whole-archive 語義） |

---

## 安全互鎖

| 條件 | 效果 | 原因 |
|------|------|------|
| `-fno-builtin` | 抑制 mimalloc | 無 CRT 覆蓋場景 |
| `-mkernel` | 抑制 mimalloc | 核心無使用者空間堆積 |
| `-fshellcode-mode` | 抑制 mimalloc | shellcode 無堆積 |
| `-ffreestanding` | 抑制 mimalloc | 無 libc 可覆蓋 |

---

## 預處理器巨集

當內建功能啟動時，會定義相應的預處理器巨集：

```c
#ifdef __NEVERC_MIMALLOC__
// mimalloc 已啟動 — malloc/free 被透明覆蓋
#endif
```

---

## 檔案結構

```
neverc/
├── include/neverc/Foundation/
│   ├── LangOpts/LangOptions.def          # LANGOPT 宣告
│   └── Builtin/
│       ├── BuiltinString.h               # string API
│       ├── BuiltinMimalloc.h             # mimalloc API
│       └── ...
│
├── lib/Foundation/
│   ├── CMakeLists.txt                    # 所有內建功能的引導目標
│   └── Builtin/
│       ├── BuiltinString.cpp             # string bitcode 嵌入
│       ├── BuiltinMimalloc.cpp           # mimalloc 按 OS bitcode 嵌入
│       ├── bin2c.py                      # .bc → C 標頭檔轉換器（共用）
│       ├── gen_string_runtime.py         # string 原始碼產生器
│       └── gen_mimalloc_source.py        # mimalloc 原始碼產生器
│
├── lib/Emit/Backend/
│   ├── BackendUtil.cpp                   # PipelineStartEP 註冊
│   ├── StringRuntimeLinker.{h,cpp}       # string IR 合併 Pass
│   └── MimallocRuntimeLinker.{h,cpp}     # mimalloc IR 合併 Pass
│
├── lib/Invoke/ToolChains/
│   └── NeverC.cpp                        # addNeverCFeatureFlags()
│
└── lib/Compiler/Preprocessor/
    └── InitPreprocessor.cpp              # __NEVERC_MIMALLOC__ 巨集
```

---

## 新增內建功能

要新增內建執行時：

1. 在 `LangOptions.def` 中新增 `LANGOPT`
2. 在 `Options.td.h` 中新增驅動器旗標
3. 建立 Foundation API（`BuiltinFoo.h` + `.cpp`）
4. 建立原始碼產生器
5. 新增 CMake 引導目標
6. 建立 IR Pass 並在 `PipelineStartEP` 註冊
7. 定義預處理器巨集
8. 新增安全檢查
9. 新增測試
10. 新增文件及 i18n 翻譯
