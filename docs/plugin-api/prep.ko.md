**언어**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

# NeverC 플러그인 전처리기 API

`PluginPrep.h` 는 전처리기를 두 가지 방식으로 공개합니다. 39가지 이벤트 종류에
대한 **구독** 은 전처리기가 하는 모든 일 — 파일 진입, 매크로 정의와 확장, 조건
평가, 프래그마 — 의 읽기 전용 추적을 제공합니다. 여섯 개의 **단계** 는 한 걸음
더 나아가 결과를 다시 쓰게 해 줍니다. `#include` 를 다른 곳으로 돌리거나, 매크로
확장 토큰을 대체하거나, 프래그마를 직접 처리하거나, `__has_feature` 에 다르게
답하는 식입니다.

## 인터페이스

```c
#include "neverc/Plugin/PluginPrep.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
    NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table, &Minor, &TableSize);
```

230가지 토큰 종류(`NEVERC_TOKEN_KIND_COUNT`)와 전처리기 키워드 종류는
`Schema/PluginPrepSchema.inc` 에서 옵니다. 헤더가 이를 포함하며, 그 capability
major 는 반드시 `NEVERC_PREP_API_MAJOR` 와 같아야 합니다. 불일치는 런타임의
뜻밖의 사고가 아니라 컴파일 오류가 됩니다. 각 종류는 범주도 함께 갖습니다:
`NEVERC_TOKEN_CATEGORY_SPECIAL`, `COMMENT`, `IDENTIFIER`, `LITERAL`,
`PUNCTUATOR`, `KEYWORD`, `ANNOTATION`.

## 여섯 개의 전처리기 단계

| 단계 | 정책 | 입력 → 출력 |
|---|---|---|
| `neverc.prep.token` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | 토큰 하나 → 토큰 목록 |
| `neverc.prep.build_token_stream` | 위와 같음 | 범위 → 토큰 스트림 |
| `neverc.prep.include.intercept` | 위와 같음 | include 요청 → include 결정 |
| `neverc.prep.macro.intercept` | 위와 같음 | 매크로 연산 → 동작 + 토큰 |
| `neverc.prep.pragma.intercept` | 위와 같음 | 프래그마 → 동작 + 토큰 |
| `neverc.prep.feature_query.intercept` | 위와 같음 | `__has_*` 질의 → 값 |

각 단계는 `NevercPrepAPI` 위에 `Get<Kind>PhaseInput` 과
`Create<Kind>PhaseOutput` 쌍을 가지며, `Create` 쪽은 인터셉터의
`NevercPhaseContinuation` 을 받습니다. 따라서 출력은 그것을 소유한 단계 안에서만
만들 수 있습니다.

## 토큰 읽기

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

`Origin` 은 `NEVERC_TOKEN_ORIGIN_FILE`, `MACRO_REPLACEMENT`, `MACRO_ARGUMENT`,
`SYNTHESIZED` 중 하나이며, 사용자가 직접 친 토큰과 매크로가 만들어 낸 토큰을
구별하는 수단입니다.

플래그는 전처리기 자신의 장부이고, 토큰을 합성할 때 중요해집니다:

| 플래그 | 의미 |
|---|---|
| `NEVERC_TOKEN_FLAG_START_OF_LINE` | 그 줄의 첫 토큰 |
| `_LEADING_SPACE` | 앞에 공백이 있다 |
| `_DISABLE_EXPANSION` | 이 토큰을 매크로 확장하지 말 것 |
| `_NEEDS_CLEANING` | 철자에 이스케이프된 줄바꿈이나 삼중자가 들어 있다 |
| `_LEADING_EMPTY_MACRO` | 바로 앞에서 빈 매크로가 확장되었다 |
| `_HAS_UCN` | 범용 문자 이름을 포함한다 |
| `_IGNORED_COMMA`, `_COMMA_AFTER_ELIDED` | 가변 인자 쉼표 생략에 대한 장부 |
| `_STRINGIFIED_IN_MACRO` | `#` 가 만들어 냈다 |
| `_REINJECTED` | 토큰 스트림으로 되돌려 넣어졌다 |

`NEVERC_TOKEN_FLAG_ALL` 은 정의된 모든 비트의 마스크입니다. 일괄 읽기는
`GetTokenInfoBatch` 를 씁니다. 스트림 전체는 `GetTokenStreamView` 로
`NevercTokenView` 레코드의 가벼운 뷰로 읽거나, `GetTokenStreamToken` 으로 한
번에 하나씩 핸들을 읽습니다. 스트림 하나는 최대
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS`(16,777,216)개의 토큰을 담습니다.

## 식별자와 매크로

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

`NevercMacroDefinitionInfo` 는 이름, 정의한 지시문, 정의·종료·정의 해제 위치,
매개변수와 대체 토큰의 개수, 그리고 플래그 `NEVERC_MACRO_FUNCTION_LIKE`,
`VARIADIC`, `C99_VARIADIC`, `GNU_VARIADIC`, `HAS_VA_OPT`, `BUILTIN`,
`COMMA_PASTING` 을 보고합니다. 개별 매개변수와 대체 토큰은 `GetMacroParameter`
와 `GetMacroReplacementToken` 에서 얻습니다.

`NevercIdentifierInfo` 는 여기에 토큰 종류, 전처리기 키워드 종류, builtin ID,
그리고 `NEVERC_IDENTIFIER_KEYWORD`, `_HAS_MACRO`, `_POISONED`, `_RESERVED`
같은 플래그를 더합니다.

확장 지점에서 `GetMacroArgumentInfo` 는 인자 개수와 가변 인자가 생략되었는지를
보고하고, `GetMacroArgumentTokenStream` 은 각 인자의 토큰을 내놓습니다.

## 이벤트 구독

콜백 하나가 구독한 모든 이벤트를 받습니다. 마스크는 관심 있는 이벤트 종류로
조립합니다:

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
    /* Event->Payload.Condition.Value 는 NOT_EVALUATED, FALSE, TRUE */
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

`NEVERC_PREP_EVENT_MASK_ALL` 은 전부를 구독합니다. 39가지 종류를 사용하는
페이로드 공용체 멤버별로 묶으면 다음과 같습니다:

| 페이로드 | 이벤트 |
|---|---|
| `File` | `FILE_CHANGED`, `LEXED_FILE_CHANGED`, `FILE_SKIPPED`, `FILE_NOT_FOUND`, `END_OF_MAIN_FILE` |
| `Include` | `INCLUSION_DIRECTIVE` |
| `Text` | `IDENT`, `PRAGMA_DIRECTIVE`, `PRAGMA_COMMENT`, `PRAGMA_MARK`, `PRAGMA_DETECT_MISMATCH`, `PRAGMA_DEBUG`, `PRAGMA_MESSAGE`, `PRAGMA_DIAGNOSTIC_PUSH`, `PRAGMA_DIAGNOSTIC_POP`, `PRAGMA_DIAGNOSTIC`, `PRAGMA_WARNING`, `PRAGMA_WARNING_PUSH`, `PRAGMA_WARNING_POP`, `PRAGMA_EXEC_CHARSET_PUSH`, `PRAGMA_EXEC_CHARSET_POP`, `PRAGMA_ASSUME_NONNULL_BEGIN`, `PRAGMA_ASSUME_NONNULL_END`, `SOURCE_RANGE_SKIPPED` |
| `Macro` | `MACRO_EXPANDS`, `MACRO_DEFINED`, `MACRO_UNDEFINED`, `DEFINED`, `HAS_INCLUDE` |
| `Condition` | `IF`, `ELIF`, `IFDEF`, `ELIFDEF`, `ELIFDEF_SKIPPED`, `IFNDEF`, `ELIFNDEF`, `ELIFNDEF_SKIPPED`, `ELSE`, `ENDIF` |

`NevercPrepFileEvent.Reason` 은 `NEVERC_PREP_FILE_ENTER`, `EXIT`,
`SYSTEM_HEADER_PRAGMA`, `RENAME` 을 구별합니다. 이벤트는 읽기 전용입니다.
레코드와 그 안의 모든 뷰는 콜백 동안만 빌려온 것이고, 이벤트에서 공개된 핸들은
바깥 태스크 범위로 승격됩니다.

## include 방향 바꾸기

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

동작은 `NEVERC_PREP_INCLUDE_CONTINUE`, `_SKIP`, `_REDIRECT` 입니다. 입력은
`IsImport` 와 `IsIncludeNext` 도 보고하므로 `#import` 와 `#include_next` 를
구별할 수 있습니다.

## 매크로 확장 대체하기

매크로 단계의 입력은 수행 중인 연산 — `NEVERC_PREP_MACRO_DEFINE`, `_UNDEFINE`,
`_EXPAND`, `_EXPAND_BUILTIN` — 을 이름 토큰, 정의, 인자, 그리고 전처리기가 쓰려던
대체 토큰과 함께 실어 나릅니다.

```c
NevercPrepMacroPhaseOutput Out = {0};
Out.Header     = /* … */;
Out.Action     = NEVERC_PREP_MACRO_REPLACE_TOKENS;
Out.Tokens     = MyTokens;      /* const NevercTokenHandle * */
Out.TokenCount = MyTokenCount;
Prep->CreateMacroPhaseOutput(Prep->Context, Frame, Continuation, &Out, &Output);
```

`NEVERC_PREP_MACRO_CONTINUE` 는 내장 동작을 그대로 두고, `_SUPPRESS` 는 아무것도
아닌 것으로 확장됩니다.

## 토큰 만들기

합성 토큰은 빌더에서 나옵니다. 빌더는 커밋 전에 종류·철자·식별자의 조합을
검증합니다:

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

구두점과 키워드에는 `TokenBuilderSetKind` 를, 식별자에는
`TokenBuilderSetIdentifier` 를 쓰십시오. 토큰 종류 상수는
`PluginPrepSchema.inc` 에서 옵니다.

스트림 전체를 다룰 때 — `neverc.prep.build_token_stream` 단계 — 는 스트림 빌더에
쌓아 두었다가 한 번에 커밋합니다:

```c
NevercTokenStreamBuilderHandle Stream;
Prep->CreateTokenStreamBuilder(Prep->Context, Task, &Stream);
Prep->TokenStreamBuilderAppend(Prep->Context, Task, Stream, Tokens, Count);
Prep->TokenStreamBuilderCommit(Prep->Context, Frame, Stream, &Output);
Prep->DestroyTokenStreamBuilder(Prep->Context, Task, Stream);
```

단계 입력인 `NevercPrepTokenStreamPhaseInput` 은 시작과 끝 위치, 그리고 출력이
지켜야 할 `MaximumTokenCount` 를 줍니다.

## 프래그마와 기능 질의

프래그마 단계의 입력은 도입자(`NEVERC_PREP_PRAGMA_HASH`, `_Pragma` 에 해당하는
`_OPERATOR`, `__pragma` 에 해당하는 `_MS`), 네임스페이스와 이름, 그리고 인자
토큰을 보고합니다. 출력 동작은 `NEVERC_PREP_PRAGMA_CONTINUE`, `_HANDLED`,
`_REPLACE_TOKENS` 입니다.

기능 질의는 `NEVERC_PREP_QUERY_HAS_FEATURE` 등을 통해 `__has_feature`,
`__has_extension`, `__has_builtin`, `__has_include`, `__has_include_next` 를
포괄합니다. 입력은 이름과 컴파일러가 계산한 `BuiltinValue` 를 나르고, 출력은
계속하거나 대체합니다:

```c
NevercPrepFeatureQueryPhaseOutput Out = {0};
Out.Header = /* … */;
Out.Action = NEVERC_PREP_QUERY_REPLACE;
Out.Value  = NEVERC_TRUE;
Prep->CreateFeatureQueryPhaseOutput(Prep->Context, Frame, Continuation, &Out,
                                    &Output);
```

## 규칙

- 이벤트 레코드, 문자열 뷰, 정수 배열은 콜백 동안만 빌려온 것입니다. 이벤트에서
  공개된 핸들은 태스크가 끝날 때까지 살아 있습니다.
- 모든 빌더에는 짝이 되는 `Destroy*` 호출이 필요하며, 오류 경로에서도
  마찬가지입니다.
- `Create<Kind>PhaseOutput` 호출에는 그것이 속한 단계의 continuation 이
  필요합니다. 다른 단계의 continuation 을 쓰면 `NEVERC_STATUS_WRONG_SCOPE` 가
  반환됩니다.
- 처리할 이벤트만 구독하십시오. 마스크가 곧 조절 밸브입니다 —
  `NEVERC_PREP_EVENT_MASK_ALL` 을 받아 C 쪽에서 걸러내는 플러그인은 모든 콜백에
  대한 대가를 치릅니다.
- 전처리기 콜백은 전처리기가 한창 동작하는 도중에 태스크 스레드에서 실행됩니다.
  그 안에서 전처리기로 다시 들어가지 마십시오.
- 필수 포인터가 없으면 `NEVERC_STATUS_INVALID_ARGUMENT` 를 반환하고, 예외가
  경계를 넘게 하지 마십시오.

규범적 선언은 `PluginPrep.h` 와 `Schema/PluginPrepSchema.inc` 를, 토큰 종류
스키마는 `Schema/PrepSchema.json` 을 참조하십시오.
