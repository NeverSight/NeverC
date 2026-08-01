**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件索引](../README.zh-TW.md) · [← NeverC 專案主頁](../../docs/i18n/README.zh-TW.md)

# NeverC 範例

完整的可建置範例，展示 NeverC 的跨平台編譯能力。所有範例均可從 macOS / Linux 交叉編譯 — 無需 Windows 建置環境。

---

## 可用範例

### 伺服器後端

| 範例 | 說明 | 關鍵特性 |
|------|------|---------|
| [權威遊戲伺服器](../../examples/network-authoritative-server/README.zh-TW.md) | 跨平台遊戲後端 | 固定 60 Hz tick、TCP 工作階段、UDP/QUIC 輸入、重放保護 |
| [反作弊收集器](../../examples/network-anticheat-collector/README.zh-TW.md) | 加固遙測擷取 | mTLS、串流 NRPC、HMAC 遙測、有界稽核管線 |

### Windows

| 範例 | 說明 | 關鍵特性 |
|------|------|---------|
| [Windows 核心驅動](../../examples/windows-driver/README.zh-TW.md) | 最小 WDM 核心驅動 | 交叉編譯 `.sys`，支援 **x64**（預設）和 **ARM64**，自動 LTO，內建連結器，`DbgPrint` 裝置 I/O |
| [Windows 驅動 + CET](../../examples/windows-driver-cet/README.zh-TW.md) | 帶 Intel CET 影子堆疊的核心驅動 | CET 相容核心程式碼（**僅 x64**），`/guard:ehcont`，影子堆疊強制 |
| [Windows 驅動 + 浮點](../../examples/windows-driver-float/README.zh-TW.md) | 帶浮點/SIMD 的核心驅動 | **x64** 與 **ARM64** 下的核心模式安全浮點，`KeSaveExtendedProcessorState` / `KeRestoreExtendedProcessorState` |
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

### macOS

| 範例 | 說明 | 關鍵特性 |
|------|------|---------|
| [macOS 應用程式](../../examples/macos-app/README.zh-TW.md) | 原生 Mach-O 可執行檔 | sysctl、uname、Mach host_info/task_info、行程自省 |
| [macOS 動態函式庫](../../examples/macos-dylib/README.zh-TW.md) | 原生 `.dylib` 動態函式庫 | Mach vm_read/vm_write、vm_alloc/vm_dealloc、task_info、XOR 輔助 |

### Android

| 範例 | 說明 | 關鍵特性 |
|------|------|---------|
| [Android ELF](../../examples/android-elf/README.zh-TW.md) | Root 裝置上的原生 ARM64 可執行檔 | 交叉編譯到 Android，dlopen/liblog，/proc 資訊，root 檢測 |
| [Android 共享庫](../../examples/android-so/README.zh-TW.md) | 原生 ARM64 `.so` 庫 | 共享庫，mmap RWX，XOR 加密，dlopen liblog |

### Android 核心模組 (.ko)

無需核心原始碼樹 — NeverC 使用內建的最小化 runtime 編譯。單一原始檔覆蓋 GKI 5.10–6.12。

| 範例 | 說明 | 關鍵特性 |
|------|------|---------|
| [核心 Hello](../../examples/android-kernel-hello/README.zh-TW.md) | 最小 `.ko` 模組 | kprobe 引導 kallsyms，最簡 insmod 驗證 |
| [核心驅動模板](../../examples/android-kernel-driver/README.zh-TW.md) | 動態符號解析模板 | `kallsyms_lookup_name`，GKI 穩定 ABI，5.10–6.12 |
| [核心 Inline Interpose](../../examples/android-kernel-inline-interpose/README.zh-TW.md) | `do_faccessat` 的 inline interpose | BTI/PAC 安全補丁，context interpose 模式，PC 相對重定位 |
| [核心 Syscall Interpose](../../examples/android-kernel-syscall-interpose/README.zh-TW.md) | 系統呼叫表 / inline / context interpose | `sys_call_table` 替換、inline interpose、context interpose 三種模式 |
| [核心低可見性](../../examples/android-kernel-lowvis/README.zh-TW.md) | 模組可見性管理 | list/sysfs/proc 可見性，憑證包裝，SELinux 強制狀態 |
| [核心全功能 SDK](../../examples/android-kernel-full/README.zh-TW.md) | 完整 SDK 整合 | Netlink IPC、interpose、憑證包裝、模組可見性、SELinux 策略控制、VMA、檔案 I/O |
| [核心字元裝置](../../examples/android-kernel-chardev/README.zh-TW.md) | 字元裝置 + ioctl | `misc_register`，ioctl 分派，`/proc` seq_file |
| [核心 Netlink](../../examples/android-kernel-netlink/README.zh-TW.md) | 雙向 netlink IPC | PING/VERSION/ECHO 命令，`nvk_nl_open`/`nvk_nl_reply` |
| [核心 Probe](../../examples/android-kernel-probe/README.zh-TW.md) | 探測任意一條指令 | `neverc_krt_probe_register`、完整暫存器上下文、依優先權鏈式派發、略過/重導向 |
| [核心多檔案模組](../../examples/android-kernel-multifile/README.zh-TW.md) | 多檔案核心模組 | 只需一次 `NEVERC_KRT_BOOTSTRAP()`、`weak_odr` 共享狀態、init/interpose/helper 分檔 |

---

## 快速開始

所有範例遵循相同模式：

```bash
cd examples/範例名
neverc make
```

如需指定編譯器路徑：

```bash
neverc make NEVERC=/path/to/neverc
```

Windows 驅動範例透過 `ARCH` 選擇架構（預設 x64）。CET 範例僅支援 x64——CET 是 x86 特性：

```bash
neverc make ARCH=x64        # 建置 x64 版本（預設）
neverc make ARCH=arm64      # 建置 ARM64 版本
neverc make all-arch        # 建置該範例支援的全部架構
neverc make TESTSIGN=1      # 附加 Authenticode 測試簽章
```

Linux 範例支援架構選擇：

```bash
neverc make TARGET=aarch64-linux-gnu   # 建置 ARM64 版本
neverc make TARGET=x86_64-linux-gnu    # 建置 x86_64 版本（預設）
```

macOS 範例支援架構選擇：

```bash
neverc make TARGET=arm64-apple-macos     # 建置 Apple Silicon 版本（預設）
neverc make TARGET=x86_64-apple-macos    # 建置 Intel 版本
```

Android 範例預設面向 ARM64：

```bash
cd examples/android-elf
neverc make            # 建置
neverc make run        # 建置 + 推送到裝置 + 透過 adb 執行
```

---

## 跨平台亮點

- **單一工具鏈**：NeverC 在一次呼叫中處理預處理、編譯、最佳化（自動 LTO）與連結
- **捆綁 SDK**：Windows SDK/WDK、Linux sysroot（Ubuntu 22.04）、macOS sysroot（macOS 14）與 Android sysroot（NDK r26c, API 21+）標頭/函式庫已捆綁於 `runtime/` — 零外部相依
- **宿主無關**：從 macOS（arm64/x86_64）、Linux（x86_64/aarch64）或 Windows 使用相同命令建置
- **多目標**：從任意宿主交叉編譯到 Windows PE（`.sys`/`.exe`/`.dll`）、Linux ELF、macOS Mach-O（`.dylib`）與 Android ELF
- **除錯支援**：傳入 `-g` 以嵌入 DWARF 除錯資訊；使用 `llvm-dwarfdump` 檢查
