**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 專案主頁](i18n/README.zh-TW.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (shellcode README and breadcrumbs).

# NeverC 文件

各子系統的設計說明、API 參考與指南。

---

## Shellcode 編譯器

Shellcode 編譯管線是 NeverC 的核心研究方向。架構、CLI 選項、平台矩陣與範例見：

**[Shellcode 編譯器 →](shellcode-compiler/README.zh-TW.md)**

| 文件 | 說明 |
|------|------|
| [README](shellcode-compiler/README.zh-TW.md) | 概述、快速開始、支援的目標 |
| [Pipeline & PIC](shellcode-compiler/pipeline-and-pic/README.zh-TW.md) | IR → 物件檔 → 擷取設計 |
| [IR Pass Design](shellcode-compiler/ir-pass-design/README.zh-TW.md) | 各 IR pass 的設計 rationale |
| [MIR Pass Design](shellcode-compiler/mir-pass-design/README.zh-TW.md) | 後端 MIR pass |
| [Kernel-Mode Shellcode](shellcode-compiler/kernel-mode-shellcode/README.zh-TW.md) | Ring-0 編譯 |
| [Cross-Platform Architecture](shellcode-compiler/cross-platform-architecture/README.zh-TW.md) | `TargetDesc` 與擷取器 |
| [Platform Extension Guide](shellcode-compiler/platform-extension-guide/README.zh-TW.md) | 新增目標平台 |
| [ARM64 Assembly Tutorial](shellcode-compiler/arm64-assembly-tutorial/README.zh-TW.md) | 從 shellcode 角度講解 ARM64 指令 |
| [Roadmap](shellcode-compiler/roadmap/README.zh-TW.md) | 計畫中的工作 |
| [Progress](shellcode-compiler/progress/README.zh-TW.md) | 實作進度 |

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
| [字串加密 (xorstr)](builtins/xorstr/README.zh-TW.md) | `-fencrypt-call-strings` | 編譯期字串加密，堆疊分配 XOR 解密，反簽名演算法 |

---

## 外掛 API

NeverC 提供純 C ABI 的樹外 pass 外掛介面。外掛是一個共享程式庫（`.dll` / `.so` / `.dylib`），可在編譯管線的指定掛鈎點註冊自訂 pass。只需一個標頭檔，零 LLVM/CRT 相依性。

**[外掛 API →](plugin-api/README.zh-TW.md)**
