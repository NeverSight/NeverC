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
