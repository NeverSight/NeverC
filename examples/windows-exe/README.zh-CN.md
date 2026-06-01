**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows Ring3 EXE 示例

使用 NeverC 交叉编译的 Windows 用户态可执行文件。演示 Win32 API 的系统信息查询、进程枚举和虚拟内存操作。可从 macOS、Windows 或 Linux 构建——无需 MSVC 或 Visual Studio。

## 构建

```bash
cd examples/windows-exe
neverc make
```

## 手动构建（不使用 Make）

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## 功能说明

- 通过 `GetSystemInfo` 和 `GlobalMemoryStatusEx` 查询系统信息
- 使用 `CreateToolhelp32Snapshot` 枚举正在运行的进程
- 演示 `VirtualAlloc`/`VirtualQuery`/`VirtualFree` 内存管理

