**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文档索引](../README.zh-CN.md) · [← NeverC 项目](../../README.md)

# `neverc runtime`

管理从 [GitHub Releases](https://github.com/NeverSight/NeverC/releases) 下载的
**交叉编译 runtime**（sysroot / SDK）。包安装在编译器旁的
`<NeverC-root>/runtime/`（默认安装即 `~/.neverc/runtime/`）。

请优先使用 `neverc runtime install …`，不要手动解压
`neverc-runtime-<target>.zip`。

## 语法

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

别名：`upgrade` → `update`；`uninstall` → `remove`；`ls` → `list`。

## 可用目标

| 目标 | 内容布局（位于 `runtime/` 下） |
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

默认按**编译器 release 标签**安装单个目标（或使用 `--version <tag>`）。资产名：
`neverc-runtime-<target>.zip`。

```bash
neverc runtime install windows-x64
neverc runtime install linux-arm64 --version v3389.1.2
```

若目标已安装：

- 标签相同 → 提示后成功退出。
- 标签不同 / 未知 → 以 `[Y/n]` 确认是否重装。

### `install all`

按编译器版本（或 `--version`）安装目录中**所有尚未安装**的目标。已安装的会跳过；
若要改钉扎版本，请对单个目标再执行 `install`。

```bash
neverc runtime install all
```

### `update` / `upgrade`

强制拉取单个目标，无交互确认。默认版本为 **latest**（与 `install` 默认跟编译器
不同）。可用 `--version` 钉扎。

```bash
neverc runtime update windows-x64
neverc runtime update android-arm64 --version v3389.1.2
```

### `remove` / `uninstall`

删除已安装目标目录，并更新 `runtime/manifest.json`。

```bash
neverc runtime remove linux-x64
```

### `list` / `ls`

列出目录中每个目标的安装状态（含记录的标签）以及当前编译器标签。

```bash
neverc runtime list
```

## 版本规则

| 命令 | 省略 `--version` 时的默认 |
|------|---------------------------|
| `install` / `install all` | 编译器 release 标签 |
| `update` | 发布了该 runtime 资产的最新 release |

标签形如 `vMAJOR.MINOR.PATCH`。解压前会按 release 的 `SHA256SUMS` 校验。

## 与 `neverc update` 的关系

- `neverc runtime …` **只**改动 sysroot。
- [`neverc update`](../update/README.zh-CN.md) 把**编译器与所有已安装 runtime**
  作为一次事务同步到同一标签。

用 `neverc update` 升级编译器后，已安装 runtime 已对齐；只需对**新目标**再执行
`runtime install`。

## 相关命令

| 命令 | 适用场景 |
|------|----------|
| [`neverc update`](../update/README.zh-CN.md) | 编译器与已装 runtime 一起升级/降级 |
| [`neverc build` / `make`](../build/README.zh-CN.md) | 构建依赖这些 sysroot 的交叉编译示例 |
| [示例](../examples/README.zh-CN.md) | 带 `--target=…` 的示例 `Makefile` |
| `neverc runtime --help` | 内置用法摘要 |
