**Языки**: [English](link-lto.md) | [简体中文](link-lto.zh-CN.md) | [繁體中文](link-lto.zh-TW.md) | [日本語](link-lto.ja.md) | [한국어](link-lto.ko.md) | [Français](link-lto.fr.md) | [Deutsch](link-lto.de.md) | [Español](link-lto.es.md) | [Italiano](link-lto.it.md) | [Русский](link-lto.ru.md) | [العربية](link-lto.ar.md)

# API компоновки и LTO для плагинов NeverC

Компоновка смоделирована как **конечный автомат над одним графом**.
`PluginLink.h` раскрывает этот граф — входы, секции, атомы, символы, рёбра,
COMDAT, импорты, экспорты, записи раскрутки, синтетику и ограничения
размещения — а также двадцать фаз, которые продвигают его от списка файлов до
зафиксированного двоичного образа. `PluginLTO.h` покрывает две фазы посередине,
где биткод превращается в объекты.

Плагин может наблюдать каждый шаг, перехватывать большинство из них, заменить
один шаг, заменить всю компоновку целиком или слить объекты. Он никогда не видит
структуру данных lld: граф — это нормализованная проекция, на которую
отображаются бэкенды ELF, COFF и Mach-O.

## Интерфейсы

```c
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"   /* включает PluginLink.h */
```

| Интерфейс | Таблица | Назначение |
|---|---|---|
| `NEVERC_INTERFACE_LINK_{HIGH,LOW}` | `NevercLinkAPI` | Чтение и изменение графа компоновки (52 слота) |
| `NEVERC_INTERFACE_LINK_REGISTRAR_{HIGH,LOW}` | `NevercLinkRegistrarAPI` | Регистрация провайдеров компоновщика, слияния объектов и проверки образа |
| `NEVERC_INTERFACE_LINK_PHASE_{HIGH,LOW}` | `NevercLinkPhaseAPI` | Доступ к графу или образу за `NevercArtifactHandle` |
| `NEVERC_INTERFACE_LTO_{HIGH,LOW}` | `NevercLTOAPI` | Чтение LTO-запроса, модулей и разрешений символов |
| `NEVERC_INTERFACE_LTO_REGISTRAR_{HIGH,LOW}` | `NevercLTORegistrarAPI` | Регистрация провайдера кодогенерации LTO |

Все пять при мажорной версии 1 имеют статус `NEVERC_INTERFACE_STABLE`, поэтому
более новый хост может только дописывать. Сочетайте каждую с её
`NEVERC_LINK_API_MAJOR` / `NEVERC_LTO_API_MAJOR` и проверяйте `TableSize` по
последнему слоту, который вызываете.

## Конечный автомат

`NevercLinkGraphInfo.State` принимает одно из четырнадцати значений, и
тринадцать из двадцати фаз существуют исключительно ради того, чтобы продвинуть
его на один шаг:

| Фаза | Итоговое `NEVERC_LINK_STATE_…` | Верификатор хоста |
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

Каждая из этих тринадцати имеет политику
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE | SKIPPABLE_WITH_PROOF`, так что
провайдер может сам выполнить переход, а плагин с действительным
`NevercLinkProofHandle` может его пропустить.

Оставшиеся семь — структурные:

| Фаза | Политика | Роль |
|---|---|---|
| `neverc.link.full` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | Заменить всю компоновку, из `INITIAL` сразу в двоичный образ |
| `neverc.link.object_merge` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE | Перемещаемое слияние ObjectGraph по `-r` |
| `neverc.link.post_emit` | OBSERVABLE, INTERCEPTABLE | Последняя возможность тронуть байты образа |
| `neverc.link.image_verify` | OBSERVABLE, **ЗАПЕЧАТАНА** | Верификатор образа хоста |
| `neverc.link.side_outputs_verify` | OBSERVABLE, **ЗАПЕЧАТАНА** | Map-файлы, dSYM, побочные артефакты |
| `neverc.link.commit` | OBSERVABLE, **ЗАПЕЧАТАНА** | Атомарная публикация выходного набора |
| `neverc.link.after_commit` | OBSERVABLE | Уведомление после фиксации |

Три запечатанных шлюза можно наблюдать, но никогда нельзя перехватить, заменить
или пропустить. `NEVERC_BUILTIN_LINK_PHASE_COUNT` равно 20.

## Как добраться до графа из фазы

`NevercLinkPhaseAPI` превращает артефакт кадра в пригодный дескриптор:

```c
NevercLinkPhaseGraphInfo GraphInfo = {0};
GraphInfo.Header = (NevercABITableHeader){sizeof(GraphInfo),
                                          NEVERC_LINK_PHASE_API_MAJOR,
                                          NEVERC_LINK_PHASE_API_MINOR, 0};
LinkPhase->GetGraph(LinkPhase->Context, Frame, Frame->Input, &GraphInfo);
/* GraphInfo.Link, .Graph, .Proof, .State, .Generation */
```

`GraphInfo.Link` — это `NevercLinkAPI`, привязанная к данной задаче, поэтому
наблюдателю не нужен отдельный `QueryInterface`. Провайдер публикует результат
через `PublishGraph`, а `GetImage` делает то же самое для артефакта-образа,
возвращая `NevercLinkPhaseImageInfo` с образом, выходным набором и
`NevercBinaryImageState` (`CANDIDATE`, `VERIFIED`, `COMMITTED`, `ABORTED` или
`FAILED_PARTIAL`).

## Чтение графа

`NevercLinkGraphInfo` — это сводка: цель, формат, состояние, поколение,
семнадцать счётчиков сущностей и 32-байтовый `SemanticDigest`. Сами сущности
возвращаются одним постраничным вызовом на каждый вид, и все они используют
страницу, принадлежащую вызывающей стороне:

```c
typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;                /* массив, который вы даёте и которым владеете */
  uint64_t ElementCapacity;  /* сколько записей помещается                  */
  uint64_t ElementStride;    /* sizeof вашего элемента                      */
  uint64_t OutCount;         /* сколько записал хост                        */
  uint64_t NextCursor;       /* передайте обратно, чтобы продолжить         */
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;
```

Хост пишет не более `ElementCapacity` записей по `ElementStride` байт и никогда
не удерживает `Data`, поэтому достаточно массива на стеке:

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

Пятнадцать постраничных функций графа устроены так же — `GetInputPage`,
`GetArchivePage`, `GetArchiveMemberPage`, `GetSharedLibraryPage`,
`GetBitcodeModulePage`, `GetSectionPage`, `GetAtomPage`, `GetSymbolPage`,
`GetEdgePage`, `GetComdatPage`, `GetImportPage`, `GetExportPage`,
`GetUnwindPage`, `GetSyntheticPage` и `GetConstraintPage` — а ещё две,
`GetBinarySegmentPage` и `GetBinarySectionPage`, листают выпущенный образ. У
каждой есть парная `Get…Info` для одиночного дескриптора.

Каждая информация о сущности несёт `NevercLinkOrigin`:

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

Именно это делает компоновку проверяемой: для любого атома в выходе вы можете
назвать входной файл, член архива, из которого он был извлечён, фазу, которая
его создала, и плагин, который трогал его последним.

### Сущности

| Вид | Структура Info | Заметные поля |
|---|---|---|
| Input | `NevercLinkInputInfo` | `Kind` (OBJECT, ARCHIVE, SHARED_LIBRARY, BITCODE, SCRIPT, BLOB), `Ordinal`, `ContentDigest`, `ReaderRoute` |
| Архив / член | `NevercLinkArchiveInfo`, `NevercLinkArchiveMemberInfo` | `Thin`, `Materialized`, `MaterializationReason` |
| Разделяемая библиотека | `NevercLinkSharedLibraryInfo` | `InstallName` |
| Модуль биткода | `NevercLinkBitcodeModuleInfo` | `Summary` |
| Секция | `NevercLinkSectionInfo` | `Kind`, `Flags`, `Alignment`, `Address`, `Size`, `Comdat` |
| Атом | `NevercLinkAtomInfo` | `Flags`, `Content`, `ZeroFillSize`, `FoldLeader` |
| Символ | `NevercLinkSymbolInfo` | `Binding`, `Visibility`, `Definition`, `IsPrevailing`, `IsRoot` |
| Ребро | `NevercLinkEdgeInfo` | `Kind`, `Offset`, `RelocationKind`, `Addend`, `TargetSymbol`, `TargetAtom` |
| COMDAT | `NevercLinkComdatInfo` | `Selection`, `Selected` |
| Импорт / экспорт | `NevercLinkImportInfo`, `NevercLinkExportInfo` | `Library`, `Symbol` |
| Раскрутка | `NevercLinkUnwindInfo` | `PersonalitySymbol` |
| Синтетика | `NevercLinkSyntheticInfo` | `Role`, `Section`, `Atom` |
| Ограничение | `NevercLinkConstraintInfo` | `Kind`, `SubjectID`, `Value`, `Required` |

Флаги атома: `LIVE`, `ROOT`, `SYNTHETIC`, `FOLDED`, `ADDRESS_SIGNIFICANT`,
`TLS` и `UNWIND`. Привязки символов: `LOCAL`, `GLOBAL`, `WEAK` и `COMMON`;
определения: `UNDEFINED`, `DEFINED`, `ABSOLUTE`, `COMMON` и `SHARED`. Виды
рёбер: `RELOCATION`, `ASSOCIATION`, `KEEP_ALIVE`, `UNWIND` и
`FORMAT_EXTENSION`. Выбор COMDAT охватывает `ANY`, `EXACT_MATCH`, `SAME_SIZE`,
`LARGEST`, `NEWEST` и `NO_DUPLICATES`.

## Изменение графа

Изменение транзакционно и всегда ограничено одним графом:

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

Фиксация подготавливает рабочую копию, проверяет её и лишь затем публикует и
увеличивает `Generation`. `AbandonMutation` отбрасывает всё. Например, фиксация
в момент, когда граф находится в `GC_COMPLETE`, повторно запускает верификатор
живости, так что изменение, которое оставило бы живой атом без связей,
отвергается, а не записывается.

### Изменения аннулируют состояние ниже по потоку

Именно это чаще всего застаёт врасплох. Каждый подготовительный вызов
классифицируется, и классификация определяет **самое раннее состояние, которое
становится недействительным**; хост обязан заново выполнить все фазы начиная с
него:

| Вызов | Самое раннее аннулированное состояние |
|---|---|
| `RebindSymbol`, `RetargetEdge` | `SYMBOLS_RESOLVED` |
| `SetSymbolResolution` | `COMDAT_SELECTED` |
| `SetSymbolRoot` | `GC_COMPLETE` |
| `SetAtomLive` | `ICF_COMPLETE` |
| `SetFoldLeader`, `ReplaceAtomContent` | `SYNTHETICS_READY` |
| `CreateSynthetic`, `ReplaceSynthetic`, `EraseSynthetic` | `SYNTHETICS_READY` |
| `CreateConstraint`, `ReplaceConstraint`, `EraseConstraint` | `LAYOUT_COMPLETE` |

Изменение, затрагивающее несколько пунктов, берёт минимальный. Поэтому
перепривязка символа после размещения выбрасывает результаты размещения,
перемещения и образа — дёшево во время `gc`, дорого во время `post_emit`.
Меняйте настолько рано в конечном автомате, насколько позволяет ваша правка.

`SetSymbolResolution` принимает небольшую запись обновления, а не символ
целиком, чтобы изменение разрешения случайно не переписало имя или значение:

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

## Пропуск фазы по доказательству

Фаза `SKIPPABLE_WITH_PROOF` принимает `NevercLinkProofHandle` вместо того,
чтобы выполняться. Доказательство фиксирует всё, от чего зависит пропуск:

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

Поскольку записаны и `GraphGeneration`, и `SemanticDigest`, любое
зафиксированное изменение между выдачей доказательства и его использованием
делает доказательство устаревшим, и хост выполняет фазу по-настоящему.

## Двоичный образ

После `emit_image` продуктом становится `NevercBinaryImageHandle`:

```c
NevercBinaryImageInfo Image = {0};
Image.Header = /* … */;
Link->GetBinaryImageInfo(Link->Context, Task, ImageHandle, &Image);
/* .State, .OutputKind, .EntryAddress, .ImageBase, .Size,
   .SegmentCount, .SectionCount, .ImportCount, .ExportCount,
   .DynamicRelocationCount, .ContentDigest                     */
```

Виды вывода: `RELOCATABLE`, `EXECUTABLE`, `SHARED_LIBRARY` и `BUNDLE`. Флаги
сегмента: `READ`, `WRITE` и `EXECUTE`.

`Image.Binary` и `Image.Builder` — это ограниченный транзакционный писатель из
`PluginObject.h`: `Reserve`, `Write`, `WriteAt`, `Tell`, `ReadAt`, `Insert`,
`Append`, `Resize`. Перехватчик `post_emit`, который правит байты, обязан идти
через него; запись за зарезервированную границу прерывает подготовку, а не
увеличивает файл.

## Провайдеры

Регистрируйте во время `Register`, никогда позже.

### Замена компоновщика

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
Provider.VerifyImage  = my_verify;      /* необязательно */
LinkRegistrar->RegisterLinkerProvider(LinkRegistrar->Context,
                                      RegistrarContext, &Provider);
```

Обратный вызов получает запрос и сырой набор входов и заполняет кандидата:

```c
static NevercStatus NEVERC_CALL
my_link(void *UserData, NevercTaskHandle Task,
        const NevercLinkRequest *Request,
        const NevercRawLinkInputSet *Inputs,
        NevercLinkerProductCandidate *OutCandidate) {
  /* Request->Target, ->OutputKind, ->OutputURI, ->Options, ->RequestDigest
     Inputs->Inputs — это NevercRawLinkInput[], Inputs->OrderDigest фиксирует
     порядок */
  OutCandidate->Image     = MyImage;
  OutCandidate->Outputs   = MyBundle;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

`NevercLinkOptions` несёт флаги, по которым компоновщик действительно
ветвится, — `PIE`, `STATIC`, `GC_SECTIONS`, `ICF`, `EXPORT_DYNAMIC`,
`ALLOW_UNDEFINED`, `WHOLE_ARCHIVE`, `DETERMINISTIC`, — а также `EntrySymbol`,
`InstallName`, `Soname`, `ImageBase`, `PageSize`, `ThreadBudget`, пути поиска и
библиотеки. Флаги отдельного входа: `WHOLE_ARCHIVE`, `AS_NEEDED`,
`START_GROUP`, `END_GROUP` и `LAZY`.

При успехе хост принимает кандидата. При неудаче всё созданное остаётся во
владении провайдера. Запечатанные шлюзы проверки и фиксации выполняются в любом
случае.

### Слияние объектов и проверка образов

`RegisterObjectMergeProvider` обслуживает `-r`: запрос несёт входные
`NevercObjectMergeInput[]`, а также заранее открытые выходной граф и мутацию,
так что провайдер пишет в транзакцию, принадлежащую хосту, а не собирает файл.

`RegisterBinaryImageVerifier` добавляет проверку только на чтение, которая
работает рядом с собственным верификатором образа хоста. Заменить его она не
может.

## LTO

`lto_resolve` порождает разрешения символов; `lto_generate` превращает биткод в
объекты. `NevercLTOAPI` читает и то и другое.

```c
NevercLTORequest Request = {0};
Request.Header = /* … */;
LTO->GetRequest(LTO->Context, Task, RequestHandle, &Request);
/* .LinkRequest, .LinkGraph, .Target, .OutputFormat, .Options,
   .Modules, .Resolutions, .ResolutionDigest, .RequestDigest */
```

`GetModulePage` и `GetResolutionPage` используют тот же протокол
`NevercLinkEntityPage`, заполняя `NevercLTOInputModuleInfo` и
`NevercLTOSymbolResolution`. Каждое разрешение называет модуль, символ,
соответствующий `NevercLinkSymbolHandle` и свои флаги:

| Флаг | Значение |
|---|---|
| `PREVAILING` | Этот модуль владеет определением. |
| `VISIBLE_TO_REGULAR_OBJECT` | Небиткодовый объект может его видеть. |
| `EXPORTED` | Присутствует в таблице динамических символов. |
| `FINAL_DEFINITION` | Никакое последующее определение не может его заменить. |
| `CAN_INLINE` | Встраивание через границу разрешено. |
| `CAN_INTERNALIZE` | Интернализация разрешена. |
| `LINKER_REDEFINED` | Компоновщик его переопределил. |
| `REFERENCED_BY_REGULAR_OBJECT` | На него ссылается обычный объект. |

`NevercLTOOptions` выбирает `NEVERC_LTO_FULL` или `NEVERC_LTO_THIN`, уровни
оптимизации, `ThreadBudget`, `ThinBackendPartitions`, CPU и возможности, а
также область кэша: `DISABLED`, `TASK`, `LOCAL_SHARED` или `REMOTE_SHARED`.
Флаги параметров: `EMIT_OPTIMIZED_BITCODE`, `EMIT_INDEX`, `SAVE_TEMPS`,
`WHOLE_PROGRAM_VISIBILITY`, `UNIFIED_LTO` и `DETERMINISTIC`.

### Провайдер LTO

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

`BuildCacheKey` пишет в предоставленный вызывающей стороной
`NevercMutableByteView` и сообщает нужный ему размер, чтобы хост мог подобрать
буфер и повторить. Это должна быть чистая функция от запроса — безопасная
конструкция получается выводом из `RequestDigest` и `ResolutionDigest`.
Объявление `CACHEABLE` с ключом, игнорирующим часть запроса, порождает
устаревшие объекты, переживающие чистую пересборку.

`Codegen` заполняет `NevercLTOProductCandidate`: массив
`NevercLTOObjectProduct` (каждый называет свой исходный модуль, ObjectGraph и
артефакт), при необходимости `OptimizedBitcode` и `ThinIndex`, а также
фактически использованный `CacheKey`.

## Правила

- Дескрипторы имеют область действия задачи и принадлежат хосту. Никогда не
  сохраняйте их после обратного вызова, не используйте в другой задаче и не
  придумывайте значения.
- `NevercLinkEntityPage.Data` принадлежит вам. Хост пишет не более
  `ElementCapacity × ElementStride` байт и не сохраняет ссылок на него.
- Каждый `BeginMutation` доходит ровно до одного `CommitMutation` или
  `AbandonMutation`, в том числе на пути ошибки.
- Меняйте настолько рано в конечном автомате, насколько позволяет правка;
  позднее изменение молча аннулирует все нижележащие фазы.
- Не меняйте граф из наблюдателя. Наблюдателю выдаётся мост только для чтения, и
  попытка отвергается с `NEVERC_STATUS_POLICY_VIOLATION`.
- Пишите байты образа только через `NevercBinaryImageInfo.Binary` и его
  строитель. Переполнение прерывает подготовку, а не увеличивает вывод.
- Заявляйте `DETERMINISTIC` только если один и тот же дайджест запроса всегда
  даёт побайтово одинаковый вывод, и `CACHEABLE` только если ваш ключ кэша
  покрывает каждый вход, способный этот вывод изменить.
- `image_verify`, `side_outputs_verify` и `commit` запечатаны. Наблюдайте за
  ними; не пытайтесь перехватывать или пропускать.

Нормативные объявления смотрите в `PluginLink.h` и `PluginLTO.h`, политики
двадцати фаз — в `Schema/PhaseSchema.json`, а тесты, закрепляющие каждую из
них, — в `coverage.json`.
