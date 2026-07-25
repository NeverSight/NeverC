**Языки**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

[← ABI плагинов NeverC](README.ru.md)

# API исходников и ввода-вывода плагинов NeverC

[`PluginSource.h`] публикует две таблицы. `NevercIOAPI` — это файловая система:
поставщики виртуальных файлов, чтение, обход каталогов, приёмники вывода и
записи о зависимостях. `NevercSourceLocationAPI` отображает внутренние позиции
компилятора обратно в файлы, строки и написанный текст. Вместе они позволяют
плагину отдать заголовок, существующий только в памяти, разрешить раскрытие
макроса до места его написания или записать побочный вывод, участвующий в учёте
устойчивости сборки.

## Интерфейсы

```c
#include "neverc/Plugin/PluginSource.h"
```

| Интерфейс | Таблица | Макросы версии |
|---|---|---|
| `NEVERC_INTERFACE_IO_{HIGH,LOW}` | `NevercIOAPI` | `NEVERC_IO_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SOURCE_LOCATION_{HIGH,LOW}` | `NevercSourceLocationAPI` | `NEVERC_SOURCE_LOCATION_API_MAJOR` / `_MINOR` |

`NEVERC_SOURCE_API_MAJOR` и `_MINOR` — псевдонимы пары source-location.

## Три фазы исходников

| Фаза | Политика | Смысл |
|---|---|---|
| `neverc.source.resolve_input` | OBSERVABLE, INTERCEPTABLE | Превратить вход драйвера во вход исходника |
| `neverc.source.open` | плюс REPLACEABLE | Произвести исходную единицу для входа |
| `neverc.source.after_open` | OBSERVABLE | Уведомление, что единица доступна |

Поскольку `neverc.source.open` заменяема, поставщик может вернуть единицу, байты
которой он синтезировал сам, — это поддерживаемый способ внедрить сгенерированный
код, не трогая диск.

## Поставщики виртуальной файловой системы

Поставщик VFS забирает себе префикс пути и отвечает на четыре вопроса, которые
компилятор задаёт о файле.

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

Каждый обратный вызов заполняет результат, поле `Disposition` которого говорит,
обработал ли поставщик запрос:

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

Возврат `NEVERC_VFS_RESULT_NOT_HANDLED` передаёт запрос следующему поставщику и
в итоге настоящей файловой системе. Типы файлов:
`NEVERC_VFS_FILE_UNKNOWN`, `REGULAR`, `DIRECTORY`, `SYMLINK` и `OTHER`.

Регистрация выполняется внутри `Register`:

```c
IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Descriptor);
```

Для одиночного файла в памяти, который нужен лишь на одну сессию, поставщик
можно вообще не заводить:

```c
IO->AddMemoryFile(IO->Context, Session, SV("/virtual/config.h"),
                  Content, ModificationTime);
```

[`pluginsdk/examples/VirtualHeaderPlugin.c`] —
полноценный рабочий поставщик.

## Чтение файлов

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

`CopyBuffer` превращает принадлежащие вам байты в буфер хоста, `Canonicalize`
разрешает путь, а `GetWorkingDirectory` / `SetWorkingDirectory` управляют
текущим каталогом задачи. Каталоги обходятся с помощью `OpenDirectory`,
`ReadDirectory` (в конце он устанавливает `OutHasEntry` в `NEVERC_FALSE`) и
`CloseDirectory`.

Коды ошибок ввода-вывода сообщаются в `NevercStatus.Detail`:
`NEVERC_IO_ERROR_NOT_FOUND`, `PERMISSION_DENIED`, `NOT_DIRECTORY`,
`IS_DIRECTORY`, `INVALID_PATH` и `IO`.

## Запись вывода

Вывод транзакционен. Вы открываете приёмник, пишете, затем завершаете и получаете
печать — размер и 32-байтовый дайджест, который система сборки может проверить.

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

| Функция | Назначение |
|---|---|
| `BeginMemoryOutput` | Приёмник в памяти с логическим именем |
| `BeginFileOutput` | Приёмник, атомарно приземляющийся по конечному пути |
| `BeginStreamOutput` | Приёмник на `NEVERC_OUTPUT_STREAM_STDOUT` или `_STDERR` |
| `OutputWrite`, `OutputWriteAt` | Дописать или записать по смещению |
| `OutputTell`, `OutputTruncate` | Управление позицией и размером |
| `OutputMetadataSet` | Прикрепить к выводу пару ключ/значение |
| `OutputFinish` | Запечатать вывод и получить `NevercOutputSeal` |
| `OutputAbort` | Отбросить всё записанное |
| `OutputGetSummary` | В любой момент осмотреть состояние, флаги, размер, дайджест |

`NevercOutputSummary.State` проходит через `NEVERC_OUTPUT_OPEN`, `FINISHED`,
`COMMITTED`, `ABORTED` или `FAILED_PARTIAL`, а `Flags` фиксирует `PUBLISHED`,
`DURABLE`, `MAY_BE_PARTIAL`, `RECOVERY_REQUIRED` и `DURABILITY_UNCONFIRMED`. Эти
флаги несут ту же информацию, которую драйвер выставляет в `NevercStatus.Flags`,
поэтому сбой посреди записи отличим от чистой неудачи.

Нулевой `SizeBudget` означает отсутствие ограничения; ненулевой бюджет заставит
превышение завершиться ошибкой `NEVERC_STATUS_RESOURCE_EXHAUSTED`, вместо того
чтобы забить диск.

## Запись зависимостей

Если плагин читает то, что система сборки должна отслеживать, сообщите об этом.
Иначе инкрементальная сборка не пересоберётся, когда этот вход изменится.

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

Виды: `NEVERC_INPUT_DEPENDENCY_SOURCE`, `INCLUDE`, `MODULE`, `RESOURCE`, `TOOL`
и `PLUGIN`.

## Позиции в исходниках

`NevercSourceLocation` непрозрачна. Таблица позиций превращает её в то, что
можно напечатать или сравнить.

```c
NevercSourceLocationInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Source->GetLocationInfo(Source->Context, Task, Location, &Info);
/* Info.Kind — это NEVERC_SOURCE_LOCATION_FILE или _MACRO;
   далее идут Info.FileOffset, Info.Line, Info.Column. */
```

Четыре преобразования перемещаются между представлениями позиции, и все они
разделяют сигнатуру `NevercTransformSourceLocationFn`:

| Функция | Возвращает |
|---|---|
| `GetSpellingLocation` | Где символы токена написаны на самом деле |
| `GetExpansionLocation` | Где раскрытие макроса появляется в исходнике |
| `GetFileLocation` | Ближайшую файловую позицию |
| `GetIncludeLocation` | Ту `#include`, что втянула файл |
| `GetTokenEnd` | Позицию сразу за последним символом токена |

`GetPresumedLocation` применяет директивы `#line` и выдаёт имя файла, строку,
колонку и позицию включения. `GetLocationFile` вместе с `GetFileInfo` дают
канонический путь, размер, время изменения, уникальный идентификатор и то,
является ли файл пользовательским, системным или системным extern-C:

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

Диапазоны читаются через `GetRangeInfo` (он сообщает `Begin`, `End` и то,
является ли диапазон `NEVERC_SOURCE_RANGE_CHARACTER` или `_TOKEN`), а сами байты
— через `GetSourceText` или `GetCharacterData`.

Когда позиций нужно много сразу — скажем, диагностический проход по целой
функции, — используйте пакетную форму вместо вызова на каждую позицию:

```c
Source->GetLocationInfoBatch(Source->Context, Task, Locations, LocationCount,
                             OutInfos, OutInfoCapacity);
```

## Исходные единицы

Взгляд на вход и его байты на уровне фазы:

```c
NevercSourceInputInfo Input = {0};
Source->GetSourceInput(Source->Context, Frame, Frame->Input, &Input);
/* Input.Path, .Kind (FILE или BUFFER), .Language, .System, .Preprocessed */
```

Поставщик для `neverc.source.open` отвечает единицей, опирающейся на память:

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

Кэш ключуется по `CanonicalIdentity`, поэтому это значение обязано меняться
всякий раз, когда меняется содержимое. `GetSourceUnit` читает единицу обратно и
дополнительно сообщает `MemoryBacked`.

## Правила

- Буферы из `ReadFile`, `CopyBuffer` и `PathToBuffer` принадлежат хосту;
  освобождайте каждый через `ReleaseBuffer`.
- Каждому `OpenFileForRead` нужен `CloseFile`; каждому `OpenDirectory` —
  `CloseDirectory`; каждому приёмнику вывода — `OutputFinish` или `OutputAbort`.
- Представления внутри `NevercFileInfo`, `NevercVFSStatus` и результатов позиций
  одолжены лишь на время обратного вызова.
- Обратный вызов поставщика VFS выполняется в потоке задачи и не должен звать
  компилятор обратно; отвечайте из данных, которые у вас уже есть.
- Объявляйте `Deterministic` и `Cacheable` правдиво. Поставщик, читающий часы
  или окружение и при этом заявляющий детерминизм, отравит кэш сборки.
- `AddMemoryFile` действует в рамках сессии; когда содержимое зависит от задачи,
  правильный инструмент — поставщик.

Нормативные объявления смотрите в [`PluginSource.h`], а полный пример поставщика —
в [`pluginsdk/examples/VirtualHeaderPlugin.c`].

<!-- reference links -->
[`pluginsdk/examples/VirtualHeaderPlugin.c`]: ../../pluginsdk/examples/VirtualHeaderPlugin.c
[`PluginSource.h`]: ../../neverc/include/neverc/Plugin/PluginSource.h
