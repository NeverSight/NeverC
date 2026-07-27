**語言**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

[← NeverC 外掛 ABI](README.zh-TW.md)

# NeverC 外掛前置處理器 API

[`PluginPrep.h`] 以兩種方式公開前置處理器。**訂閱** 39 種事件種類，可以取得前置
處理器一切作為的唯讀軌跡──進入檔案、巨集定義與展開、條件求值、pragma。六個
**階段** 則更進一步，讓你改寫結果：把某個 `#include` 導向別處、取代巨集的展開
token、自己處理某個 pragma，或是讓 `__has_feature` 給出不同的答案。

## 介面

```c
#include "neverc/Plugin/PluginPrep.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
    NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table, &Minor, &TableSize);
```

230 種 token 種類（`NEVERC_TOKEN_KIND_COUNT`）與前置處理器關鍵字種類來自
[`Schema/PluginPrepSchema.inc`]；標頭檔會包含它，而它的 capability major 必須等
於 `NEVERC_PREP_API_MAJOR`──不一致是編譯錯誤，而不是執行期的意外。每個種類還
帶有一個分類：`NEVERC_TOKEN_CATEGORY_SPECIAL`、`COMMENT`、`IDENTIFIER`、
`LITERAL`、`PUNCTUATOR`、`KEYWORD` 或 `ANNOTATION`。

## 六個前置處理器階段

| 階段 | 政策 | 輸入 → 輸出 |
|---|---|---|
| `neverc.prep.token` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | 單一 token → token 清單 |
| `neverc.prep.build_token_stream` | 同上 | 範圍 → token 串流 |
| `neverc.prep.include.intercept` | 同上 | include 請求 → include 決策 |
| `neverc.prep.macro.intercept` | 同上 | 巨集操作 → 動作 + tokens |
| `neverc.prep.pragma.intercept` | 同上 | pragma → 動作 + tokens |
| `neverc.prep.feature_query.intercept` | 同上 | `__has_*` 查詢 → 值 |

六個裡有五個在 `NevercPrepAPI` 上都有成對的 `Get<Kind>PhaseInput` 與
`Create<Kind>PhaseOutput`，而 `Create` 那一半要吃攔截器的
`NevercPhaseContinuation`，因此輸出只能從擁有它的那個階段內部產生。
`neverc.prep.build_token_stream` 是例外：它只有 `GetTokenStreamPhaseInput`，
並透過階段 `Frame` 上的 `TokenStreamBuilderCommit` 發布，而不是帶
continuation 的 `Create*PhaseOutput`。

## 讀取 token

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

`Origin` 是 `NEVERC_TOKEN_ORIGIN_FILE`、`MACRO_REPLACEMENT`、`MACRO_ARGUMENT`
或 `SYNTHESIZED`，你就是靠它分辨使用者親手打的 token 和巨集產生的 token。

那些旗標是前置處理器自己的記帳，在你合成 token 時特別重要：

| 旗標 | 意義 |
|---|---|
| `NEVERC_TOKEN_FLAG_START_OF_LINE` | 該行的第一個 token |
| `_LEADING_SPACE` | 前面有空白 |
| `_DISABLE_EXPANSION` | 不要對這個 token 做巨集展開 |
| `_NEEDS_CLEANING` | 拼寫中含有跳脫換行或三字元組 |
| `_LEADING_EMPTY_MACRO` | 緊接在它之前展開了一個空巨集 |
| `_HAS_UCN` | 含有通用字元名稱 |
| `_IGNORED_COMMA`、`_COMMA_AFTER_ELIDED` | 可變參數逗號省略的記帳 |
| `_STRINGIFIED_IN_MACRO` | 由 `#` 產生 |
| `_REINJECTED` | 被重新送回 token 串流 |

`NEVERC_TOKEN_FLAG_ALL` 是所有已定義位元的遮罩。批次讀取用
`GetTokenInfoBatch`；整條串流可以透過 `GetTokenStreamView` 讀成一組輕量的
`NevercTokenView` 記錄，或是用 `GetTokenStreamToken` 一次拿一個控制代碼。一條串
流最多容納 `NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS`（16,777,216）個 token。

## 識別字與巨集

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

`NevercMacroDefinitionInfo` 回報名稱、定義它的指示、定義／結束／取消定義的位
置、參數與替換 token 的數量，以及旗標：`NEVERC_MACRO_FUNCTION_LIKE`、
`VARIADIC`、`C99_VARIADIC`、`GNU_VARIADIC`、`HAS_VA_OPT`、`BUILTIN`、
`COMMA_PASTING`。個別的參數與替換 token 來自 `GetMacroParameter` 與
`GetMacroReplacementToken`。

`NevercIdentifierInfo` 另外提供 token 種類、前置處理器關鍵字種類、builtin ID，
以及 `NEVERC_IDENTIFIER_KEYWORD`、`_HAS_MACRO`、`_POISONED`、`_RESERVED` 這類
旗標。

在展開點上，`GetMacroArgumentInfo` 回報引數數量以及是否省略了可變引數，而
`GetMacroArgumentTokenStream` 交出每個引數的 token。

## 事件訂閱

一個回呼會收到所有已訂閱的事件。遮罩由你關心的事件種類組成：

```c
static NevercStatus NEVERC_CALL
on_event(NevercTaskHandle Task, const NevercPrepEvent *Event, void *UserData) {
  switch (Event->Kind) {
  case NEVERC_PREP_EVENT_INCLUSION_DIRECTIVE:
    /* Event->Payload.Include.Filename、.IsAngled、.File、.FilenameRange */
    break;
  case NEVERC_PREP_EVENT_MACRO_EXPANDS:
    /* Event->Payload.Macro.NameToken、.Definition、.Arguments、.Range */
    break;
  case NEVERC_PREP_EVENT_IFDEF:
    /* Event->Payload.Condition.Value 是 NOT_EVALUATED、FALSE 或 TRUE */
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

`NEVERC_PREP_EVENT_MASK_ALL` 會訂閱全部。這 39 種依它們使用的 payload 聯集成員
分組如下：

| Payload | 事件 |
|---|---|
| `File` | `FILE_CHANGED`、`LEXED_FILE_CHANGED`、`FILE_SKIPPED`、`END_OF_MAIN_FILE` |
| `Include` | `INCLUSION_DIRECTIVE`、`FILE_NOT_FOUND`、`HAS_INCLUDE` |
| `Text` | `IDENT`、`PRAGMA_DIRECTIVE`、`PRAGMA_COMMENT`、`PRAGMA_MARK`、`PRAGMA_DETECT_MISMATCH`、`PRAGMA_DEBUG`、`PRAGMA_MESSAGE`、`PRAGMA_DIAGNOSTIC_PUSH`、`PRAGMA_DIAGNOSTIC_POP`、`PRAGMA_DIAGNOSTIC`、`PRAGMA_WARNING`、`PRAGMA_WARNING_PUSH`、`PRAGMA_WARNING_POP`、`PRAGMA_EXEC_CHARSET_PUSH`、`PRAGMA_EXEC_CHARSET_POP`、`PRAGMA_ASSUME_NONNULL_BEGIN`、`PRAGMA_ASSUME_NONNULL_END` |
| `Macro` | `MACRO_EXPANDS`、`MACRO_DEFINED`、`MACRO_UNDEFINED`、`DEFINED` |
| `Condition` | `IF`、`ELIF`、`IFDEF`、`ELIFDEF`、`ELIFDEF_SKIPPED`、`IFNDEF`、`ELIFNDEF`、`ELIFNDEF_SKIPPED`、`ELSE`、`ENDIF`、`SOURCE_RANGE_SKIPPED` |

`NevercPrepFileEvent.Reason` 區分 `NEVERC_PREP_FILE_ENTER`、`EXIT`、
`SYSTEM_HEADER_PRAGMA` 與 `RENAME`。事件是唯讀的：記錄本身與其中的每個 view 都
只在該回呼期間借用，而在事件中發布的控制代碼則會提升到外層的任務範圍。

## 導向另一個 include

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

動作有 `NEVERC_PREP_INCLUDE_CONTINUE`、`_SKIP`、`_REDIRECT`。輸入還會回報
`IsImport` 與 `IsIncludeNext`，所以 `#import` 與 `#include_next` 是可以區分的。

## 取代巨集展開

巨集階段的輸入帶著正在執行的操作──`NEVERC_PREP_MACRO_DEFINE`、`_UNDEFINE`、
`_EXPAND` 或 `_EXPAND_BUILTIN`──連同名稱 token、定義、引數，以及前置處理器原
本打算使用的替換 token。

```c
NevercPrepMacroPhaseOutput Out = {0};
Out.Header     = /* … */;
Out.Action     = NEVERC_PREP_MACRO_REPLACE_TOKENS;
Out.Tokens     = MyTokens;      /* const NevercTokenHandle * */
Out.TokenCount = MyTokenCount;
Prep->CreateMacroPhaseOutput(Prep->Context, Frame, Continuation, &Out, &Output);
```

`NEVERC_PREP_MACRO_CONTINUE` 保留內建行為，`_SUPPRESS` 則展開成空。

## 建構 token

合成的 token 來自建構器，它會在提交前驗證種類、拼寫與識別字的組合：

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

標點與關鍵字用 `TokenBuilderSetKind`，識別字用 `TokenBuilderSetIdentifier`。
token 種類常數來自 [`PluginPrepSchema.inc`]。

若要處理整條串流──也就是 `neverc.prep.build_token_stream` 階段──請累積到串流
建構器裡，然後一次提交：

```c
NevercTokenStreamBuilderHandle Stream;
Prep->CreateTokenStreamBuilder(Prep->Context, Task, &Stream);
Prep->TokenStreamBuilderAppend(Prep->Context, Task, Stream, Tokens, Count);
Prep->TokenStreamBuilderCommit(Prep->Context, Frame, Stream, &Output);
Prep->DestroyTokenStreamBuilder(Prep->Context, Task, Stream);
```

階段輸入 `NevercPrepTokenStreamPhaseInput` 會給出起訖位置，以及輸出必須遵守的
`MaximumTokenCount`。

## Pragma 與功能查詢

pragma 階段的輸入回報引入者（`NEVERC_PREP_PRAGMA_HASH`、對應 `_Pragma` 的
`_OPERATOR`，或對應 `__pragma` 的 `_MS`）、命名空間與名稱，以及引數 token。輸出
動作是 `NEVERC_PREP_PRAGMA_CONTINUE`、`_HANDLED` 或 `_REPLACE_TOKENS`。

功能查詢透過 `NEVERC_PREP_QUERY_HAS_FEATURE` 之類的常數涵蓋
`__has_feature`、`__has_extension`、`__has_builtin`、`__has_include` 與
`__has_include_next`。輸入帶著名稱以及編譯器算出的 `BuiltinValue`；輸出則是繼續
或取代：

```c
NevercPrepFeatureQueryPhaseOutput Out = {0};
Out.Header = /* … */;
Out.Action = NEVERC_PREP_QUERY_REPLACE;
Out.Value  = NEVERC_TRUE;
Prep->CreateFeatureQueryPhaseOutput(Prep->Context, Frame, Continuation, &Out,
                                    &Output);
```

## 規則

- 事件記錄、字串 view 與整數陣列都只在該回呼期間借用。在事件中發布的控制代碼則
  存活到任務結束。
- 每個建構器都需要對應的 `Destroy*` 呼叫，錯誤路徑上也不例外。
- 呼叫 `Create<Kind>PhaseOutput` 需要它所屬階段的 continuation；用了別的階段的
  continuation 會回傳 `NEVERC_STATUS_WRONG_SCOPE`。`TokenStreamBuilderCommit`
  則使用 `build_token_stream` 階段的 `Frame`，而不是 continuation。
- 只訂閱你真的會處理的事件。遮罩就是節流閥──一個接下
  `NEVERC_PREP_EVENT_MASK_ALL` 再用 C 程式碼過濾的外掛，每一次回呼都要付出代
  價。
- 前置處理器的回呼在任務執行緒上、且在前置處理器執行到一半時被呼叫。不要從回呼
  裡再次進入前置處理器。
- 缺少必要指標時回傳 `NEVERC_STATUS_INVALID_ARGUMENT`，並且絕不讓例外跨越邊界。

規範性宣告請見 [`PluginPrep.h`] 與 [`Schema/PluginPrepSchema.inc`]，token 種類的
schema 請見 [`Schema/PrepSchema.json`]，六個前處理階段及其政策請見
[`Schema/PhaseSchema.json`]。

<!-- reference links -->
[`PluginPrep.h`]: ../../neverc/include/neverc/Plugin/PluginPrep.h
[`PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PrepSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PrepSchema.json
