**语言**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

[← NeverC 插件 ABI](README.zh-CN.md)

# NeverC 插件预处理器 API

[`PluginPrep.h`] 用两种方式暴露预处理器。**订阅** 39 种事件可以拿到预处理器所作所
为的只读轨迹——进入文件、宏定义与展开、条件求值、pragma。六个**阶段**则更进一
步，允许你改写结果：重定向一个 `#include`、替换某个宏的展开 token、自己处理一条
pragma，或者给 `__has_feature` 一个不同的答案。

## 接口

```c
#include "neverc/Plugin/PluginPrep.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
    NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table, &Minor, &TableSize);
```

230 种 token 种类（`NEVERC_TOKEN_KIND_COUNT`）和预处理器关键字种类来自
[`Schema/PluginPrepSchema.inc`]，该文件由头文件包含，其能力主版本必须等于
`NEVERC_PREP_API_MAJOR`——不匹配是编译错误，而不是运行期惊喜。每种 token 还带一
个类别：`NEVERC_TOKEN_CATEGORY_SPECIAL`、`COMMENT`、`IDENTIFIER`、`LITERAL`、
`PUNCTUATOR`、`KEYWORD` 或 `ANNOTATION`。

## 六个预处理器阶段

| 阶段 | 策略 | 输入 → 输出 |
|---|---|---|
| `neverc.prep.token` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | 一个 token → token 列表 |
| `neverc.prep.build_token_stream` | 同上 | 区间 → token 流 |
| `neverc.prep.include.intercept` | 同上 | include 请求 → include 决策 |
| `neverc.prep.macro.intercept` | 同上 | 宏操作 → 动作 + token |
| `neverc.prep.pragma.intercept` | 同上 | pragma → 动作 + token |
| `neverc.prep.feature_query.intercept` | 同上 | `__has_*` 查询 → 取值 |

六个里有五个在 `NevercPrepAPI` 上有配对的 `Get<Kind>PhaseInput` 和
`Create<Kind>PhaseOutput`，而 `Create` 那一半需要拦截器的
`NevercPhaseContinuation`，因此输出只能在拥有它的那个阶段内部产生。
`neverc.prep.build_token_stream` 是例外：它只有 `GetTokenStreamPhaseInput`，
并通过阶段 `Frame` 上的 `TokenStreamBuilderCommit` 发布，而不是带
continuation 的 `Create*PhaseOutput`。

## 读取 token

```c
typedef struct NevercTokenInfo {
  NevercABITableHeader Header;
  NevercTokenKind Kind;
  NevercTokenFlags Flags;
  NevercTokenOriginKind Origin;
  uint32_t Reserved;
  NevercStringView Spelling;
  NevercSourceLocation Location;
  NevercSourceRange Range;
  NevercIdentifierHandle Identifier;
  NevercMacroDefinitionHandle MacroDefinition;
} NevercTokenInfo;
```

`Origin` 取值为 `NEVERC_TOKEN_ORIGIN_FILE`、`MACRO_REPLACEMENT`、
`MACRO_ARGUMENT` 或 `SYNTHESIZED`，你据此区分用户敲进去的 token 和宏产生的
token。

这些标志是预处理器自己的记账信息，在你合成 token 时很重要：

| 标志 | 含义 |
|---|---|
| `NEVERC_TOKEN_FLAG_START_OF_LINE` | 本行第一个 token |
| `_LEADING_SPACE` | 前面有空白 |
| `_DISABLE_EXPANSION` | 不要对该 token 做宏展开 |
| `_NEEDS_CLEANING` | 拼写中含转义换行或三字符组 |
| `_LEADING_EMPTY_MACRO` | 紧邻其前展开了一个空宏 |
| `_HAS_UCN` | 含通用字符名 |
| `_IGNORED_COMMA`、`_COMMA_AFTER_ELIDED` | 可变参数逗号省略的记账 |
| `_STRINGIFIED_IN_MACRO` | 由 `#` 产生 |
| `_REINJECTED` | 被重新注入到 token 流中 |

`NEVERC_TOKEN_FLAG_ALL` 是所有已定义位的掩码。批量读取用 `GetTokenInfoBatch`；
整个流既可以通过 `GetTokenStreamView` 以 `NevercTokenView` 记录的轻量视图读取，
也可以用 `GetTokenStreamToken` 逐个句柄读取。一个流最多容纳
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS`（16,777,216）个 token。

## 标识符与宏

```c
NevercIdentifierHandle Identifier;
Prep->GetOrCreateIdentifier(Prep->Context, Task, SV("MY_MACRO"), &Identifier);

NevercMacroDefinitionHandle Definition;
Prep->GetMacroDefinitionForIdentifier(Prep->Context, Task, Identifier,
                                      &Definition);

NevercMacroDefinitionInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_PREP_API_MAJOR,
                                     NEVERC_PREP_API_MINOR, 0};
Prep->GetMacroDefinitionInfo(Prep->Context, Task, Definition, &Info);
```

`NevercMacroDefinitionInfo` 报告名字、定义它的指示、定义／结束／取消定义的位置、
参数与替换 token 的数量，以及标志：`NEVERC_MACRO_FUNCTION_LIKE`、`VARIADIC`、
`C99_VARIADIC`、`GNU_VARIADIC`、`HAS_VA_OPT`、`BUILTIN`、`COMMA_PASTING`。单个参
数和替换 token 来自 `GetMacroParameter` 和 `GetMacroReplacementToken`。

`NevercIdentifierInfo` 另外给出 token 种类、预处理器关键字种类、builtin ID，以及
`NEVERC_IDENTIFIER_KEYWORD`、`_HAS_MACRO`、`_POISONED`、`_RESERVED` 等标志。

在展开点上，`GetMacroArgumentInfo` 报告参数个数以及可变参数是否被省略，
`GetMacroArgumentTokenStream` 给出每个参数的 token。

## 事件订阅

一个回调接收所有已订阅的事件。掩码由你关心的事件种类拼出来：

```c
static NevercStatus NEVERC_CALL
on_event(NevercTaskHandle Task, const NevercPrepEvent *Event, void *UserData) {
  switch (Event->Kind) {
  case NEVERC_PREP_EVENT_INCLUSION_DIRECTIVE:
    /* Event->Payload.Include.Filename, .IsAngled, .File, .FilenameRange */
    break;
  case NEVERC_PREP_EVENT_MACRO_EXPANDS:
    /* Event->Payload.Macro.NameToken, .Definition, .Arguments, .Range */
    break;
  case NEVERC_PREP_EVENT_IFDEF:
    /* Event->Payload.Condition.Value 为 NOT_EVALUATED、FALSE 或 TRUE */
    break;
  default:
    break;
  }
  return neverc_status_ok();
}

NevercPrepObserverDescriptor Observer = {0};
Observer.Header = (NevercABITableHeader){sizeof(Observer),
                                         NEVERC_PREP_API_MAJOR,
                                         NEVERC_PREP_API_MINOR, 0};
Observer.Events = NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_INCLUSION_DIRECTIVE) |
                  NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_MACRO_EXPANDS) |
                  NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_IFDEF);
Observer.Callback = on_event;
Observer.UserData = State;
Prep->RegisterEventObserver(Prep->Context, Task, &Observer);
```

`NEVERC_PREP_EVENT_MASK_ALL` 订阅全部。39 种事件按它们使用的 payload 联合成员分
组如下：

| Payload | 事件 |
|---|---|
| `File` | `FILE_CHANGED`、`LEXED_FILE_CHANGED`、`FILE_SKIPPED`、`END_OF_MAIN_FILE` |
| `Include` | `INCLUSION_DIRECTIVE`、`FILE_NOT_FOUND`、`HAS_INCLUDE` |
| `Text` | `IDENT`、`PRAGMA_DIRECTIVE`、`PRAGMA_COMMENT`、`PRAGMA_MARK`、`PRAGMA_DETECT_MISMATCH`、`PRAGMA_DEBUG`、`PRAGMA_MESSAGE`、`PRAGMA_DIAGNOSTIC_PUSH`、`PRAGMA_DIAGNOSTIC_POP`、`PRAGMA_DIAGNOSTIC`、`PRAGMA_WARNING`、`PRAGMA_WARNING_PUSH`、`PRAGMA_WARNING_POP`、`PRAGMA_EXEC_CHARSET_PUSH`、`PRAGMA_EXEC_CHARSET_POP`、`PRAGMA_ASSUME_NONNULL_BEGIN`、`PRAGMA_ASSUME_NONNULL_END` |
| `Macro` | `MACRO_EXPANDS`、`MACRO_DEFINED`、`MACRO_UNDEFINED`、`DEFINED` |
| `Condition` | `IF`、`ELIF`、`IFDEF`、`ELIFDEF`、`ELIFDEF_SKIPPED`、`IFNDEF`、`ELIFNDEF`、`ELIFNDEF_SKIPPED`、`ELSE`、`ENDIF`、`SOURCE_RANGE_SKIPPED` |

`NevercPrepFileEvent.Reason` 区分 `NEVERC_PREP_FILE_ENTER`、`EXIT`、
`SYSTEM_HEADER_PRAGMA`、`RENAME`。事件是只读的：记录本身及其中每个视图都只在回
调期间借用，而事件里发布的句柄会被提升到外层任务作用域。

## 重定向一个 include

```c
NevercPrepIncludePhaseInput In = {0};
In.Header = (NevercABITableHeader){sizeof(In), NEVERC_PREP_API_MAJOR,
                                   NEVERC_PREP_API_MINOR, 0};
Prep->GetIncludePhaseInput(Prep->Context, Frame, Frame->Input, &In);

NevercPrepIncludePhaseOutput Out = {0};
Out.Header = In.Header;
if (view_equals(In.Filename, "legacy.h")) {
  Out.Action    = NEVERC_PREP_INCLUDE_REDIRECT;
  Out.Filename  = SV("modern.h");
  Out.IsAngled  = NEVERC_FALSE;
} else {
  Out.Action = NEVERC_PREP_INCLUDE_CONTINUE;
}

NevercArtifactHandle Output;
Prep->CreateIncludePhaseOutput(Prep->Context, Frame, Continuation, &Out,
                               &Output);
```

动作有 `NEVERC_PREP_INCLUDE_CONTINUE`、`_SKIP`、`_REDIRECT`。输入还报告
`IsImport` 和 `IsIncludeNext`，因此 `#import` 与 `#include_next` 是可区分的。

## 替换宏展开

宏阶段的输入携带正在执行的操作——`NEVERC_PREP_MACRO_DEFINE`、`_UNDEFINE`、
`_EXPAND` 或 `_EXPAND_BUILTIN`——以及名字 token、定义、参数，还有预处理器本来打
算使用的替换 token。

```c
NevercPrepMacroPhaseOutput Out = {0};
Out.Header     = /* … */;
Out.Action     = NEVERC_PREP_MACRO_REPLACE_TOKENS;
Out.Tokens     = MyTokens;      /* const NevercTokenHandle * */
Out.TokenCount = MyTokenCount;
Prep->CreateMacroPhaseOutput(Prep->Context, Frame, Continuation, &Out, &Output);
```

`NEVERC_PREP_MACRO_CONTINUE` 保持内建行为，`_SUPPRESS` 展开为空。

## 构造 token

合成 token 来自 builder，它在提交前会校验种类、拼写和标识符的组合是否自洽：

```c
NevercTokenBuilderHandle Builder;
Prep->CreateTokenBuilder(Prep->Context, Task, &Builder);
Prep->TokenBuilderSetLiteral(Prep->Context, Task, Builder,
                             NEVERC_TOKEN_NUMERIC_CONSTANT, SV("42"));
Prep->TokenBuilderSetLocation(Prep->Context, Task, Builder, Location);
Prep->TokenBuilderSetFlags(Prep->Context, Task, Builder,
                           NEVERC_TOKEN_FLAG_LEADING_SPACE);

NevercTokenHandle Token;
Prep->TokenBuilderCommit(Prep->Context, Task, Builder, &Token);
Prep->DestroyTokenBuilder(Prep->Context, Task, Builder);
```

标点和关键字用 `TokenBuilderSetKind`，标识符用 `TokenBuilderSetIdentifier`。
token 种类常量来自 [`PluginPrepSchema.inc`]。

对于整个流——即 `neverc.prep.build_token_stream` 阶段——请累积到流 builder 里
并一次性提交：

```c
NevercTokenStreamBuilderHandle Stream;
Prep->CreateTokenStreamBuilder(Prep->Context, Task, &Stream);
Prep->TokenStreamBuilderAppend(Prep->Context, Task, Stream, Tokens, Count);
Prep->TokenStreamBuilderCommit(Prep->Context, Frame, Stream, &Output);
Prep->DestroyTokenStreamBuilder(Prep->Context, Task, Stream);
```

阶段输入 `NevercPrepTokenStreamPhaseInput` 给出起止位置，以及输出必须遵守的
`MaximumTokenCount`。

## Pragma 与特性查询

pragma 阶段的输入报告引入方式（`NEVERC_PREP_PRAGMA_HASH`、对应 `_Pragma` 的
`_OPERATOR`、对应 `__pragma` 的 `_MS`）、命名空间与名字，以及参数 token。输出动
作是 `NEVERC_PREP_PRAGMA_CONTINUE`、`_HANDLED` 或 `_REPLACE_TOKENS`。

特性查询通过 `NEVERC_PREP_QUERY_HAS_FEATURE` 等常量覆盖
`__has_feature`、`__has_extension`、`__has_builtin`、`__has_include` 和
`__has_include_next`。输入携带名字以及编译器算出的 `BuiltinValue`；输出要么继
续，要么替换：

```c
NevercPrepFeatureQueryPhaseOutput Out = {0};
Out.Header = /* … */;
Out.Action = NEVERC_PREP_QUERY_REPLACE;
Out.Value  = NEVERC_TRUE;
Prep->CreateFeatureQueryPhaseOutput(Prep->Context, Frame, Continuation, &Out,
                                    &Output);
```

## 规则

- 事件记录、字符串视图和整数数组都只在回调期间借用。事件中发布的句柄存活到任务
  结束。
- 每个 builder 都需要配对的 `Destroy*` 调用，错误路径上也一样。
- `Create<Kind>PhaseOutput` 需要它所属阶段的 continuation；用了别的阶段的
  continuation 会返回 `NEVERC_STATUS_WRONG_SCOPE`。`TokenStreamBuilderCommit`
  则使用 `build_token_stream` 阶段的 `Frame`，而不是 continuation。
- 只订阅你会处理的事件。掩码就是节流阀——一个订阅
  `NEVERC_PREP_EVENT_MASK_ALL` 再在 C 里过滤的插件，要为每一次回调付出代价。
- 预处理器回调运行在任务线程上，且此时预处理器正在半途中。不要从回调里重入预处
  理器。
- 缺少必需指针时返回 `NEVERC_STATUS_INVALID_ARGUMENT`，并且绝不让异常越过边界。

规范性声明见 [`PluginPrep.h`] 和 [`Schema/PluginPrepSchema.inc`]，token 种类的
schema 见 [`Schema/PrepSchema.json`]，六个预处理阶段及其策略见
[`Schema/PhaseSchema.json`]。

<!-- reference links -->
[`PluginPrep.h`]: ../../neverc/include/neverc/Plugin/PluginPrep.h
[`PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PrepSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PrepSchema.json
