**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# ABI плагинов NeverC

Первый публичный ABI плагинов NeverC — это интерфейс на чистом C, построенный
вокруг фаз. Плагин — это разделяемый модуль, который экспортирует ровно одну
функцию, согласовывает версионированные таблицы возможностей и работает внутри
явных областей Process, Session и Task. Он не подключает заголовки LLVM, не
компонуется с компилятором и не передаёт типы C++ через границу.

Невыпущенный прототипный API и его точка входа `nevercGetPluginInfo`
**удалены**. Прототипные бинарники отклоняются с диагностикой миграции;
пересоберите их исходники с публичными заголовками. Полное соответствие
«старое → новое» см. в
[Миграции с прототипного API](migration-from-prototype.ru.md).

## Начните отсюда

- [API Source и ввода-вывода](source.ru.md)
- [API препроцессора](prep.ru.md)
- [API AST и семантики](ast-sema.ru.md)
- [API IR](ir.ru.md)
- [API MIR](mir.ru.md)
- [API Target, MC, ассемблера и объектных файлов](target-mc-object.ru.md)
- [API DynCode](dyncode.ru.md)
- [Пользовательские соглашения о вызовах](custom-callconv/README.ru.md)
- [Миграция с прототипного API](migration-from-prototype.ru.md)
- [Доказательства покрытия фаз](coverage.json)

## Модель исполнения

Хост управляет плагином через три вложенные области. Каждая область передаёт
плагину непрозрачный указатель состояния, который плагин выделяет и которым
владеет сам, поэтому правильно написанному плагину не нужно никакое глобальное
изменяемое состояние.

| Область | Обратные вызовы | Смысл |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | Один процесс компилятора. Здесь запрашивают интерфейсы и регистрируют возможности. |
| Session | `SessionBegin`, `SessionEnd` | Один вызов драйвера. |
| Task | `TaskBegin`, `TaskEnd` | Одна единица работы, определяемая `NevercTaskKind`. |

Виды задач: `INVOCATION`, `TRANSLATION_UNIT`, `LTO`, `LINK`, `CODEGEN`,
`OBJECT` и `DYNCODE`.

Хост сначала вызывает `ProcessBegin`, затем ровно один раз `Register`.
Регистрация — единственное место, где можно добавить опции, наблюдателей,
перехватчики и провайдеры; после неё граф фаз замораживается.

## Фазы

Фаза — это именованный версионированный переход от входного артефакта к
выходному. NeverC содержит **130 встроенных фаз** в доменах драйвера, source,
препроцессора, синтаксиса, семантики, IR, codegen, MIR, MC, ассемблера,
объектных файлов, компоновки и dyncode, плюс 8 семейств расширяемых ID,
зарезервированных для фаз, определяемых плагинами.

Каждая фаза объявляет политику, и плагин может подключиться только так, как эта
политика разрешает:

| Флаг политики | Что может плагин |
|---|---|
| `NEVERC_PHASE_OBSERVABLE` | Зарегистрировать наблюдателя для уведомления только на чтение. |
| `NEVERC_PHASE_INTERCEPTABLE` | Обернуть фазу и решить, вызывать ли остаток цепочки. |
| `NEVERC_PHASE_REPLACEABLE` | Зарегистрировать провайдер, который сам формирует вывод. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | Пропустить переход, предоставив handle доказательства. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | Ничего. Верификаторы и фиксации принадлежат хосту: их нельзя заменить, перехватить или пропустить. |

Наблюдатели доставляются в точках, объявленных фазой:
`NEVERC_OBSERVER_BEFORE`, `NEVERC_OBSERVER_AFTER` и
`NEVERC_OBSERVER_AFTER_COMMIT`.

Перехватчик получает `NevercPhaseContinuation`. Он обязан вызвать `InvokeNext`
**не более одного раза**, в потоке обратного вызова, и затем сообщить
`NEVERC_PHASE_CONTINUE`, `NEVERC_PHASE_REPLACE` или `NEVERC_PHASE_SKIP` в
`NevercPhaseResult.Action`.

Нормативный источник для ID фаз, политик, уровней стабильности и
верификационных шлюзов —
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`. Сгенерированный
`PluginPhaseSchema.inc` предоставляет их как константы времени компиляции вида
`NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH` / `_LOW`.

## Полный минимальный плагин

Это `pluginsdk/templates/minimal/Plugin.c`. Он загружается, согласовывает ABI,
ничего не регистрирует и корректно выгружается — скопируйте каталог и
развивайте его отсюда.

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
  /* Здесь регистрируются опции, наблюдатели, перехватчики или провайдеры. */
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
`Header.StructSize` — доступная для записи ёмкость; плагин записывает не больше
этого числа байт и сообщает размер, который фактически сформировал.

## Согласование интерфейсов

Таблицы возможностей запрашиваются по 128-битному идентификатору интерфейса, а
не по символу. Запрашивайте ту мажорную версию, с которой вы компилировались, и
минимальную минорную, с которой вы можете работать:

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

Сверка `TableSize` со смещением последней вызываемой функции — именно то
правило, которое делает этот ABI расширяемым: более новый хост дописывает поля
в конец, а более старый плагин продолжает работать, потому что никогда не
читает за пределами проверенного им префикса. Макрос
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` применяет ту же проверку к
полученной структуре.

Публичные интерфейсы и их заголовки:

| Интерфейс | Таблица | Заголовок |
|---|---|---|
| `NEVERC_INTERFACE_CORE` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO`, `..._SOURCE_LOCATION` | `NevercIOAPI`, `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST`, `..._PARSER` | `NevercASTAPI`, `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE`, `..._BUILDER`, `..._ANALYSIS`, `..._PASS`, `..._GEN`, `..._OPTIMIZATION` | Таблицы IR | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET`, `..._TARGET_ABI`, `..._CALLING_CONVENTION` | Таблицы Target | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR`, `..._MIR_ANALYSIS`, `..._MIR_PASS`, `..._MIR_PROVIDER` | Таблицы MIR | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC`, `..._MC_EMISSION`, `..._MC_PROVIDER`, `..._ASSEMBLY_PROVIDER` | Таблицы MC | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT`, `..._OBJECT_FORMAT`, `..._OBJECT_PHASE` | Таблицы Object | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK`, `..._LINK_REGISTRAR`, `..._LINK_PHASE` | Таблицы Link | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO`, `..._LTO_REGISTRAR` | Таблицы LTO | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE`, `..._DYNCODE_REGISTRAR`, `..._DYNCODE_PHASE` | Таблицы DynCode | `PluginDynCode.h` |

Интерфейс бывает либо STABLE (более новый хост может только дописывать), либо
LOCKSTEP (схемы, специфичные для целевой платформы, должны совпадать точно).
Сравнивайте дайджест схемы, прежде чем использовать значения LOCKSTEP.

## Сборка

Подключайте агрегирующий заголовок или только те домены, которые используете:

```c
#include "neverc/Plugin/NevercPluginAPI.h"
```

Собрать разделяемый модуль самим NeverC:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

Или через CMake против установленного SDK:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

Или через pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

Используйте `.so`, `.dylib` или `.dll` в зависимости от хоста. SDK не
компонуется ни с LLVM, ни с рантаймом NeverC — `NevercPluginSDK::headers`
состоит только из заголовков.

## Загрузка и настройка

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| Опция | Форма | Назначение |
|---|---|---|
| `-fplugin=<path>` | повторяемая | Загрузить разделяемый модуль плагина. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | повторяемая | Передать значение с пространством имён зарегистрированной опции плагина. |
| `-fplugin-provider=<phase>:<plugin-id>` | повторяемая | Выбрать, какой плагин предоставляет заменяемую фазу. |

Квалификатор `<plugin-id>:` можно опустить, только когда активен ровно один
плагин. Опции, зарегистрированные плагином через `RegisterOption`, также
принимаются напрямую в объявленном написании — в форме флага, слитной,
раздельной или многоаргументной. Аргументы плагина или выбор провайдера без
`-fplugin=` — это жёсткая ошибка, а не тихое бездействие.

## Правила ABI

- Запрашивайте таблицы через `QueryInterface`; требуйте совпадения мажорной
  версии и проверяйте `StructSize`, прежде чем обращаться к полю.
- Инициализируйте `Header` и зарезервированные области каждой публичной
  структуры. Обнулите структуру, затем задайте `StructSize`, `Major`, `Minor` и
  `Flags`.
- Считайте handle-ы и заимствованные представления непрозрачными значениями с
  областью действия. Никогда не сохраняйте handle области задачи после её
  обратного вызова, не используйте его в другой сессии или задаче и не
  конструируйте значение handle самостоятельно.
- Возвращайте `NevercStatus` из каждого обратного вызова. Не допускайте, чтобы
  исключение C++ или принадлежащий хосту указатель пересекли границу C.
- Объявляйте самые узкие **правдивые** `NevercConcurrencyModel`
  (`SESSION_SERIAL`, `THREAD_SAFE`, `PROCESS_SERIAL`) и
  `NevercReentrancyModel` (`NONE`, `ALLOWED`).
- Изменения IR, MIR, AST, графов и артефактов выполняйте через транзакционные
  API хоста: начать mutation, подготовить изменения, затем зафиксировать или
  прервать. Фиксация проверяет и публикует атомарно; неудачная фиксация
  оставляет прежнее состояние нетронутым.
- Держите изменяемое состояние в предоставленных хостом состояниях
  process/session/task. Глобальное изменяемое состояние проверяется скриптом
  `utils/plugin-api/check-global-state.py`.

Новые функции дописываются в конец независимо версионируемых таблиц
возможностей. Стабильный префикс таблицы не меняется в пределах первой мажорной
версии ABI (`NEVERC_PLUGIN_ABI_MAJOR` = 1).

## Статусы и диагностика

`NevercStatus` несёт `Code`, `Flags` и слово `Detail`. Частые коды:

| Код | Значение |
|---|---|
| `NEVERC_STATUS_OK` | Успех. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | Отсутствует или некорректен обязательный указатель либо значение. |
| `NEVERC_STATUS_ABI_MISMATCH` | Согласованная таблица слишком мала или отличается мажорная версия. |
| `NEVERC_STATUS_MISSING_INTERFACE` / `CAPABILITY_UNAVAILABLE` | Хост не предоставляет запрошенную возможность. |
| `NEVERC_STATUS_STALE_HANDLE` / `WRONG_SESSION` / `WRONG_SCOPE` / `WRONG_TYPE` | Handle использован вне области своей действительности. |
| `NEVERC_STATUS_POLICY_VIOLATION` | Политика фазы не разрешает эту операцию. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | Запечатанный верификатор хоста отклонил продукт. |
| `NEVERC_STATUS_CANCELLED` / `BUSY` / `RESOURCE_EXHAUSTED` | Кооперативная отмена или ограничения ресурсов. |

Биты флагов (`RECOVERABLE`, `OUTPUT_ALREADY_COMMITTED`,
`OUTPUT_MAY_BE_PARTIAL`, `OUTPUT_RECOVERY_REQUIRED`,
`DURABILITY_UNCONFIRMED`) описывают, что произошло с выводом, — именно это
нужно системе сборки, чтобы решить, безопасен ли повтор.

Сообщайте о проблемах через `NevercCoreAPI.EmitDiagnostic` и
`NevercDiagnosticDescriptor`, несущий уровень серьёзности, код, ID плагина, ID
фазы, сообщение, примечания, позицию в исходнике, диапазоны и fix-it. Перед
дорогостоящей работой вызывайте `CheckCancelled`.

## Примеры

Собрать все:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

Каждый пример компилируется дважды — настроенным хостовым компилятором C и
только что собранным NeverC, — так что ABI подтверждается с обеих сторон.
Модули появляются в `build-neverc/neverc/pluginsdk/examples/host/`.

| Пример | Цель CMake | Демонстрирует |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | Регистрация опций, наблюдение за фазами, перехват задания |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | VFS-провайдер, отдающий заголовок из памяти |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | Перехват парсера и атомарная мутация AST |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | IR-проход уровня модуля, обходящий список функций через курсор значений |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | Стабильный функциональный проход IR |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | Стабильный проход MIR на хуке pre-emit |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | События эмиссии MC только для чтения |
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
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | ID фаз, политики, стабильность, верификационные шлюзы |
| `pluginsdk/manifest/plugin.json` | Версия ABI, ID/версии/стабильность интерфейсов, дайджесты схем, поддерживаемые цели |
| `pluginsdk/abi/plugin.json` | Измеренные размер, выравнивание и смещения полей каждой публичной структуры по ключу ABI хоста |
| `docs/plugin-api/coverage.json` | Сопоставляет каждой стабильной фазе позитивные, негативные, замещающие, наблюдательные тесты и тесты запечатанных шлюзов |

Благодаря этому SDK можно машинно проверить на соответствие хосту, а сборка
плагина может утверждать раскладку своих структур относительно того ключа ABI, в
который она будет загружена.
