**语言**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

[← NeverC 插件 ABI](README.zh-CN.md)

# NeverC 插件 Target、MC、汇编与目标文件 API

后端是四个头文件、二十九个阶段。[`PluginTarget.h`] 描述一个目标以及穿过代码生成
的各条路由。[`PluginMC.h`] 构造并观察机器码，汇编的解析与打印也在同一个头文件
里。[`PluginObject.h`] 把可重定位文件变成归一化的图，再变回去。

它们合起来让插件可以新增一个目标、替换某一个降级步骤或全部步骤、在每条指令发射
时观察它、定义一种汇编方言，或者改写一个目标文件——全程通过纯 C ABI，永远不会
暴露 LLVM 的 `MCInst`、`MCSection` 或 `object::ObjectFile`。

## 接口

```c
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"   /* 已包含上面两个 */
```

| 接口 | 表 | 槽位 | 用途 |
|---|---|--:|---|
| `NEVERC_INTERFACE_TARGET_*` | `NevercTargetAPI` | 2 | `RegisterTarget`、`RegisterCodeGenEdge` |
| `NEVERC_INTERFACE_TARGET_ABI_*` | `NevercTargetABIAPI` | 1 | `RegisterABI` |
| `NEVERC_INTERFACE_CALLING_CONVENTION_*` | `NevercCallingConventionAPI` | 1 | `RegisterCallingConvention` |
| `NEVERC_INTERFACE_MC_*` | `NevercMCAPI` | 53 | 读写 `MCUnit`；注册编码器、解码器、后端 |
| `NEVERC_INTERFACE_MC_EMISSION_*` | `NevercMCEmissionAPI` | 7 | 发射事件与布局快照 |
| `NEVERC_INTERFACE_MC_PROVIDER_*` | `NevercMCProviderAPI` | 4 | 替换 MIR → MC |
| `NEVERC_INTERFACE_ASSEMBLY_PROVIDER_*` | `NevercAssemblyProviderAPI` | 8 | 替换汇编解析器或打印器 |
| `NEVERC_INTERFACE_OBJECT_*` | `NevercObjectAPI` | 34 | 读写 ObjectGraph |
| `NEVERC_INTERFACE_OBJECT_FORMAT_*` | `NevercObjectFormatAPI` | 1 | `RegisterFormat` |
| `NEVERC_INTERFACE_OBJECT_PHASE_*` | `NevercObjectPhaseAPI` | 2 | `GetGraph`、`GetImage` |

## 两个兼容性层级

这条规则统辖本文其余所有内容。

**STABLE**，可以放心硬编码：与目标无关的描述符、阶段 ID、artifact ID、MC 与
ObjectGraph 容器、输出事务，以及所有回调契约。

**LOCKSTEP**，不检查就用是不安全的：目标相关的 opcode、寄存器、操作数、fixup、
重定位和调用约定 schema。它们的数值只对某一个确切的 schema 修订版有意义。

凡是出现 LOCKSTEP 值的地方，旁边都会有一个 schema 摘要。读值之前先比对它：

```c
if (!string_equal(Target.SchemaDigest, MY_COMPILED_SCHEMA_DIGEST))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

NeverC 也会在调用 Provider 之前拒绝不匹配的 schema，所以这道检查是双保险——但
跳过它、照样去读裸 opcode 的插件，会默默地把指令解释错。

## 阶段

一共二十九个，分属四个域。

### `codegen` —— 路由（4 个）

| 阶段 | 策略 |
|---|---|
| `neverc.codegen.ir_to_mir` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.mir_to_mc` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.coarse_lower` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.product_verify` | OBSERVABLE, **SEALED** |

### `mc` —— 机器码（13 个）

`neverc.mc.encode`、`neverc.mc.decode`、`neverc.mc.layout` 是 OBSERVABLE、
INTERCEPTABLE、REPLACEABLE。

`neverc.mc.emission.pre_instruction` 是唯一同时可 REPLACEABLE 的发射事件——替
换指令就在这里做。另外九个（`unit_begin`、`unit_end`、`section_change`、
`post_instruction`、`post_encode`、`fixup`、`relaxation_round`、`pre_layout`、
`post_layout`）只能观察。

### `assembly`（4 个）

`neverc.assembly.parse` 与 `neverc.assembly.print` 可替换；
`neverc.assembly.final_verify` 和 `neverc.assembly.commit` 是 SEALED。

### `object`（8 个）

`neverc.object.probe`、`read`、`write`、`pre_write`、`post_layout` 可替换；
`neverc.object.post_write` 只可拦截；`neverc.object.final_verify` 与
`neverc.object.commit` 是 SEALED。

## 注册目标

`NevercTargetDescriptor` 是整个 ABI 里最大的描述符，因为它要携带前端和后端需要
知道的全部信息：

```c
typedef struct NevercTargetDescriptor {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercStringArrayView Aliases;
  NevercStructArrayView TripleMatchers;    /* NevercTargetTripleMatcher[] */
  NevercTargetABIID DefaultABI;
  NevercCallingConventionID DefaultCallingConvention;
  NevercInterfaceID MCSchemaID;
  NevercInterfaceID DefaultObjectFormatID;
  NevercTargetMachineDescriptor Machine;
  NevercStructArrayView Macros;            /* 预定义宏                   */
  NevercStructArrayView Builtins;          /* 目标 builtin 及其降级      */
  NevercStructArrayView Registers;         /* 内联汇编寄存器名           */
  NevercStructArrayView Constraints;       /* 内联汇编约束               */
  NevercStringView Clobbers;
  uint64_t Flags;
  NevercTargetValidateCPUFn ValidateCPU;
  NevercTargetCanonicalizeCPUFn CanonicalizeCPU;
  NevercTargetListCPUsFn ListCPUs;
  NevercTargetResolveFeaturesFn ResolveFeatures;
  NevercCreateTargetMachineFn CreateTargetMachine;
  NevercDestroyTargetMachineFn DestroyTargetMachine;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercTargetDescriptor;
```

`TripleMatchers` 决定何时选中这个目标：每个匹配器指明架构、厂商、操作系统和环
境，外加一个 `Priority`，用来在与内建目标冲突时决出胜负。

`Machine` 是一个 `NevercTargetMachineDescriptor`——数据布局、默认与可调 CPU、
特性表、支持的 ABI／调用约定／目标文件格式、地址空间、重定位与代码模型（同时给
出默认值和支持位掩码）、异常模型（`NONE`、`DWARF`、`SJLJ`、`SEH`、`WASM`）、展
开模型、字节序、指针／int／long／long long 的宽度、栈对齐、最大原子与向量宽度、
`va_list` 种类、执行级（`USER`、`KERNEL`、`HYPERVISOR`、`FIRMWARE`）以及 TLS 支
持。

目标 builtin 自带降级回调，回调会收到一个可用的 IR builder：

```c
static NevercStatus NEVERC_CALL
lower_builtin(void *UserData,
              const NevercTargetBuiltinLoweringInvocation *In,
              NevercIRValueHandle *OutResult) {
  /* In->Core, In->Builder, In->Mutation, In->IRBuilder,
     In->ResultType, In->Arguments, In->ArgumentCount */
  return In->Builder->BuildCall(/* … */);
}
```

## ABI 与调用约定

ABI 负责对函数签名分类：

```c
static NevercStatus NEVERC_CALL
classify(void *UserData, const NevercABIFunctionQuery *Query,
         NevercABIArgumentClassification *ReturnValue,
         NevercABIArgumentClassificationArray *Arguments) {
  ReturnValue->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  for (uint64_t I = 0; I != Arguments->Count; ++I) {
    NevercABIArgumentClassification *A = &Arguments->Data[I];
    A->Kind  = NEVERC_ABI_ARGUMENT_INDIRECT;
    A->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
  }
  return neverc_status_ok();
}
```

参数种类有 `DIRECT`、`EXTEND`、`INDIRECT`、`IGNORE`、`EXPAND`、
`INDIRECT_ALIASED`、`COERCE_AND_EXPAND`；标志有 `BYVAL`、`REALIGN`、`INREG`、
`SRET_AFTER_THIS`、`CAN_BE_FLATTENED`、`SIGN_EXTEND`、`PADDING_INREG`。强制转换
是 `NONE`、`INTEGER`、`FLOAT` 或 `POINTER`，`COERCE_AND_EXPAND` 另外提供一组
`NevercABICoercionElement`。

调用约定再低一层，负责分配实际位置：

```c
static NevercStatus NEVERC_CALL
plan(void *UserData, const NevercCallingConventionQuery *Query,
     NevercCallingConventionPlan *Plan) {
  /* Query->TargetID, ->CallingConventionID, ->SchemaDigest, ->Function */
  /* 用 NevercCallingConventionLocation 记录填充 Plan->ReturnLocations
     和 Plan->ArgumentLocations：REGISTER 或 STACK、ValueIndex、
     PieceOffset、Size、Alignment、RegisterNumber、StackOffset，
     以及 INDIRECT / BYVAL 标志。                                    */
  Plan->CalleeSavedRegisters = MySavedRegisters;
  Plan->StackAlignment       = 16;
  return neverc_status_ok();
}
```

`Query->SchemaDigest` 是 LOCKSTEP 值——`RegisterNumber` 只在它指名的那个 schema
下才有意义。完整示例见
[自定义调用约定](custom-callconv/README.zh-CN.md#物化的-plan) 和
[`pluginsdk/examples/CustomCallConvPlugin.c`]。

## 代码生成路由

路由由规范的 `NevercTargetKey` 选出：目标 ID、triple 各部分、CPU、tune CPU、特
性、ABI、调用约定、目标文件格式、重定位模型、代码模型、执行级、指针宽度、字节序
和 schema 摘要。注册你能承担的边：

```c
NevercCodeGenEdgeDescriptor Edge = {0};
Edge.Header          = /* … */;
Edge.EdgeID          = MyEdgeID;
Edge.CanonicalName   = SV("com.example.mir-to-mc");
Edge.TargetID        = MyTargetID;
Edge.InputKind       = NEVERC_CODEGEN_PRODUCT_MIR;
Edge.OutputKind      = NEVERC_CODEGEN_PRODUCT_MC;
Edge.CompatibilityKey = SV("…");
Edge.ProviderID      = SV("com.example.backend");
Target->RegisterCodeGenEdge(Target->Context, RegistrarContext, &Edge);
```

产物种类有 `IR`、`MIR`、`MC`、`ASSEMBLY`、`OBJECT_GRAPH`、`OBJECT_IMAGE`、
`CUSTOM`。细粒度路由是 `IR → MIR → MC → ObjectGraph → ObjectImage`。

设置 `NEVERC_CODEGEN_EDGE_COARSE` 并提供 `CoarseLower`，就能一步替换掉整个
`IR → ObjectImage` 区间：

```c
static NevercStatus NEVERC_CALL
coarse_lower(void *UserData, NevercTaskHandle Task,
             const NevercCodeGenRequest *Request,
             NevercCodeGenProductCandidate *OutCandidate) {
  /* Request->Target, ->Input, ->InputKind, ->OutputKind,
     ->OptimizationLevel, ->HasFinalIRProof                */
  OutCandidate->Kind      = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  OutCandidate->Artifact  = MyImage;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

粗粒度路由同样要过 `neverc.codegen.product_verify` 和事务式输出提交。
`VerifyProduct` 被调用时会带上宿主期望你已经履行的义务——`VERIFY_FINAL_IR`、
`VERIFY_TARGET_KEY`、`VERIFY_PRODUCT_KIND`、`VERIFY_PRODUCT_ID`、
`VERIFY_STRUCTURE`——所以 Provider 无法靠抄近路悄悄绕过某道关卡。

## 构造 MC

一个 `MCUnit` 承载节、符号、表达式、片段、指令、操作数和 fixup。读取采用
first/next 迭代：

```c
NevercMCUnitInfo Unit = {0};
Unit.Header = /* … */;
MC->GetUnitInfo(MC->Context, Task, UnitHandle, &Unit);

NevercMCSectionHandle Section;
MC->GetFirstSection(MC->Context, Task, UnitHandle, &Section);
while (!neverc_handle_is_null(Section)) {
  NevercMCFragmentHandle Fragment;
  MC->GetFirstFragment(MC->Context, Task, Section, &Fragment);
  /* … */
  MC->GetNextSection(MC->Context, Task, Section, &Section);
}
```

修改是事务式的，和别处一样：

```c
NevercMCMutationHandle Mutation;
MC->BeginMutation(MC->Context, Task, Unit, &Mutation);
MC->CreateSection(MC->Context, Task, Mutation, &SectionDescriptor, &Section);
MC->CreateSymbol(MC->Context, Task, Mutation, &SymbolDescriptor, &Symbol);
MC->AppendInstruction(MC->Context, Task, Mutation, Section, &Instruction);
Status = MC->CommitMutation(MC->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MC->AbandonMutation(MC->Context, Task, Mutation);
```

句柄是任务作用域且带代号校验的，所以来自已放弃 mutation 的句柄会被拒绝而不是被
重用。

节标志有 `ALLOCATED`、`EXECUTABLE`、`WRITABLE`、`MERGEABLE`、`DEBUG`。符号绑定
有 `LOCAL`、`GLOBAL`、`WEAK`；类型有 `NONE`、`FUNCTION`、`OBJECT`、`SECTION`、
`TLS`；定义状态有 `UNDEFINED`、`SECTION`、`ABSOLUTE`、`COMMON`。表达式支持一元
`PLUS`、`MINUS`、`NOT` 和二元 `ADD`、`SUBTRACT`、`MULTIPLY`、`DIVIDE`、`AND`、
`OR`、`XOR`、`SHIFT_LEFT`、`SHIFT_RIGHT`。想让宿主替你决定位置时传
`NEVERC_MC_AUTOMATIC_OFFSET`。

`RegisterSchema` 发布一份目标 MC schema，`GetSchemaToken` /
`GetSchemaTokenInfo` 在名字与 LOCKSTEP token 之间互相解析。

## 观察发射过程

发射流按顺序报告十种事件——对应每个 `neverc.mc.emission.*` 阶段各一种。ABI 还
保留了 `NEVERC_MC_EMISSION_PRE_OBJECT_WRITE`；对象写入本身是单独的
`neverc.object.pre_write` 阶段。以观察者身份订阅并读取事件：

```c
NevercMCEmissionEventInfo Event = {0};
Event.Header = /* … */;
Emission->GetEvent(Emission->Context, Frame, Frame->Input, &Event);
/* Event.Kind, Event.Flags */
```

`Flags` 说明事件里哪些部分是有效的：`HAS_SECTION`、`HAS_INSTRUCTION`、
`HAS_ENCODING`、`HAS_FIXUP`、`HAS_LAYOUT`、`CAN_REPLACE_INSTRUCTION`。读取对应字
段之前先检查标志——一个尚无编码结果的事件不会因为你问了就凭空有一个。

一旦 `HAS_LAYOUT` 置位，`GetLayoutSection`、`GetLayoutFragment`、
`GetLayoutSymbol`、`GetLayoutFixup` 就能给出地址和大小。

在 `pre_instruction` 处，且仅当 `CAN_REPLACE_INSTRUCTION` 置位时，可以替换指
令：

```c
const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
NevercMCInstHandle Instruction;
Emission->BeginInstructionReplacement(Emission->Context, Frame, Continuation,
                                       &MC, &Unit, &Instruction);
/* mutate Instruction through MC->BeginMutation / … / CommitMutation */
Emission->PublishInstructionReplacement(Emission->Context, Frame, Continuation,
                                         &OutResult->Output);
```

[`pluginsdk/examples/MCObserverPlugin.c`] 是它的只读版本。

## 编码器、解码器与布局

三种注册扩展机器码后端，全部按目标和 schema 摘要索引：

```c
MC->RegisterEncoder(MC->Context, RegistrarContext, &EncoderDescriptor);
MC->RegisterDecoder(MC->Context, RegistrarContext, &DecoderDescriptor);
MC->RegisterAsmBackend(MC->Context, RegistrarContext, &BackendDescriptor);
```

编码器通过 sink 写出而不是返回缓冲区，这样所有权始终留在宿主一侧：

```c
Sink->WriteBytes(Sink->Context, Bytes);
Sink->AddFixup(Sink->Context, &Fixup);
```

解码器报告 `NEVERC_MC_DECODE_SUCCESS`、`_SOFT_FAIL`、`_UNKNOWN` 或 `_FAIL` 之
一。fixup 种类通过 `NevercMCFixupKindInfo` 自我描述，带 `PC_RELATIVE`、
`SIGNED`、`RELAXABLE`、`TARGET` 标志。

汇编后端负责松弛（relaxation）。布局会产出一个证明摘要，而**布局之后的任何修改
都会让该证明失效**，必须重新布局才能写出目标文件——和链接图用的是同一套代号校
验模式。

## 汇编

解析器 Provider 消费源字节并发布一个 `MCUnit`：

```c
NevercAssemblyParseInputInfo In = {0};
In.Header = /* … */;
Asm->GetParseInput(Asm->Context, Frame, Frame->Input, &In);

NevercAssemblyTokenInfo Token = {0};
Asm->PeekSourceToken(Asm->Context, Frame, In.Source.Cursor, &Token);
Asm->AdvanceSourceToken(Asm->Context, Frame, In.Source.Cursor);

const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
Asm->GetParseMCBuilder(Asm->Context, Frame, &MC, &Unit);
/* … build into Unit … */
Asm->PublishParsedMCUnit(Asm->Context, Frame, &Output);
```

源要么是 `NEVERC_ASSEMBLY_SOURCE_BUFFER`，要么是
`NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS`。经预处理的汇编（`.S`）先走正常的前端
预处理器，以渲染后的 token 形式到达；纯汇编（`.s`）直接以缓冲区进入解析器。

打印器方向相反——`GetPrintInput`，然后 `WritePrintOutput` 写进提供的输出事务，
最后 `PublishAssemblyOutput`。往别处写是不支持的：解析／打印校验和宿主提交关卡
都在字节可见之前运行，所以打印失败不会留下半个文件。

## 目标文件图

`NevercObjectAPI` 把可重定位文件归一化为节、符号、重定位和 COMDAT。内建适配器覆
盖 ELF、COFF、Mach-O；`RegisterFormat` 可以再加一种。

```c
NevercObjectGraphInfo Info = {0};
Info.Header = /* … */;
Object->GetGraphInfo(Object->Context, Task, Graph, &Info);
/* Info.Target, .ObjectSchemaDigest, .Generation, .SectionCount,
   .SymbolCount, .RelocationCount, .ComdatCount, .HasLayoutProof */

NevercObjectSymbolHandle Symbol;
Object->GetFirstSymbol(Object->Context, Task, Graph, &Symbol);
while (!neverc_handle_is_null(Symbol)) {
  NevercObjectSymbolInfo SymInfo = {0};
  SymInfo.Header = /* … */;
  Object->GetSymbolInfo(Object->Context, Task, Symbol, &SymInfo);
  Object->GetNextSymbol(Object->Context, Task, Symbol, &Symbol);
}
```

四类实体的修改都遵循 create/replace/move/erase 模式，暂存在
`BeginMutation` … `CommitMutation` / `AbandonMutation` 之间。

节标志有 `ALLOCATED`、`EXECUTABLE`、`WRITABLE`、`MERGEABLE`、`STRINGS`、`TLS`、
`DEBUG`、`UNWIND`、`DISCARDABLE`、`RETAIN`。重定位目标是 `SYMBOL`、`SECTION`、
`ABSOLUTE` 或 `FORMAT_EXTENSION`。

每个描述符都有 `ExtensionOwner` / `ExtensionVersion` / `Extension` 三元组。格式
借此保留归一化图里没有字段可放的数据——这些字节跟着实体走，写回时原样带回，而
不是在往返过程中被丢掉。

内建 ELF 适配器用带标签的 extension 保存精确原生事实：`NCSE v2` 保存段索引、
地址、类型、flags、文件偏移和 entry size；`NCSY v2` 保存 `st_info`、完整
`st_other`、`st_size`，以及明确的原生名称空／非空状态；`NCRL v1` 保存原生重定位
类型及其官方名称。因此，普通的 ELF 空名符号会继续保持空名，绝不会被改写为合成的
`$symbol.N`；源码中真的叫 `$symbol.N` 的符号仍是普通非空名称。未改动的原生镜像
直通可以精确保留匿名符号；一旦改为以图为权威的内建写回，因为可移植 MC 拼写无法
重建同一个匿名符号表项，宿主会在打开输出 sink 前拒绝。Android canonical release
审计要求当前版本的完整标签 payload，并由这些原生事实重放稳定图投影。

### 注册一种格式

```c
NevercObjectFormatDescriptor Format = {0};
Format.Header           = /* … */;
Format.FormatID         = MyFormatID;
Format.CanonicalName    = SV("com.example.myfmt");
Format.SupportedTargets = MyTargets;
Format.DefaultExtension = SV(".mof");
Format.Flags            = NEVERC_OBJECT_FORMAT_CAN_PROBE |
                          NEVERC_OBJECT_FORMAT_CAN_READ  |
                          NEVERC_OBJECT_FORMAT_CAN_WRITE;
Format.Probe            = probe;
Format.Reader           = read;
Format.Writer           = write;
ObjectFormat->RegisterFormat(ObjectFormat->Context, RegistrarContext,
                             &Format);
```

`Probe` 报告一个从 0 到 `NEVERC_OBJECT_PROBE_MAX_CONFIDENCE`（1000）的
`Confidence`、它识别出的 `NevercObjectArtifactKind`（`RELOCATABLE`、`ARCHIVE`、
`EXECUTABLE_IMAGE`、`SHARED_IMAGE`、`UNIVERSAL_BINARY`），以及一个
`ConsumedMinimum`——它为了确认需要读多少字节，上限是
`NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM`（65536）。置信度最高者胜出。

`Reader` 会拿到一张图和一个已打开的 mutation，负责填充它们。`Writer` 会拿到
图、它的布局证明，以及受界的二进制 builder。

### Object Format 1.1 写入策略

`NevercObjectFormatDescriptor.Header.Minor` 表示 provider 的能力，而不是宿主范围
内的模式开关。1.0 descriptor 的 probe、read 和普通默认 write 仍完全兼容；它的
writer 收到 `NevercObjectWriteRequest.Header.Minor == 0` 和 `Header.Flags == 0`。
只有 writer 理解 1.1 request flags 时才应声明 minor 1；普通 write 仍携带零 flags，
保持 1.1 之前的输出行为不变。

Object Format 1.1 为 `NevercObjectWriteRequest.Header.Flags` 定义了以下位：

- `NEVERC_OBJECT_WRITE_CANONICAL_ELF_TABLES` 要求独立且规范的 `.strtab` 与
  `.shstrtab`，并重映射所有依赖索引。这只是 ELF 表规范化，而不是可重定位链接：节区顺序、
  COMDAT 组、linker 元数据、重复符号与别名、重定位记录，以及所有非名称表 payload 都保持
  不变。由格式自身定义的其他合法 `SHT_STRTAB` 节区也会保留；仅重建所选 `SHT_SYMTAB` 使用的
  字符串表和 `e_shstrndx` 指定的表。与 `DROP_DEBUG_INFO` 组合时，只过滤调试节区以及引用
  这些节区已删除索引的元数据。
- `NEVERC_OBJECT_WRITE_ANDROID_KERNEL_RELEASE` 还把最终序列化 ELF 作为权威边界：
  删除 writer 合成的 mapping symbols，并依据实际序列化节区坐标重放 release 名称。
- `NEVERC_OBJECT_WRITE_DROP_DEBUG_INFO` 请求在上述某一种 ELF 策略中删除调试节区。

`NEVERC_OBJECT_WRITE_REQUEST_KNOWN_FLAGS` 是完整的已知位掩码。合法组合仅有 `0`、
`CANONICAL_ELF_TABLES`、`CANONICAL_ELF_TABLES | DROP_DEBUG_INFO`、
`CANONICAL_ELF_TABLES | ANDROID_KERNEL_RELEASE`，以及三个位全部置位。release 或
debug 位不能脱离 canonical 位单独使用。

宿主会在打开输出 sink 之前拒绝未知或非法组合，也会拒绝发给 minor-0 provider 的
任何特殊请求。1.1 writer 同样必须拒绝、而不是忽略收到的未知或非法 flags。writer
以及任何 `object.post_write` 拦截器运行后，宿主语义校验和
sealed 的 `object.final_verify` 会再次审计序列化字节，并且它们才是权威结果。这些
flags 不是对所有第三方格式的通用承诺。minor 1 表示 writer 理解 flags 协议：它可以
实现适用的 ELF 策略；策略不适用或不受支持时，也可以明确返回
`NEVERC_STATUS_CAPABILITY_UNAVAILABLE`，但绝不能静默忽略请求。

### 最终 Android 发布的写出权限

当 `--strip` 最终生成 Android `.ko` 时，上述通用可变对象 API 会收窄为由宿主建立
并受信任的写出路径。该边界包含两个相互独立的身份封印：

- 在任何可替换的 `ObjectGraph` 阶段之前，图封印会绑定每个保留逻辑段的
  `section ID`、`final ordinal` 与精确名称，以及每个精确名称符号的
  `symbol ID`、owner、class、section、value、size、binding、type 和完整 `st_other`；
- 宿主自有 writer 建立可信映像基线后，映像封印会绑定每个保留段的 ordinal 与
  精确名称、`.symtab` 条目总数，以及每个精确名称符号的原始 `.symtab` `slot`
  与属性。完整发布验证器还会独立重算每个结构化发布名称。

| 绑定 | 最终 Android 发布行为 |
|---|---|
| `neverc.object.write` `provider` / `interceptor` | 回调前即为 `REJECTED`；它不能替换可信写出路径 |
| `plugin-owned ObjectFormat graph writer` | `REJECTED`；此路径必须使用负责建立可信基线的宿主自有 graph writer |
| `observer` | `READ_ONLY`；只能检查，不能修改或替换输出 |
| `neverc.object.post_write` `interceptor` | `VALIDATED`；其有界可变 API 只能修改结构化验证 ABI 与身份表面之外的 payload，结果仍须通过输入 ABI 检查、两道封印和完整发布验证器 |

最终合并的所有权同样由宿主封闭。来自 `third-party ObjectMergeProvider` 的
`MergedImage` 或独立字节会被丢弃，由 `host-owned graph writer` 序列化该 provider
已验证并完成收尾的图。反向一侧，`built-in finalized input serialization` 会绕过
`external object phases`，把完全一致的 `audited native bytes` 交给宿主 merger；
这一内部输入步骤不会绕过上述输出边界。

Finalization 只在 `Android module merge semantics` 下接受；
`relocatable output request` 与 `relocatable driver configuration` 也必须同时成立，
否则会在 `before routing` 失败。对于最终 Android relocatable 发布，
`frozen input format`、
`TargetKey.ObjectFormatID` 与 `frozen output format` 必须共享
`one format identity`。不一致会在 `before provider dispatch` 被拒绝——这也早于
route planning 与 sink creation——因此能力预检和实际 graph-writer dispatch
不可能看到不同格式。

native-image passthrough 会拒绝所有可替换的 `route-matching provider` 与所有
interceptor；target/CPU/features/object-format/execution-level route 不匹配的 provider
既不运行，也不阻止发布，只允许 observer。只有发生在 `before sealed commit` 的
拒绝或验证失败才会中止 staging 且不发布文件；
`AFTER_COMMIT` observer 的失败发生在发布之后，只会被报告，不能回滚已经发布的文件。

### 写出流水线

1. 探测并把字节读入 ObjectGraph；
2. 运行 `object.pre_write` 图拦截器；
3. 布局，然后运行 `object.post_layout`（任何修改之后要重新布局）；
4. 写出一个受界的候选映像；
5. 运行 `object.post_write` 二进制拦截器；
6. 运行 sealed 的 `object.final_verify` 和原子的 `object.commit`。

映像状态依次是 `CANDIDATE` → `VERIFIED` → `COMMITTED`，或者 `ABORTED` /
`FAILED_PARTIAL`。

观察者拿到的是只读桥接；从观察者里尝试修改会被
`NEVERC_STATUS_POLICY_VIOLATION` 拒绝。写入器和 post-write 拦截器只拿得到受界
的 `NevercMutableBinaryAPI` builder——`Reserve`、`Write`、`WriteAt`、`Tell`、
`ReadAt`、`Insert`、`Append`、`Resize`。越界、回调失败或校验失败都会中止暂存，
所以失败绝不会在磁盘上留下半个文件。

[`pluginsdk/examples/ObjectRewritePlugin.c`] 是一个完整的事务式改写示例。

## 规则

- 消费任何 LOCKSTEP 的 opcode、寄存器、操作数、fixup、重定位或调用约定值之前，
  先比对 schema 摘要。
- 把可变状态放进宿主提供的 process、session、task 作用域里。
- 回调返回后不要缓存任务句柄或借来的视图。
- 拦截器的 continuation 最多调用一次，且必须在回调线程上。
- 每个 `BeginMutation` 恰好对应一次 commit 或 abandon。
- 修改过已布局的 MCUnit 或 ObjectGraph 之后要重新布局；旧的布局证明已经陈旧，
  宿主会拒绝它。
- 读取事件字段之前先检查 `NevercMCEmissionEventInfo.Flags`，且只在
  `CAN_REPLACE_INSTRUCTION` 置位时才替换指令。
- 只能通过提供的事务或字节 sink 写输出。
- 失败时返回原始的 `NevercStatus`，不要发布任何半成品。
- 声明最窄的、真实的并发与可重入模型。
- `codegen.product_verify`、`assembly.final_verify`、`assembly.commit`、
  `object.final_verify`、`object.commit` 是 sealed 的，只能观察。

规范性声明见 [`PluginTarget.h`]、[`PluginMC.h`]、[`PluginObject.h`] 和
[`Schema/PhaseSchema.json`]；它们使用的实体、操作数、fixup 与节区种类来自
[`Schema/MCSchema.json`] 与 [`Schema/ObjectSchema.json`]，二者分别生成
[`Schema/PluginMCSchema.inc`] 和 [`Schema/PluginObjectSchema.inc`]。
[`coverage.json`] 把这里每个稳定阶段映射到它的正向、负向、替换、只读观察者和
sealed gate 测试。

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMC.h`]: ../../neverc/include/neverc/Plugin/PluginMC.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/MCObserverPlugin.c`]: ../../pluginsdk/examples/MCObserverPlugin.c
[`pluginsdk/examples/ObjectRewritePlugin.c`]: ../../pluginsdk/examples/ObjectRewritePlugin.c
[`PluginTarget.h`]: ../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/MCSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/MCSchema.json
[`Schema/ObjectSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/ObjectSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMCSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMCSchema.inc
[`Schema/PluginObjectSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginObjectSchema.inc
