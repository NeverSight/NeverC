# 带浮点运算的 Windows 内核驱动

使用 NeverC 构建的 WDM 内核驱动，演示**内核态浮点 / SIMD 的安全使用方式**。
支持从 macOS / Linux 交叉编译。

## 构建

```bash
cd examples/windows-driver-float
make
```

使用独立的 NeverC 发行版：

```bash
make NEVERC=/path/to/neverc
```

输出为 `FloatDriver.sys`（auto-LTO 优化）。
默认构建包含 `-g` 用于调试；发布版本应去掉 `-g`。

---

## 两个问题要处理

内核态浮点有两个独立的问题：

### 问题 1 — `_fltused` ABI 标记（编译/链接期）

只要翻译单元里有任何浮点运算，MSVC 编译器就会发出对符号 `_fltused`
的未定义引用。用户态程序通过 `libcmt.lib` 提供该符号，链接器满意，
同时拉入一些 FP 相关的 CRT 代码。

内核驱动**不**链接 `libcmt`（我们传了 `-nostdlib` 和 `-Xlinker --nodefaultlib`），
所以未解析的 `_fltused` 会导致链接错误。

**NeverC 的解决方式**：使用 `-fms-kernel` 时，LLVM X86 后端会将 `_fltused`
本地定义为 0。可在生成的汇编中看到：

```asm
# 用户态目标：
    .globl  _fltused              # 外部引用 —— 需要 libcmt
```

```asm
# -fms-kernel 目标：
    .globl  _fltused
    .set    _fltused, 0           # 本地定义！无需外部符号
```

所以你**永远不用手动在驱动里写 `int _fltused = 0;`**。

### 问题 2 — 内核**不**保存 FP/SIMD 寄存器（运行期）

Windows 内核默认**不**在上下文切换时保存/恢复 x87 / XMM / YMM / ZMM
寄存器。如果驱动从任意内核代码里碰这些寄存器，就会悄悄污染当前
运行在该 CPU 上的用户态线程的 SIMD 状态。

**解决方式**：用
[`KeSaveExtendedProcessorState`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesaveextendedprocessorstate)
和 `KeRestoreExtendedProcessorState` 包裹每段浮点 / SIMD 代码：

```c
XSTATE_SAVE save;
NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
if (!NT_SUCCESS(status))
    return status;

// ... 你的 FP / SIMD 代码 ...

KeRestoreExtendedProcessorState(&save);
```

### XSTATE 掩码

| 掩码 | 覆盖范围 |
|------|---------|
| `XSTATE_MASK_LEGACY_FLOATING_POINT`（位 0） | x87 栈 |
| `XSTATE_MASK_LEGACY_SSE`（位 1） | XMM0–15 |
| `XSTATE_MASK_LEGACY` | 位 0 \| 位 1（覆盖大多数普通 `double` / SSE 代码） |
| `XSTATE_MASK_GSSE` / AVX（位 2） | YMM0–15 的高位部分 |
| `XSTATE_MASK_AVX512` | AVX-512 ZMM 寄存器 |

按代码使用的最宽寄存器，按位或组合后传入。

---

## 这个驱动做了什么

- 在 `\Device\FloatDriver` 创建设备对象，在 `\DosDevices\FloatDriver` 创建符号链接
- 在 `DriverEntry` 中调用 `ComputeAreaSafe()`（用 FP 状态保存/恢复包裹 `ComputeArea()`），
  分别传入 `radius=1.0` 和 `radius=5.0`
- 通过 `DbgPrint` 打印 double 的原始位（因为 `DbgPrint` 不支持 `%f`
  —— 我们用 `RtlCopyMemory` 提取 64 位模式）
- 通过 `-fms-kernel` 隐式定义 `_fltused`

## 验证 `_fltused` 生成

对比使用 / 不使用 `-fms-kernel` 时编译器的输出：

```bash
# 用户态（需要 libcmt）：
neverc --target=x86_64-pc-windows-msvc -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused

# 内核（本地定义为 0）：
neverc --target=x86_64-pc-windows-msvc -fms-kernel -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused
#  .set    _fltused, 0
```

## 加载（在 Windows 测试机上）

```cmd
sc create FloatDriver type= kernel binPath= C:\path\to\FloatDriver.sys
sc start FloatDriver
sc stop FloatDriver
sc delete FloatDriver
```

请启用测试签名或使用代码签名证书用于生产环境。

## 注意事项

- **`DbgPrint` 不支持 `%f`** —— 内核调试输出例程没有浮点格式化能力。
  将 double 转换为定点整数显示，或像本例一样打印原始位。
- **不要在 IRQL ≥ DISPATCH_LEVEL 使用浮点**，除非绝对必要。
  `KeSaveExtendedProcessorState` 文档说明了 IRQL 约束。
- **性能**：状态保存/恢复并非免费；对热路径应将 FP 工作打包进单个包裹区域。
