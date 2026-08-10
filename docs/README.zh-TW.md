**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 專案主頁](i18n/README.zh-TW.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (dyncode README and breadcrumbs).

# NeverC 文件

各子系統的設計說明、API 參考與指南。

---

## DynCode 編譯器

DynCode 編譯管線是 NeverC 的核心研究方向。架構、CLI 選項、平台矩陣與範例見：

**[DynCode 編譯器 →](dyncode-compiler/README.zh-TW.md)**

| 文件 | 說明 |
|------|------|
| [README](dyncode-compiler/README.zh-TW.md) | 概述、快速開始、支援的目標 |
| [Pipeline & PIC](dyncode-compiler/pipeline-and-pic/README.zh-TW.md) | IR → 物件檔 → 擷取設計 |
| [IR Pass Design](dyncode-compiler/ir-pass-design/README.zh-TW.md) | 各 IR pass 的設計 rationale |
| [MIR Pass Design](dyncode-compiler/mir-pass-design/README.zh-TW.md) | 後端 MIR pass |
| [Kernel-Mode DynCode](dyncode-compiler/kernel-mode-dyncode/README.zh-TW.md) | Ring-0 編譯 |
| [Cross-Platform Architecture](dyncode-compiler/cross-platform-architecture/README.zh-TW.md) | `TargetDesc` 與擷取器 |
| [Platform Extension Guide](dyncode-compiler/platform-extension-guide/README.zh-TW.md) | 新增目標平台 |
| [ARM64 Assembly Tutorial](dyncode-compiler/arm64-assembly-tutorial/README.zh-TW.md) | 從 dyncode 角度講解 ARM64 指令 |
| [Roadmap](dyncode-compiler/roadmap/README.zh-TW.md) | 計畫中的工作 |
| [Progress](dyncode-compiler/progress/README.zh-TW.md) | 實作進度 |

---

## `.nc` 檔案副檔名

NeverC 將 `.nc` 作為原生原始檔副檔名。使用 `.nc` 時，編譯器自動啟用所有 NeverC 語言擴充（`-fneverc-types`、`-fbuiltin-string`）— 無需額外旗標。

**[`.nc` 副檔名 →](nc-extension/README.zh-TW.md)**

---

## 內建執行時

NeverC 透過嵌入 LLVM bitcode 的內建執行時擴展標準 C，每個由 `-fbuiltin-<name>` 旗標控制。`.nc` 檔案自動啟用 `string`。

**[內建執行時系統 →](builtins/README.zh-TW.md)**

| 內建功能 | 旗標 | 描述 |
|---------|------|------|
| [內建字串](builtins/string/README.zh-TW.md) | `-fbuiltin-string` | 值語義 `string` 型別，點呼叫方法、自動記憶體管理和原生 UTF-8 |
| [內建 mimalloc](builtins/mimalloc/README.zh-TW.md) | `-fbuiltin-mimalloc` | 透明高效能 `mimalloc` 配置器覆蓋 `malloc`/`free`/`calloc`/`realloc` |
| [字串加密 (xorstr)](builtins/xorstr/README.zh-TW.md) | `-fencrypt-call-strings` | 逐實例加密、強制 late 封口、逐呼叫點最終展開與 volatile 堆疊清零 |
| [字串雜湊 (strhash)](builtins/strhash/README.zh-TW.md) | `-fstrhash-algo` / `-fstrhash-fold` | 編譯期字串雜湊，執行時演算法一致，可選 IR 常數摺疊 |

---

## 外掛 API

NeverC 透過一套純 C ABI 開放整條工具鏈。外掛是一個共享模組（`.dll` / `.so` / `.dylib`），可以觀察者、攔截器或替換 Provider 的身分，附著到 130 個具名編譯階段中的任意一個——從命令列解析一直到最終連結產物。SDK 只有標頭檔：不含 LLVM 標頭檔，也不連結編譯器。

**[外掛 API →](plugin-api/README.zh-TW.md)**

| 文件 | 說明 |
|------|------|
| [README](plugin-api/README.zh-TW.md) | 進入點、階段、介面協商、註冊、ABI 規則 |
| [Python 外掛](plugin-api/python.zh-TW.md) | 選用的嵌入式 Python、生命週期、選項、唯讀 observer、diagnostic 與限制 |
| [驅動程式 API](plugin-api/driver.zh-TW.md) | 命令列、工具鏈選擇、action 圖、job 圖 |
| [Source 與 I/O API](plugin-api/source.zh-TW.md) | VFS Provider、原始碼位置、緩衝區、輸出 sink、相依性 |
| [前置處理器 API](plugin-api/prep.zh-TW.md) | token、巨集、pragma、include、特性查詢、39 種事件 |
| [AST 與語意 API](plugin-api/ast-sema.zh-TW.md) | 剖析器擴充、AST 變更、名稱查找、型別、常數 |
| [IR API](plugin-api/ir.zh-TW.md) | LLVM IR 讀取、交易式建構、分析、pass、Provider |
| [MIR API](plugin-api/mir.zh-TW.md) | 機器函式、暫存器、堆疊框、MIR pass 與分析 |
| [Target、MC、組合語言、目的檔](plugin-api/target-mc-object.zh-TW.md) | 目標註冊、呼叫慣例、MC 編碼、目的檔圖 |
| [連結與 LTO API](plugin-api/link-lto.zh-TW.md) | 連結圖、符號解析、GC/ICF、連結器與 LTO Provider |
| [DynCode API](plugin-api/dyncode.zh-TW.md) | 扁平位置無關映像、匯入降級、字元集編碼 |
| [自訂呼叫慣例](plugin-api/custom-callconv/README.zh-TW.md) | 資料驅動的呼叫慣例外掛 |

---

## 路線圖

NeverC 專案的主要規劃方向：標準函式庫、EVM 智慧合約後端和 Solana eBPF 後端。

**[路線圖 →](roadmap/README.zh-TW.md)**

| 功能 | 描述 |
|------|------|
| 標準函式庫 (`std`) | Go 風格開箱即用套件：`fmt`、`os`、`io`、`net`、`crypto`、`encoding`、`sync` 等 |
| 混淆外掛套件 (`neverc-obfuscation`) | 第一方 VM、MBA、控制流平坦化、多態引擎和反竄改外掛 |
| UI 元件庫 (`neverc-ui`) | 類 Qt 跨平台 UI，HTML/JS/CSS 渲染器，拖曳式設計器，AI 原生工作流 |
| IDE 與語言工具 (`neverc-ide`) | `.nc` 檔案的 VSCode 擴充 + 獨立 IDE，支援智慧補全、偵錯和 dyncode 管線視覺化 |
| EVM 智慧合約 | 把 C 編譯為 EVM 位元組碼——用 C 取代 Solidity 撰寫智慧合約 |
| Solana eBPF | 把 C 編譯為 Solana eBPF 位元組碼——用 C 開發鏈上程式 |

---

## CLI 工具

單次編譯之外、面向使用者的命令。

| 文件 | 說明 |
|------|------|
| [`neverc run`](run/README.zh-TW.md) | 編譯、在本機執行並刪除暫存二進位檔（類似 `go run`） |
| [`neverc update`](update/README.zh-TW.md) | 升級或降級 release 安裝（編譯器與已裝 runtime 同步到同一標籤） |
| [`neverc runtime`](runtime/README.zh-TW.md) | 安裝、列出、更新或移除交叉編譯 sysroot |
| [`neverc build` / `neverc make`](build/README.zh-TW.md) | 面向範例與專案 Makefile 的 GNU Make 相容驅動 |
| [發布二進位檔與 `--strip`](release-builds/README.zh-TW.md) | 移除非執行期符號與原始碼偵錯資訊，包括核心感知的 `.ko` 符號假名化 |

---

## 本地開發

從原始碼建置 NeverC 並設定本地開發環境，包括 PATH 設定。

**[本地開發 →](local-dev/README.zh-TW.md)**

---

## 範例

完整的可建置範例，展示 NeverC 的跨平台編譯能力。所有範例均可從 macOS / Linux 交叉編譯。

**[範例 →](examples/README.zh-TW.md)**
