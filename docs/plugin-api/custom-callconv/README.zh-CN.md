**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 插件 ABI](../README.zh-CN.md)

# 自定义调用约定

NeverC 支持**数据驱动的自定义调用约定** —— 你可以完全通过外部插件或源码属性，把任意物理寄存器分配给任意函数的参数和返回值，无需修改编译器本体，也不用改动任何 TableGen 定义。

## 概述

传统 LLVM 调用约定通过 `.td` / `.inc` 文件固化在后端。新增或修改一个约定需要编辑编译器源码并重新运行 TableGen。NeverC 用**运行时数据驱动**模型取代了这套流程，它由两层构成：

- **spec** —— 一个人类可手写的短字符串，例如 `gpr:rcx,rdx;ret:rax` —— 由插件或源码属性作为 `"neverc-callconv"` 字符串属性附加到函数上。
- 代码生成之前，宿主把这份 spec **物化**成 `"neverc-cc-plan-v1"` 属性：一张不可变的、经过校验的精确位置表，且绑定到特定的目标 schema。后端只消费 plan。

spec 是你写的东西，plan 是后端信任的东西。调用约定由此从"编译期写死在后端"变成"运行期由外部策略驱动"，同时没有放弃校验。

## Spec 格式

spec 是分号分隔的字符串。每一段由一个 key 和逗号分隔的寄存器名列表组成（大小写不敏感，容忍空白）：

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| 段名 | 别名 | 含义 |
|---|---|---|
| `args` |  | **位置模式**：每一项是寄存器名或 `stack`/`mem`，按参数索引逐个对应 |
| `gpr` | `arg_gpr` | **池模式**：整数/指针参数寄存器，按顺序取用，用尽后溢出到栈 |
| `xmm` | `arg_xmm` | **池模式**：浮点/向量参数寄存器 |
| `fpr` |  | `xmm` 的目标中立别名 |
| `ret_gpr` | `ret` | 整数/指针返回值寄存器 |
| `ret_xmm` |  | 浮点/向量返回值寄存器 |
| `ret_fpr` |  | `ret_xmm` 的目标中立别名 |
| `csr` |  | 自定义 callee-saved 寄存器集合（默认为标准 ABI 集合） |

任何一段都可以省略，无法识别的段会被忽略。这些 key 只在 [`llvm/include/llvm/CodeGen/NeverCCallConv.h`] 中定义一次，因此生产者与解析器不会产生偏离。

### 两种参数模式

**池模式**（`gpr:` / `xmm:`）：整数参数按顺序从 `gpr` 池取寄存器，浮点与向量参数从 `xmm` 池取。某个池耗尽后，剩余参数溢出到栈。

**位置模式**（`args:`）：第 *i* 个参数使用第 *i* 项 token。每项要么是寄存器名，要么是 `stack` / `mem`，后者强制该参数走栈：

```
args:rcx,stack,r8;ret:rax   # 参数0→rcx、参数1→栈、参数2→r8、返回值→rax
```

`args` 段存在时优先于 `gpr` / `xmm`。若某项 token 与参数类型的寄存器类别不符、索引超出 token 列表范围，或寄存器已被占用，都会回退到栈槽，而不会让编译失败。

### 支持的架构

寄存器名通过按目标划分的表来解析，该表是 spec 可以书写哪些名字的唯一依据。

| 架构 | GPR 名 | SIMD 名 | 位宽选择 |
|---|---|---|---|
| **x86-64** | `rax`、`rbx`、`rcx`、`rdx`、`rsi`、`rdi`、`rbp`、`r8`–`r15` | `xmm0`–`xmm15` | i32 → 32 位子寄存器，i64/指针 → 64 位 |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`，i64→`x`，f16→`h`，f32→`s`，f64→`d`，f128/向量→`q` |

GPR 一律写 64 位形式，后端会按每个值的类型收窄到对应子寄存器。AArch64 的向量寄存器写作 `v0`–`v31`，后端按类型挑选 `H`/`S`/`D`/`Q` 形式。

### 约束

- **保留寄存器**：栈指针不在两张表内（x86-64 的 `rsp`，AArch64 的 `sp`/`x31`），AArch64 的 `x29`/`x30`（FP/LR）同样不在。spec 里写到它们会被直接跳过，该值落到下一个合法位置。
- **帧指针**：x86-64 上 `rbp` **是**可选的，因为它本就是合法的 callee-saved 寄存器；但把它当参数寄存器只在 `-fomit-frame-pointer` 下才成立，风险自负。
- **Callee-saved**：默认使用标准 ABI 集合。`csr:r12,r13` 声明自定义集合，调用方会构造与之匹配的保留寄存器掩码，从而知道哪些寄存器能跨调用存活。x86-64 与 AArch64 均支持。
- **csr 冲突**：若某寄存器同时出现在 `csr` 与参数/返回列表中，插件会发出警告 —— callee 会恢复它，从而破坏它的传值作用。编译仍会成功。
- **变参函数**：不支持。两个后端都会给出明确诊断，而不是静默地错传变参部分。
- **间接调用**：函数指针调用无法携带自定义约定。当自定义约定函数的地址被取用时插件会警告；间接调用回退到标准约定。
- **尾调用**：只要调用的任一侧使用自定义约定，两个后端都会禁用尾调用。
- **未覆盖的值**：plan 未覆盖的参数或返回值回退到目标的标准约定（x86-64 用 SysV，AArch64 用 AAPCS）。

## 用法

### 1. 插件驱动（推荐）

参考插件 [`CustomCallConvPlugin.c`] 位于 `pluginsdk/examples/`。它在 `neverc.ir.pass.post_opt` 阶段注册了一个模块级 IR pass。

**编译插件：**

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib   # 或 .so / .dll
```

**属性模式**（默认）—— 只影响带有 `custom_attr` 源码标注的函数：

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib input.c -o output.o
```

**全局模式** —— 给每个已定义函数套用同一份 spec（需要显式给出 `cc-all`）：

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

**按名称前缀过滤：**

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccprefix=secret_ \
       -fplugin-arg=org.neverc.example.custom-callconv:ccspec="gpr:r9,r8;ret:rax" \
       input.c -o output.o
```

**多样化** —— 在四种内置布局间轮换，使函数之间不共用同一份布局（反逆向）：

```bash
neverc -fplugin=./CustomCallConvPlugin.dylib \
       -fplugin-arg=org.neverc.example.custom-callconv:cc-all \
       -fplugin-arg=org.neverc.example.custom-callconv:ccshuffle \
       input.c -o output.o
```

插件注册的四个选项是 `cc-all` 与 `ccshuffle`（标志型，`=1` 或 `=true` 可省略），以及 `ccspec` 与 `ccprefix`（字符串值）。未给出 `ccspec` 时，全局模式使用默认值 `gpr:r10,r11,rsi,rdi;ret:rdx`。

### 2. 源码属性

在 C 源码中用 `custom_attr` 属性直接标注函数，支持 GNU 与微软两种语法：

```c
// GNU 语法
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

// 微软语法
__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

`custom_attr("key", "value")` 生成干净的函数字符串属性（`"key"="value"`），**不产生**警告，**也不进入** `llvm.global.annotations`。这是一个**通用**机制 —— 任意 key/value 都可以，不限于调用约定。IR 与 MIR pass 用 `F.getFnAttribute("key")` 读回。

### 3. 组合使用

源码属性与插件参数可以同时使用。带 `custom_attr` 的函数走插件的属性模式路径，`cc-all` 覆盖其余函数。每个函数最多被处理一次。

## 物化的 plan

spec 只指定寄存器名，并没有说明每个值的每个字节落在哪里。在优化管线结束之后、代码生成之前，宿主会运行 `materializeCallingConventionPlans`，把每个 `CallingConv::NeverC_Custom` 函数转换成精确且经过校验的 plan：

- 已经带有 `"neverc-cc-plan-v1"` 属性的函数只会被**校验，不会被重新生成** —— 它的 schema 摘要、目标 ID 和约定 ID 必须与当前目标一致。
- 带有 `"neverc-callconv"` spec 的函数，其寄存器名会对照目标寄存器表解析。生成的 plan 取代该 spec，spec 随后从 IR 中移除。
- 两者都没有、但其目标通过插件 ABI 注册了调用约定的函数，由该约定的 `PlanCallingConvention` 回调来规划。

每个直接调用点都会继承被调方的 plan，这正是调用方与被调方跨翻译单元保持布局一致的原因。plan 是一个扁平字符串：

```
neverc-cc-plan-v1;schema=<摘要>;target=<high>:<low>;cc=<high>:<low>;stack=<字节>;returns=<位置>;arguments=<位置>;callee-saved=<寄存器编号>
```

每个位置的格式是 `<r|s>,<值索引>,<片偏移>,<大小>,<对齐>,<寄存器编号>,<栈偏移>,<标志>`，多个位置之间用 `|` 分隔。内置路径的 schema 摘要是 `llvm-<目标三元组>`；由插件注册的目标则提供自己的摘要。

由于寄存器编号只在定义它的 schema 下才有意义，不匹配会直接报错，而不是静默生成错误代码：

| 情形 | 诊断信息 |
|---|---|
| plan 字符串无法解析 | `malformed NeverC calling convention plan` |
| schema 摘要不一致 | `NeverC calling convention plan belongs to a foreign target schema` |
| 目标 ID 不一致 | `NeverC calling convention plan has a foreign target ID` |
| 约定 ID 不一致 | `NeverC calling convention plan has a foreign convention ID` |

正是这一点让 plan 可以安全地嵌入 bitcode 并穿过 LTO：为另一个目标生成的 plan 不可能被误用。

## 插件 API

示例插件只用到了稳定的 IR core 表 —— 并不存在专用的调用约定入口。给一个函数施加约定，是三次调用外加调用点同步：

```c
NevercIRAttributeHandle Attribute = {0};
Core->CreateStringAttribute(Core->Context, Task, SV("neverc-callconv"), Spec,
                            &Attribute);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION, 0, Attribute);
Core->SetFunctionCallingConvention(Core->Context, Task, Function,
                                   NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM);
```

`NEVERC_IR_CALLING_CONVENTION_NEVER_C_CUSTOM` 是 `CallingConv::NeverC_Custom`（LLVM 值 1000）在 ABI 层的稳定名称。随后插件用 `GetValueUseCount` / `GetValueUse` 遍历该函数的所有使用点，对每一个作为 `call`、`invoke` 或 `callbr` 被调用方操作数的使用点，通过 `SetInstructionProperty` 配合 `NEVERC_IR_PROPERTY_CALLING_CONVENTION` 给指令设置相同的约定。其余任何使用点都意味着地址发生了逃逸，这正是"地址被取用"警告的来源。

如果插件注册了自己的目标，也可以在其 `NevercCallingConventionDescriptor` 上提供 `PlanCallingConvention` 回调直接产出 plan，跳过 spec 这一层。参见[目标、MC、汇编与目标文件](../target-mc-object.zh-CN.md#abi-与调用约定)。

## 测试

GoogleTest 套件位于 [`tests/neverc/CustomCallConvTests.cpp`]，共 26 个测试。每个测试都会构建示例插件、在给定 spec 下把一小段程序编译成汇编，然后断言最终的寄存器或栈位置。

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```

覆盖范围：

| 类别 | 测试数 |
|---|---|
| x86-64 池 / 位置 / 栈 / 溢出 / i64 / sret / byval / 回退 | 9 |
| AArch64 GPR / FPR / 栈 / `csr` / 非统一 spec 跨调用 | 5 |
| 前端 `custom_attr`（GNU / `__declspec` / 端到端） | 3 |
| plan 物化与 schema 拒绝 | 3 |
| 加固（`csr`、两个目标上的变参、间接调用、`rsp`、csr 冲突） | 6 |

## 架构

```
源码属性                       插件 IR pass
custom_attr(...)              (neverc.ir.pass.post_opt)
       │                            │
       └─────────────┬──────────────┘
                     ▼
   "neverc-callconv" = spec、CallingConv::NeverC_Custom
   施加到函数及其直接调用点
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ materializeCallingConventionPlans        │
   │ （优化之后、代码生成之前）               │
   │                                          │
   │  spec       → 把名字解析成物理寄存器     │
   │  插件约定   → PlanCallingConvention      │
   │  已有 plan  → 校验 schema / 目标         │
   └──────────────────────────────────────────┘
                     │
                     ▼
   "neverc-cc-plan-v1" = 已校验的位置表
   spec 被移除；plan 复制到各直接调用点
                     │
                     ▼
   ┌──────────────────────────────────────────┐
   │ 后端 CCAssignFn（每个目标一份）          │
   │  CC_X86_NeverC     / RetCC_X86_NeverC    │
   │  CC_AArch64_NeverC / RetCC_AArch64_NeverC│
   │                                          │
   │  读取 plan → 分配位置                    │
   │  未覆盖的值 → 标准约定                   │
   │  禁用尾调用                              │
   └──────────────────────────────────────────┘
                     │
                     ▼
   使用自定义寄存器布局的机器码
```

后端执行器是**一次性实现** —— 所有策略决策都在插件里。新增一个约定永远不需要重新构建 NeverC。

上面用到的核心表见 [`PluginIR.h`]，`NevercCallingConventionDescriptor` 见 [`PluginTarget.h`]，该 pass 挂载的 `neverc.ir.pass.post_opt` 阶段见 [`Schema/PhaseSchema.json`]。

<!-- reference links -->
[`CustomCallConvPlugin.c`]: ../../../pluginsdk/examples/CustomCallConvPlugin.c
[`llvm/include/llvm/CodeGen/NeverCCallConv.h`]: ../../../llvm/include/llvm/CodeGen/NeverCCallConv.h
[`PluginIR.h`]: ../../../neverc/include/neverc/Plugin/PluginIR.h
[`PluginTarget.h`]: ../../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/PhaseSchema.json`]: ../../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`tests/neverc/CustomCallConvTests.cpp`]: ../../../tests/neverc/CustomCallConvTests.cpp
