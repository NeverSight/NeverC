**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件索引](../README.zh-TW.md) · [← NeverC 專案](../../README.md)

# `neverc runtime`

管理從 [GitHub Releases](https://github.com/NeverSight/NeverC/releases) 下載的
**交叉編譯 runtime**（sysroot / SDK）。套件位於編譯器旁的
`<NeverC-root>/runtime/`（預設為 `~/.neverc/runtime/`）。

請優先使用 `neverc runtime install …`，不要手動解壓 `neverc-runtime-<target>.zip`。

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
|--------|-------------------------|
| `windows-x64` | `windows/x64` (+ shared `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ shared `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## 範例

```bash
neverc runtime install windows-x64
neverc runtime install all
neverc runtime update linux-arm64 --version v3389.1.2
neverc runtime remove macos-arm64
neverc runtime list
```

## 子命令

- **`install`**：預設依**編譯器 release 標籤**安裝單一目標（或 `--version`）。已安裝且標籤相同則直接成功；標籤不同時以 `[Y/n]` 確認重裝。
- **`install all`**：安裝目錄中所有**尚未安裝**的目標；已安裝者跳過。
- **`update` / `upgrade`**：強制拉取單一目標，無互動。預設為 **latest**（與 `install` 不同）。
- **`remove` / `uninstall`**：刪除目標目錄並更新 `runtime/manifest.json`。
- **`list` / `ls`**：列出各目標安裝狀態與編譯器標籤。

## 版本規則

| 命令 | 省略 `--version` 時的預設 |
|------|---------------------------|
| `install` / `install all` | 編譯器 release 標籤 |
| `update` | 發佈了該 runtime 資產的最新 release |

標籤形如 `vMAJOR.MINOR.PATCH`；解壓前依 `SHA256SUMS` 校驗。

## 與 `neverc update` 的關係

- `neverc runtime …` **只**改動 sysroot。
- [`neverc update`](../update/README.zh-TW.md) 把編譯器與所有已安裝 runtime 作為一次事務同步。

升級編譯器後已安裝 runtime 已對齊；只需對**新目標**再執行 `runtime install`。

## 相關命令

| 命令 | 適用場景 |
|------|----------|
| [`neverc update`](../update/README.zh-TW.md) | 編譯器與已裝 runtime 一起升級/降級 |
| [`neverc build` / `make`](../build/README.zh-TW.md) | 構建交叉編譯範例 |
| [範例](../examples/README.zh-TW.md) | 帶 `--target=…` 的 Makefile |
| `neverc runtime --help` | 內建用法摘要 |
