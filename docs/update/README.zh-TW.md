**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件索引](../README.zh-TW.md) · [← NeverC 專案](../../README.md)

# `neverc update`

將 **release 安裝** 中的編譯器，以及所有**已經安裝**的交叉編譯 runtime，同步到
**同一個具體 release 標籤**。`neverc upgrade` 為同義命令。

適用於 `install.sh`（或安裝到 `~/.neverc`）之後的升級/降級。它**不會**更新
CMake/Ninja 原始碼建置樹——那種環境請改 PATH 並自行重建，見
[本地開發](../local-dev/README.zh-TW.md)。

## 語法

```text
neverc update
neverc update <version>
neverc update --version <version>
neverc update --help
```

範例：

```bash
neverc update                 # 本機宿主對應的最新完整 release
neverc update v3389.1.2       # 精確標籤（升級或降級）
neverc update 3389.1.2        # 前綴 v 可省略
neverc upgrade                # 等同 neverc update
```

`-y` / `--yes` 為腳本相容而接受；更新過程本身是非互動的。

## 同步範圍

| 元件 | 行為 |
|------|------|
| 編譯器（`bin/`、`lib/`、`pluginsdk/`） | 目標標籤與目前編譯器不同時替換 |
| `runtime/` 下已安裝的 runtime | **僅**重裝已經存在的目標，並釘到同一標籤 |
| 未安裝的 runtime | **不會**自動安裝——請用 [`neverc runtime install`](../runtime/README.zh-TW.md) |

## 安全模型

1. 在 `<install>/.neverc-update.lock` 上取得排他鎖。
2. 解析目標標籤（最新宿主编譯器資產，或你指定的精確標籤）。
3. 下載 `SHA256SUMS` 與全部所需封包並校驗。
4. 解壓到暫存目錄並驗證後再提交；失敗則回滾。暫存或校驗失敗不會改動目前安裝。

若某個 runtime release 有問題，指定較早標籤即可一併回退：

```bash
neverc update v3389.0.1
```

## 宿主與安裝約束

- 僅適用於 release 安裝根目錄（通常為 `~/.neverc`）。會拒絕檔案系統根，以及看起來像 CMake 建置樹的目錄。
- 宿主平台須匹配已發佈的編譯器資產。
- 在 Windows 上，提交階段可能透過短生命週期輔助行程，在 `neverc.exe` 結束後完成替換。

## 相關命令

| 命令 | 適用場景 |
|------|----------|
| [`neverc runtime`](../runtime/README.zh-TW.md) | 增刪查單個 sysroot，不改動編譯器 |
| [`neverc run`](../run/README.zh-TW.md) | 編譯並在本機執行暫存二進位 |
| [`neverc build` / `make`](../build/README.zh-TW.md) | 驅動範例或專案 Makefile |
| `neverc update --help` | 內建用法摘要 |
