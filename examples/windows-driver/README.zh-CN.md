**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 示例](../../docs/examples/README.zh-CN.md)

# Windows 内核驱动示例

使用 NeverC 构建的最小 WDM 内核驱动。默认面向 **x64**，也可以构建 ARM64 版本。
支持从 macOS / Linux 交叉编译。

NeverC 是一体化编译器——单次调用即可完成预处理、编译、优化（auto-LTO）
以及通过内置链接器进行链接。

## 构建

从仓库根目录：

```bash
cd examples/windows-driver
neverc make
```

这会生成 `ExampleDriver-x64.sys`。如需改为构建 ARM64，或两者都构建：

```bash
neverc make ARCH=arm64
neverc make all-arch
```

使用独立的 NeverC 发行版：

```bash
neverc make NEVERC=/path/to/neverc
```

输出为 `ExampleDriver-<架构>.sys`（auto-LTO 优化）。
默认构建包含 `-g` 用于调试；**发布版本应去掉 `-g`** 以剥离调试符号并减小二进制体积
（~38 KB → ~3 KB）。

## 手动构建（不使用 Make）

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

构建 ARM64 时只需把 target 换成 `aarch64-pc-windows-msvc`，其余不变。
`-fms-kernel` 会自动选用与目标架构匹配的 WDK 头文件和导入库，并定义 WDK
所需的架构宏，因此无需手动传入。

> `-g` 将 DWARF 调试信息嵌入 PE；可使用 `llvm-dwarfdump` 检查。
> 发布版本应省略此选项以减小二进制体积。

## 功能说明

- 在 `\Device\ExampleDriver` 创建设备对象
- 在 `\DosDevices\ExampleDriver` 创建符号链接
- 处理 `IRP_MJ_CREATE`、`IRP_MJ_CLOSE`、`IRP_MJ_DEVICE_CONTROL`
- 通过 `DbgPrint` 输出加载/卸载消息

## 加载（在 Windows 测试机上）

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

请启用测试签名或使用代码签名证书用于生产环境。
