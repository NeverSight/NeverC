**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文档索引](../README.zh-CN.md) · [← NeverC 项目](../../README.md)

# `neverc update`

将 **release 安装** 中的编译器，以及所有**已经安装**的交叉编译 runtime，同步到
**同一个具体 release 标签**。`neverc upgrade` 为同义命令。

适用于 `install.sh`（或安装到 `~/.neverc`）之后的升级/降级。它**不会**更新
CMake/Ninja 源码构建树——那种环境请改 PATH 并自行重建，见
[本地开发](../local-dev/README.zh-CN.md)。

## 语法

```text
neverc update
neverc update <version>
neverc update --version <version>
neverc update --help
```

示例：

```bash
neverc update                 # 本机宿主对应的最新完整 release
neverc update v3389.1.2       # 精确标签（升级或降级）
neverc update 3389.1.2        # 前缀 v 可省略
neverc upgrade                # 等同 neverc update
```

`-y` / `--yes` 为脚本兼容而接受；更新过程本身是非交互的。

## 同步范围

| 组件 | 行为 |
|------|------|
| 编译器（`bin/`、`lib/`、`pluginsdk/`） | 目标标签与当前编译器不同时替换 |
| `runtime/` 下已安装的 runtime | **仅**重装已经存在的目标，并钉到同一标签 |
| 未安装的 runtime | **不会**自动安装——请用 [`neverc runtime install`](../runtime/README.zh-CN.md) |

Windows 的编译器包可能自带 `runtime/` 目录；更新器仍把编译器根与已安装的
runtime 包作为独立单元管理。

## 安全模型

1. 在 `<install>/.neverc-update.lock` 上获取排他锁。
2. 解析目标标签（最新宿主编译器资产，或你指定的精确标签）。
3. 下载 `SHA256SUMS` 与全部所需包并校验。
4. 解压到暂存目录并验证（编译器 `-dumpversion` 必须匹配标签；runtime 目录与
   `manifest.json` 必须存在）。
5. 提交到现有安装；失败则回滚。暂存或校验失败不会改动当前安装。

若某个 runtime release 有问题，指定较早标签即可一并回退：

```bash
neverc update v3389.0.1
```

## 宿主与安装约束

- 仅适用于 release 安装根目录（通常为 `~/.neverc`）。会拒绝文件系统根，以及
  看起来像 CMake 构建树的目录（存在 `CMakeCache.txt` 且另有 `build.ninja` 或
  `Makefile`）。
- 宿主平台须匹配已发布的编译器资产（与安装脚本同一分发矩阵）。
- 在 Windows 上，提交阶段可能通过短生命周期辅助进程，在 `neverc.exe` 退出后
  完成替换。

## 相关命令

| 命令 | 适用场景 |
|------|----------|
| [`neverc runtime`](../runtime/README.zh-CN.md) | 增删查单个 sysroot，不改动编译器 |
| [`neverc run`](../run/README.zh-CN.md) | 编译并在本机运行临时二进制 |
| [`neverc build` / `make`](../build/README.zh-CN.md) | 驱动示例或项目 Makefile |
| `neverc update --help` | 内置用法摘要 |
