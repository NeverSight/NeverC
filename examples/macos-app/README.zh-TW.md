**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# macOS 應用程式範例

使用 NeverC 交叉編譯的原生 macOS Mach-O 可執行檔。示範透過 sysctl、uname 和 Mach 核心 API 取得系統與行程資訊。可從 macOS、Windows 或 Linux 建置——無需 Xcode。

## 建置

從儲存庫根目錄（預設目標：`arm64-apple-macos`）：

```bash
cd examples/macos-app
neverc make          # debug：-g（首次建置預設）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
```

Makefile 會持久化 `PROFILE`，後續 `neverc make` 會保持同一 debug/release
選擇。release 使用 NeverC 內建 `--strip`：刪除除錯中繼資料與不需要的靜態
符號名，同時保留載入器/動態 ABI 仍需要的名稱。詳見
[發行建置](../../docs/release-builds/README.zh-TW.md)。


建置 Intel 版本：

```bash
neverc make TARGET=x86_64-apple-macos
```

使用獨立的 NeverC 發行版：

```bash
neverc make NEVERC=/path/to/neverc
```

## 手動建置（不使用 Make）

```bash
neverc --target=arm64-apple-macos -Wall -o macos-app main.c
```

## 執行

```bash
./macos-app
```

## 功能說明

- 透過 `uname` 查詢核心資訊
- 透過 `sysctl` 讀取硬體詳情（型號、CPU 數量、記憶體大小、分頁大小）
- 報告行程身分（`getpid`、`getppid`、`getuid`）
- 透過 Mach `host_info` 取得主機資訊，`task_info` 取得任務記憶體統計
