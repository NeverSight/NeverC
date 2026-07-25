**Языки**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

# API плагинов NeverC: целевая платформа, MC, ассемблер и объектные файлы

Бэкенд — это четыре заголовка и двадцать девять фаз. `PluginTarget.h`
описывает целевую платформу и маршруты через генерацию кода. `PluginMC.h`
строит и наблюдает машинный код. Разбор и печать ассемблера живут в том же
заголовке. `PluginObject.h` превращает перемещаемый файл в нормализованный
граф и обратно.

Вместе они позволяют плагину добавить целевую платформу, заменить один шаг
понижения или все сразу, следить за каждой инструкцией в момент её выпуска,
определить диалект ассемблера или переписать объектный файл — и всё это через
чистый C ABI, который никогда не раскрывает `MCInst`, `MCSection` или
`object::ObjectFile` из LLVM.

## Интерфейсы

```c
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"   /* includes both of the above */
```

| Интерфейс | Таблица | Слоты | Назначение |
|---|---|--:|---|
| `NEVERC_INTERFACE_TARGET_*` | `NevercTargetAPI` | 2 | `RegisterTarget`, `RegisterCodeGenEdge` |
| `NEVERC_INTERFACE_TARGET_ABI_*` | `NevercTargetABIAPI` | 1 | `RegisterABI` |
| `NEVERC_INTERFACE_CALLING_CONVENTION_*` | `NevercCallingConventionAPI` | 1 | `RegisterCallingConvention` |
| `NEVERC_INTERFACE_MC_*` | `NevercMCAPI` | 53 | Чтение и изменение `MCUnit`; регистрация кодировщиков, декодировщиков, бэкендов |
| `NEVERC_INTERFACE_MC_EMISSION_*` | `NevercMCEmissionAPI` | 7 | События выпуска и снимки раскладки |
| `NEVERC_INTERFACE_MC_PROVIDER_*` | `NevercMCProviderAPI` | 4 | Замена MIR → MC |
| `NEVERC_INTERFACE_ASSEMBLY_PROVIDER_*` | `NevercAssemblyProviderAPI` | 8 | Замена разборщика или печатника ассемблера |
| `NEVERC_INTERFACE_OBJECT_*` | `NevercObjectAPI` | 34 | Чтение и изменение ObjectGraph |
| `NEVERC_INTERFACE_OBJECT_FORMAT_*` | `NevercObjectFormatAPI` | 1 | `RegisterFormat` |
| `NEVERC_INTERFACE_OBJECT_PHASE_*` | `NevercObjectPhaseAPI` | 2 | `GetGraph`, `GetImage` |

## Два уровня совместимости

Это правило управляет всем остальным здесь.

**STABLE**, и его безопасно зашивать в код: независимые от платформы
дескрипторы, идентификаторы фаз, идентификаторы артефактов, контейнеры MC и
ObjectGraph, транзакции вывода и все контракты обратных вызовов.

**LOCKSTEP**, и без проверки небезопасно: специфичные для платформы схемы
опкодов, регистров, операндов, фиксапов, перемещений и соглашений о вызовах.
Их числовые значения имеют смысл только относительно одной точной ревизии
схемы.

Везде, где появляется значение LOCKSTEP, рядом появляется дайджест схемы.
Сверьте его, прежде чем читать значение:

```c
if (!string_equal(Target.SchemaDigest, MY_COMPILED_SCHEMA_DIGEST))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

NeverC тоже отклоняет несовпадающую схему до вызова провайдера, так что эта
проверка — двойная страховка. Но плагин, который её пропустит и всё же
прочитает сырой опкод, будет молча неверно интерпретировать инструкции.

## Фазы

Двадцать девять, в четырёх доменах.

### `codegen` — маршрутизация (4)

| Фаза | Политика |
|---|---|
| `neverc.codegen.ir_to_mir` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.mir_to_mc` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.coarse_lower` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.product_verify` | OBSERVABLE, **SEALED** |

### `mc` — машинный код (13)

`neverc.mc.encode`, `neverc.mc.decode` и `neverc.mc.layout` — OBSERVABLE,
INTERCEPTABLE, REPLACEABLE.

`neverc.mc.emission.pre_instruction` — единственное событие выпуска, которое
вдобавок REPLACEABLE: именно там подменяют инструкцию. Остальные девять
(`unit_begin`, `unit_end`, `section_change`, `post_instruction`,
`post_encode`, `fixup`, `relaxation_round`, `pre_layout`, `post_layout`) —
только для наблюдения.

### `assembly` (4)

`neverc.assembly.parse` и `neverc.assembly.print` — REPLACEABLE.
`neverc.assembly.final_verify` и `neverc.assembly.commit` — SEALED.

### `object` (8)

`neverc.object.probe`, `read`, `write`, `pre_write` и `post_layout` —
REPLACEABLE; `neverc.object.post_write` — только INTERCEPTABLE;
`neverc.object.final_verify` и `neverc.object.commit` — SEALED.

## Регистрация целевой платформы

`NevercTargetDescriptor` — самый большой дескриптор в этом ABI, потому что он
несёт всё, что нужно знать фронтенду и бэкенду:

```c
typedef struct NevercTargetDescriptor {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercStringArrayView Aliases;
  NevercStructArrayView TripleMatchers;    /* NevercTargetTripleMatcher[] */
  NevercTargetABIID DefaultABI;
  NevercCallingConventionID DefaultCallingConvention;
  NevercInterfaceID MCSchemaID;
  NevercInterfaceID DefaultObjectFormatID;
  NevercTargetMachineDescriptor Machine;
  NevercStructArrayView Macros;            /* predefined macros           */
  NevercStructArrayView Builtins;          /* target builtins + lowering  */
  NevercStructArrayView Registers;         /* inline-asm register names   */
  NevercStructArrayView Constraints;       /* inline-asm constraints      */
  NevercStringView Clobbers;
  uint64_t Flags;
  NevercTargetValidateCPUFn ValidateCPU;
  NevercTargetCanonicalizeCPUFn CanonicalizeCPU;
  NevercTargetListCPUsFn ListCPUs;
  NevercTargetResolveFeaturesFn ResolveFeatures;
  NevercCreateTargetMachineFn CreateTargetMachine;
  NevercDestroyTargetMachineFn DestroyTargetMachine;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercTargetDescriptor;
```

`TripleMatchers` решает, когда выбирается эта платформа: каждый сопоставитель
называет архитектуру, поставщика, операционную систему и окружение, а также
`Priority`, который разрешает ничью со встроенными платформами.

`Machine` — это `NevercTargetMachineDescriptor`: раскладка данных, CPU по
умолчанию и для тюнинга, таблица возможностей, поддерживаемые ABI, соглашения
о вызовах и объектные форматы, адресные пространства, модели перемещения и
кода (и значение по умолчанию, и маска поддерживаемых), модель исключений
(`NONE`, `DWARF`, `SJLJ`, `SEH`, `WASM`), модель раскрутки, порядок байтов,
ширина pointer/int/long/long long, выравнивание стека, максимальные атомарная
и векторная ширины, вид `va_list`, уровни выполнения (`USER`, `KERNEL`,
`HYPERVISOR`, `FIRMWARE`) и поддержка TLS.

Встроенные функции платформы несут собственный обратный вызов понижения,
который получает живой построитель IR:

```c
static NevercStatus NEVERC_CALL
lower_builtin(void *UserData,
              const NevercTargetBuiltinLoweringInvocation *In,
              NevercIRValueHandle *OutResult) {
  /* In->Core, In->Builder, In->Mutation, In->IRBuilder,
     In->ResultType, In->Arguments, In->ArgumentCount */
  return In->Builder->BuildCall(/* … */);
}
```

## ABI и соглашения о вызовах

ABI классифицирует сигнатуры функций:

```c
static NevercStatus NEVERC_CALL
classify(void *UserData, const NevercABIFunctionQuery *Query,
         NevercABIArgumentClassification *ReturnValue,
         NevercABIArgumentClassificationArray *Arguments) {
  ReturnValue->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  for (uint64_t I = 0; I != Arguments->Count; ++I) {
    NevercABIArgumentClassification *A = &Arguments->Data[I];
    A->Kind  = NEVERC_ABI_ARGUMENT_INDIRECT;
    A->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
  }
  return neverc_status_ok();
}
```

Виды аргументов: `DIRECT`, `EXTEND`, `INDIRECT`, `IGNORE`, `EXPAND`,
`INDIRECT_ALIASED` и `COERCE_AND_EXPAND`; флаги: `BYVAL`, `REALIGN`, `INREG`,
`SRET_AFTER_THIS`, `CAN_BE_FLATTENED`, `SIGN_EXTEND` и `PADDING_INREG`.
Приведение бывает `NONE`, `INTEGER`, `FLOAT` или `POINTER`, а
`COERCE_AND_EXPAND` поставляет массив `NevercABICoercionElement`.

Соглашение о вызовах спускается на уровень ниже и назначает фактические
места:

```c
static NevercStatus NEVERC_CALL
plan(void *UserData, const NevercCallingConventionQuery *Query,
     NevercCallingConventionPlan *Plan) {
  /* Query->TargetID, ->CallingConventionID, ->SchemaDigest, ->Function */
  /* Fill Plan->ReturnLocations and Plan->ArgumentLocations with
     NevercCallingConventionLocation records: REGISTER or STACK,
     ValueIndex, PieceOffset, Size, Alignment, RegisterNumber,
     StackOffset, and INDIRECT / BYVAL flags.                       */
  Plan->CalleeSavedRegisters = MySavedRegisters;
  Plan->StackAlignment       = 16;
  return neverc_status_ok();
}
```

`Query->SchemaDigest` — значение LOCKSTEP: `RegisterNumber` что-то значит
только относительно схемы, которую он называет. Полный разобранный пример см.
в [Пользовательские соглашения о вызовах](custom-callconv/README.ru.md) и
`pluginsdk/examples/CustomCallConvPlugin.c`.

## Маршруты генерации кода

Маршрут выбирается из канонического `NevercTargetKey`: идентификатор
платформы, части тройки, CPU, CPU для тюнинга, возможности, ABI, соглашение о
вызовах, объектный формат, модель перемещения, модель кода, уровень
выполнения, ширина указателя, порядок байтов и дайджест схемы.
Зарегистрируйте те рёбра, которые вы способны обслужить:

```c
NevercCodeGenEdgeDescriptor Edge = {0};
Edge.Header          = /* … */;
Edge.EdgeID          = MyEdgeID;
Edge.CanonicalName   = SV("com.example.mir-to-mc");
Edge.TargetID        = MyTargetID;
Edge.InputKind       = NEVERC_CODEGEN_PRODUCT_MIR;
Edge.OutputKind      = NEVERC_CODEGEN_PRODUCT_MC;
Edge.CompatibilityKey = SV("…");
Edge.ProviderID      = SV("com.example.backend");
Target->RegisterCodeGenEdge(Target->Context, RegistrarContext, &Edge);
```

Виды продукта: `IR`, `MIR`, `MC`, `ASSEMBLY`, `OBJECT_GRAPH`,
`OBJECT_IMAGE` и `CUSTOM`. Мелкозернистый маршрут —
`IR → MIR → MC → ObjectGraph → ObjectImage`.

Установка `NEVERC_CODEGEN_EDGE_COARSE` вместе с `CoarseLower` заменяет весь
пролёт `IR → ObjectImage` одним шагом:

```c
static NevercStatus NEVERC_CALL
coarse_lower(void *UserData, NevercTaskHandle Task,
             const NevercCodeGenRequest *Request,
             NevercCodeGenProductCandidate *OutCandidate) {
  /* Request->Target, ->Input, ->InputKind, ->OutputKind,
     ->OptimizationLevel, ->HasFinalIRProof                */
  OutCandidate->Kind      = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  OutCandidate->Artifact  = MyImage;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

Грубый маршрут всё равно проходит `neverc.codegen.product_verify` и
транзакционную фиксацию вывода. `VerifyProduct` вызывается с теми
обязательствами, выполнения которых от вас ожидает хост, —
`VERIFY_FINAL_IR`, `VERIFY_TARGET_KEY`, `VERIFY_PRODUCT_KIND`,
`VERIFY_PRODUCT_ID`, `VERIFY_STRUCTURE`, — так что провайдер не сможет тихо
проскочить шлюз, выбрав короткий путь.

## Построение MC

`MCUnit` содержит секции, символы, выражения, фрагменты, инструкции, операнды
и фиксапы. Чтение — итерация first/next:

```c
NevercMCUnitInfo Unit = {0};
Unit.Header = /* … */;
MC->GetUnitInfo(MC->Context, Task, UnitHandle, &Unit);

NevercMCSectionHandle Section;
MC->GetFirstSection(MC->Context, Task, UnitHandle, &Section);
while (!neverc_handle_is_null(Section)) {
  NevercMCFragmentHandle Fragment;
  MC->GetFirstFragment(MC->Context, Task, Section, &Fragment);
  /* … */
  MC->GetNextSection(MC->Context, Task, Section, &Section);
}
```

Изменение транзакционно, как и везде:

```c
NevercMCMutationHandle Mutation;
MC->BeginMutation(MC->Context, Task, Unit, &Mutation);
MC->CreateSection(MC->Context, Task, Mutation, &SectionDescriptor, &Section);
MC->CreateSymbol(MC->Context, Task, Mutation, &SymbolDescriptor, &Symbol);
MC->AppendInstruction(MC->Context, Task, Mutation, Section, &Instruction);
Status = MC->CommitMutation(MC->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MC->AbandonMutation(MC->Context, Task, Mutation);
```

Дескрипторы имеют область задачи и проверяются по поколению, поэтому
дескриптор из отменённого изменения отклоняется, а не используется повторно.

Флаги секций: `ALLOCATED`, `EXECUTABLE`, `WRITABLE`, `MERGEABLE` и `DEBUG`.
Связывания символов: `LOCAL`, `GLOBAL` и `WEAK`; типы: `NONE`, `FUNCTION`,
`OBJECT`, `SECTION` и `TLS`; определения: `UNDEFINED`, `SECTION`,
`ABSOLUTE` и `COMMON`. Выражения поддерживают унарные `PLUS`, `MINUS`, `NOT`
и бинарные `ADD`, `SUBTRACT`, `MULTIPLY`, `DIVIDE`, `AND`, `OR`, `XOR`,
`SHIFT_LEFT`, `SHIFT_RIGHT`. Передавайте `NEVERC_MC_AUTOMATIC_OFFSET` там, где
хотите, чтобы хост разместил что-то за вас.

`RegisterSchema` публикует MC-схему платформы, а `GetSchemaToken` /
`GetSchemaTokenInfo` переводят имя в LOCKSTEP-токен и обратно.

## Наблюдение за выпуском

Поток выпуска по порядку сообщает об одиннадцати видах событий. Подпишитесь
как наблюдатель и прочитайте событие:

```c
NevercMCEmissionEventInfo Event = {0};
Event.Header = /* … */;
Emission->GetEvent(Emission->Context, Frame, &Event);
/* Event.Kind, Event.Flags */
```

`Flags` говорит, какие части события заполнены: `HAS_SECTION`,
`HAS_INSTRUCTION`, `HAS_ENCODING`, `HAS_FIXUP`, `HAS_LAYOUT` и
`CAN_REPLACE_INSTRUCTION`. Проверяйте флаг, прежде чем читать соответствующее
поле: событие, у которого ещё нет кодировки, не обзаведётся ею оттого, что вы
спросили.

`GetLayoutSection`, `GetLayoutFragment`, `GetLayoutSymbol` и
`GetLayoutFixup` дают адреса и размеры, как только выставлен `HAS_LAYOUT`.

На `pre_instruction` и только когда выставлен `CAN_REPLACE_INSTRUCTION`, вы
можете подменить:

```c
Emission->BeginInstructionReplacement(Emission->Context, Frame, &Builder);
/* build the replacement through the MC builder */
Emission->PublishInstructionReplacement(Emission->Context, Frame, NewInstr);
```

`pluginsdk/examples/MCObserverPlugin.c` — это версия того же самого только для
чтения.

## Кодировщики, декодировщики и раскладка

Три регистрации расширяют бэкенд машинного кода, и все они ключуются
платформой и дайджестом схемы:

```c
MC->RegisterEncoder(MC->Context, RegistrarContext, &EncoderDescriptor);
MC->RegisterDecoder(MC->Context, RegistrarContext, &DecoderDescriptor);
MC->RegisterAsmBackend(MC->Context, RegistrarContext, &BackendDescriptor);
```

Кодировщик пишет через приёмник, а не возвращает буфер, — так владение
остаётся на стороне хоста:

```c
Sink->WriteBytes(Sink->Context, Bytes);
Sink->AddFixup(Sink->Context, &Fixup);
```

Декодировщик сообщает одно из `NEVERC_MC_DECODE_SUCCESS`, `_SOFT_FAIL`,
`_UNKNOWN` или `_FAIL`. Виды фиксапов описывают себя сами через
`NevercMCFixupKindInfo` флагами `PC_RELATIVE`, `SIGNED`, `RELAXABLE` и
`TARGET`.

Ослаблением (relaxation) владеет asm-бэкенд. Раскладка выпускает дайджест
доказательства, и **любое изменение после раскладки аннулирует это
доказательство** и вынуждает переразложить перед тем, как объект можно будет
записать, — тот же приём проверки по поколению, что и в графе компоновки.

## Ассемблер

Провайдер разбора потребляет исходные байты и публикует `MCUnit`:

```c
NevercAssemblyParseInputInfo In = {0};
In.Header = /* … */;
Asm->GetParseInput(Asm->Context, Frame, &In);

NevercAssemblyTokenInfo Token = {0};
Asm->PeekSourceToken(Asm->Context, Frame, &Token);
Asm->AdvanceSourceToken(Asm->Context, Frame);

const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
Asm->GetParseMCBuilder(Asm->Context, Frame, &MC, &Unit);
/* … build … */
Asm->PublishParsedMCUnit(Asm->Context, Frame, Unit, &Output);
```

Источники бывают либо `NEVERC_ASSEMBLY_SOURCE_BUFFER`, либо
`NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS`. Предобработанный ассемблер (`.S`)
сначала проходит обычный препроцессор фронтенда и приходит как отрендеренные
токены; чистый ассемблер (`.s`) попадает в разборщик напрямую как буфер.

Печатник идёт в обратную сторону: `GetPrintInput`, затем `WritePrintOutput` в
предоставленную транзакцию вывода, затем `PublishAssemblyOutput`. Запись
куда-либо ещё не поддерживается: проверка разбора/печати и шлюз фиксации хоста
выполняются до того, как байты станут видимыми, поэтому неудавшаяся печать не
оставляет после себя частичного файла.

## Объектные графы

`NevercObjectAPI` нормализует перемещаемый файл в секции, символы, перемещения
и COMDAT. Встроенные адаптеры покрывают ELF, COFF и Mach-O;
`RegisterFormat` добавляет ещё один.

```c
NevercObjectGraphInfo Info = {0};
Info.Header = /* … */;
Object->GetGraphInfo(Object->Context, Task, Graph, &Info);
/* Info.Target, .ObjectSchemaDigest, .Generation, .SectionCount,
   .SymbolCount, .RelocationCount, .ComdatCount, .HasLayoutProof */

NevercObjectSymbolHandle Symbol;
Object->GetFirstSymbol(Object->Context, Task, Graph, &Symbol);
while (!neverc_handle_is_null(Symbol)) {
  NevercObjectSymbolInfo SymInfo = {0};
  SymInfo.Header = /* … */;
  Object->GetSymbolInfo(Object->Context, Task, Symbol, &SymInfo);
  Object->GetNextSymbol(Object->Context, Task, Symbol, &Symbol);
}
```

Изменение следует схеме создать/заменить/переместить/стереть для всех четырёх
видов сущностей и подготавливается внутри `BeginMutation` …
`CommitMutation` / `AbandonMutation`.

Флаги секций: `ALLOCATED`, `EXECUTABLE`, `WRITABLE`, `MERGEABLE`, `STRINGS`,
`TLS`, `DEBUG`, `UNWIND`, `DISCARDABLE` и `RETAIN`. Цели перемещений:
`SYMBOL`, `SECTION`, `ABSOLUTE` или `FORMAT_EXTENSION`.

У каждого дескриптора есть тройка `ExtensionOwner` / `ExtensionVersion` /
`Extension`. Именно так формат сохраняет данные, под которые в нормализованном
графе нет поля: эти байты путешествуют вместе с сущностью и возвращаются при
записи, а не теряются в ходе преобразования туда и обратно.

### Регистрация формата

```c
NevercObjectFormatDescriptor Format = {0};
Format.Header           = /* … */;
Format.FormatID         = MyFormatID;
Format.CanonicalName    = SV("com.example.myfmt");
Format.SupportedTargets = MyTargets;
Format.DefaultExtension = SV(".mof");
Format.Flags            = NEVERC_OBJECT_FORMAT_CAN_PROBE |
                          NEVERC_OBJECT_FORMAT_CAN_READ  |
                          NEVERC_OBJECT_FORMAT_CAN_WRITE;
Format.Probe            = probe;
Format.Reader           = read;
Format.Writer           = write;
ObjectFormat->RegisterFormat(ObjectFormat->Context, RegistrarContext,
                             &Format);
```

`Probe` сообщает `Confidence` от 0 до
`NEVERC_OBJECT_PROBE_MAX_CONFIDENCE` (1000), распознанный
`NevercObjectArtifactKind` (`RELOCATABLE`, `ARCHIVE`, `EXECUTABLE_IMAGE`,
`SHARED_IMAGE`, `UNIVERSAL_BINARY`) и `ConsumedMinimum` — сколько байт ему
понадобилось, чтобы убедиться, с потолком
`NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM` (65536). Побеждает наибольшая
уверенность.

`Reader` получает граф и открытое изменение и заполняет их. `Writer` получает
граф, его доказательство раскладки и ограниченный двоичный построитель.

### Конвейер записи

1. прозондировать и прочитать байты в ObjectGraph;
2. выполнить графовые перехватчики `object.pre_write`;
3. разложить, затем выполнить `object.post_layout` (после любого изменения —
   переразложить);
4. записать ограниченный образ-кандидат;
5. выполнить двоичные перехватчики `object.post_write`;
6. выполнить запечатанный `object.final_verify` и атомарный
   `object.commit`.

Состояние образа движется `CANDIDATE` → `VERIFIED` → `COMMITTED` либо
`ABORTED` / `FAILED_PARTIAL`.

Наблюдатели получают мосты только для чтения; изменение, предпринятое из
наблюдателя, отклоняется с `NEVERC_STATUS_POLICY_VIOLATION`. Писателям и
перехватчикам после записи достаётся только ограниченный построитель
`NevercMutableBinaryAPI` — `Reserve`, `Write`, `WriteAt`, `Tell`, `ReadAt`,
`Insert`, `Append`, `Resize`. Переполнение, неудавшийся обратный вызов или
провалившаяся проверка прерывают подготовку, так что сбой никогда не оставляет
на диске половину файла.

`pluginsdk/examples/ObjectRewritePlugin.c` — полноценная транзакционная
перезапись.

## Правила

- Сверяйте дайджест схемы, прежде чем использовать любое LOCKSTEP-значение
  опкода, регистра, операнда, фиксапа, перемещения или соглашения о вызовах.
- Держите изменяемое состояние в предоставленном хостом состоянии process,
  session и task.
- Не кешируйте дескрипторы задач и заимствованные представления после
  возврата из обратного вызова.
- Вызывайте продолжение перехватчика не более одного раза и только в потоке
  обратного вызова.
- Каждый `BeginMutation` приходит ровно к одной фиксации или отмене.
- Переразложите после изменения уже разложенного MCUnit или ObjectGraph:
  старое доказательство раскладки устарело, и хост его отклонит.
- Проверяйте `NevercMCEmissionEventInfo.Flags`, прежде чем читать поле
  события, и подменяйте инструкцию только тогда, когда выставлен
  `CAN_REPLACE_INSTRUCTION`.
- Пишите вывод только через предоставленную транзакцию или байтовый приёмник.
- При сбое возвращайте исходный `NevercStatus` и не публикуйте ничего
  частичного.
- Объявляйте самые узкие правдивые модели параллелизма и реентерабельности.
- `codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
  `object.final_verify` и `object.commit` запечатаны. Только наблюдение.

Нормативные объявления см. в `PluginTarget.h`, `PluginMC.h`,
`PluginObject.h` и `Schema/PhaseSchema.json`, а `coverage.json` сопоставляет
каждую из этих стабильных фаз с её положительными, отрицательными,
замещающими, наблюдательными (только чтение) тестами и тестами запечатанных
шлюзов.
