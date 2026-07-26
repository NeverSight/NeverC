**Языки**: [English](driver.md) | [简体中文](driver.zh-CN.md) | [繁體中文](driver.zh-TW.md) | [日本語](driver.ja.md) | [한국어](driver.ko.md) | [Français](driver.fr.md) | [Deutsch](driver.de.md) | [Español](driver.es.md) | [Italiano](driver.it.md) | [Русский](driver.ru.md) | [العربية](driver.ar.md)

[← ABI плагинов NeverC](README.ru.md)

# API драйвера плагинов NeverC

Драйвер превращает командную строку в набор выполняемых заданий.
[`PluginDriver.h`] раскрывает этот конвейер в виде шести фаз и одной таблицы
возможностей `NevercDriverAPI`, так что плагин может переписывать аргументы,
выбирать инструментальную цепочку, перестраивать граф действий, добавлять или
заменять задания и даже выполнять задание внутри текущего процесса вместо
порождения нового.

## Интерфейс

```c
#include "neverc/Plugin/PluginDriver.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_DRIVER_HIGH,
                        NEVERC_INTERFACE_DRIVER_LOW},
    NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
    &Table, &Minor, &TableSize);
```

`NevercDriverAPI` — это одна плоская таблица из 67 функциональных слотов,
сгруппированных в пять областей: сырые аргументы, разобранные опции, выбор
инструментальной цепочки, граф действий и граф заданий. Проверяйте `TableSize`
по смещению последнего слота, который вы используете, — сейчас хвостом является
`GetJobResult`.

## Шесть фаз драйвера

| Фаза | Политика | Вход → выход |
|---|---|---|
| `neverc.driver.raw_arguments` | OBSERVABLE, INTERCEPTABLE | argv → argv |
| `neverc.driver.parsed_arguments` | OBSERVABLE, INTERCEPTABLE | список разобранных опций → список разобранных опций |
| `neverc.driver.select_toolchain` | плюс REPLACEABLE | запрос цепочки → выбор цепочки |
| `neverc.driver.build_actions` | плюс REPLACEABLE | запрос → граф действий |
| `neverc.driver.build_jobs` | плюс REPLACEABLE | граф действий → граф заданий |
| `neverc.driver.execute_job` | плюс REPLACEABLE | запрос на выполнение → результат задания |

Их макросы следуют обычному образцу:
`NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_{NAME,HIGH,LOW,POLICY,…}`.

## Регистрация опции

Опции объявляются ровно один раз, во время `Register`, после чего драйвер
принимает их в командной строке точно так же, как если бы они были встроенными.

```c
typedef struct NevercOptionDescriptor {
  NevercABITableHeader Header;
  NevercStringView Spelling;
  NevercStringList Aliases;
  NevercOptionForm Form;                  /* FLAG, JOINED, SEPARATE, MULTI_ARG */
  NevercOptionValueType ValueType;        /* BOOL, INT, UINT, STRING, ENUM, PATH */
  NevercOptionMultiplicity Multiplicity;  /* SINGLE, LAST_WINS, APPEND */
  uint32_t ArgumentCount;
  NevercBool Required;
  NevercBool Hidden;
  NevercStringView Help;
  NevercStringView Metavar;
  NevercStructArrayView EnumValues;       /* NevercOptionEnumValue[] */
  NevercStringList Conflicts;
  NevercStringList Requires;
  NevercStringView TargetPredicate;
  NevercOptionValidatorFn Validator;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercOptionDescriptor;
```

Из [`pluginsdk/examples/DriverTracePlugin.c`]:

```c
NevercOptionDescriptor Option = {0};
Option.Header = (NevercABITableHeader){sizeof(Option), NEVERC_DRIVER_API_MAJOR,
                                       NEVERC_DRIVER_API_MINOR, 0};
Option.Spelling     = SV("--driver-trace");
Option.Form         = NEVERC_OPTION_FLAG;
Option.ValueType    = NEVERC_OPTION_BOOL;
Option.Multiplicity = NEVERC_OPTION_SINGLE;
Option.Help         = SV("enable the driver trace example plugin");
Status = Registrar->RegisterOption(RegistrarContext, &Option);
```

`Validator` вызывается для каждого вхождения и получает
`NevercOptionValidationContext` с идентификатором плагина, написанием, целевым
триплетом и индексом вхождения, так что значение можно отвергнуть настоящей
диагностикой, а не провалиться позже. `TargetPredicate` ограничивает опцию
подходящими триплетами. Значения читаются обратно через
`NevercCoreAPI.GetPluginOptionValueCount` и `GetPluginOptionValue`.

## Сырые аргументы

На фазе `neverc.driver.raw_arguments` артефактом является вектор argv. Чтение
идёт по индексу, и каждая запись сообщает своё происхождение:

```c
Driver->GetArgumentCount(Driver->Context, Frame, Frame->Input, &Count);

NevercStringView Value, Source;
NevercArgumentOrigin Origin;   /* COMMAND_LINE, CONFIGURATION, PLUGIN */
uint64_t Position;
Driver->GetArgument(Driver->Context, Frame, Frame->Input, Index,
                    &Value, &Origin, &Source, &Position);
```

Редактирование транзакционно и допустимо только из перехватчика, потому что
мутация привязана к продолжению:

```c
NevercArgumentMutationHandle Mutation;
Driver->BeginArgumentMutation(Driver->Context, Frame, Continuation,
                              Frame->Input, &Mutation);
Driver->InsertArgument(Driver->Context, Mutation, Index, SV("-O2"));
Driver->ReplaceArgument(Driver->Context, Mutation, Index, SV("-O3"));
Driver->EraseArgument(Driver->Context, Mutation, Index);
Driver->CommitArgumentMutation(Driver->Context, Mutation);  /* или Abort */
```

## Разобранные аргументы

`neverc.driver.parsed_arguments` работает с вхождениями опций, а не со
строками, — именно это нужно, когда добавляется флаг, который не должен
подвергаться повторному лексическому разбору:

```c
typedef struct NevercOptionOccurrence {
  NevercABITableHeader Header;
  uint64_t Occurrence;
  NevercStringView Spelling;
  NevercStringList Values;
  NevercArgumentOrigin Origin;
  uint32_t Reserved;
} NevercOptionOccurrence;
```

Читают `GetOptionOccurrenceCount` и `GetOptionOccurrence`; затем редактируют
`BeginParsedArgumentMutation`, `AddOptionOccurrence`,
`RemoveOptionOccurrence`, `ReplaceOptionOccurrence` и
`CommitParsedArgumentMutation` / `AbortParsedArgumentMutation`.

## Выбор инструментальной цепочки

Запрос описывает и то, что было запрошено, и то, что вычислил драйвер:

```c
typedef struct NevercToolChainRequest {
  NevercABITableHeader Header;
  NevercStringView RequestedTriple;
  NevercStringView ComputedTriple;
  NevercStringView SysRoot;
  NevercStringView ResourceDir;
  NevercStringView CPU;
  NevercStringList Features;
  NevercExecutionLevel ExecutionLevel;  /* UNSPECIFIED, USER, KERNEL */
  NevercBool DynamicCodeProfile;
  uint32_t Reserved;
} NevercToolChainRequest;
```

Перехватчик может подправить запрос через `BeginToolChainMutation`,
`SetToolChainTriple`, `SetToolChainCPU`, `SetToolChainFeatures` и
`CommitToolChainMutation`. Провайдер же отвечает на фазу целиком через
`CreateToolChainSelection`, называя один из встроенных идентификаторов цепочки
или свой собственный:

```c
NEVERC_TOOLCHAIN_ID_DARWIN        /* "neverc.builtin.darwin"      */
NEVERC_TOOLCHAIN_ID_LINUX         /* "neverc.builtin.linux"       */
NEVERC_TOOLCHAIN_ID_MSVC          /* "neverc.builtin.msvc"        */
NEVERC_TOOLCHAIN_ID_GENERIC_ELF   /* "neverc.builtin.generic-elf" */
NEVERC_TOOLCHAIN_ID_MACHO         /* "neverc.builtin.macho"       */
NEVERC_TOOLCHAIN_ID_GENERIC_GCC   /* "neverc.builtin.generic-gcc" */
```

`GetToolChainSelection` читает результат обратно и сообщает
`BuiltinProviderUsed`, так что наблюдатель может понять, выиграл ли фазу плагин.

## Граф действий

Узел действия — это типизированный шаг компиляции. Узлы ссылаются на входы
драйвера и на другие узлы:

```c
typedef struct NevercActionNode {
  NevercABITableHeader Header;
  NevercActionNodeID Node;
  NevercActionKind Kind;
  NevercDriverType OutputType;
  uint64_t InputCount;
  NevercDriverInputID DriverInput;
  NevercStringView BindArch;
  uint64_t Reserved;
} NevercActionNode;
```

| `NevercActionKind` | | `NevercDriverType` | |
|---|---|---|---|
| `INPUT` | `BIND_ARCH` | `PP_C`, `C`, `C_HEADER` | `PP_ASM`, `ASM` |
| `PREPROCESS` | `COMPILE` | `LLVM_IR`, `LLVM_BC` | `LTO_IR`, `LTO_BC` |
| `BACKEND` | `ASSEMBLE` | `OBJECT`, `IMAGE` | `DSYM` |
| `LINK`, `LIPO` | `DSYMUTIL` | `DEPENDENCIES` | `NOTHING` |
| `STATIC_LIB` | `DYNCODE` | | |

Чтение — через `GetDriverInputCount` / `GetDriverInput`,
`GetActionNodeCount` / `GetActionNode` / `GetActionNodeInput` и
`GetActionRootCount` / `GetActionRoot`.

Построение замещающего графа идёт через строитель и одну публикацию:

```c
NevercActionGraphBuilderHandle Builder;
Driver->CreateActionGraphBuilder(Driver->Context, Frame, Request, &Builder);

NevercActionNodeDescriptor Node = {0};
Node.Header     = (NevercABITableHeader){sizeof(Node), NEVERC_DRIVER_API_MAJOR,
                                         NEVERC_DRIVER_API_MINOR, 0};
Node.Kind       = NEVERC_ACTION_COMPILE;
Node.OutputType = NEVERC_DRIVER_TYPE_OBJECT;
Node.Inputs     = /* NevercActionNodeIDList */;
NevercActionNodeID Created;
Driver->AddActionNode(Driver->Context, Builder, &Node, &Created);

Driver->SetActionRoots(Driver->Context, Builder, Roots);
Driver->PublishActionGraph(Driver->Context, Frame, Builder, &OutGraph);
```

`RemoveActionNode`, `ReplaceActionNodeInputs`, `SetActionNodeOutputType` и
`SetActionNodeBindArch` редактируют строитель в работе. Чтобы подправить
существующий граф хоста, а не строить заново, используйте
`BeginActionGraphMutation` и `CommitActionGraphMutation`;
`AbortActionGraphEdit` отбрасывает любую из форм.

## Граф заданий

Задание — это команда для выполнения. `NevercJobDescriptor` описывает одно:

```c
typedef struct NevercJobDescriptor {
  NevercABITableHeader Header;
  NevercJobKind Kind;                             /* COMMAND, FRONTEND, LINKER,
                                                     ARCHIVE, PLUGIN, DYNCODE  */
  NevercResponseFileKind ResponseFileKind;        /* NONE, FULL, LIST          */
  NevercResponseFileEncoding ResponseFileEncoding;/* UTF8, CURRENT_CODE_PAGE,
                                                     UTF16                     */
  NevercBool InProcess;
  NevercActionNodeID SourceAction;
  NevercLinkerFlavor LinkerFlavor;                /* NONE, GNU, WIN_LINK, DARWIN */
  uint32_t Reserved;
  NevercStringView Executable;
  NevercStringList Arguments;
  NevercStringList Environment;
  NevercJobFileList Inputs;
  NevercJobFileList Outputs;
  NevercJobIDList Dependencies;
  NevercStringView CallbackID;
  NevercPluginJobCallbackFn Callback;
  void *UserData;
} NevercJobDescriptor;
```

Задайте `Kind` равным `NEVERC_JOB_PLUGIN` вместе с `Callback`, и драйвер
выполнит вашу функцию там, где иначе породил бы процесс:

```c
static NevercStatus NEVERC_CALL run_job(const NevercPluginJobContext *Context,
                                        int32_t *OutExitCode, void *UserData) {
  /* Context->Arguments, ->Environment, ->Inputs, ->Outputs — заимствованы. */
  *OutExitCode = 0;
  return neverc_status_ok();
}
```

Чтение графа зеркалит граф действий: `GetJobCount` / `GetJob`,
`GetJobDependency`, `GetJobArgument` / `GetJobEnvironment`, `GetJobInput` /
`GetJobOutput`. Учтите, что `NevercJob` сообщает только количества — забирайте
каждую строку или файл по индексу, а не рассчитывайте на встроенный массив.

Редактирование начинается с `CreateJobGraphBuilder` или
`BeginJobGraphMutation`, далее — `AddJob`, `RemoveJob`, `MoveJobBefore`,
`ReplaceJob`, `SetJobArgument`, `SetJobEnvironment`, `SetJobInput`,
`SetJobOutput` и `ReplaceJobDependencies`. Публикация — `PublishJobGraph` или
`CommitJobGraphMutation`; отказ — `AbortJobGraphEdit`.

## Выполнение задания

На фазе `neverc.driver.execute_job` входным артефактом служит
`NevercJobExecutionRequest` — само задание плюс полностью материализованные
списки аргументов, окружения, входов, выходов и зависимостей. Провайдер
выполняет задание и сообщает результат:

```c
typedef struct NevercJobResultDescriptor {
  NevercABITableHeader Header;
  int32_t ExitCode;
  NevercBool ExecutionFailed;
  NevercBool HasProcessStatistics;
  uint32_t Reserved;
  NevercStringView ErrorMessage;
  NevercOutputSealList OutputSeals;
  uint64_t TotalTimeMicroseconds;
  uint64_t UserTimeMicroseconds;
  uint64_t PeakMemoryKiB;
} NevercJobResultDescriptor;
```

`OutputSeals` несёт дескрипторы `NevercOutputSealHandle`, созданные через
API ввода-вывода (см. [Source и ввод-вывод](source.ru.md#запись-вывода));
именно так хост подтверждает, что файлы, которые задание заявило записанными,
действительно существуют с указанными дайджестами. `GetJobResult` читает
зафиксированный результат и, как и выбор цепочки, сообщает
`BuiltinProviderUsed`.

## Разобранный пример: наблюдение за аргументами и перехват выполнения

Сжато из [`pluginsdk/examples/DriverTracePlugin.c`]. Плагин не держит глобальных
переменных: состояние процесса хранит согласованные таблицы, а счётчики уровня
сессии и задачи запрашиваются у хоста внутри каждого обратного вызова.

```c
static NevercStatus NEVERC_CALL
observe_arguments(const NevercPhaseFrame *Frame, NevercObserverPoint Point,
                  void *UserData) {
  DriverTraceProcessState *Process = (DriverTraceProcessState *)UserData;
  DriverTraceSessionState *Session = NULL;
  uint64_t ArgumentCount = 0;
  NevercStatus Status;
  if (Frame == NULL || Process == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Process->Core->GetSessionState(Process->Core->Context,
                                          Frame->Session, plugin_id(),
                                          (void **)&Session);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Process->Driver->GetArgumentCount(Process->Driver->Context, Frame,
                                             Frame->Input, &ArgumentCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ++Session->ArgumentCallbacks;
  if (Point == NEVERC_OBSERVER_BEFORE && !Session->Announced) {
    Session->Announced = NEVERC_TRUE;
    return emit_trace_remark(Process, Frame, "driver argument phase observed",
                             30, 1001);
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
intercept_job(const NevercPhaseFrame *Frame,
              NevercPhaseContinuation *Continuation,
              NevercPhaseResult *OutResult, void *UserData) {
  DriverTraceProcessState *Process = (DriverTraceProcessState *)UserData;
  NevercJobExecutionRequest Request = {0};
  NevercPhaseResult Downstream = {0};
  NevercStatus Status;
  if (Frame == NULL || Continuation == NULL || OutResult == NULL || !Process)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Request.Header = (NevercABITableHeader){
      sizeof(Request), NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR, 0};
  Status = Process->Driver->GetJobExecutionRequest(
      Process->Driver->Context, Frame, Frame->Input, &Request);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Downstream.Header = (NevercABITableHeader){
      sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  *OutResult = (NevercPhaseResult){0};
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}
```

Регистрация связывает обе функции с их фазами:

```c
Observer.Phase = phase_id(NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                          NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW);
Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
Observer.Callback = observe_arguments;
Observer.UserData = Process;
Registrar->RegisterObserver(RegistrarContext, &Observer);

Interceptor.Phase = phase_id(NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH,
                             NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW);
Interceptor.Callback = intercept_job;
Interceptor.UserData = Process;
Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
```

Сборка и запуск:

```sh
cmake --build build-neverc --target neverc-plugin-example-driver-trace
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/DriverTracePlugin.so \
  --driver-trace -c input.c -o input.o
```

## Правила

- Мутации аргументов, разобранных аргументов, инструментальной цепочки, графа
  действий и графа заданий требуют `NevercPhaseContinuation` перехватчика; вне
  него они отвергаются с `NEVERC_STATUS_WRONG_SCOPE`.
- Вызывайте `InvokeNext` не более одного раза и только в потоке обратного
  вызова.
- Каждый дескриптор мутации должен дойти ровно до одного `Commit*` или
  `Abort*`.
- Представления, возвращённые вызовом `Get*`, заимствованы на время обратного
  вызова. Копируйте всё, что нужно сохранить.
- Обратный вызов `NEVERC_JOB_PLUGIN` не должен порождать процесс, который
  породил бы хост, и при этом сообщать об успехе встроенного пути; объявите
  `REPLACE` и отвечайте за исход сами.
- О неудачном задании сообщайте через
  `NevercJobResultDescriptor.ExecutionFailed` и `ErrorMessage`, а не возвратом
  статуса, отличного от OK, для задания, которое выполнилось и закономерно
  завершилось неудачей.

Нормативные объявления смотрите в [`PluginDriver.h`], политики фаз драйвера — в
[`PhaseSchema.json`], свидетельства тестов — в [`coverage.json`].

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`PluginDriver.h`]: ../../neverc/include/neverc/Plugin/PluginDriver.h
[`pluginsdk/examples/DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
