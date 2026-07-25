**语言**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

[← NeverC 插件 ABI](README.zh-CN.md)

# NeverC 插件源与 I/O API

`PluginSource.h` 发布两张表。`NevercIOAPI` 是文件系统：虚拟文件 Provider、读
取、目录遍历、输出 sink 和依赖记录。`NevercSourceLocationAPI` 把编译器内部的位
置映射回文件、行号和拼写文本。有了这两者，插件可以提供一个只存在于内存中的头文
件、把宏展开解析到它的拼写位置，或者写出一个参与构建持久性核算的旁路输出。

## 接口

```c
#include "neverc/Plugin/PluginSource.h"
```

| 接口 | 表 | 版本宏 |
|---|---|---|
| `NEVERC_INTERFACE_IO_{HIGH,LOW}` | `NevercIOAPI` | `NEVERC_IO_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SOURCE_LOCATION_{HIGH,LOW}` | `NevercSourceLocationAPI` | `NEVERC_SOURCE_LOCATION_API_MAJOR` / `_MINOR` |

`NEVERC_SOURCE_API_MAJOR` 和 `_MINOR` 是 source-location 那一对的别名。

## 三个源阶段

| 阶段 | 策略 | 含义 |
|---|---|---|
| `neverc.source.resolve_input` | OBSERVABLE, INTERCEPTABLE | 把驱动输入变成源输入 |
| `neverc.source.open` | 另加 REPLACEABLE | 为某个输入产出源单元 |
| `neverc.source.after_open` | OBSERVABLE | 通知：某个单元已就绪 |

由于 `neverc.source.open` 可替换，Provider 可以交回一个字节由它自己合成的单元，
这正是注入生成代码而不碰磁盘的受支持做法。

## 虚拟文件系统 Provider

一个 VFS Provider 认领某个路径前缀，并回答编译器关于文件的四个问题。

```c
typedef struct NevercVFSProviderDescriptor {
  NevercABITableHeader Header;
  NevercStringView ProviderID;
  NevercStringView RoutePrefix;
  NevercBool Deterministic;
  NevercBool Cacheable;
  uint64_t Reserved;
  NevercVFSPathPredicateFn MatchesPath;
  NevercVFSProviderStatusFn Status;
  NevercVFSProviderOpenReadFn OpenRead;
  NevercVFSProviderReadDirectoryFn ReadDirectory;
  NevercVFSProviderCanonicalizeFn Canonicalize;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercVFSProviderDescriptor;
```

每个回调都要填一个结果，其中的 `Disposition` 说明这个 Provider 是否处理了该请
求：

```c
static NevercStatus NEVERC_CALL
open_read(NevercTaskHandle Task, NevercStringView Path, void *UserData,
          NevercVFSOpenReadResult *OutResult) {
  static const char Header[] = "#define GENERATED 1\n";
  if (!path_matches(Path)) {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
    return neverc_status_ok();
  }
  OutResult->Disposition   = NEVERC_VFS_RESULT_HANDLED;
  OutResult->Status.Type   = NEVERC_VFS_FILE_REGULAR;
  OutResult->Status.Size   = sizeof(Header) - 1;
  OutResult->Content.Data  = (const uint8_t *)Header;
  OutResult->Content.Length = sizeof(Header) - 1;
  OutResult->Content.NullTerminated = NEVERC_TRUE;
  return neverc_status_ok();
}
```

返回 `NEVERC_VFS_RESULT_NOT_HANDLED` 会落到下一个 Provider，最终落到真实文件系
统。文件类型有 `NEVERC_VFS_FILE_UNKNOWN`、`REGULAR`、`DIRECTORY`、`SYMLINK`、
`OTHER`。

在 `Register` 期间注册：

```c
IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Descriptor);
```

如果只是一个内存文件、且只需要在一个会话内存在，那就完全不用写 Provider：

```c
IO->AddMemoryFile(IO->Context, Session, SV("/virtual/config.h"),
                  Content, ModificationTime);
```

[`pluginsdk/examples/VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
是一个可运行的完整 Provider。

## 读文件

```c
NevercVFSStatus Status;
IO->Stat(IO->Context, Task, Path, &Status);

NevercFileHandle File;
IO->OpenFileForRead(IO->Context, Task, Path, &File);

NevercBufferHandle Buffer;
IO->ReadFile(IO->Context, Task, File, /*Offset=*/0, /*Length=*/Status.Size,
             &Buffer);

NevercBufferView View;
IO->GetBufferView(IO->Context, Task, Buffer, &View);
/* View.Data / View.Length / View.NullTerminated */

IO->ReleaseBuffer(IO->Context, Task, Buffer);
IO->CloseFile(IO->Context, Task, File);
```

`CopyBuffer` 把你自己持有的字节变成宿主缓冲区，`Canonicalize` 解析路径，
`GetWorkingDirectory` / `SetWorkingDirectory` 管理任务的当前目录。目录用
`OpenDirectory`、`ReadDirectory`（遍历结束时把 `OutHasEntry` 置为
`NEVERC_FALSE`）和 `CloseDirectory` 来走。

I/O 错误码通过 `NevercStatus.Detail` 报告：`NEVERC_IO_ERROR_NOT_FOUND`、
`PERMISSION_DENIED`、`NOT_DIRECTORY`、`IS_DIRECTORY`、`INVALID_PATH`、`IO`。

## 写输出

输出是事务式的。你打开一个 sink、写入，然后 finish 得到一个封印（seal）——一个
大小加一个 32 字节摘要，构建系统可以据此校验。

```c
NevercOutputSinkHandle Sink;
IO->BeginFileOutput(IO->Context, Task, SV("out.json"), /*SizeBudget=*/0, &Sink);
IO->OutputWrite(IO->Context, Task, Sink, Bytes);
IO->OutputMetadataSet(IO->Context, Task, Sink, SV("content-type"),
                      SV("application/json"));

NevercOutputSeal Seal = {0};
Seal.Header = (NevercABITableHeader){sizeof(Seal), NEVERC_IO_API_MAJOR,
                                     NEVERC_IO_API_MINOR, 0};
IO->OutputFinish(IO->Context, Task, Sink, &Seal);
```

| 函数 | 用途 |
|---|---|
| `BeginMemoryOutput` | 由内存支撑、逻辑命名的 sink |
| `BeginFileOutput` | 原子落到最终路径的 sink |
| `BeginStreamOutput` | 落到 `NEVERC_OUTPUT_STREAM_STDOUT` 或 `_STDERR` 的 sink |
| `OutputWrite`、`OutputWriteAt` | 追加，或按偏移写 |
| `OutputTell`、`OutputTruncate` | 位置与大小控制 |
| `OutputMetadataSet` | 给输出附加一个键值对 |
| `OutputFinish` | 封印输出并产出 `NevercOutputSeal` |
| `OutputAbort` | 丢弃已写入的一切 |
| `OutputGetSummary` | 随时查看状态、标志、大小、摘要 |

`NevercOutputSummary.State` 会经过 `NEVERC_OUTPUT_OPEN`、`FINISHED`、
`COMMITTED`、`ABORTED` 或 `FAILED_PARTIAL`，而 `Flags` 记录 `PUBLISHED`、
`DURABLE`、`MAY_BE_PARTIAL`、`RECOVERY_REQUIRED`、`DURABILITY_UNCONFIRMED`。这
些标志和驱动在 `NevercStatus.Flags` 中透出的是同一份信息，所以写到一半崩溃与干
净失败是可以区分的。

`SizeBudget` 为零表示不限；非零预算会让超额以
`NEVERC_STATUS_RESOURCE_EXHAUSTED` 失败，而不是把磁盘写满。

## 记录依赖

如果插件读了某个构建系统应当追踪的东西，就要说出来。否则增量构建在那个输入变化
时不会重新构建。

```c
NevercDependencyDescriptor Dependency = {0};
Dependency.Header = (NevercABITableHeader){sizeof(Dependency),
                                           NEVERC_IO_API_MAJOR,
                                           NEVERC_IO_API_MINOR, 0};
Dependency.CanonicalPath = SV("/etc/mytool/rules.txt");
Dependency.ContentDigest = Digest;
Dependency.Kind          = NEVERC_INPUT_DEPENDENCY_RESOURCE;
Dependency.System        = NEVERC_FALSE;
Dependency.ProviderID    = SV("com.example.myplugin");

NevercDependencyHandle Handle;
IO->RecordDependency(IO->Context, Task, &Dependency, &Handle);
```

种类有 `NEVERC_INPUT_DEPENDENCY_SOURCE`、`INCLUDE`、`MODULE`、`RESOURCE`、
`TOOL`、`PLUGIN`。

## 源位置

`NevercSourceLocation` 是不透明的。位置表把它变成你能打印或比较的东西。

```c
NevercSourceLocationInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Source->GetLocationInfo(Source->Context, Task, Location, &Info);
/* Info.Kind 为 NEVERC_SOURCE_LOCATION_FILE 或 _MACRO；
   随后是 Info.FileOffset、Info.Line、Info.Column。 */
```

四个变换在同一位置的不同视角之间移动，它们都是
`NevercTransformSourceLocationFn` 签名：

| 函数 | 返回 |
|---|---|
| `GetSpellingLocation` | token 的字符实际写在哪里 |
| `GetExpansionLocation` | 宏展开在源码中出现的位置 |
| `GetFileLocation` | 最近的文件位置 |
| `GetIncludeLocation` | 把该文件引入进来的那条 `#include` |
| `GetTokenEnd` | token 最后一个字符之后的位置 |

`GetPresumedLocation` 应用 `#line` 指示，给出文件名、行、列和 include 位置。
`GetLocationFile` 配合 `GetFileInfo` 给出规范路径、大小、修改时间、唯一 ID，以
及该文件属于用户、系统还是 extern-C 系统：

```c
typedef struct NevercFileInfo {
  NevercABITableHeader Header;
  NevercStringView Path;
  NevercStringView CanonicalPath;
  uint64_t Size;
  int64_t ModificationTime;
  NevercFileUniqueID UniqueID;      /* {Device, File} */
  NevercFileCharacteristic Characteristic;
  NevercBool NamedPipe;
} NevercFileInfo;
```

区间用 `GetRangeInfo` 读取（它报告 `Begin`、`End` 以及该区间是
`NEVERC_SOURCE_RANGE_CHARACTER` 还是 `_TOKEN`），字节本身用 `GetSourceText` 或
`GetCharacterData` 读取。

当你一次需要很多位置时——比如对整个函数做一遍诊断——请用批量形式，而不是每个
位置调一次：

```c
Source->GetLocationInfoBatch(Source->Context, Task, Locations, LocationCount,
                             OutInfos, OutInfoCapacity);
```

## 源单元

输入及其字节在阶段层面的视图：

```c
NevercSourceInputInfo Input = {0};
Source->GetSourceInput(Source->Context, Frame, Frame->Input, &Input);
/* Input.Path, .Kind（FILE 或 BUFFER）, .Language, .System, .Preprocessed */
```

`neverc.source.open` 的 Provider 用一个内存支撑的单元来回答：

```c
NevercMemorySourceUnitDescriptor Unit = {0};
Unit.Header = (NevercABITableHeader){sizeof(Unit),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Unit.LogicalPath      = SV("/virtual/generated.c");
Unit.CanonicalIdentity = SV("com.example:generated:v1");
Unit.Content          = Bytes;
Unit.ProviderID       = SV("com.example.myplugin");
Unit.Deterministic    = NEVERC_TRUE;
Unit.Cacheable        = NEVERC_TRUE;

NevercArtifactHandle Output;
Source->CreateMemorySourceUnit(Source->Context, Frame, Frame->Input, &Unit,
                               &Output);
```

缓存以 `CanonicalIdentity` 为 key，所以内容一变它就必须跟着变。`GetSourceUnit`
读回一个单元，并额外报告 `MemoryBacked`。

## 规则

- `ReadFile`、`CopyBuffer` 和 `PathToBuffer` 返回的缓冲区归宿主所有；每一个都要
  用 `ReleaseBuffer` 释放。
- 每个 `OpenFileForRead` 都要有 `CloseFile`；每个 `OpenDirectory` 都要有
  `CloseDirectory`；每个输出 sink 都要有 `OutputFinish` 或 `OutputAbort`。
- `NevercFileInfo`、`NevercVFSStatus` 以及位置查询结果里的视图只在回调期间有
  效。
- VFS Provider 回调运行在任务线程上，不得反过来回调编译器；用你手头已有的数据
  作答。
- 如实声明 `Deterministic` 和 `Cacheable`。一个读时钟或读环境变量却声称确定性的
  Provider，会污染整个构建缓存。
- `AddMemoryFile` 是会话作用域的；当内容依赖于任务时，Provider 才是合适的工具。

规范性声明见 `PluginSource.h`，完整 Provider 示例见
`pluginsdk/examples/VirtualHeaderPlugin.c`。
