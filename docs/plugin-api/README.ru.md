**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# ABI плагинов NeverC

Плагин NeverC — это разделяемый модуль, который экспортирует ровно одну
функцию, согласует версионированные таблицы возможностей по 128-битному
идентификатору интерфейса и подключается к замороженному графу именованных
фаз компилятора. Весь интерфейс — чистый C11. Плагин никогда не подключает
заголовки LLVM, никогда не компонуется с компилятором и никогда не передаёт
через границу типы C++.

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);
```

Эта сигнатура, объявленная в `PluginCore.h`, и есть весь контракт компоновки.
Всё остальное — чтение IR, перезапись графа объектного файла, замена конвейера
оптимизации — достигается через таблицы, которые вы запрашиваете у хоста по
идентификатору.

## Руководства

| Руководство | Что охватывает |
|---|---|
| [API драйвера](driver.ru.md) | Командная строка, выбор тулчейна, граф действий, граф заданий |
| [API источников и ввода-вывода](source.ru.md) | Провайдеры VFS, позиции в исходниках, буферы, приёмники вывода, зависимости |
| [API препроцессора](prep.ru.md) | Токены, макросы, прагмы, включения, запросы возможностей, 39 видов событий |
| [API AST и семантики](ast-sema.ru.md) | Расширение парсера, изменение AST, поиск имён, типы, константы |
| [API IR](ir.ru.md) | Чтение LLVM IR, транзакционное построение, анализы, проходы, провайдеры |
| [API MIR](mir.ru.md) | Машинные функции, регистры, кадры стека, проходы и анализы MIR |
| [Целевая платформа, MC, ассемблер, объектные файлы](target-mc-object.ru.md) | Регистрация целевых платформ, соглашения о вызовах, кодирование MC, графы объектных файлов |
| [API компоновки и LTO](link-lto.ru.md) | Граф компоновки, разрешение символов, GC/ICF, провайдеры компоновщика и LTO |
| [API DynCode](dyncode.ru.md) | Плоские позиционно-независимые образы, понижение импортов, кодирование набора символов |
| [Пользовательские соглашения о вызовах](custom-callconv/README.md) | Плагины соглашений о вызовах, управляемые данными |
| [Свидетельства покрытия фаз](coverage.json) | Сопоставление тестов для каждой стабильной фазы |

## Модель выполнения

Хост управляет плагином через три вложенные области. Каждая область передаёт
плагину непрозрачный указатель состояния, который плагин сам выделяет и
которым владеет, — поэтому правильно написанному плагину не нужно никакое
глобальное изменяемое состояние.

| Область | Обратные вызовы | Смысл |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | Один процесс компилятора. Здесь запрашивают интерфейсы и регистрируют возможности. |
| Session | `SessionBegin`, `SessionEnd` | Один вызов драйвера. |
| Task | `TaskBegin`, `TaskEnd` | Одна единица работы, определяемая `NevercTaskKind`. |

```c
typedef struct NevercPluginDescriptor {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView DisplayName;
  NevercSemanticVersion Version;
  NevercConcurrencyModel Concurrency;
  NevercReentrancyModel Reentrancy;
  NevercStructArrayView RequiredInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView OptionalInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView Dependencies;         /* NevercPluginDependency[]     */
  NevercProcessBeginFn ProcessBegin;
  NevercRegisterPluginFn Register;
  NevercSessionBeginFn SessionBegin;
  NevercSessionEndFn SessionEnd;
  NevercTaskBeginFn TaskBegin;
  NevercTaskEndFn TaskEnd;
  NevercPluginDestroyFn Destroy;
} NevercPluginDescriptor;
```

Практически обязательны только `PluginID` и `Register`; любой слот обратного
вызова может остаться `NULL`. Виды задач: `NEVERC_TASK_INVOCATION`,
`TRANSLATION_UNIT`, `LTO`, `LINK`, `CODEGEN`, `OBJECT` и `DYNCODE`.

Хост сначала вызывает `ProcessBegin`, затем ровно один раз `Register`.
Регистрация — единственное место, где можно добавить опции, наблюдателей,
перехватчики и провайдеров; после этого граф фаз заморожен.

Состояние извлекается внутри обратного вызова, а не захватывается заранее:

```c
Core->GetSessionState(Core->Context, Frame->Session, PluginID, &SessionState);
Core->GetTaskState(Core->Context, Frame->Task, PluginID, &TaskState);
```

## Фазы

Фаза — это именованный версионированный переход от входного артефакта к
выходному. NeverC поставляет **130 встроенных фаз**, плюс 8 семейств
идентификаторов расширения, зарезервированных для фаз, определяемых
плагинами:

| Домен | Фазы | Домен | Фазы |
|---|--:|---|--:|
| `driver` | 6 | `mir` | 10 |
| `source` | 3 | `codegen` | 4 |
| `prep` | 6 | `mc` | 13 |
| `syntax` | 7 | `assembly` | 4 |
| `sema` | 7 | `object` | 8 |
| `ir` | 8 | `link` | 20 |
| | | `dyncode` | 34 |

Все 130 имеют уровень стабильности `stable` в мажорной версии ABI 1. Каждая
фаза объявляет политику, и плагин может подключиться только теми способами,
которые эта политика разрешает:

| Флаг политики | Фазы | Что может плагин |
|---|--:|---|
| `NEVERC_PHASE_OBSERVABLE` | 130 | Зарегистрировать наблюдателя для уведомлений только на чтение. |
| `NEVERC_PHASE_INTERCEPTABLE` | 105 | Обернуть фазу и решить, вызывать ли остальную часть цепочки. |
| `NEVERC_PHASE_REPLACEABLE` | 86 | Зарегистрировать провайдера, который сам поставляет выход. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 13 | Пропустить переход, предоставив дескриптор доказательства. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 14 | Ничего. Верификаторы и фиксации принадлежат хосту. |

14 запечатанных шлюзов: `ir.final_verify`, `mir.final_verify`,
`codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
`object.final_verify`, `object.commit`, `link.image_verify`,
`link.side_outputs_verify`, `link.commit`, `dyncode.ir.final_verify`,
`dyncode.mir.final_verify`, `dyncode.verify` и `dyncode.commit`. За ними можно
наблюдать, но их нельзя перехватить, заменить или пропустить.

Наблюдатели получают уведомления в точках, объявленных фазой:
`NEVERC_OBSERVER_BEFORE`, `NEVERC_OBSERVER_AFTER` и
`NEVERC_OBSERVER_AFTER_COMMIT`. Перехватчик получает
`NevercPhaseContinuation` и обязан вызвать `InvokeNext` **не более одного
раза**, в потоке обратного вызова, а затем сообщить
`NEVERC_PHASE_CONTINUE`, `NEVERC_PHASE_REPLACE` или `NEVERC_PHASE_SKIP` в
`NevercPhaseResult.Action`.

Каждый обратный вызов фазы получает один и тот же кадр:

```c
typedef struct NevercPhaseFrame {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercPhaseRoute Route;        /* triple, CPU, features, object format */
  NevercArtifactHandle Input;
  NevercArtifactHandle CurrentOutput;
  NevercHandle Cancellation;
} NevercPhaseFrame;
```

`Schema/PhaseSchema.json` — нормативный источник идентификаторов фаз, политик,
уровней стабильности и шлюзов верификации. Сгенерированный
`Schema/PluginPhaseSchema.inc` открывает каждый из них как константу времени
компиляции — для фазы `neverc.ir.pass.pipeline_start`:

```c
NEVERC_PHASE_IR_PASS_PIPELINE_START_NAME       /* "neverc.ir.pass.pipeline_start" */
NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH       /* UINT64_C(0x4e43504849520001)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW        /* UINT64_C(0x0000000000000004)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_POLICY     /* OBSERVABLE | INTERCEPTABLE       */
NEVERC_PHASE_IR_PASS_PIPELINE_START_STABILITY
NEVERC_PHASE_IR_PASS_PIPELINE_START_INPUT_HIGH /* and _INPUT_LOW, _OUTPUT_*        */
```

Константы `NEVERC_BUILTIN_PHASE_COUNT` и подоменные
`NEVERC_BUILTIN_<DOMAIN>_PHASE_COUNT` позволяют плагину утверждать тот граф, с
которым он собирался.

## Полный минимальный плагин

Это дословно `pluginsdk/templates/minimal/Plugin.c`. Он загружается,
согласует ABI, ничего не регистрирует и чисто выгружается — скопируйте
каталог и растите плагин отсюда.

```c
#include "neverc/Plugin/NevercPluginAPI.h"

#define MINIMAL_PLUGIN_ID "com.example.minimal"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void copy_bytes(void *Destination, const void *Source, uint64_t Count) {
  uint64_t Index;
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  for (Index = 0; Index != Count; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)RegistrarContext;
  (void)ProcessState;
  if (Registrar == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  /* Register options, observers, interceptors, or providers here. */
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor = {0};
  uint32_t Capacity;
  uint64_t BytesToWrite;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = (NevercStringView)STRING_VIEW_LITERAL(MINIMAL_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW_LITERAL("Minimal Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
```

`OutPlugin` — это буфер, принадлежащий вызывающей стороне. На входе его
`Header.StructSize` — это доступная для записи ёмкость; плагин пишет не больше
этого числа байт и сообщает размер, который фактически произвёл. Если сначала
записать собственный `Header` дескриптора, а затем усечь копию, обе половины
этого правила выполняются одновременно.

## Согласование интерфейсов

Таблицы возможностей запрашиваются по 128-битному идентификатору интерфейса, а
не по символу. Запрашивайте мажорную версию, с которой вы собирались, и
наименьшую минорную, с которой можете работать:

```c
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &TableSize);
if (Status.Code != NEVERC_STATUS_OK)
  return Status;
if (!Table || TableSize < offsetof(NevercIRPassAPI, RegisterPass) +
                              sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

Сверка `TableSize` со смещением последней вызываемой вами функции — это то
правило, которое делает ABI расширяемым: более новый хост дописывает поля в
конец, а более старый плагин продолжает работать, потому что он никогда не
читает за пределами проверенного им префикса. Макрос
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` применяет ту же проверку к
полученной вами структуре. Такая же сигнатура `QueryInterface` есть и в
`NevercCoreAPI`, так что согласовывать можно позже, а не на входе.

Публичные интерфейсы, их таблицы и макросы идентификаторов:

| Пара макросов интерфейса | Таблица | Заголовок |
|---|---|---|
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO_*`, `..._SOURCE_LOCATION_*` | `NevercIOAPI`, `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST_*`, `..._PARSER_*` | `NevercASTAPI`, `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE_*`, `..._IR_BUILDER_*`, `..._IR_ANALYSIS_*`, `..._IR_PASS_*`, `..._IR_GEN_*`, `..._IR_OPTIMIZATION_*` | шесть таблиц IR | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET_*`, `..._TARGET_ABI_*`, `..._CALLING_CONVENTION_*` | `NevercTargetAPI`, `NevercTargetABIAPI`, `NevercCallingConventionAPI` | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR_*`, `..._MIR_ANALYSIS_*`, `..._MIR_PASS_*`, `..._MIR_PROVIDER_*` | четыре таблицы MIR | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC_*`, `..._MC_EMISSION_*`, `..._MC_PROVIDER_*`, `..._ASSEMBLY_PROVIDER_*` | четыре таблицы MC | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT_*`, `..._OBJECT_FORMAT_*`, `..._OBJECT_PHASE_*` | три объектные таблицы | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK_*`, `..._LINK_REGISTRAR_*`, `..._LINK_PHASE_*` | три таблицы компоновки | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO_*`, `..._LTO_REGISTRAR_*` | `NevercLTOAPI`, `NevercLTORegistrarAPI` | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE_*`, `..._DYNCODE_REGISTRAR_*`, `..._DYNCODE_PHASE_*` | три таблицы dyncode | `PluginDynCode.h` |

Каждый заголовок также определяет соответствующие
`NEVERC_<DOMAIN>_API_MAJOR` и `_MINOR`, которые следует передавать в
`QueryInterface`.

Интерфейс бывает либо `NEVERC_INTERFACE_STABLE` (более новый хост может только
дописывать), либо `NEVERC_INTERFACE_LOCKSTEP` (схемы, специфичные для целевой
платформы, которые должны совпадать точно). Сверяйте дайджест схемы, прежде
чем использовать значения LOCKSTEP.

## Регистрация

`Register` получает `NevercRegistrarAPI` и непрозрачный `RegistrarContext`:

```c
typedef struct NevercRegistrarAPI {
  NevercABITableHeader Header;
  NevercRegisterInterfaceFn RegisterInterface;
  NevercRegisterPhaseFn RegisterPhase;
  NevercRegisterObserverFn RegisterObserver;
  NevercRegisterInterceptorFn RegisterInterceptor;
  NevercRegisterProviderFn RegisterProvider;
  NevercRegisterOptionFn RegisterOption;
} NevercRegistrarAPI;
```

Регистраторы доменов — `NevercIRPassAPI.RegisterPass`,
`NevercTargetAPI.RegisterTarget`, `NevercObjectFormatAPI.RegisterFormat` и
остальные — принимают тот же `RegistrarContext` вторым аргументом; именно так
хост относит регистрацию к вашему плагину.

Провайдер дополнительно объявляет свой контракт детерминизма, на который
опирается кеш сборки:

```c
Provider.ProviderID    = SV("com.example.my-lowering");
Provider.Route         = /* triple / CPU / features / object format */;
Provider.Deterministic = NEVERC_TRUE;
Provider.Cacheable     = NEVERC_TRUE;
Provider.FallbackSafe  = NEVERC_FALSE;  /* built-in cannot silently take over */
```

## Сборка

Подключите сводный заголовок или только те домены, которые используете:

```c
#include "neverc/Plugin/NevercPluginAPI.h"   /* everything */
#include "neverc/Plugin/PluginIR.h"          /* or one domain */
```

Собрать разделяемый модуль самим NeverC:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

Или против установленного SDK с помощью CMake:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

Или с помощью pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

Используйте `.so`, `.dylib` или `.dll` в зависимости от хоста. SDK не
компонуется ни с LLVM, ни со средой выполнения NeverC —
`NevercPluginSDK::headers` состоит только из заголовков.

## Загрузка и настройка

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| Опция | Форма | Назначение |
|---|---|---|
| `-fplugin=<path>` | повторяемая | Загрузить разделяемый модуль плагина для всего тулчейна. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | повторяемая | Передать значение с пространством имён в зарегистрированную опцию плагина. |
| `-fplugin-provider=<phase>:<plugin-id>` | повторяемая | Выбрать, какой плагин обеспечивает заменяемую фазу. |
| `-fplugin-pass=<dsopath>` | повторяемая | Загрузить внешний плагин прохода с C-ABI. |
| `-fplugin-pass-arg=<key>=<value>` | повторяемая | Передать аргумент плагинам проходов с C-ABI. |

Квалификатор `<plugin-id>:` можно опустить, только если активен ровно один
плагин. Опции, которые плагин регистрирует через `RegisterOption`, также
принимаются напрямую в объявленном написании — в виде флага, слитной,
раздельной или многоаргументной формы. Аргументы плагина и выбор провайдера
без соответствующего `-fplugin=` — это жёсткая ошибка, а не молчаливое
бездействие.

Зарегистрированную опцию можно в любой момент прочитать через таблицу core:

```c
uint64_t Count = 0;
Core->GetPluginOptionValueCount(Core->Context, Session, PluginID,
                                SV("--driver-trace"), &Count);
NevercStringView Value;
Core->GetPluginOptionValue(Core->Context, Session, PluginID,
                           SV("--driver-trace"), 0, &Value);
```

## Правила ABI

- Запрашивайте таблицы возможностей через `QueryInterface`; требуйте
  совпадения мажорной версии и проверяйте `StructSize` до обращения к полю.
- Инициализируйте `Header` и зарезервированную память каждой публичной
  структуры. Обнулите структуру, затем задайте `StructSize`, `Major`, `Minor`
  и `Flags`.
- Считайте дескрипторы и заимствованные представления непрозрачными
  значениями с областью действия. Никогда не сохраняйте дескриптор области
  задачи после её обратного вызова, не используйте его в другой сессии или
  задаче и никогда не изготавливайте значение дескриптора сами.
- Возвращайте `NevercStatus` из каждого обратного вызова. Не позволяйте
  исключению C++ или принадлежащему хосту указателю пересечь границу C.
- Объявляйте самую узкую правдивую `NevercConcurrencyModel`
  (`SESSION_SERIAL`, `THREAD_SAFE`, `PROCESS_SERIAL`) и
  `NevercReentrancyModel` (`NONE`, `ALLOWED`).
- Выполняйте изменения IR, MIR, AST, графов и артефактов через транзакционные
  API хоста: начните изменение, подготовьте правки, затем зафиксируйте или
  отмените. Фиксация атомарно проверяет и публикует; неудавшаяся фиксация
  оставляет прежнее состояние нетронутым.
- Выделяйте память через `NevercCoreAPI.Allocate` / `Reallocate` /
  `Deallocate`, когда хост должен её учитывать.
- Держите изменяемое состояние в предоставленном хостом состоянии
  process/session/task. Глобальное изменяемое состояние проверяется скриптом
  `utils/plugin-api/check-global-state.py`.

Все публичные структуры размещаются под `NEVERC_ABI_PACK_BEGIN` (упаковка по 8
байт) и используют только типы фиксированной ширины. Новые функции
дописываются в конец независимо версионируемых таблиц возможностей; стабильный
префикс таблицы не меняется в пределах первой мажорной версии ABI
(`NEVERC_PLUGIN_ABI_MAJOR` = 1).

## Статусы и диагностика

`NevercStatus` несёт `Code`, `Flags` и слово `Detail`. Полный набор кодов:

| Код | Значение |
|---|---|
| `NEVERC_STATUS_OK` | Успех. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | Обязательный указатель или значение отсутствовали либо были некорректны. |
| `NEVERC_STATUS_ABI_MISMATCH` | Согласованная таблица слишком мала или мажорная версия отличается. |
| `NEVERC_STATUS_MISSING_INTERFACE` | Хост не публикует запрошенный интерфейс. |
| `NEVERC_STATUS_VERSION_MISMATCH` | Запрошенную мажорную/минорную версию невозможно удовлетворить. |
| `NEVERC_STATUS_INVALID_DESCRIPTOR` | Дескриптор не прошёл структурную проверку. |
| `NEVERC_STATUS_DUPLICATE_ID` | Такой идентификатор уже зарегистрирован. |
| `NEVERC_STATUS_DEPENDENCY_MISSING` | Объявленная зависимость отсутствует. |
| `NEVERC_STATUS_DEPENDENCY_CYCLE` | Порядок регистрации невозможно удовлетворить. |
| `NEVERC_STATUS_BUSY` | Ресурс удерживается в другом месте. |
| `NEVERC_STATUS_CANCELLED` | Запрошена кооперативная отмена. |
| `NEVERC_STATUS_RESOURCE_EXHAUSTED` | Достигнут бюджет или предел. |
| `NEVERC_STATUS_STALE_HANDLE` | Дескриптор пережил объект, который он называл. |
| `NEVERC_STATUS_WRONG_SESSION` | Дескриптор использован в другой сессии. |
| `NEVERC_STATUS_WRONG_SCOPE` | Дескриптор использован вне своей области. |
| `NEVERC_STATUS_WRONG_TYPE` | Дескриптор называл сущность другого вида. |
| `NEVERC_STATUS_INVALID_STATE` | Операция недопустима в текущем состоянии. |
| `NEVERC_STATUS_POLICY_VIOLATION` | Политика фазы запрещает операцию. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | Запечатанный верификатор хоста отклонил продукт. |
| `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` | Хост не может предоставить эту возможность здесь. |
| `NEVERC_STATUS_PLUGIN_FAILURE` | Плагин сообщил об общей ошибке. |
| `NEVERC_STATUS_PLUGIN_EXCEPTION` | Из обратного вызова плагина вырвалось исключение. |
| `NEVERC_STATUS_OUTPUT_PARTIAL` | Вывод записан лишь частично. |
| `NEVERC_STATUS_REENTRANCY_DENIED` | Реентерабельный вызов отклонён. |
| `NEVERC_STATUS_NOT_FOUND` | Названная сущность не существует. |

Биты флагов описывают, что произошло с выводом, — именно это нужно системе
сборки, чтобы решить, безопасен ли повтор:
`NEVERC_STATUS_FLAG_RECOVERABLE`, `_OUTPUT_ALREADY_COMMITTED`,
`_OUTPUT_MAY_BE_PARTIAL`, `_OUTPUT_RECOVERY_REQUIRED` и
`_DURABILITY_UNCONFIRMED`.

Сообщайте о проблемах через `NevercCoreAPI.EmitDiagnostic` и
`NevercDiagnosticDescriptor`, несущий уровень серьёзности (`NOTE`, `REMARK`,
`WARNING`, `ERROR`, `FATAL`), код, идентификатор плагина, идентификатор фазы,
сообщение, примечания, позицию в исходнике, диапазоны и исправления. Перед
дорогой работой вызывайте `CheckCancelled`.

## Примеры

Собрать все:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

Каждый пример компилируется дважды — один раз настроенным хостовым
компилятором C и один раз только что собранным NeverC, — так что ABI
подтверждается с обеих сторон. Модули появляются в
`build-neverc/neverc/pluginsdk/examples/host/`.

| Пример | Цель CMake | Что показывает |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | Регистрация опций, наблюдение за фазами, перехват заданий |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | Провайдер VFS, отдающий заголовок из памяти |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | Перехват парсера и атомарное изменение AST |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | Проход IR уровня модуля, обходящий список функций курсором значений |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | Стабильный функциональный проход IR |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | Стабильный проход MIR на хуке pre-emit |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | События выдачи MC только на чтение |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | Транзакционная перезапись ObjectGraph |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | Соглашения о вызовах, управляемые данными |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | Наблюдение за конвейером dyncode |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | Перехват кодирования набора символов dyncode |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | Плагин с нулевой зависимостью от CRT |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | Микробенчмарк пропускной способности вызовов ABI |

Загрузить один из них:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## Нормативные источники

| Файл | Что гарантирует |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | Идентификаторы фаз, политики, стабильность, шлюзы верификации |
| `pluginsdk/manifest/plugin.json` | Версия ABI, идентификаторы/версии/стабильность интерфейсов, дайджесты схем, поддерживаемые целевые платформы |
| `pluginsdk/abi/plugin.json` | Измеренные размер, выравнивание и смещения полей каждой публичной структуры для каждого ключа ABI хоста |
| `docs/plugin-api/coverage.json` | Сопоставляет каждую стабильную фазу с положительными, отрицательными, замещающими, наблюдательными тестами и тестами запечатанных шлюзов |

Благодаря этому SDK можно механически проверить относительно хоста, а сборка
плагина может утверждать раскладку своих структур относительно того ключа ABI,
в который она будет загружена.
