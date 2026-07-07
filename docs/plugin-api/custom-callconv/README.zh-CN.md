**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# 自定义调用约定

NeverC 支持**数据驱动的自定义调用约定** —— 你可以通过外部插件或源码属性，将任意物理寄存器分配给任意函数的参数和返回值，无需修改编译器本体或任何 TableGen 定义。

## 概述

传统 LLVM 调用约定通过 `.td` / `.inc` 文件固化在后端。增加或修改约定需要编辑编译器源码并重新运行 TableGen。NeverC 用**运行时数据驱动**方案替代了这一流程：

- 一份**寄存器分配清单**（纯字符串）作为字符串属性附加到函数上。
- 后端读取这份清单，将参数 / 返回值分配到指定的物理寄存器。
- 清单可来自**外部插件**（IR pass）、**源码属性**（`__attribute__` / `__declspec`），或两者兼用。

调用约定从"编译期写死在后端"变成"运行期由外部策略驱动"。

## 清单格式

清单是分号分隔的字符串。每段由 key 和逗号分隔的寄存器名组成（大小写不敏感、容忍空白）：

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| 段名 | 别名 | 含义 |
|---|---|---|
| `args` | | **位置模式**：每项为寄存器名或 `stack`/`mem`，按参数索引逐个指定 |
| `gpr` | `arg_gpr` | **池模式**：整数/指针参数寄存器，按顺序使用，用尽溢出到栈 |
| `xmm` | `arg_xmm` | **池模式**：浮点/向量参数寄存器 |
| `fpr` | `arg_fpr` | AArch64 的 `xmm` 别名 |
| `ret_gpr` | `ret` | 整数/指针返回值寄存器 |
| `ret_xmm` | | 浮点/向量返回值寄存器 |
| `ret_fpr` | | AArch64 的 `ret_xmm` 别名 |
| `csr` | | 自定义 callee-saved 寄存器集合（默认：标准 ABI 集合） |

### 两种参数模式

**池模式**（`gpr:` / `xmm:`）：整数参数按顺序从 `gpr` 池取寄存器，浮点参数从 `xmm` 池取。池耗尽后剩余参数溢出到栈。

**位置模式**（`args:`）：第 *i* 个参数使用第 *i* 项 token。token 可以是寄存器名或 `stack` / `mem`（强制该参数走栈）：

```
args:rcx,stack,r8;ret:rax   # 参数0→rcx, 参数1→栈, 参数2→r8, 返回→rax
```

`args` 段存在时优先于 `gpr` / `xmm`。类型不匹配（如整型参数用了 XMM 名）、索引越界、寄存器已被占用等情况均回退到栈槽。

### 支持的架构

| 架构 | GPR 名 | SIMD 名 | 位宽选择 |
|---|---|---|---|
| **x86-64** | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | `xmm0`–`xmm15` | i32→32 位子寄存器, i64→64 位 |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`, i64→`x`, f16→`h`, f32→`s`, f64→`d`, f128/vec→`q` |

### 约束

- **Callee-saved**：默认使用标准 ABI 集合。用 `csr:r12,r13` 声明自定义集合（函数只保存/恢复这些寄存器）。x86-64 与 AArch64 均支持。
- **保留寄存器**：栈指针（`rsp` / `sp`）以及 AArch64 的 `x29`/`x30`（FP/LR）永远不能作为参数/返回寄存器 —— spec 里写到它们会被直接跳过。
- **csr 冲突**：若某寄存器同时出现在 `csr` 与参数/返回列表里，bridge 会发出警告（callee 会保存/恢复它，破坏其传值作用）。
- **变参函数**：不支持 —— 编译器会输出明确错误而非静默错传参数。
- **间接调用**：函数指针调用无法携带自定义约定。插件在函数地址被取时发出警告；间接调用回退到标准约定。
- **尾调用**：自定义约定函数自动禁用尾调用（保守安全策略）。

## 用法

### 1. 插件驱动（推荐）

参考插件 `CustomCallConvPlugin.c` 位于 `pluginsdk/examples/`。

**编译插件：**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib   # 或 .so / .dll
```

**属性模式**（默认）—— 只影响有 `custom_attr` 源码标注的函数：

```bash
neverc -fplugin-pass=./CustomCallConvPlugin.dylib input.c -o output.o
```

**全局模式** —— 给所有函数套用指定约定（需显式 `cc-all=1`）：

```bash
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

**按名称前缀过滤：**

```bash
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccprefix=secret_ \
       -fplugin-pass-arg=ccspec="gpr:r9,r8;ret:rax" \
       input.c -o output.o
```

**多样化** —— 每个函数使用不同布局（反逆向工程）：

```bash
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccshuffle=1 \
       input.c -o output.o
```

### 2. 源码属性

在 C 源码中使用 `custom_attr` 属性直接标注函数（支持 GNU 和微软语法）：

```c
// GNU 语法
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

// 微软语法
__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

`custom_attr("key", "value")` 生成干净的函数字符串属性（`"key"="value"`），**无**警告、**不进** `llvm.global.annotations`。这是一个**通用**机制 —— 任意 key/value 均可，不限于调用约定。IR/MIR pass 用 `F.getFnAttribute("key")` 读取。

### 3. 组合使用

源码属性和插件参数可以同时使用。带有 `custom_attr` 的函数由插件的属性模式路径处理；`cc-all=1` 覆盖其余函数。每个函数最多处理一次。

## LTO 支持

插件同时注册 `NEVERC_INTERPOSE_POST_OPT`（普通编译）和 `NEVERC_INTERPOSE_LTO_POST_OPT`（LTO 优化管线之后）。这确保在链接时优化合并翻译单元后仍能应用自定义约定 —— 实现跨翻译单元的调用点同步，拥有完整的模块可见性。

## 插件 API

插件使用 API v2 新增的单一入口：

```c
API->FunctionSetCustomCallConv(F, "gpr:r10,r11,rsi;ret:rdx");
```

此调用会设置 `CallingConv::NeverC_Custom`（CC 1000）、写入 `"neverc-callconv"` 字符串属性，并**同步所有直接调用点**（每个 call 指令也会设置 CC 和属性）。传入 `NULL` 或 `""` 可清除自定义约定。

## 测试

GoogleTest 套件位于 `tests/neverc/CustomCallConvTests.cpp`（22 个测试，全部 PASS）：

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

覆盖范围：

| 类别 | 测试数 |
|---|---|
| x86-64 池/位置/栈/溢出/i64/sret/byval/回退 | 9 |
| AArch64 GPR/FPR/栈/`csr`/非统一 spec 跨调用 | 5 |
| 前端 `custom_attr`（GNU / `__declspec` / 端到端） | 3 |
| 加固（`csr` / 变参 / 间接 / rsp 拒绝 / csr 冲突告警） | 5 |

## 架构

```
源码                      插件
  │                        │
  ▼                        ▼
custom_attr("neverc-callconv", spec)
  │                        │
  └────────┬───────────────┘
           ▼
  "neverc-callconv" = spec   (函数字符串属性)
           │
           ▼
  ┌─────────────────────────────────┐
  │   后端执行器（每目标架构一次）   │
  │   CC_X86_NeverC / RetCC_X86_.. │
  │   CC_AArch64_NeverC / RetCC_.. │
  │                                 │
  │   读取清单 → 分配寄存器        │
  │   调用方注入被调方清单          │
  │   尾调用禁用                    │
  └─────────────────────────────────┘
           │
           ▼
  使用自定义寄存器布局的机器码
```

后端执行器是**一次性实现** —— 所有策略决策都在插件中。新增调用约定永远不需要重新构建 NeverC。
