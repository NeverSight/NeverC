# Windows 内核驱动示例

使用 NeverC 构建的最小 WDM 内核驱动。支持从 macOS / Linux 交叉编译。

NeverC 是一体化编译器——单次调用即可完成预处理、编译、优化（auto-LTO）
以及通过内置链接器进行链接。

## 构建

从仓库根目录：

```bash
cd examples/windows-driver
make
```

使用独立的 NeverC 发行版：

```bash
make NEVERC=/path/to/neverc
```

输出为 `ExampleDriver.sys`（auto-LTO 优化）。
默认构建包含 `-g` 用于调试；**发布版本应去掉 `-g`** 以剥离调试符号并减小二进制体积
（~38 KB → ~3 KB）。

## 手动构建（不使用 Make）

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver.sys driver.c
```

> `-g` 将 DWARF 调试信息嵌入 PE；可使用 `llvm-dwarfdump` 检查。
> 发布版本应省略此选项以减小二进制体积。

## 功能说明

- 在 `\Device\ExampleDriver` 创建设备对象
- 在 `\DosDevices\ExampleDriver` 创建符号链接
- 处理 `IRP_MJ_CREATE`、`IRP_MJ_CLOSE`、`IRP_MJ_DEVICE_CONTROL`
- 通过 `DbgPrint` 输出加载/卸载消息

## 加载（在 Windows 测试机上）

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

请启用测试签名或使用代码签名证书用于生产环境。
