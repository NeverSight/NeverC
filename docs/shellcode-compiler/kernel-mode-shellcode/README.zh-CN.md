**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 编译器](../README.zh-CN.md)

# 内核模式（Ring-0）Shellcode 支持

`-fshellcode` 最初仅覆盖 ring-3 载荷（PEB walk、`svc`/`syscall` stub、libSystem 桥接）。Ring-0 载荷（Windows 驱动、Linux/Android 内核模块、macOS kext）不能简单复用 ring-3 ABI：TEB/PEB 不存在、syscall 指令是用户到内核的陷入（驱动不应发出它们）、x86_64 还需要不同的代码模型和禁用红区。

## 1. 核心开关：`-mshellcode-context={user,kernel}`

- **用户模式**（默认）：维持现有 PEB / syscall stub 流水线。
- **内核模式**：
  - 禁用 `SyscallStubPass`（`svc`/`syscall` 在 ring-0 中无意义）。
  - 禁用 `WinPEBImportPass`（PEB 在用户态 TEB 中，ring-0 不可达）。
  - Windows 内核 `TargetDesc` 清除 TCB/PEB 读取模板和 syscall 寄存器描述。
  - 注入平台特定驱动标志（见[第 3 节](#3-per-platform-driver-flag-differences)）。
  - 启用 `KernelImportPass` 进行自动解析器支持的调用点改写（[第 4 节](#4-kernelimportpass-automatic-resolver-injection)）。
  - 注入 `-D__NEVERC_SHELLCODE_KERNEL__=1`，使用户态 shim 头（`<windows.h>` / `<unistd.h>` / 等）发出 `#error`，防止意外包含。

## 2. `TargetDesc` 新字段

执行级别是 `describeTriple()` 的附加维度：

- `TargetDesc::Level`：`User` 或 `Kernel`。
- `TargetDesc::KernelImport`：`WindowsKernelResolverShim` / `LinuxKallsymsShim` / `DarwinXNUKextShim` / `None`。仅 `Level == Kernel` 时有意义。
- `TargetDesc::KernelInjectFlags`：仅内核模式的驱动标志静态数组。

为新 OS/arch 添加内核模式支持仍是"多加一行表"。

## 3. 每平台驱动标志差异

| 维度 | x86_64 内核 | AArch64 内核 |
|------|------------|-------------|
| 红区 | `-mno-red-zone` | 天然不存在 |
| 代码模型 | `-mcmodel=kernel` | 复用现有 `-mcmodel=small` |
| 隐式 SIMD | `-mno-sse -mno-sse2 -mno-mmx` | `-mgeneral-regs-only` |
| 栈探针 | 继承 `-mno-stack-arg-probe` | 相同 |

这些标志以"用户态基线 + 内核增量"方式叠加。

## 4. `KernelImportPass`：自动解析器注入

Ring-0 符号解析在平台间差异显著：

- Windows：`PsLoadedModuleList` + `RtlFindExportedRoutineByName`，或 `MmGetSystemRoutineAddress`。
- Linux/Android：`kallsyms_lookup_name`（5.7+ 需 kprobe 变通），或 ksymtab。
- macOS：`OSKextLookupKextWithIdentifier` + Mach-O 符号表。

`KernelImportPass` **自动重写未解析的 extern 直接调用为解析器支持的间接调用**。用户写普通 C；pass 处理改写。

### 4.1 隐式参数注入

当模块含需要解析器的 extern 直接调用时，用户代码：
```c
void shellcode_entry(void) {
    printk("hello %d\n", 7);
}
```

被转换为等效于：
```c
void shellcode_entry(void *__resolver, void *__cookie) {
    void *fn = __resolver(hash("printk"), __cookie);
    ((int(*)(const char*, ...))fn)("hello %d\n", 7);
}
```

用户无需手动写 resolver/cookie 参数 — `KernelImportPass` 在入口前端注入。纯计算载荷或已显式接受 `neverc_kern_resolve_t` 的载荷不触发此自动前置。

### 4.2 调用点优先改写

每个直接 extern 调用点就地替换：resolver → hash → cast → forward args。选择调用点改写（而非通用包装器）是为了支持 `printk("x=%d", v)` 等可变参数辅助函数。

### 4.3 哈希算法

FNV-1a 64 位，与 `<neverc/kernel.h>` 中的 `neverc_kern_hash()` 一致。pass 在哈希前剥离前导下划线（Mach-O `_` 前缀）以确保跨平台一致。

### 4.4 Loader 调用约定

```c
typedef int (*Entry)(void *resolver, void *cookie /*, user params... */);
Entry e = (Entry)shellcode_memory;
e(my_resolver, my_cookie);
```

### 4.5 三层防御

如果 `KernelImportPass` 无法完成改写：
1. **IR 层**：pass 输出诊断
2. **MIR 层**：`ShellcodeMIRPrepPass::auditExternalReferences` 重审计
3. **提取器**：拒绝所有未解析重定位

### 4.6 内核辅助函数名表驱动诊断

`Tables/KernelHelperNames.def` 列出每个 OS 的常见 ring-0 辅助函数。两条诊断路径均查询此表。

## 5. Android 内核 vs Android Ring-3

- Ring-3：bionic + Linux syscall ABI。复用 Linux arm64 表项。
- Ring-0：纯 Linux 内核（GKI/KMI）。复用 `LinuxKallsymsShim` + `KernelInjectFlags`。

## 6. 头文件划分

| 模式 | 允许的 shim 头 | 拒绝的 shim 头 |
|------|-------------|-------------|
| 用户态 | `<windows.h>` / `<unistd.h>` / 等 | `<neverc/kernel.h>` |
| 内核态 | `<neverc/kernel.h>` / `<string.h>` / `<stdlib.h>` / `<stddef.h>` / `<stdint.h>` | `<windows.h>` / `<unistd.h>` / 等 |

`<neverc/kernel.h>` 暴露 `neverc_kern_resolve_t`、`neverc_kern_hash()` 和 `NEVERC_KERNEL_ENTRY` 宏。

## 7. 编写 Ring-0 Shellcode

### 7.1 纯计算载荷

```c
#include <neverc/kernel.h>
NEVERC_KERNEL_ENTRY
int shellcode_entry(int a, int b) {
    int s = a * 13 + b * 7;
    for (int i = 0; i < 4; ++i) s ^= (s << 3) + i;
    return s;
}
```

```bash
neverc -fshellcode -mshellcode-context=kernel \
       -target aarch64-linux-gnu shellcode.c -o sc.bin
```

### 7.2 基于解析器的载荷（推荐用于真实驱动）

```c
#include <neverc/kernel.h>
typedef void (*PrintkFn)(const char *fmt, int a, int b);
NEVERC_KERNEL_ENTRY
int shellcode_entry(neverc_kern_resolve_t resolver, void *cookie,
                    int a, int b) {
    PrintkFn pf = (PrintkFn)resolver(neverc_kern_hash("printk"), cookie);
    if (pf) pf("neverc: %d %d\n", a, b);
    return a * 13 + b * 7;
}
```

### 7.3 宿主驱动骨架（Linux LKM 示例）

```c
static void *my_resolver(uint64_t hash, void *cookie) {
    struct sym_table *tbl = cookie;
    for (int i = 0; tbl[i].name; ++i)
        if (fnv1a(tbl[i].name) == hash) return tbl[i].addr;
    return NULL;
}
typedef int (*Entry)(void *(*)(uint64_t, void*), void*, int, int);
Entry e = (Entry)kmem;
int result = e(my_resolver, st, 1, 2);
```

## 8. 路线图

| 阶段 | 状态 | 内容 |
|------|------|------|
| 内核上下文切换 + 平台标志 | 完成 | `-mshellcode-context=kernel`、`KernelInjectFlags`、pass 门控 |
| 解析器改写 + 诊断回退 | 完成 | `KernelImportPass` 自动调用点改写；MIR / 提取器回退 |
| Ring-0 纯计算载荷 | 完成 | 8 triple 覆盖 |
| 基于解析器的载荷 | 完成 | `<neverc/kernel.h>` + 压力测试覆盖 |
| 自动 extern → 解析器改写 | 完成 | `KernelImportPass` 含隐式参数注入 |
| 内核 SDK 头子集 | 计划中 | 根据真实驱动载荷需求添加 |
