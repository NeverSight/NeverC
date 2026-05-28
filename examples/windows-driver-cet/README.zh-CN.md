# 带 CET 影子栈的 Windows 内核驱动

使用 NeverC 构建的最小 WDM 内核驱动，启用了 Intel CET（控制流强制技术）
内核影子栈。支持从 macOS / Linux 交叉编译。

## 构建

```bash
cd examples/windows-driver-cet
make
```

使用独立的 NeverC 发行版：

```bash
make NEVERC=/path/to/neverc
```

输出为 `CetDriver.sys`（auto-LTO 优化）。
默认构建包含 `-g` 用于调试；**发布版本应去掉 `-g`** 以剥离调试符号并减小二进制体积。

## CET 专用标志

| 标志 | 层级 | 用途 |
|------|------|------|
| `-fcf-protection=return` | 编译器 | 生成影子栈兼容代码 |
| `-Xlinker --cetcompat` | 链接器 | 在 PE 中设置 `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` |

## 手动构建（不使用 Make）

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fcf-protection=return \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -Xlinker --cetcompat \
  -lntoskrnl -lhal \
  -o CetDriver.sys driver.c
```

## 功能说明

- 在 `\Device\CetDriver` 创建设备对象
- 在 `\DosDevices\CetDriver` 创建符号链接
- 通过间接调用（`ComputeFn` 函数指针）验证 CET 兼容性——影子栈保护这些调用的返回地址
- 通过 `DbgPrint` 输出加载/卸载消息

---

## CET 技术详解

CET 有**两个独立的保护机制**：

### 1. 影子栈 — 后向边保护（RET）

硬件维护第二个栈（影子栈）来镜像 CALL/RET。**不需要在函数头添加任何特殊指令**——完全透明：

```
┌─ CALL target ─────────────────────────────────┐
│                                                │
│  常规栈:     PUSH return_addr  (RSP)           │
│  影子栈:     PUSH return_addr  (SSP, 硬件)     │
│                                                │
└────────────────────────────────────────────────┘

┌─ RET ─────────────────────────────────────────┐
│                                                │
│  常规栈:     POP return_addr_A  (RSP)          │
│  影子栈:     POP return_addr_B  (SSP, 硬件)    │
│                                                │
│  比较: return_addr_A == return_addr_B ?         │
│    ✓ 匹配   → 正常返回                         │
│    ✗ 不匹配 → #CP 异常 (BUGCHECK)              │
│                                                │
└────────────────────────────────────────────────┘
```

影子栈管理指令（操作系统用于上下文切换，不放在函数头）：

```asm
RDSSPQ  rax         ; 读取当前影子栈指针
INCSSPQ rax         ; 推进 SSP（丢弃条目）
SAVEPREVSSP         ; 保存前一个影子栈令牌
RSTORSSP [addr]     ; 恢复到已保存的影子栈
WRSS  [addr], rax   ; 写入管理员影子栈
WRUSS [addr], rax   ; 写入用户影子栈（仅 ring 0）
SETSSBSY            ; 标记当前影子栈为繁忙
CLRSSBSY [addr]     ; 清除繁忙标记
```

### 2. 间接分支跟踪（IBT） — 前向边保护（间接 CALL/JMP）

需要在每个合法的间接调用/跳转目标处放置 `ENDBR64` 指令（`F3 0F 1E FA`，4 字节）。
在不支持 CET 的 CPU 上，`ENDBR64` 等效于 NOP。

```
┌─ 间接 CALL/JMP ──────────────────────────────┐
│                                               │
│  CPU 设置内部 TRACKER = WAIT_FOR_ENDBR        │
│  跳转到目标地址...                             │
│                                               │
│  目标处第一条指令是 ENDBR64 ?                   │
│    ✓ 是 → 清除 TRACKER，正常执行               │
│    ✗ 否 → #CP 异常                            │
│                                               │
│  直接 CALL/JMP 不设置 TRACKER                  │
│                                               │
└───────────────────────────────────────────────┘
```

### Windows 内核的选择

| 保护 | 机制 | Windows 内核是否使用？ |
|------|------|----------------------|
| 后向边（RET） | CET 影子栈 | **是** (KCET) |
| 前向边（间接 CALL/JMP） | CET IBT (ENDBR) | **否** — 用 CFG 替代 |

因此默认使用 `-fcf-protection=return`：仅启用影子栈，不生成 ENDBR64。
如需 ENDBR64 可改为 `-fcf-protection=full`（在 Windows 上为无害 NOP，但提供 Linux 移植兼容性）。

### 汇编对比：`-fcf-protection` 各模式

源代码：

```c
unsigned long rotate13(unsigned long val) {
    return (val << 13) | (val >> 19);
}
```

#### `-fcf-protection=none`（无 CET）

```asm
rotate13:
    mov  eax, ecx
    rol  eax, 13
    ret
```

#### `-fcf-protection=return`（仅影子栈——本例使用此模式）

```asm
rotate13:
    mov  eax, ecx      ; 与 "none" 完全相同！
    rol  eax, 13        ; 影子栈完全透明——
    ret                 ; 硬件在 CALL/RET 时自动操作
```

代码生成与 `none` **完全相同**。唯一区别是链接器标志 `--cetcompat` 在 PE 调试目录中
设置 `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` 位，告知 Windows 此二进制文件
兼容影子栈。

#### `-fcf-protection=full`（影子栈 + IBT）

```asm
rotate13:
    endbr64             ; ← IBT 标记 (F3 0F 1E FA)
    mov  eax, ecx       ;    在非 CET CPU 上为 NOP
    rol  eax, 13        ;    Windows 不使用（CFG 处理前向边）
    ret
```

`ENDBR64` 出现在每个函数入口。在 Windows 上每个函数浪费 4 字节，但不会造成问题。

---

## 编译器 vs bin2bin：谁对 CET 友好？

CET 在**源码级编译器**和 **bin2bin 工具**（加壳、混淆、hook、dump+rebuild）
之间画了一道清晰的红线。硬件 Shadow Stack 强制三条规则，重塑整个防护 /
混淆产业：

> 1. **不要修改返回地址。**
> 2. **不要自修改代码**（HVCI 强制代码页 W^X）。
> 3. **寻找尊重 1、2 的强混淆变换。**

### 编译器能"加密返回地址"吗？

**不能。** 这是个常见误解。Shadow Stack 由 CPU 而非操作系统强制，对用户态
代码不可见。如果你在函数 epilogue 里 XOR 加密主栈上的返回地址：

```c
void my_func() {
    // ... 函数体 ...
    // epilogue 尝试加密返回地址：
    // XOR [rsp], 0xDEADBEEF
    // RET           <- 硬件比较主栈 vs 影子栈
                     //   不再匹配 -> #CP 异常 -> BUGCHECK
}
```

影子栈仍保存着原始返回地址。RET 触发硬件比较；不匹配则触发 `#CP`，BUGCHECK
内核。编译器**无法**触及影子栈：

- 用户态：没有任何指令能写影子栈
- 内核态：`WRSSQ` 是特权指令，只有 `ntoskrnl` 使用

### 编译器能做的 CET 友好混淆

| 变换 | 为什么 CET 安全 |
|------|--------------|
| **控制流平坦化** | switch dispatcher 用直接 CALL/JMP；cases 需要时加 ENDBR64 |
| **VM 虚拟化** | handler 之间用间接 JMP（带 ENDBR64）连接，不用 push+ret |
| **字符串 / 常量加密** | 纯数据变换，不影响控制流 |
| **MBA 表达式** | `x + y → (x ^ y) + 2*(x & y)` —— 仅数据 |
| **不透明谓词** | 通过直接跳转实现条件分支 |
| **函数克隆 / 内联** | 不改变调用栈语义 |
| **指令替换** | `MOV → XOR + ADD` —— 不影响栈 |

### CET 敌对模式（KCET 下会崩）

| 模式 | 为何不可行 |
|------|----------|
| **返回地址加密** | 影子栈不匹配 → `#CP` |
| **PUSH addr; RET dispatcher**（经典 VMProtect / Themida 风格） | 同上 —— 影子栈没有 `addr` 这一项 |
| **Stack pivoting**（ROP gadget chain） | 影子栈无法跟随 pivot |
| **自修改代码** | HVCI 阻止对可执行页的写 |
| **运行时代码生成** | 同上 —— HVCI W^X 违规 |
| **基于 trampoline 的 inline hook** | 修改函数 prologue 触发 HVCI；即使绕过 HVCI，trampoline RET 处的影子栈也会出问题 |

### 为什么 bin2bin 工具有结构性劣势

编译器从语义化的 IR 生成 CET 正确的代码。bin2bin 工具必须从编译后的字节
**重新发现**语义：

1. **指令边界歧义** —— x86 是变长指令。在错误偏移加 ENDBR64（4 字节）会破坏所有 RIP-relative 寻址和重定位。
2. **间接目标识别** —— bin2bin 不能总是判断 `.data` 里哪些地址是跳转表项 vs 原始数据。要么过度标记（代码膨胀、新的 ROP gadget 起点），要么标记不足（运行时 `#CP`）。
3. **自证危险** —— 设置 `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` 是一种承诺。如果 bin2bin 输出包含任何 CET 敌对模式，驱动在非 CET 机器上能加载，但在 KCET 主机上立刻 BSOD。
4. **CFG 完整性** —— 编译器看到完整调用图；bin2bin 必须推断，没有精确目标的间接调用迫使保守的 ENDBR 放置。

### 产业现状

| 工具 / 类别 | CET 状态 |
|------------|---------|
| **NeverC / Clang / MSVC（编译器）** | 通过 `-fcf-protection` + 链接器标志原生 CET 友好 |
| **OLLVM / Tigress / NeverC passes** | IR 级变换 → 天然 CET 安全 |
| **Microsoft Detours (4.0+)** | 已更新为 CET 兼容 |
| **VMProtect / Themida（旧版）** | Push+RET dispatcher 在 KCET 主机上杀死驱动 |
| **VMProtect / Themida（新版）** | 添加 ENDBR-aware dispatcher，混合支持 |
| **Manual map / dump+rebuild loader** | 必须重建所有 ENDBR 标记 —— 容易出错 |

### 游戏安全视角

反作弊驱动（EAC、BattlEye、FACEIT AC、Vanguard）出厂时设置了 `--cetcompat`，
因此可以在启用 KCET 的机器上正常运行。
作弊驱动——通常通过 bin2bin 工具加壳、hook 或 trampoline 注入——很难保持
CET 合规。KCET + HVCI 形成一道**"编译器友好、bin2bin 敌对"的硬件壁垒**，
不对称地有利于工程化良好的安全软件，而非恶意代码风格的程序。

这就是 Microsoft 对内核软件如此推动 KCET 的更深层原因：让合法内核代码更
容易加固，同时让攻击者技术逐渐变得更难。

---

## 在目标机器上启用 KCET

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

需要重启。通过 `msinfo32.exe` → "内核模式硬件强制堆栈保护" 验证。

**要求：**

- 目标机器启用 HVCI
- Windows build 21389 或更高
- 支持 CET 的 CPU（Intel Tiger Lake+ / AMD Zen 3+）

## 加载

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

请启用测试签名或使用代码签名证书用于生产环境。
