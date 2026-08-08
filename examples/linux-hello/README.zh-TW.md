**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Linux Hello World 範例

使用 NeverC 交叉編譯到 Linux ELF 的最小 C 程式。支援從 macOS、Windows 或 Linux 建置——不需要目標系統工具鏈。

NeverC 在 `runtime/linux/` 中內建了 Linux sysroot（Ubuntu 22.04，glibc 2.35），單次呼叫即可完成預處理、編譯、最佳化（auto-LTO）以及透過內建連結器進行連結。

## 建置

從倉庫根目錄（預設目標：`x86_64-linux-gnu`）：

```bash
cd examples/linux-hello
neverc make          # debug：-g（首次建置預設）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
```

Makefile 會持久化 `PROFILE`，後續 `neverc make` 會保持同一 debug/release
選擇。release 使用 NeverC 內建 `--strip`：刪除除錯中繼資料與不需要的靜態
符號名，同時保留載入器/動態 ABI 仍需要的名稱。詳見
[發行建置](../../docs/release-builds/README.zh-TW.md)。


建置 AArch64 版本：

```bash
neverc make TARGET=aarch64-linux-gnu
```

使用獨立的 NeverC 發行版：

```bash
neverc make NEVERC=/path/to/neverc
```

## 手動建置（不使用 Make）

```bash
neverc --target=x86_64-linux-gnu -Wall -o hello main.c
```

## 執行

將 `hello` 複製到 Linux 機器（或 Docker 容器）中執行：

```bash
chmod +x hello
./hello
```

## 功能說明

- 列印問候語和命令列參數
- 演示內建 libc 的 `printf`、`strncpy`、`strlen`、`atoi`
- XOR 變換字串以驗證基本的整數/字元操作
