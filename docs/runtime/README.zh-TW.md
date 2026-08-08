**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件索引](../README.zh-TW.md) · [← NeverC 專案](../../README.md)

# `neverc runtime`

管理從 [GitHub Releases](https://github.com/NeverSight/NeverC/releases) 下載的
**交叉編譯 runtime**（sysroot / SDK）。套件安裝在編譯器旁的
`<NeverC-root>/runtime/`（預設安裝即 `~/.neverc/runtime/`）。

請優先使用 `neverc runtime install …`，不要手動解壓
`neverc-runtime-<target>.zip`。

## 語法

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

別名：`upgrade` → `update`；`uninstall` → `remove`；`ls` → `list`。

## 可用目標

| 目標 | 內容布局（位於 `runtime/` 下） |
|------|--------------------------------|
| `windows-x64` | `windows/x64`（以及共享的 `windows/shared`） |
| `windows-arm64` | `windows/arm64`（以及共享的 `windows/shared`） |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## 子命令

### `install`

預設依**編譯器 release 標籤**安裝單一目標（或使用 `--version <tag>`）。資產名：
`neverc-runtime-<target>.zip`。

```bash
neverc runtime install windows-x64
neverc runtime install linux-arm64 --version v3389.1.2
```

若目標已安裝：

- 標籤相同 → 提示後成功退出。
- 標籤不同 / 未知 → 以 `[Y/n]` 確認是否重裝。

### `install all`

依編譯器版本（或 `--version`）安裝目錄中**所有尚未安裝**的目標。已安裝的會跳過；
若要改釘扎版本，請對單一目標再執行 `install`。

```bash
neverc runtime install all
```

### `update` / `upgrade`

強制拉取單一目標，無互動確認。預設版本為 **latest**（與 `install` 預設跟編譯器
不同）。可用 `--version` 釘扎。

```bash
neverc runtime update windows-x64
neverc runtime update android-arm64 --version v3389.1.2
```

### `remove` / `uninstall`

刪除已安裝目標目錄，並更新 `runtime/manifest.json`。

```bash
neverc runtime remove linux-x64
```

### `list` / `ls`

列出目錄中每個目標的安裝狀態（含記錄的標籤）以及當前編譯器標籤。

```bash
neverc runtime list
```

## 版本規則

| 命令 | 省略 `--version` 時的預設 |
|------|---------------------------|
| `install` / `install all` | 編譯器 release 標籤 |
| `update` | 發佈了該 runtime 資產的最新 release |

標籤形如 `vMAJOR.MINOR.PATCH`。解壓前會依 release 的 `SHA256SUMS` 校驗。

## 與 `neverc update` 的關係

- `neverc runtime …` **只**改動 sysroot。
- [`neverc update`](../update/README.zh-TW.md) 把**編譯器與所有已安裝 runtime**
  作為一次事務同步到同一標籤。

用 `neverc update` 升級編譯器後，已安裝 runtime 已對齊；只需對**新目標**再執行
`runtime install`。

## 相關命令

| 命令 | 適用場景 |
|------|----------|
| [`neverc update`](../update/README.zh-TW.md) | 編譯器與已裝 runtime 一起升級/降級 |
| [`neverc build` / `make`](../build/README.zh-TW.md) | 構建依賴這些 sysroot 的交叉編譯範例 |
| [範例](../examples/README.zh-TW.md) | 帶 `--target=…` 的範例 `Makefile` |
| `neverc runtime --help` | 內建用法摘要 |
