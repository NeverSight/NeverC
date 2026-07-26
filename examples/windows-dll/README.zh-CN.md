**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 示例](../../docs/examples/README.zh-CN.md)

# Windows Ring3 DLL 示例

使用 NeverC 交叉编译的 Windows 用户态 DLL。提供进程内存操作的辅助函数——专为游戏安全研究设计。可从 macOS、Windows 或 Linux 构建——无需 MSVC 或 Visual Studio。

## 构建

```bash
cd examples/windows-dll
neverc make
```

## 手动构建（不使用 Make）

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -shared -Xlinker --entry=DllMain -Xlinker --subsystem=windows -lkernel32 -luser32 -o example.dll dllmain.c
```

## 功能说明

- 导出 `ReadProcessMemory`/`VirtualAllocEx`/`VirtualFreeEx` 封装，用于跨进程内存访问
- 通过 `OpenProcess` 和 `CreateToolhelp32Snapshot` 进行进程/模块枚举
- XOR 缓冲区加密辅助函数和 PID/TID 查询

