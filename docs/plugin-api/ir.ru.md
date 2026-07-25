**Языки**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

[← ABI плагинов NeverC](README.ru.md)

# API IR плагинов NeverC

`PluginIR.h` открывает LLVM IR через шесть таблиц возможностей и порождённую
схему. Плагин читает и переписывает IR, регистрирует проходы в пяти устойчивых
точках конвейера, определяет собственные анализы или целиком заменяет генерацию
IR и оптимизационный конвейер — не включив при этом ни одного заголовка LLVM.

Опкоды, виды типов и свойства инструкций — это **устойчивые идентификаторы
схемы**, а не значения перечислений LLVM. Именно эта косвенность позволяет
плагину, скомпилированному сегодня, продолжать работать, когда хост перейдёт на
новый выпуск LLVM.

## Интерфейсы

```c
#include "neverc/Plugin/PluginIR.h"
```

| Интерфейс | Таблица | Слоты | Назначение |
|---|---|--:|---|
| `NEVERC_INTERFACE_IR_CORE_{HIGH,LOW}` | `NevercIRCoreAPI` | 99 | Чтение и правка модулей, значений, типов, констант, метаданных, атрибутов |
| `NEVERC_INTERFACE_IR_BUILDER_{HIGH,LOW}` | `NevercIRBuilderAPI` | 29 | Транзакционное построение |
| `NEVERC_INTERFACE_IR_ANALYSIS_{HIGH,LOW}` | `NevercIRAnalysisAPI` | 13 | Встроенные и плагинные анализы |
| `NEVERC_INTERFACE_IR_PASS_{HIGH,LOW}` | `NevercIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_IR_GEN_{HIGH,LOW}` | `NevercIRGenAPI` | 5 | Замена понижения SemanticUnit → IR |
| `NEVERC_INTERFACE_IR_OPTIMIZATION_{HIGH,LOW}` | `NevercIROptimizationAPI` | 7 | Замена всего оптимизационного конвейера |

Каждая при major 1 имеет статус `NEVERC_INTERFACE_STABLE`. Договаривайтесь через
соответствующие `NEVERC_IR_*_API_MAJOR` / `_MINOR` и проверяйте, что `TableSize`
дотягивается до последнего вызываемого вами слота — ровно так, как это делает
`pluginsdk/examples/FunctionPass.c`:

```c
Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &StructSize);
if (!Table ||
    StructSize < offsetof(NevercIRPassAPI, RegisterPass) +
                     sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

## Фазы

Восемь фаз IR:

| Фаза | Политика |
|---|---|
| `neverc.ir.generate` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.optimize` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.pass.pre_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pipeline_start` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.optimizer_last` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.post_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pre_codegen` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.final_verify` | OBSERVABLE, **ЗАПЕЧАТАННЫЙ ШЛЮЗ ХОСТА** |

Пять фаз `pass.*` — это то, куда указывает `NevercIRPassDescriptor.Phase`.
`neverc.ir.final_verify` запускает верификатор LLVM, и её нельзя ни перехватить,
ни заменить, ни пропустить — включая поставщика оптимизации.

## Схема

`Schema/PluginIRSchema.inc` порождается и включается из `PluginIR.h`. Он
публикует дайджест и следующие наборы констант:

```c
#define NEVERC_IR_SCHEMA_CAPABILITY_MAJOR   UINT16_C(1)
#define NEVERC_IR_SCHEMA_DIGEST             "4302919d…"
#define NEVERC_IR_TYPE_KIND_COUNT           UINT32_C(22)
#define NEVERC_IR_VALUE_KIND_COUNT          UINT32_C(29)
#define NEVERC_IR_OPCODE_COUNT              UINT32_C(67)
#define NEVERC_IR_PREDICATE_COUNT           UINT32_C(26)
#define NEVERC_IR_LINKAGE_COUNT             UINT32_C(11)
#define NEVERC_IR_CALLING_CONVENTION_COUNT  UINT32_C(21)
#define NEVERC_IR_PROPERTY_COUNT            UINT32_C(23)
```

Идентификаторы помечены доменом в старшем байте — `0x41……` для типов,
`0x42……` для видов значений, `0x43……` для опкодов, `0x49……` для свойств, — так
что значение, использованное не на своём месте, будет отвергнуто, а не прочитано
неверно.

## Дескрипторы и владение

Дескрипторы IR — непрозрачные пары `{Owner, Value}`, ограниченные одной задачей,
и всем, что за ними стоит, владеет хост.

- Никогда не удерживайте дескриптор после конца его обратного вызова или задачи.
- Никогда не используйте дескриптор в другой сессии или задаче.
- Зафиксированная замена делает недействительными дескрипторы заменённых
  объектов.
- Отменённое изменение делает созданные им дескрипторы устаревшими.
- Ошибки — это `NEVERC_STATUS_STALE_HANDLE`, `WRONG_SCOPE` или `WRONG_TYPE`, но
  никогда не сырой указатель LLVM.

Строки и байтовые представления из запроса одолжены на время обратного вызова.
Единственное исключение — `ExportModule`: он возвращает
`NevercIRSerializedBufferHandle`, который надо вернуть в
`ReleaseSerializedBuffer`.

## Обход модуля

Коллекции читаются через курсор, несущий собственное поколение, поэтому
изменение посреди обхода обнаруживается, а не приводит к молчаливому пропуску
записей:

```c
NevercIRValueCursor Cursor = {0};
Cursor.Header = (NevercABITableHeader){sizeof(Cursor),
                                       NEVERC_IR_CORE_API_MAJOR,
                                       NEVERC_IR_CORE_API_MINOR, 0};
Core->BeginValueCursor(Core->Context, Task, Module,
                       NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, &Cursor);

NevercIRValueHandle Batch[32];
uint64_t Count = 0;
for (;;) {
  Core->CollectValueCursor(Core->Context, Task, &Cursor, Batch, 32, &Count);
  if (Count == 0)
    break;
  for (uint64_t I = 0; I != Count; ++I) {
    NevercStringView Name;
    Core->GetValueName(Core->Context, Task, Batch[I], &Name);
  }
}
```

Повторяйте, пока `Count` не вернётся нулём. Семь коллекций:
`MODULE_FUNCTIONS`, `MODULE_GLOBALS`, `MODULE_ALIASES`, `MODULE_I_FUNCS`,
`FUNCTION_ARGUMENTS`, `FUNCTION_BLOCKS` и `BLOCK_INSTRUCTIONS`.

Всё остальное — прямые запросы: `GetValueKind`, `GetValueType`,
`GetOperandCount` / `GetOperand` / `SetOperand`, `GetValueUseCount` /
`GetValueUse`, `GetTerminator`, `GetPredecessor*`, `GetSuccessor*`,
`GetPHIIncoming*`, а также модульные `GetModuleIdentifier`,
`GetModuleTargetTriple`, `GetModuleDataLayout`, `GetModuleInlineAssembly` с их
сеттерами.

## Типы и константы

Типы интернируются, поэтому дважды спросив, вы получите тот же дескриптор:

```c
NevercIRTypeHandle I32, Ptr, Fn;
Core->GetIntegerType(Core->Context, Task, 32, &I32);
Core->GetPointerType(Core->Context, Task, /*AddressSpace=*/0, &Ptr);

NevercIRTypeHandle Params[] = {I32, Ptr};
Core->GetFunctionType(Core->Context, Task, I32, Params, 2,
                      /*Variadic=*/0, &Fn);
```

`GetPrimitiveType` принимает вид схемы, например `NEVERC_IR_TYPE_VOID`,
`_FLOAT`, `_DOUBLE` или `_TOKEN`; остальное покрывают `GetArrayType`,
`GetVectorType` (с флагом `Scalable`) и `GetStructType` (именованная или
литеральная, упакованная или нет).

Целые и вещественные константы строятся из 64-битных слов в порядке
little-endian, так что для `i128` не нужен особый путь:

```c
uint64_t Words[2] = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
NevercIRValueHandle C;
Core->CreateIntegerConstant(Core->Context, Task, I128, Words, 2, &C);
```

`GetNullConstant`, `GetPoisonConstant`, `GetUndefConstant`,
`CreateAggregateConstant` и `GetGlobalAddressConstant` покрывают простые случаи;
`CreateConstantBinaryExpression`, `CreateConstantCastExpression`,
`CreateConstantCompareExpression` и `CreateConstantGEPExpression` строят
константные выражения.

## Свойства инструкций

Вместо отдельного аксессора на каждый флаг детали инструкции проходят через
тегированное значение свойства с ключом из идентификатора схемы:

```c
typedef struct NevercIRPropertyValue {
  NevercABITableHeader Header;
  NevercIRPropertyValueKind Kind;   /* BOOL, UINT, ENUM, FLAGS, STRING, TYPE */
  uint32_t Reserved;
  uint64_t UnsignedValue;
  NevercIRTypeHandle TypeValue;
  NevercStringView StringValue;
} NevercIRPropertyValue;

NevercIRPropertyValue Value = {0};
Value.Header = /* … */;
Core->GetInstructionProperty(Core->Context, Task, Instruction,
                             NEVERC_IR_PROPERTY_ALIGNMENT, &Value);
```

23 свойства: `NAME`, `FAST_MATH_FLAGS`, `NUW`, `NSW`, `EXACT`, `DISJOINT`,
`VOLATILE`, `ALIGNMENT`, `ATOMIC_ORDERING`, `SYNC_SCOPE`, `PREDICATE`,
`CALLING_CONVENTION`, `TAIL_CALL_KIND`, `INDICES`, `WEAK`,
`SUCCESS_ORDERING`, `FAILURE_ORDERING`, `INBOUNDS`, `SOURCE_ELEMENT_TYPE`,
`ALLOCATED_TYPE`, `ATTRIBUTES`, `CLEANUP` и `NUSW`. Атомарные порядки идут от
`NOT_ATOMIC` до `SEQUENTIALLY_CONSISTENT`; виды хвостовых вызовов — `NONE`,
`TAIL`, `MUST_TAIL` и `NO_TAIL`; флаги fast-math — привычные семь битов от
`ALLOW_REASSOC` до `APPROX_FUNC`.

## Атрибуты

Атрибуты — это значения, которые вы создаёте, а затем прикрепляете; благодаря
этому четыре вида (`ENUM`, `INTEGER`, `STRING`, `TYPE`) выглядят одинаково:

```c
NevercIRAttributeHandle NoInline;
Core->CreateEnumAttribute(Core->Context, Task, SV("noinline"), &NoInline);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION,
                           /*ParameterIndex=*/0, NoInline);

NevercBool Present = NEVERC_FALSE;
Core->HasFunctionAttribute(Core->Context, Task, Function, SV("noinline"),
                           &Present);
```

`pluginsdk/examples/CustomCallConvPlugin.c` использует это вместе с
`GetFunctionStringAttribute`, чтобы управлять соглашением о вызовах, заданным
данными.

## Транзакционное изменение

Любое структурное изменение проходит через `NevercIRBuilderAPI`. Изменение —
это транзакция, а строитель — курсор внутри неё.

```c
NevercIRMutationHandle Mutation;
NevercIRBuilderHandle Builder;

Builders->BeginMutation(Builders->Context, Task,
                        NEVERC_IR_MUTATION_SCOPE_FUNCTION, Function,
                        &Mutation);
Builders->CreateBuilder(Builders->Context, Task, Mutation, &Builder);
Builders->SetInsertBefore(Builders->Context, Task, Builder, Terminator);

NevercIRValueHandle Sum;
Builders->BuildBinary(Builders->Context, Task, Builder,
                      NEVERC_IR_OPCODE_ADD, Left, Right, SV("sum"), &Sum);

Status = Builders->CommitMutation(Builders->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  Builders->AbortMutation(Builders->Context, Task, Mutation);

Builders->DestroyBuilder(Builders->Context, Task, Builder);
Builders->DestroyMutation(Builders->Context, Task, Mutation);
```

Области — `NEVERC_IR_MUTATION_SCOPE_MODULE`, `_FUNCTION` и `_LOOP`;
`ScopeRoot` называет функцию или заголовок цикла. Фиксация проверяет кандидата и
публикует атомарно: при отказе верификатора хост откатывается, и прежний модуль
остаётся нетронутым.

Вызовы построения: `BuildBinary`, `BuildUnary`, `BuildCompare`, `BuildCast`,
`BuildSelect`, `BuildAlloca`, `BuildLoad`, `BuildStore`, `BuildGetElementPtr`,
`BuildCall`, `BuildPhi`, `BuildBranch`, `BuildConditionalBranch`,
`BuildUnreachable`, `BuildReturn` и `BuildReturnVoid`. `SetDebugLocation` и
`SetFastMathFlags` действуют на всё, что строитель выпустит после них.

Обратите внимание на асимметрию: `AddPhiIncoming`, `CreateFunction` и
`CreateBasicBlock` принимают **изменение**, а не строитель, потому что они не
привязаны к точке вставки.

`DestroyMutation` отделён от фиксации и отмены. Каждому `BeginMutation` нужен
ровно один `DestroyMutation`, чем бы транзакция ни закончилась.

## Проходы

```c
NevercIRPassDescriptor Pass = {0};
Pass.Header = (NevercABITableHeader){sizeof(Pass), NEVERC_IR_PASS_API_MAJOR,
                                     NEVERC_IR_PASS_API_MINOR, 0};
Pass.PassID        = SV("example.function-pass");
Pass.Phase         = (NevercInterfaceID){
                         NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
                         NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW};
Pass.Level         = NEVERC_IR_PASS_LEVEL_FUNCTION;
Pass.Deterministic = NEVERC_TRUE;
Pass.Cacheable     = NEVERC_TRUE;
Pass.Run           = run_function;
PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
```

Уровни: `MODULE`, `CGSCC`, `FUNCTION` и `LOOP`. Вызов несёт только те
дескрипторы, что действительны для его уровня:

```c
typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;  /* O0…O3, Os, Oz */
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;                 /* FUNCTION и LOOP       */
  NevercIRValueHandle LoopHeader;               /* только LOOP           */
  const NevercIRValueHandle *SCCFunctions;      /* только CGSCC          */
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;
```

Три указателя на API приходят вместе с вызовом, поэтому телу прохода не нужно
хранить таблицу.

Сообщайте о том, что уцелело, через `OutPreserved`:

```c
OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;   /* или _NONE, или _CFG */
```

`NEVERC_IR_PRESERVE_CFG` означает, что граф потока управления цел, хотя
инструкции изменились. Собственные анализы сохраняются, если перечислить их в
`CustomAnalyses`. Не заявляйте `PRESERVE_ALL` после изменения IR — адаптер
сравнит поколение модуля и отвергнет ложное заявление.

Функциональные и цикловые проходы могут исполняться параллельно, поэтому
изменяемое состояние плагина обязано соответствовать заявленной
`NevercConcurrencyModel`.

## Анализы

Семь встроенных анализов доступны по идентификатору: `DOMINATOR_TREE`,
`POST_DOMINATOR_TREE`, `LOOP_INFO`, `SCALAR_EVOLUTION`, `MEMORY_SSA`,
`CALL_GRAPH` и `ALIAS`.

```c
NevercIRAnalysisResultHandle Loops;
Analyses->QueryBuiltin(Analyses->Context, Task,
                       NEVERC_IR_ANALYSIS_LOOP_INFO, Function, &Loops);

uint64_t LoopCount = 0;
Analyses->GetLoopCount(Analyses->Context, Task, Loops, &LoopCount);
for (uint64_t I = 0; I != LoopCount; ++I) {
  NevercIRValueHandle Header;
  Analyses->GetLoopHeader(Analyses->Context, Task, Loops, I, &Header);
}
```

У каждого есть типизированные аксессоры, а не непрозрачный ком:
`DominatorTreeDominates`, `GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`, `GetScalarEvolutionConstantTripCount`,
`GetMemoryAccessKind` (`NONE`, `USE`, `DEF`, `PHI`, `LIVE_ON_ENTRY`),
`GetDirectCalleeCount` / `GetDirectCallee` и `Alias` (`NO`, `MAY`, `PARTIAL`,
`MUST`).

Плагинный анализ регистрируется вместе со своим жизненным циклом:

```c
NevercIRAnalysisDescriptor Analysis = {0};
Analysis.Header          = /* … */;
Analysis.AnalysisID      = MyAnalysisID;
Analysis.Name            = SV("example.my-analysis");
Analysis.Level           = NEVERC_IR_PASS_LEVEL_FUNCTION;
Analysis.Dependencies    = Deps;
Analysis.DependencyCount = DepCount;
Analysis.Compute         = compute;
Analysis.Query           = query;
Analysis.Invalidate      = invalidate;
Analysis.Destroy         = destroy;
Analyses->RegisterAnalysis(Analyses->Context, RegistrarContext, &Analysis);
```

`Invalidate` узнаёт причину — `INVALIDATED_BY_PASS` или
`INVALIDATED_BY_PLAN_DESTROY`. Результаты кэшируются на каждый вызов и
отбрасываются в зависимости от того, что сохранил идущий проход. Циклы
зависимостей отвергаются при регистрации, а изменение IR изнутри обратного
вызова анализа запрещено.

## Замена генерации и оптимизации

`NevercIRGenAPI` заменяет `neverc.ir.generate`:

```c
NevercIRGeneratePhaseInput In = {0};
In.Header = /* … */;
Gen->GetGeneratePhaseInput(Gen->Context, Frame, Frame->Input, &In);
/* In.SemanticUnit, .TargetTriple, .DataLayout, .SourceIdentity,
   .SourceDigest */

const NevercIRCoreAPI *Core;
const NevercIRBuilderAPI *Builders;
Gen->CreateModule(Gen->Context, Frame, SV("my.module"), &Core, &Builders);
/* … построить модуль … */

NevercIRModuleArtifactDescriptor Descriptor = {0};
Descriptor.Header           = /* … */;
Descriptor.Product          = MyProductID;
Descriptor.DependencyDigest = Digest;
Gen->PublishModule(Gen->Context, Frame, &Descriptor, &Output);
```

`ImportModule` начинает с биткода или текстовой IR вместо пустого модуля. У
`NevercIROptimizationAPI` та же форма для `neverc.ir.optimize`, плюс
`GetInputModule`, чтобы добраться до входящего модуля, и `RunBuiltinPipeline`,
чтобы делегировать встроенному конвейеру и затем обработать его результат.

Оба пути публикуют через хост, а не возвращают указатель, оба проверяют
совместимость с целью и оба атомарно сохраняют старый модуль, если публикация не
удалась. `neverc.ir.final_verify` всё равно выполнится после.

## Примеры

| Файл | Что показывает |
|---|---|
| `pluginsdk/examples/FunctionPass.c` | Функциональный проход только для чтения, вместе с согласованием ABI |
| `pluginsdk/examples/ExamplePlugin.c` | Модульный проход, обходящий функции курсором значений |
| `pluginsdk/examples/CustomCallConvPlugin.c` | Атрибуты и свойства места вызова |

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

Используйте то расширение модуля, которое CMake создал для вашей платформы.

## Правила

- Возвращайте `NevercStatus` из каждого обратного вызова. Отказ плагина
  превращается в структурированную диагностику; никогда не позволяйте исключению
  пересечь границу C.
- Обнуляйте каждую выходную структуру и задавайте её `Header` перед вызовом,
  который её заполняет.
- Не зашивайте числовые значения опкодов, типов и свойств. Берите имена из
  `PluginIRSchema.inc`, чтобы пересмотр схемы становился ошибкой компиляции.
- Каждому `BeginMutation` соответствует ровно один `DestroyMutation`, а каждому
  `CreateBuilder` — ровно один `DestroyBuilder`, включая пути ошибок.
- Освобождайте то, что вручает `ExportModule`, через
  `ReleaseSerializedBuffer`.
- Никогда не заявляйте `NEVERC_IR_PRESERVE_ALL` после изменения IR.
- Считайте, что функциональные и цикловые проходы идут параллельно, если плагин
  не объявил `NEVERC_CONCURRENCY_SESSION_SERIAL`.
- `neverc.ir.final_verify` запечатана. Ничто из того, что делает плагин, её не
  обойдёт.

Нормативные объявления, константы схемы, политики фаз и свидетельства тестов
смотрите в `PluginIR.h`, `Schema/PluginIRSchema.inc`,
`Schema/PhaseSchema.json` и `coverage.json`.
