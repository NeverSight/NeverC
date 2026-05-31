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
| [Windows Ring3 EXE](../../examples/windows-exe/README.zh-TW.md) | 使用者態控制台程式 | GetSystemInfo，程序列舉，VirtualAlloc |
| [Windows Ring3 DLL](../../examples/windows-dll/README.zh-TW.md) | 使用者態 DLL | ReadProcessMemory，VirtualAllocEx，模組列舉 |

### Linux

| 範例 | 說明 | 關鍵特性 |
|------|------|--------|
| [Linux Hello World](../../examples/linux-hello/README.zh-TW.md) | 最小 C 程式 | 從 macOS/Windows 交叉編譯 ELF |
| [Linux POSIX](../../examples/linux-posix/README.zh-TW.md) | POSIX 系統程式設計 | pthreads、mmap、pipe、訊號處理 |
| [Linux 全靜態](../../examples/linux-static/README.zh-TW.md) | 全靜態連結二進位 | `-static` 連結，零執行階段相依性 |
| [Linux 網路](../../examples/linux-network/README.zh-TW.md) | TCP Socket 示範 | 客戶端/伺服器，Socket API |
| [Linux 數學 + zlib](../../examples/linux-math/README.zh-TW.md) | 數學 + 壓縮 | 三角函數，zlib 壓縮/解壓，CRC32 |

### Android

| 範例 | 說明 | 關鍵特性 |
|------|------|---------|
| [Android ELF](../../examples/android-elf/README.zh-TW.md) | Root 裝置上的原生 ARM64 可執行檔 | 交叉編譯到 Android，dlopen/liblog，/proc 資訊，root 檢測 |
| [Android 共享庫](../../examples/android-so/README.zh-TW.md) | 原生 ARM64 `.so` 庫 | 共享庫，mmap RWX，XOR 加密，dlopen liblog |

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
