**Языки**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

[← ABI плагинов NeverC](README.ru.md)

# API препроцессора плагинов NeverC

[`PluginPrep.h`] открывает препроцессор двумя способами. **Подписка** на 39 видов
событий даёт трассировку только для чтения всего, что делает препроцессор: вход
в файл, определение и раскрытие макросов, вычисление условий, прагмы. Шесть
**фаз** идут дальше и позволяют переписать результат: перенаправить `#include`,
заменить токены раскрытия макроса, обработать прагму самому или ответить на
`__has_feature` иначе.

## Интерфейс

```c
#include "neverc/Plugin/PluginPrep.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
    NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table, &Minor, &TableSize);
```

230 видов токенов (`NEVERC_TOKEN_KIND_COUNT`) и виды ключевых слов препроцессора
берутся из [`Schema/PluginPrepSchema.inc`], который включает заголовок и чей
capability major обязан равняться `NEVERC_PREP_API_MAJOR` — расхождение это
ошибка компиляции, а не сюрприз во время выполнения. Каждый вид несёт ещё и
категорию: `NEVERC_TOKEN_CATEGORY_SPECIAL`, `COMMENT`, `IDENTIFIER`, `LITERAL`,
`PUNCTUATOR`, `KEYWORD` или `ANNOTATION`.

## Шесть фаз препроцессора

| Фаза | Политика | Вход → выход |
|---|---|---|
| `neverc.prep.token` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | один токен → список токенов |
| `neverc.prep.build_token_stream` | так же | диапазон → поток токенов |
| `neverc.prep.include.intercept` | так же | запрос включения → решение о включении |
| `neverc.prep.macro.intercept` | так же | операция с макросом → действие + токены |
| `neverc.prep.pragma.intercept` | так же | прагма → действие + токены |
| `neverc.prep.feature_query.intercept` | так же | запрос `__has_*` → значение |

У каждой в `NevercPrepAPI` есть пара `Get<Kind>PhaseInput` и
`Create<Kind>PhaseOutput`, причём половина `Create` принимает
`NevercPhaseContinuation` перехватчика, поэтому выход можно произвести только
изнутри владеющей им фазы.

## Чтение токенов

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

`Origin` — это `NEVERC_TOKEN_ORIGIN_FILE`, `MACRO_REPLACEMENT`,
`MACRO_ARGUMENT` или `SYNTHESIZED`; именно так отличают токен, набранный
пользователем, от порождённого макросом.

Флаги — собственная бухгалтерия препроцессора, и они важны, когда вы синтезируете
токены:

| Флаг | Значение |
|---|---|
| `NEVERC_TOKEN_FLAG_START_OF_LINE` | Первый токен в своей строке |
| `_LEADING_SPACE` | Перед ним есть пробел |
| `_DISABLE_EXPANSION` | Не раскрывать этот токен макросом |
| `_NEEDS_CLEANING` | Написание содержит экранированные переводы строк или триграфы |
| `_LEADING_EMPTY_MACRO` | Прямо перед ним раскрылся пустой макрос |
| `_HAS_UCN` | Содержит универсальное имя символа |
| `_IGNORED_COMMA`, `_COMMA_AFTER_ELIDED` | Учёт опускания запятой у вариативных макросов |
| `_STRINGIFIED_IN_MACRO` | Порождён оператором `#` |
| `_REINJECTED` | Возвращён обратно в поток токенов |

`NEVERC_TOKEN_FLAG_ALL` — маска всех определённых битов. Пакетное чтение идёт
через `GetTokenInfoBatch`; целый поток читается либо как лёгкое представление
записей `NevercTokenView` через `GetTokenStreamView`, либо по одному дескриптору
через `GetTokenStreamToken`. Поток вмещает не более
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS` (16 777 216) токенов.

## Идентификаторы и макросы

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

`NevercMacroDefinitionInfo` сообщает имя, определяющую директиву, позиции
определения, конца и отмены определения, число параметров и токенов замены, а
также флаги: `NEVERC_MACRO_FUNCTION_LIKE`, `VARIADIC`, `C99_VARIADIC`,
`GNU_VARIADIC`, `HAS_VA_OPT`, `BUILTIN` и `COMMA_PASTING`. Отдельные параметры и
токены замены дают `GetMacroParameter` и `GetMacroReplacementToken`.

`NevercIdentifierInfo` добавляет вид токена, вид ключевого слова препроцессора,
идентификатор встроенной функции и флаги вроде `NEVERC_IDENTIFIER_KEYWORD`,
`_HAS_MACRO`, `_POISONED` и `_RESERVED`.

В месте раскрытия `GetMacroArgumentInfo` сообщает число аргументов и были ли
опущены вариативные, а `GetMacroArgumentTokenStream` выдаёт токены каждого
аргумента.

## Подписка на события

Один обратный вызов получает все подписанные события. Маска строится из тех
видов, которые вам интересны:

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
    /* Event->Payload.Condition.Value — NOT_EVALUATED, FALSE или TRUE */
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

`NEVERC_PREP_EVENT_MASK_ALL` подписывает на всё. Все 39 видов, сгруппированные по
члену объединения полезной нагрузки, который они используют:

| Нагрузка | События |
|---|---|
| `File` | `FILE_CHANGED`, `LEXED_FILE_CHANGED`, `FILE_SKIPPED`, `FILE_NOT_FOUND`, `END_OF_MAIN_FILE` |
| `Include` | `INCLUSION_DIRECTIVE` |
| `Text` | `IDENT`, `PRAGMA_DIRECTIVE`, `PRAGMA_COMMENT`, `PRAGMA_MARK`, `PRAGMA_DETECT_MISMATCH`, `PRAGMA_DEBUG`, `PRAGMA_MESSAGE`, `PRAGMA_DIAGNOSTIC_PUSH`, `PRAGMA_DIAGNOSTIC_POP`, `PRAGMA_DIAGNOSTIC`, `PRAGMA_WARNING`, `PRAGMA_WARNING_PUSH`, `PRAGMA_WARNING_POP`, `PRAGMA_EXEC_CHARSET_PUSH`, `PRAGMA_EXEC_CHARSET_POP`, `PRAGMA_ASSUME_NONNULL_BEGIN`, `PRAGMA_ASSUME_NONNULL_END`, `SOURCE_RANGE_SKIPPED` |
| `Macro` | `MACRO_EXPANDS`, `MACRO_DEFINED`, `MACRO_UNDEFINED`, `DEFINED`, `HAS_INCLUDE` |
| `Condition` | `IF`, `ELIF`, `IFDEF`, `ELIFDEF`, `ELIFDEF_SKIPPED`, `IFNDEF`, `ELIFNDEF`, `ELIFNDEF_SKIPPED`, `ELSE`, `ENDIF` |

`NevercPrepFileEvent.Reason` различает `NEVERC_PREP_FILE_ENTER`, `EXIT`,
`SYSTEM_HEADER_PRAGMA` и `RENAME`. События предназначены только для чтения:
запись и каждое представление внутри неё одолжены на время обратного вызова, а
дескрипторы, опубликованные в событии, повышаются до области охватывающей задачи.

## Перенаправление включения

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

Действия: `NEVERC_PREP_INCLUDE_CONTINUE`, `_SKIP` и `_REDIRECT`. Вход также
сообщает `IsImport` и `IsIncludeNext`, так что `#import` и `#include_next`
различимы.

## Замена раскрытия макроса

Вход фазы макросов несёт выполняемую операцию —
`NEVERC_PREP_MACRO_DEFINE`, `_UNDEFINE`, `_EXPAND` или `_EXPAND_BUILTIN` —
вместе с токеном имени, определением, аргументами и теми токенами замены,
которые собирался использовать препроцессор.

```c
NevercPrepMacroPhaseOutput Out = {0};
Out.Header     = /* … */;
Out.Action     = NEVERC_PREP_MACRO_REPLACE_TOKENS;
Out.Tokens     = MyTokens;      /* const NevercTokenHandle * */
Out.TokenCount = MyTokenCount;
Prep->CreateMacroPhaseOutput(Prep->Context, Frame, Continuation, &Out, &Output);
```

`NEVERC_PREP_MACRO_CONTINUE` сохраняет встроенное поведение, а `_SUPPRESS`
раскрывается в ничто.

## Построение токенов

Синтезированные токены рождаются в строителе, который перед фиксацией проверяет
сочетание вида, написания и идентификатора:

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

Для знаков пунктуации и ключевых слов используйте `TokenBuilderSetKind`, для
идентификаторов — `TokenBuilderSetIdentifier`. Константы видов токенов берутся
из [`PluginPrepSchema.inc`].

Для целого потока — фаза `neverc.prep.build_token_stream` — накапливайте в
строителе потока и фиксируйте один раз:

```c
NevercTokenStreamBuilderHandle Stream;
Prep->CreateTokenStreamBuilder(Prep->Context, Task, &Stream);
Prep->TokenStreamBuilderAppend(Prep->Context, Task, Stream, Tokens, Count);
Prep->TokenStreamBuilderCommit(Prep->Context, Frame, Stream, &Output);
Prep->DestroyTokenStreamBuilder(Prep->Context, Task, Stream);
```

Вход фазы `NevercPrepTokenStreamPhaseInput` даёт начальную и конечную позиции, а
также `MaximumTokenCount`, который выход обязан соблюсти.

## Прагмы и запросы возможностей

Вход фазы прагм сообщает вводитель (`NEVERC_PREP_PRAGMA_HASH`, `_OPERATOR` для
`_Pragma` или `_MS` для `__pragma`), пространство имён и имя, а также токены
аргументов. Действие выхода — `NEVERC_PREP_PRAGMA_CONTINUE`, `_HANDLED` или
`_REPLACE_TOKENS`.

Запрос возможности покрывает `__has_feature`, `__has_extension`,
`__has_builtin`, `__has_include` и `__has_include_next` через
`NEVERC_PREP_QUERY_HAS_FEATURE` и его собратьев. Вход несёт имя и вычисленное
компилятором `BuiltinValue`; выход либо продолжает, либо заменяет:

```c
NevercPrepFeatureQueryPhaseOutput Out = {0};
Out.Header = /* … */;
Out.Action = NEVERC_PREP_QUERY_REPLACE;
Out.Value  = NEVERC_TRUE;
Prep->CreateFeatureQueryPhaseOutput(Prep->Context, Frame, Continuation, &Out,
                                    &Output);
```

## Правила

- Записи событий, строковые представления и массивы целых одолжены на время
  обратного вызова. Дескрипторы, опубликованные в событии, живут до конца задачи.
- Каждому строителю нужен парный вызов `Destroy*`, в том числе на пути ошибки.
- Вызов `Create<Kind>PhaseOutput` требует continuation той фазы, которой он
  принадлежит; использование чужой continuation вернёт
  `NEVERC_STATUS_WRONG_SCOPE`.
- Подписывайтесь только на события, которые обрабатываете. Маска и есть дроссель
  — плагин, который берёт `NEVERC_PREP_EVENT_MASK_ALL` и фильтрует на стороне C,
  платит за каждый обратный вызов.
- Обратные вызовы препроцессора выполняются в потоке задачи, пока препроцессор в
  разгаре работы. Не входите в препроцессор повторно из такого вызова.
- Возвращайте `NEVERC_STATUS_INVALID_ARGUMENT` при отсутствии обязательного
  указателя и никогда не позволяйте исключению пересечь границу.

Нормативные объявления смотрите в [`PluginPrep.h`] и
[`Schema/PluginPrepSchema.inc`], схему видов токенов — в
[`Schema/PrepSchema.json`], а шесть фаз препроцессора и их политики — в
[`Schema/PhaseSchema.json`].

<!-- reference links -->
[`PluginPrep.h`]: ../../neverc/include/neverc/Plugin/PluginPrep.h
[`PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PrepSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PrepSchema.json
