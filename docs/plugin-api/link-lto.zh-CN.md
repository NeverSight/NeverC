**语言**: [English](link-lto.md) | [简体中文](link-lto.zh-CN.md) | [繁體中文](link-lto.zh-TW.md) | [日本語](link-lto.ja.md) | [한국어](link-lto.ko.md) | [Français](link-lto.fr.md) | [Deutsch](link-lto.de.md) | [Español](link-lto.es.md) | [Italiano](link-lto.it.md) | [Русский](link-lto.ru.md) | [العربية](link-lto.ar.md)

[← NeverC 插件 ABI](README.zh-CN.md)

# NeverC 插件链接与 LTO API

链接被建模为**一张图上的状态机**。`PluginLink.h` 暴露这张图——输入、节
（section）、原子（atom）、符号、边、COMDAT、导入、导出、展开记录、合成对象
和布局约束——以及把它从一组文件推进到已提交二进制映像的二十个阶段。
`PluginLTO.h` 覆盖中间那两个把 bitcode 变成目标文件的阶段。

插件可以观察每一步、拦截其中大部分、替换某一步、替换整个链接过程，或者合并目
标文件。它永远看不到 lld 的数据结构：这张图是一个归一化投影，ELF、COFF 和
Mach-O 后端都映射到它上面。

## 接口

```c
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"   /* 已包含 PluginLink.h */
```

| 接口 | 表 | 用途 |
|---|---|---|
| `NEVERC_INTERFACE_LINK_{HIGH,LOW}` | `NevercLinkAPI` | 读取并修改链接图（52 个槽位） |
| `NEVERC_INTERFACE_LINK_REGISTRAR_{HIGH,LOW}` | `NevercLinkRegistrarAPI` | 注册链接器、目标文件合并和映像验证器 Provider |
| `NEVERC_INTERFACE_LINK_PHASE_{HIGH,LOW}` | `NevercLinkPhaseAPI` | 从 `NevercArtifactHandle` 取到图或映像 |
| `NEVERC_INTERFACE_LTO_{HIGH,LOW}` | `NevercLTOAPI` | 读取 LTO 请求、模块和符号决议 |
| `NEVERC_INTERFACE_LTO_REGISTRAR_{HIGH,LOW}` | `NevercLTORegistrarAPI` | 注册 LTO 代码生成 Provider |

五个接口在 major 1 上都是 `NEVERC_INTERFACE_STABLE`，因此更新的宿主只能追加字
段。每个都要配上对应的 `NEVERC_LINK_API_MAJOR` / `NEVERC_LTO_API_MAJOR`，并检
查 `TableSize` 是否覆盖到你要调用的最后一个槽位。

## 状态机

`NevercLinkGraphInfo.State` 有十四个取值，而二十个阶段中的十三个存在的唯一目的
就是把它向前推进一步：

| 阶段 | 产生的 `NEVERC_LINK_STATE_…` | 宿主验证器 |
|---|---|---|
| — | `INITIAL` | — |
| `neverc.link.input_probe` | `INPUT_PROBED` | `verify_input_probe` |
| `neverc.link.read_inputs` | `INPUTS_READ` | `verify_inputs` |
| `neverc.link.lto_resolve` | `LTO_RESOLUTION_READY` | |
| `neverc.link.lto_generate` | `LTO_GENERATED` | |
| `neverc.link.resolve_symbols` | `SYMBOLS_RESOLVED` | |
| `neverc.link.select_comdat` | `COMDAT_SELECTED` | |
| `neverc.link.gc` | `GC_COMPLETE` | `verify_liveness` |
| `neverc.link.icf` | `ICF_COMPLETE` | |
| `neverc.link.synthesize` | `SYNTHETICS_READY` | |
| `neverc.link.relax_thunks` | `THUNKS_RELAXED` | `verify_relaxation` |
| `neverc.link.layout` | `LAYOUT_COMPLETE` | `verify_layout` |
| `neverc.link.relocate` | `RELOCATIONS_APPLIED` | |
| `neverc.link.emit_image` | `IMAGE_EMITTED` | |

这十三个阶段的策略都是
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE | SKIPPABLE_WITH_PROOF`，因此
Provider 可以自己提供这个状态转换，而持有有效 `NevercLinkProofHandle` 的插件可
以跳过它。

其余七个是结构性的：

| 阶段 | 策略 | 作用 |
|---|---|---|
| `neverc.link.full` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | 替换整个链接，从 `INITIAL` 直接产出二进制映像 |
| `neverc.link.object_merge` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | `-r` 可重定位合并 ObjectGraph |
| `neverc.link.post_emit` | OBSERVABLE, INTERCEPTABLE | 触碰映像字节的最后机会 |
| `neverc.link.image_verify` | OBSERVABLE, **SEALED** | 宿主映像验证器 |
| `neverc.link.side_outputs_verify` | OBSERVABLE, **SEALED** | map 文件、dSYM 等副产物 |
| `neverc.link.commit` | OBSERVABLE, **SEALED** | 输出集合的原子发布 |
| `neverc.link.after_commit` | OBSERVABLE | 提交后通知 |

三个 sealed gate 只能观察，永远不能拦截、替换或跳过。
`NEVERC_BUILTIN_LINK_PHASE_COUNT` 为 20。

## 从阶段拿到图

`NevercLinkPhaseAPI` 把帧里的 artifact 转成可用句柄：

```c
NevercLinkPhaseGraphInfo GraphInfo = {0};
GraphInfo.Header = (NevercABITableHeader){sizeof(GraphInfo),
                                          NEVERC_LINK_PHASE_API_MAJOR,
                                          NEVERC_LINK_PHASE_API_MINOR, 0};
LinkPhase->GetGraph(LinkPhase->Context, Frame, Frame->Input, &GraphInfo);
/* GraphInfo.Link, .Graph, .Proof, .State, .Generation */
```

`GraphInfo.Link` 就是绑定到当前任务的 `NevercLinkAPI`，所以观察者不需要另外
`QueryInterface`。Provider 用 `PublishGraph` 发布结果；`GetImage` 对映像
artifact 做同样的事，返回 `NevercLinkPhaseImageInfo`，里面有映像、输出集合以及
`NevercBinaryImageState`（`CANDIDATE`、`VERIFIED`、`COMMITTED`、`ABORTED` 或
`FAILED_PARTIAL`）。

## 读取图

`NevercLinkGraphInfo` 是概览——目标、格式、状态、代号（generation）、十七个实
体计数，以及 32 字节的 `SemanticDigest`。实体本身通过每种一个的分页调用返回，
它们共用同一个由调用方持有的页结构：

```c
typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;                /* 你提供的、你自己拥有的数组     */
  uint64_t ElementCapacity;  /* 能装下多少条                   */
  uint64_t ElementStride;    /* 你的元素的 sizeof              */
  uint64_t OutCount;         /* 宿主实际写了多少条             */
  uint64_t NextCursor;       /* 回传以继续下一页               */
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;
```

宿主写入的条目不会超过 `ElementCapacity` 条、每条 `ElementStride` 字节，并且不
会保留 `Data`，所以用栈上数组就行：

```c
NevercLinkSymbolInfo Symbols[64];
NevercLinkEntityPage Page = {0};
uint64_t Cursor = 0;

do {
  Page.Header = (NevercABITableHeader){sizeof(Page), NEVERC_LINK_API_MAJOR,
                                       NEVERC_LINK_API_MINOR, 0};
  Page.Data            = Symbols;
  Page.ElementCapacity = 64;
  Page.ElementStride   = sizeof(Symbols[0]);
  Status = Link->GetSymbolPage(Link->Context, Task, Graph, Cursor, &Page);
  if (Status.Code != NEVERC_STATUS_OK)
    break;
  for (uint64_t I = 0; I != Page.OutCount; ++I) {
    /* Symbols[I].Name, .Binding, .Definition, .IsPrevailing, … */
  }
  Cursor = Page.NextCursor;
} while (Page.HasMore);
```

十五个图分页器都是这个形状——`GetInputPage`、`GetArchivePage`、
`GetArchiveMemberPage`、`GetSharedLibraryPage`、`GetBitcodeModulePage`、
`GetSectionPage`、`GetAtomPage`、`GetSymbolPage`、`GetEdgePage`、
`GetComdatPage`、`GetImportPage`、`GetExportPage`、`GetUnwindPage`、
`GetSyntheticPage`、`GetConstraintPage`——另有 `GetBinarySegmentPage` 和
`GetBinarySectionPage` 用来分页已生成的映像。每个都有对应的、针对单个句柄的
`Get…Info`。

每个实体信息都带一个 `NevercLinkOrigin`：

```c
typedef struct NevercLinkOrigin {
  NevercABITableHeader Header;
  NevercLinkInputHandle Input;
  NevercLinkArchiveMemberHandle ArchiveMember;
  NevercObjectGraphHandle ObjectGraph;
  uint64_t ObjectEntityID;
  NevercInterfaceID CreatedByPhase;
  NevercStringView CreatedByProvider;
  NevercInterfaceID LastMutationPhase;
  NevercStringView LastMutationPlugin;
} NevercLinkOrigin;
```

这正是链接过程可审计的原因：对输出里的任何一个原子，你都能说出它来自哪个输入
文件、从哪个归档成员里拉进来的、由哪个阶段创建、以及最后被哪个插件改过。

### 实体一览

| 种类 | 信息结构 | 关键字段 |
|---|---|---|
| 输入 | `NevercLinkInputInfo` | `Kind`（OBJECT、ARCHIVE、SHARED_LIBRARY、BITCODE、SCRIPT、BLOB）、`Ordinal`、`ContentDigest`、`ReaderRoute` |
| 归档／成员 | `NevercLinkArchiveInfo`、`NevercLinkArchiveMemberInfo` | `Thin`、`Materialized`、`MaterializationReason` |
| 共享库 | `NevercLinkSharedLibraryInfo` | `InstallName` |
| Bitcode 模块 | `NevercLinkBitcodeModuleInfo` | `Summary` |
| 节 | `NevercLinkSectionInfo` | `Kind`、`Flags`、`Alignment`、`Address`、`Size`、`Comdat` |
| 原子 | `NevercLinkAtomInfo` | `Flags`、`Content`、`ZeroFillSize`、`FoldLeader` |
| 符号 | `NevercLinkSymbolInfo` | `Binding`、`Visibility`、`Definition`、`IsPrevailing`、`IsRoot` |
| 边 | `NevercLinkEdgeInfo` | `Kind`、`Offset`、`RelocationKind`、`Addend`、`TargetSymbol`、`TargetAtom` |
| COMDAT | `NevercLinkComdatInfo` | `Selection`、`Selected` |
| 导入／导出 | `NevercLinkImportInfo`、`NevercLinkExportInfo` | `Library`、`Symbol` |
| 展开 | `NevercLinkUnwindInfo` | `PersonalitySymbol` |
| 合成对象 | `NevercLinkSyntheticInfo` | `Role`、`Section`、`Atom` |
| 约束 | `NevercLinkConstraintInfo` | `Kind`、`SubjectID`、`Value`、`Required` |

原子标志有 `LIVE`、`ROOT`、`SYNTHETIC`、`FOLDED`、`ADDRESS_SIGNIFICANT`、
`TLS`、`UNWIND`。符号绑定有 `LOCAL`、`GLOBAL`、`WEAK`、`COMMON`；定义状态有
`UNDEFINED`、`DEFINED`、`ABSOLUTE`、`COMMON`、`SHARED`。边的种类有
`RELOCATION`、`ASSOCIATION`、`KEEP_ALIVE`、`UNWIND`、`FORMAT_EXTENSION`。
COMDAT 选择策略包括 `ANY`、`EXACT_MATCH`、`SAME_SIZE`、`LARGEST`、`NEWEST`、
`NO_DUPLICATES`。

## 修改图

修改是事务式的，且始终限定在一张图上：

```c
NevercLinkMutationHandle Mutation;
Link->BeginMutation(Link->Context, Task, Graph, &Mutation);

Link->SetSymbolRoot(Link->Context, Task, Mutation, Symbol, NEVERC_TRUE);
Link->ReplaceAtomContent(Link->Context, Task, Mutation, Atom,
                         (NevercByteView){Bytes, Length},
                         /*ZeroFillSize=*/0);

Status = Link->CommitMutation(Link->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  Link->AbandonMutation(Link->Context, Task, Mutation);
```

提交会先在工作副本上暂存、校验，通过后才发布并递增 `Generation`。
`AbandonMutation` 丢弃全部改动。举例来说，在图处于 `GC_COMPLETE` 时提交会重跑
存活性验证器，所以一个会遗留悬空存活原子的改动会被拒绝，而不是被写进去。

### 修改会让下游状态失效

这一点最容易让人措手不及。每个暂存调用都会被归类，而归类决定了**最早失效的状
态**；宿主必须从那里开始重跑后续所有阶段：

| 调用 | 最早失效的状态 |
|---|---|
| `RebindSymbol`、`RetargetEdge` | `SYMBOLS_RESOLVED` |
| `SetSymbolResolution` | `COMDAT_SELECTED` |
| `SetSymbolRoot` | `GC_COMPLETE` |
| `SetAtomLive` | `ICF_COMPLETE` |
| `SetFoldLeader`、`ReplaceAtomContent` | `SYNTHETICS_READY` |
| `CreateSynthetic`、`ReplaceSynthetic`、`EraseSynthetic` | `SYNTHETICS_READY` |
| `CreateConstraint`、`ReplaceConstraint`、`EraseConstraint` | `LAYOUT_COMPLETE` |

一次修改若涉及多项，取其中最小者。因此在布局之后重新绑定符号，会把布局、重定
位和映像结果统统作废——在 `gc` 阶段这样做很便宜，在 `post_emit` 阶段就很贵。
在你的改动允许的前提下，尽量在状态机的早期动手。

`SetSymbolResolution` 接收的是一条小的更新记录而不是整个符号，这样一次决议变更
就不会顺手改掉名字或取值：

```c
NevercLinkSymbolResolutionUpdate Update = {0};
Update.Header = (NevercABITableHeader){sizeof(Update), NEVERC_LINK_API_MAJOR,
                                       NEVERC_LINK_API_MINOR, 0};
Update.Binding      = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
Update.Visibility   = NEVERC_LINK_SYMBOL_VISIBILITY_HIDDEN;
Update.Definition   = NEVERC_LINK_SYMBOL_DEFINED;
Update.IsPrevailing = NEVERC_TRUE;
Update.IsExported   = NEVERC_FALSE;
Link->SetSymbolResolution(Link->Context, Task, Mutation, Symbol, &Update);
```

## 用证明跳过阶段

`SKIPPABLE_WITH_PROOF` 的阶段接受一个 `NevercLinkProofHandle` 来代替真正执行。
证明会钉住这次跳过所依赖的一切：

```c
typedef struct NevercLinkProofInfo {
  NevercABITableHeader Header;
  NevercLinkProofHandle Proof;
  NevercLinkGraphHandle Graph;
  NevercLinkState State;
  uint32_t Reserved;
  uint64_t GraphGeneration;
  NevercTargetID TargetID;
  NevercObjectFormatID FormatID;
  NevercInterfaceID OutputArtifact;
  uint8_t RouteDigest[32];
  uint8_t SemanticDigest[32];
  uint64_t ImageBase;
  uint64_t EntryAddress;
} NevercLinkProofInfo;
```

由于 `GraphGeneration` 和 `SemanticDigest` 都被记录下来，从签发证明到使用证明之
间任何一次已提交的修改都会让它失效，宿主随即真正执行该阶段。

## 二进制映像

`emit_image` 之后的产物是一个 `NevercBinaryImageHandle`：

```c
NevercBinaryImageInfo Image = {0};
Image.Header = /* … */;
Link->GetBinaryImageInfo(Link->Context, Task, ImageHandle, &Image);
/* .State, .OutputKind, .EntryAddress, .ImageBase, .Size,
   .SegmentCount, .SectionCount, .ImportCount, .ExportCount,
   .DynamicRelocationCount, .ContentDigest                      */
```

输出类型有 `RELOCATABLE`、`EXECUTABLE`、`SHARED_LIBRARY`、`BUNDLE`。段标志有
`READ`、`WRITE`、`EXECUTE`。

`Image.Binary` 和 `Image.Builder` 是来自 `PluginObject.h` 的受界事务写入器——
`Reserve`、`Write`、`WriteAt`、`Tell`、`ReadAt`、`Insert`、`Append`、`Resize`。
在 `post_emit` 拦截器里修补字节必须走它；越过预留边界的写入会中止暂存，而不是
把文件撑大。

## Provider

只能在 `Register` 期间注册，之后不行。

### 替换链接器

```c
NevercLinkerProviderDescriptor Provider = {0};
Provider.Header = (NevercABITableHeader){sizeof(Provider),
                                         NEVERC_LINK_REGISTRAR_API_MAJOR,
                                         NEVERC_LINK_REGISTRAR_API_MINOR, 0};
Provider.ProviderID   = SV("com.example.my-linker");
Provider.TargetID     = MyTargetID;
Provider.InputFormat  = ELFFormatID;
Provider.OutputFormat = ELFFormatID;
Provider.OutputKind   = NEVERC_LINK_OUTPUT_EXECUTABLE;
Provider.Flags        = NEVERC_LINK_PROVIDER_DETERMINISTIC |
                        NEVERC_LINK_PROVIDER_CACHEABLE;
Provider.Link         = my_link;
Provider.VerifyImage  = my_verify;      /* 可选 */
LinkRegistrar->RegisterLinkerProvider(LinkRegistrar->Context,
                                      RegistrarContext, &Provider);
```

回调收到请求和原始输入集合，然后填一个候选产物：

```c
static NevercStatus NEVERC_CALL
my_link(void *UserData, NevercTaskHandle Task,
        const NevercLinkRequest *Request,
        const NevercRawLinkInputSet *Inputs,
        NevercLinkerProductCandidate *OutCandidate) {
  /* Request->Target, ->OutputKind, ->OutputURI, ->Options, ->RequestDigest
     Inputs->Inputs 是 NevercRawLinkInput[]，Inputs->OrderDigest 钉住顺序 */
  OutCandidate->Image     = MyImage;
  OutCandidate->Outputs   = MyBundle;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

`NevercLinkOptions` 携带链接器真正会据以分支的那些标志——`PIE`、`STATIC`、
`GC_SECTIONS`、`ICF`、`EXPORT_DYNAMIC`、`ALLOW_UNDEFINED`、`WHOLE_ARCHIVE`、
`DETERMINISTIC`——外加 `EntrySymbol`、`InstallName`、`Soname`、`ImageBase`、
`PageSize`、`ThreadBudget`、搜索路径和库列表。单个输入上的标志是
`WHOLE_ARCHIVE`、`AS_NEEDED`、`START_GROUP`、`END_GROUP`、`LAZY`。

成功时宿主接管候选产物；失败时 Provider 仍然要为自己创建的东西负责。无论哪种
情况，sealed 的 verify 与 commit 阶段照常运行。

### 合并目标文件与验证映像

`RegisterObjectMergeProvider` 处理 `-r`：请求里带着输入的
`NevercObjectMergeInput[]`，以及一个已经打开的输出图和输出 mutation，所以
Provider 是往宿主拥有的事务里写，而不是自己造一个文件。

`RegisterBinaryImageVerifier` 添加一个只读检查，与宿主自己的映像验证器并行运
行。它不能取代宿主的验证器。

## LTO

`lto_resolve` 产出符号决议，`lto_generate` 把 bitcode 变成目标文件。
`NevercLTOAPI` 读取两者。

```c
NevercLTORequest Request = {0};
Request.Header = /* … */;
LTO->GetRequest(LTO->Context, Task, RequestHandle, &Request);
/* .LinkRequest, .LinkGraph, .Target, .OutputFormat, .Options,
   .Modules, .Resolutions, .ResolutionDigest, .RequestDigest */
```

`GetModulePage` 和 `GetResolutionPage` 用的是同一套
`NevercLinkEntityPage` 协议，分别填充 `NevercLTOInputModuleInfo` 和
`NevercLTOSymbolResolution`。每条决议指明模块、符号、对应的
`NevercLinkSymbolHandle` 以及标志位：

| 标志 | 含义 |
|---|---|
| `PREVAILING` | 该定义归这个模块所有。 |
| `VISIBLE_TO_REGULAR_OBJECT` | 非 bitcode 的目标文件能看到它。 |
| `EXPORTED` | 出现在动态符号表里。 |
| `FINAL_DEFINITION` | 之后的定义不能再取代它。 |
| `CAN_INLINE` | 允许跨边界内联。 |
| `CAN_INTERNALIZE` | 允许内部化。 |
| `LINKER_REDEFINED` | 链接器覆盖了它。 |
| `REFERENCED_BY_REGULAR_OBJECT` | 有普通目标文件引用它。 |

`NevercLTOOptions` 选择 `NEVERC_LTO_FULL` 还是 `NEVERC_LTO_THIN`，以及优化等
级、`ThreadBudget`、`ThinBackendPartitions`、CPU 与特性，还有取值为
`DISABLED`、`TASK`、`LOCAL_SHARED`、`REMOTE_SHARED` 的缓存作用域。选项标志有
`EMIT_OPTIMIZED_BITCODE`、`EMIT_INDEX`、`SAVE_TEMPS`、
`WHOLE_PROGRAM_VISIBILITY`、`UNIFIED_LTO`、`DETERMINISTIC`。

### LTO Provider

```c
NevercLTOProviderDescriptor Provider = {0};
Provider.Header = /* … */;
Provider.ProviderID    = SV("com.example.my-lto");
Provider.TargetID      = MyTargetID;
Provider.Flags         = NEVERC_LTO_PROVIDER_THIN |
                         NEVERC_LTO_PROVIDER_DETERMINISTIC |
                         NEVERC_LTO_PROVIDER_CACHEABLE;
Provider.BuildCacheKey = my_cache_key;
Provider.Codegen       = my_codegen;
LTORegistrar->RegisterProvider(LTORegistrar->Context, RegistrarContext,
                               &Provider);
```

`BuildCacheKey` 写入调用方提供的 `NevercMutableByteView` 并报告它需要的大小，
这样宿主可以据此扩容重试。它必须是请求的纯函数——用 `RequestDigest` 和
`ResolutionDigest` 推导是安全的构造方式。声明了 `CACHEABLE` 却用了忽略请求中某
一部分的 key，会产出即使全新构建也依然存在的陈旧目标文件。

`Codegen` 填充 `NevercLTOProductCandidate`：一组 `NevercLTOObjectProduct`（每个
指明它的源模块、ObjectGraph 和 artifact），可选的 `OptimizedBitcode` 和
`ThinIndex`，以及它实际使用的 `CacheKey`。

## 规则

- 句柄是任务作用域且归宿主所有。不要在回调之外保存、不要跨任务使用、不要自己
  编造句柄值。
- `NevercLinkEntityPage.Data` 归你所有。宿主最多写入
  `ElementCapacity × ElementStride` 字节，并且不保留对它的引用。
- 每个 `BeginMutation` 都要恰好对应一次 `CommitMutation` 或
  `AbandonMutation`，错误路径上也不例外。
- 在改动允许的前提下尽量在状态机早期修改；晚期修改会悄无声息地作废所有下游阶
  段。
- 不要在观察者里修改。观察者拿到的是只读桥接，尝试修改会被
  `NEVERC_STATUS_POLICY_VIOLATION` 拒绝。
- 只能通过 `NevercBinaryImageInfo.Binary` 及其 builder 写映像字节。越界会中止暂
  存，而不是把输出撑大。
- 只有当相同的请求摘要总是产出逐字节一致的输出时才声明 `DETERMINISTIC`；只有当
  缓存 key 覆盖了所有能改变输出的输入时才声明 `CACHEABLE`。
- `image_verify`、`side_outputs_verify` 和 `commit` 是 sealed 的。观察它们即
  可，不要尝试拦截或跳过。

规范性声明见 `PluginLink.h` 与 `PluginLTO.h`，二十个阶段的策略见
`Schema/PhaseSchema.json`，逐阶段的测试锚点见 `coverage.json`。
