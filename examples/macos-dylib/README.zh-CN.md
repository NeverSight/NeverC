**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 示例](../../docs/examples/README.zh-CN.md)

# macOS 动态库示例

使用 NeverC 交叉编译的原生 macOS `.dylib` 动态库。封装 Mach 内核接口，提供任务自省和虚拟内存操作——专为安全研究设计。可从 macOS、Windows 或 Linux 构建——无需 Xcode。

## 构建

从仓库根目录（默认目标：`arm64-apple-macos`）：

```bash
cd examples/macos-dylib
neverc make
```

构建 Intel 版本：

```bash
neverc make TARGET=x86_64-apple-macos
```

使用独立的 NeverC 发行版：

```bash
neverc make NEVERC=/path/to/neverc
```

## 手动构建（不使用 Make）

```bash
neverc --target=arm64-apple-macos -Wall -dynamiclib -o libneverc.dylib lib.c
```

## 功能说明

- 导出 `nc_task_basic_info` 封装 Mach `task_info` 查询
- 提供 `nc_vm_read`/`nc_vm_write` 进行 Mach 虚拟内存读写
- `nc_vm_alloc`/`nc_vm_dealloc` 进行 Mach VM 内存分配与释放
- XOR 缓冲区加密辅助函数和 PID/任务查询
