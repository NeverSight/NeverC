**Языки**: [English](migration-from-prototype.md) | [简体中文](migration-from-prototype.zh-CN.md) | [繁體中文](migration-from-prototype.zh-TW.md) | [日本語](migration-from-prototype.ja.md) | [한국어](migration-from-prototype.ko.md) | [Français](migration-from-prototype.fr.md) | [Deutsch](migration-from-prototype.de.md) | [Español](migration-from-prototype.es.md) | [Italiano](migration-from-prototype.it.md) | [Русский](migration-from-prototype.ru.md) | [العربية](migration-from-prototype.ar.md)

# Переход с прототипного API плагинов

Невыпущенный прототипный API плагинов — его точка входа `nevercGetPluginInfo`,
единственная таблица виртуальных функций `NevercHostAPI`, вызовы
`Register*Pass`, перехватчики `NEVERC_INTERPOSE_*` и загрузчик
`-fplugin-pass=` — был удалён до первого публичного выпуска. Первый публичный
ABI — это дескрипторный ABI на основе фаз, описанный в
[README.md](README.md): плагины экспортируют `neverc_plugin_entry` и
согласовывают таблицы возможностей с независимыми версиями.

Прослойки совместимости нет, разделения на `v1`/`v2` тоже. Перекомпилируйте
*исходный код* плагина против публичных заголовков; эта страница сопоставляет
каждую конструкцию прототипа с её заменой в первой версии, с семантическим
изменением или с явным отказом от переноса.

## Прототипные двоичные модули отклоняются

Загрузка прототипного разделяемого объекта завершается неудачей со стабильной
диагностикой:

```
plugin exports the removed 'nevercGetPluginInfo' prototype ABI; migrate it to
the first public descriptor ABI and export 'neverc_plugin_entry'
```

Библиотека, не экспортирующая ни одну из точек входа, завершается с ошибкой
`plugin has no 'neverc_plugin_entry' entry`. Пока исходный код не перенесён,
ничего не загружается.

## Точка входа

| Прототип | Первый публичный ABI |
|---|---|
| `NevercPluginInfo nevercGetPluginInfo(void)` | `NevercStatus NEVERC_CALL neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin)` |

Точка входа больше не *возвращает* структуру по значению. Она заполняет
предоставленный вызывающей стороной `NevercPluginDescriptor`, соблюдая
`OutPlugin->Header.StructSize`, и возвращает `NevercStatus`. Запросите нужные
таблицы возможностей у `Bootstrap`, прежде чем объявлять их поддержку.

## Поля `NevercPluginInfo`

| Поле прототипа | Соответствие в первой версии |
|---|---|
| `APIVersion` | `Descriptor.Header` (`NevercABITableHeader` с `StructSize`, `NEVERC_PLUGIN_ABI_MAJOR`, `NEVERC_PLUGIN_ABI_MINOR`) |
| `PluginName` | `Descriptor.DisplayName` (`NevercStringView`), а также стабильный `Descriptor.PluginID` в обратной DNS-нотации, служащий ключом состояния каждой области видимости |
| `PluginVersion` | `Descriptor.Version` (`NevercSemanticVersion`) |
| `RegisterPasses(API, Reg)` | `Descriptor.Register(Core, Registrar, RegistrarContext, ProcessState)`, а также обратные вызовы жизненного цикла `ProcessBegin`, `SessionBegin`/`SessionEnd`, `TaskBegin`/`TaskEnd` |
| `Destroy()` | `Descriptor.Destroy(Core, ProcessState)` |
| *(в прототипе аналога нет)* | `Descriptor.Concurrency` и `Descriptor.Reentrancy` должны объявляться правдиво (например, `NEVERC_CONCURRENCY_SESSION_SERIAL`, `NEVERC_REENTRANCY_ALLOWED`) |

## Доступ к хосту: одна vtable → таблицы возможностей

Прототип передавал каждому обратному вызову одну таблицу `NevercHostAPI` из
более чем 200 записей и защищал новые поля макросом `NEVERC_API_FN`. Первая
версия заменяет её независимо версионируемыми таблицами возможностей, которые
запрашиваются по необходимости:

```c
NevercInterfaceID Driver = { NEVERC_INTERFACE_DRIVER_HIGH,
                             NEVERC_INTERFACE_DRIVER_LOW };
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus S = Bootstrap->QueryInterface(
    Bootstrap->Context, Driver, NEVERC_DRIVER_API_MAJOR,
    NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
```

Требуйте совпадения старшей версии и проверяйте `TableSize` через `offsetof`
перед чтением поля. Интерфейсы разграничены по доменам: Core, Driver, Source,
Prep, AST, Sema, IR, MIR, Target, MC, Object, Link, LTO и DynCode.

## Регистрация: `Register*Pass` + перехватчики → наблюдатели/интерцепторы/поставщики

Регистрация в прототипе привязывала обратный вызов к точке перехвата:

```c
API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, ud, "my-pass");
```

Первая версия регистрирует внутри `Register` типизированный обработчик на
фазе, определяемой 128-битным `NevercInterfaceID`:

| Вызов прототипа | Вызов регистратора в первой версии |
|---|---|
| проход только для чтения | `Registrar->RegisterObserver(NevercObserverDescriptor)` с точками `NEVERC_OBSERVER_BEFORE`/`NEVERC_OBSERVER_AFTER` |
| проход, оборачивающий или замыкающий фазу | `Registrar->RegisterInterceptor(NevercInterceptorDescriptor)`; вызывайте `Continuation->InvokeNext` не более одного раза и задайте `OutResult->Action` |
| проход, заменяющий встроенное преобразование | `Registrar->RegisterProvider(...)` на фазе с политикой `REPLACEABLE` |
| чтение `-fplugin-pass-arg=` | `Registrar->RegisterOption(NevercOptionDescriptor)` для объявления настоящей опции драйвера |

Прототипный «модульный проход на `PRE_OPT`» становится наблюдателем,
интерцептором или поставщиком на фазе IR `neverc.ir.pass.pre_opt`.

## Соответствие перехватчиков и фаз

| Перехватчик прототипа | Фаза первой версии (имя) |
|---|---|
| `NEVERC_INTERPOSE_PRE_OPT` | `neverc.ir.pass.pre_opt` |
| `NEVERC_INTERPOSE_POST_OPT` | `neverc.ir.pass.post_opt` |
| `NEVERC_INTERPOSE_PIPELINE_START` | `neverc.ir.pass.pipeline_start` |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | `neverc.ir.pass.optimizer_last` |
| `NEVERC_INTERPOSE_BEFORE_CODEGEN_PREEMIT` | `neverc.mir.pass.preemit` |
| `NEVERC_INTERPOSE_AFTER_CODEGEN_FINAL_MIR` | `neverc.mir.pass.final` |
| `NEVERC_INTERPOSE_LTO_PRE_OPT` / `LTO_POST_OPT` | фазы LTO `neverc.link.lto_resolve` / `neverc.link.lto_generate` (см. [mir.md](mir.md)) |
| `NEVERC_INTERPOSE_LINK_PRE_LAYOUT` / `LINK_POST_LAYOUT` | `neverc.link.layout`, наблюдаемая в точках `BEFORE` / `AFTER` |
| `NEVERC_INTERPOSE_LINK_POST_EMIT` | `neverc.link.post_emit` |
| `NEVERC_INTERPOSE_SC_*` (dyncode) | типизированные фазы dyncode из [dyncode.md](dyncode.md) |

Нормативный перечень идентификаторов фаз, политик, уровней стабильности и
верификационных шлюзов — файл
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`; исполняемый контракт
покрытия — [coverage.json](coverage.json). Перехватчик, бывший когда-то одной
точкой, может соответствовать нескольким идентификаторам фаз, у каждого из
которых своя политика и своё доказательство.

## Обратные вызовы проходов, дескрипторы и правка байтов

| Прототип | Первая версия |
|---|---|
| `NevercModulePassFn(NevercModuleRef, API, ud)` и подобные | обратные вызовы получают `NevercPhaseFrame`; объекты IR/MIR/AST/графа — это типизированные непрозрачные дескрипторы с ограниченной областью действия, получаемые из соответствующей таблицы возможностей (см. [ir.md](ir.md), [mir.md](mir.md), [ast-sema.md](ast-sema.md), [target-mc-object.md](target-mc-object.md)) |
| обобщённый `NevercValueRef` | удалён в пользу типизированных дескрипторов IR |
| изменение живого `Ref` на месте | все изменения проходят через транзакционные API хоста |
| `NevercBinaryPassFn(uint8_t **Data, uint64_t *Len, ...)` | удалён; правка байтов dyncode выполняется проверяемым построителем образа (read/write/insert/append/resize), см. [dyncode.md](dyncode.md) |

Дескрипторы и заимствованные представления действительны только в пределах
обратного вызова, ровно как и раньше; не кэшируйте их после его возврата.

## Удалённые слои удобства

Прототип встраивал в vtable вспомогательные средства общего назначения. Они
**не** входят в первый публичный ABI:

| Прототип | Первая версия |
|---|---|
| `ArenaCreate` / `StrMapCreate` / `IntMapCreate` / `StrBuilderCreate` / `ValueSetCreate` | не перенесены; используйте `Core->Allocate`/`Core->Deallocate` со своими контейнерами либо типизированные доменные API |
| макросы `NEVERC_FOR_EACH_*` / `NEVERC_COLLECT_*` | заменены типизированным обходом в таблице возможностей каждого домена |
| `API->PluginGetArg` / `-fplugin-pass-arg=` | объявляйте опции через `RegisterOption` и читайте их через Driver API |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | `Core->EmitDiagnostic(NevercDiagnosticDescriptor)` |

## Загрузка и командная строка

| Прототип | Первая версия |
|---|---|
| `-fplugin-pass=<path>` | `-fplugin=<path>` |
| `-fplugin-pass-arg=key=value` | написание опции, объявленное вами в `RegisterOption` (например, `--driver-trace` или `--my-opt=value`) |
| два загрузчика (`-fplugin` и `-fplugin-pass`) | один загрузчик; модуль передаётся единственному загрузчику |

## Версионирование

Прототип полагался на единственную монотонно растущую vtable и охранные
макросы `NEVERC_API_FN`. В первой версии каждая таблица возможностей
версионируется отдельно: требуйте совпадения старшей версии и проверяйте
`StructSize`/`TableSize` перед чтением добавленного поля. В пределах первой
старшей версии ABI новые функции дописываются после стабильного префикса
таблицы, поэтому плагин, собранный против более ранней младшей версии,
продолжает работать с более новым хостом.

## Разобранный пример

`pluginsdk/examples/DriverTracePlugin.c` показывает полную форму первой
версии: дескриптор `neverc_plugin_entry`, жизненный цикл
`ProcessBegin`/`Session`/`Task`, `RegisterOption` для настоящего флага
командной строки, `RegisterObserver` на `neverc.driver.raw_arguments` и
`RegisterInterceptor` на `neverc.driver.execute_job`, вызывающий `InvokeNext`
ровно один раз. `pluginsdk/examples/ExamplePlugin.c` покрывает фазы IR, MIR,
object и link.
