**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件索引](../README.zh-TW.md) · [← NeverC 專案](../../README.md)

# `neverc build` / `neverc make`

NeverC 內建 **GNU Make 相容** 建置驅動。`neverc build` 與 `neverc make` 是同一命令：
讀取 Makefile、展開變數與函數並執行配方。[`examples/`](../examples/README.zh-TW.md)
下的範例皆按此工作流編寫。

這**不是**基於 `neverc.toml` 的專案工具。請傳入普通 Make 選項與 `VAR=value`。

## 語法

```text
neverc build [options] [target...]
neverc make  [options] [target...]
```

```bash
cd examples/linux-hello
neverc make
neverc make clean
neverc make NEVERC=/path/to/neverc TARGET=aarch64-linux-gnu
```

完整選項見 `neverc make --help`。

## 選項

| 選項 | 含義 |
|------|------|
| `-f FILE` | 使用指定 Makefile |
| `-j [N]` | 平行任務數（單獨 `-j` 用本機 CPU 數） |
| `-C DIR` | 讀 Makefile 前切換目錄 |
| `-n`, `--dry-run` | 只列印配方 |
| `-k`, `--keep-going` | 出錯後繼續 |
| `-s`, `--silent` | 不回顯配方 |
| `-B`, `--always-make` | 無條件重建 |
| `-p` | 列印規則/變數資料庫 |
| `VAR=VALUE` | 命令列變數 |
| `-h`, `--help` | 顯示用法 |

## Makefile 探索順序

未指定 `-f` 時依序尋找：`GNUmakefile` → `makefile` → `Makefile`。

## 支援的 Make 能力（摘要）

規則與模式規則、`.PHONY`、配方前綴 `@`/`-`/`+`；賦值 `=`/`:=`/`+=`/`?=`/`!=`、
`override`、`define`；條件 `ifeq`/`ifdef` 等；`include`/`export`/`undefine`；
以及 `subst`、`patsubst`、`wildcard`、`foreach`、`call`、`eval`、`shell` 等常見函數。
`MAKE_VERSION` 相容性回報為 `4.3`。這是有意收窄的子集，並非完整 GNU Make。

## 典型 Makefile

```make
NEVERC ?= neverc
TARGET  = x86_64-linux-gnu
OUTPUT  = hello
SRCS    = main.c

FLAGS = --target=$(TARGET) -O2

all: $(OUTPUT)

$(OUTPUT): $(SRCS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
```

交叉編譯常透過命令列傳入 `ARCH=…` 或 `TARGET=…`。詳見
[範例](../examples/README.zh-TW.md)。

## 相關命令

| 命令 | 適用場景 |
|------|----------|
| `neverc file.c -o out` | 無 Makefile 的單檔編譯 |
| [`neverc run`](../run/README.zh-TW.md) | 本機暫存編譯並執行 |
| [`neverc runtime`](../runtime/README.zh-TW.md) | 安裝交叉編譯 sysroot |
| [發布二進位與 `--strip`](../release-builds/README.zh-TW.md) | 為發行剝離最終映像 |
