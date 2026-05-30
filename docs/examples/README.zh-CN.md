**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文档索引](../README.zh-CN.md) · [← NeverC 项目主页](../../docs/i18n/README.zh-CN.md)

# NeverC 示例

完整的可构建示例，展示 NeverC 的跨平台编译能力。所有示例均可从 macOS / Linux 交叉编译 — 无需 Windows 构建环境。

---

## 可用示例

| 示例 | 说明 | 关键特性 |
|------|------|---------|
| [Windows 内核驱动](../../examples/windows-driver/README.zh-CN.md) | 最小 WDM 内核驱动 | 从 macOS/Linux 交叉编译 `.sys`，自动 LTO，内置链接器，`DbgPrint` 设备 I/O |
| [Windows 驱动 + CET](../../examples/windows-driver-cet/README.zh-CN.md) | 带 Intel CET 影子栈的内核驱动 | CET 兼容内核代码，`/guard:ehcont`，影子栈强制 |
| [Windows 驱动 + 浮点](../../examples/windows-driver-float/README.zh-CN.md) | 带浮点/SIMD 的内核驱动 | 内核模式安全浮点，`KeSaveExtendedProcessorState` / `KeRestoreExtendedProcessorState` |

---

## 快速开始

所有示例遵循相同模式：

```bash
cd examples/<示例名>
make
```

如需指定编译器路径：

```bash
make NEVERC=/path/to/neverc
```

所有示例使用 **neverc** 作为编译器，通过 NeverC 的内置链接器生成 Windows PE 二进制（`.sys` 驱动）— 无需外部 `link.exe` 或 Windows SDK 安装。

---

## 跨平台亮点

- **单一工具链**：NeverC 在一次调用中处理预处理、编译、优化（自动 LTO）和链接
- **捆绑 SDK**：Windows SDK 和 WDK 头文件/库已捆绑在 `runtime/` 中 — 零外部依赖
- **宿主无关**：从 macOS（arm64/x86_64）或 Linux（x86_64）使用相同命令构建
- **调试支持**：传入 `-g` 可将 DWARF 调试信息嵌入 PE；使用 `llvm-dwarfdump` 检查
