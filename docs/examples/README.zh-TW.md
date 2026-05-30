**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件索引](../README.zh-TW.md) · [← NeverC 專案主頁](../../docs/i18n/README.zh-TW.md)

# NeverC 範例

完整的可建置範例，展示 NeverC 的跨平台編譯能力。所有範例均可從 macOS / Linux 交叉編譯 — 無需 Windows 建置環境。

---

## 可用範例

| 範例 | 說明 | 關鍵特性 |
|------|------|---------|
| [Windows 核心驅動](../../examples/windows-driver/README.zh-TW.md) | 最小 WDM 核心驅動 | 從 macOS/Linux 交叉編譯 `.sys`，自動 LTO，內建連結器，`DbgPrint` 裝置 I/O |
| [Windows 驅動 + CET](../../examples/windows-driver-cet/README.zh-TW.md) | 帶 Intel CET 影子堆疊的核心驅動 | CET 相容核心程式碼，`/guard:ehcont`，影子堆疊強制 |
| [Windows 驅動 + 浮點](../../examples/windows-driver-float/README.zh-TW.md) | 帶浮點/SIMD 的核心驅動 | 核心模式安全浮點，`KeSaveExtendedProcessorState` / `KeRestoreExtendedProcessorState` |

---

## 快速開始

所有範例遵循相同模式：

```bash
cd examples/<範例名>
make
```

如需指定編譯器路徑：

```bash
make NEVERC=/path/to/neverc
```

所有範例使用 **neverc** 作為編譯器，透過 NeverC 的內建連結器產生 Windows PE 二進位（`.sys` 驅動）— 無需外部 `link.exe` 或 Windows SDK 安裝。
