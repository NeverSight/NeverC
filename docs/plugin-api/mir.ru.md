**Языки**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

[← ABI плагинов NeverC](README.ru.md)

# API MIR плагинов NeverC

`PluginMIR.h` открывает Machine IR: машинные функции, блоки, инструкции,
операнды, виртуальные и физические регистры, кадр стека, пул констант, таблицы
переходов и операнды памяти. Плагин подключает проходы к девяти стабильным
точкам генерации кода либо полностью заменяет понижение из IR в MIR.

Здесь встречаются две схемы. **Обобщённая схема** не зависит от платформы и
доступна всегда. Всё, что специфично для платформы — настоящий опкод, номер
регистра, класс регистров, — требует согласованной **схемы платформы**, и
каждое значение, которому она нужна, заявляет об этом флагом
`RequiresTargetSchema`.

## Интерфейсы

```c
#include "neverc/Plugin/PluginMIR.h"
```

| Интерфейс | Таблица | Слоты | Назначение |
|---|---|--:|---|
| `NEVERC_INTERFACE_MIR_{HIGH,LOW}` | `NevercMIRAPI` | 89 | Чтение и изменение машинных функций |
| `NEVERC_INTERFACE_MIR_ANALYSIS_{HIGH,LOW}` | `NevercMIRAnalysisAPI` | 11 | Живость, доминаторы, циклы, давление |
| `NEVERC_INTERFACE_MIR_PASS_{HIGH,LOW}` | `NevercMIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_MIR_PROVIDER_{HIGH,LOW}` | `NevercMIRProviderAPI` | 3 | Замена понижения IR → MIR |

Все четыре — `NEVERC_INTERFACE_STABLE` при мажорной версии 1. Сверяйте
возвращённый `TableSize` со смещением последнего используемого вами слота и
игнорируйте всё, что более новый хост дописал за ним.

## Фазы

Десять фаз MIR, девять из них — точки подключения проходов:

| Фаза | Когда |
|---|---|
| `neverc.mir.pass.post_isel` | После выбора инструкций |
| `neverc.mir.pass.post_legalize` | После легализации |
| `neverc.mir.pass.pre_scheduler` | Перед планированием |
| `neverc.mir.pass.post_scheduler` | После планирования |
| `neverc.mir.pass.pre_regalloc` | Перед распределением регистров |
| `neverc.mir.pass.post_regalloc` | После распределения регистров |
| `neverc.mir.pass.post_prolog_epilog` | После вставки пролога/эпилога |
| `neverc.mir.pass.preemit` | Прямо перед выпуском |
| `neverc.mir.pass.final` | Последний слот для плагинов |
| `neverc.mir.final_verify` | **Запечатанный** `MachineVerifier` хоста |

Все девять точек — `OBSERVABLE | INTERCEPTABLE`. Какие анализы существуют,
зависит от места подключения: интервалов жизни нет до распределения регистров,
а виртуальные регистры исчезают после него.

`neverc.mir.final_verify` запускает `MachineVerifier` из LLVM после последнего
слота плагина. Ни один плагин не может его отключить, заменить или пропустить.

## Схема

`Schema/PluginMIRSchema.inc` генерируется и включается из `PluginMIR.h`:

```c
#define NEVERC_MIR_SCHEMA_DIGEST          "6b523b20…"
#define NEVERC_MIR_ENTITY_COUNT           UINT32_C(4)
#define NEVERC_MIR_OPERAND_COUNT          UINT32_C(21)
#define NEVERC_MIR_GENERIC_OPCODE_COUNT   UINT32_C(266)
#define NEVERC_MIR_PROPERTY_COUNT         UINT32_C(11)
```

Четыре вызова описывают схему во время выполнения, и каждый возвращает
`NevercMIRSchemaEntry` с каноническим именем, лежащим в основе значением LLVM
и признаком того, нужна ли схема платформы:

```c
NevercMIRSchemaEntry Entry = {0};
Entry.Header = /* … */;
MIR->GetGenericOpcodeInfo(MIR->Context, Opcode, &Entry);
/* Entry.StableID, .LLVMValue, .RequiresTargetSchema, .CanonicalName */
```

Остальные — `GetEntityInfo`, `GetOperandKindInfo` и
`GetMachinePropertyInfo`. `GetSchemaDigest` возвращает дайджест фактически
используемого соответствия: сверьте его с `NEVERC_MIR_SCHEMA_DIGEST`, прежде
чем доверять любому специфичному для платформы значению.

## Чтение MIR

Обход идёт по двусвязному списку, а не через курсор:

```c
NevercMachineBasicBlockHandle Block;
MIR->GetFirstBasicBlock(MIR->Context, Task, Function, &Block);

while (!neverc_handle_is_null(Block)) {
  NevercMachineInstrHandle Instruction;
  MIR->GetFirstInstruction(MIR->Context, Task, Block, &Instruction);

  while (!neverc_handle_is_null(Instruction)) {
    NevercMIRInstructionInfo Info = {0};
    Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_MIR_API_MAJOR,
                                         NEVERC_MIR_API_MINOR, 0};
    MIR->GetInstructionInfo(MIR->Context, Task, Instruction, &Info);
    /* Info.StableOpcode, .TargetOpcode, .RequiresTargetSchema,
       .IsBranch, .IsCall, .IsReturn, .IsTerminator, .IsBarrier,
       .IsInlineAssembly, .IsDebugInstruction, .IsPseudo, .IsBundle,
       .Flags, .OperandCount, .MemoryOperandCount                    */
    MIR->GetNextInstruction(MIR->Context, Task, Instruction, &Instruction);
  }
  MIR->GetNextBasicBlock(MIR->Context, Task, Block, &Block);
}
```

`CollectBasicBlocks` и `CollectInstructions` вместо этого заполняют
ограниченный массив, а `GetLastBasicBlock` / `GetPreviousInstruction` идут
назад. Запросы к графу потока управления — это `GetSuccessorCount` /
`GetSuccessor` (он выдаёт `NevercMIRCFGEdge`, несущий вероятность ветвления
парой «числитель/знаменатель»), `GetPredecessorCount` / `GetPredecessor`, а
также `GetLiveInCount` / `GetLiveIn`.

Флаги инструкций — это 18 бит от `FRAME_SETUP` и `FRAME_DESTROY` через группу
fast-math до `NO_MERGE`, `UNPREDICTABLE` и `NO_CONVERGENT`.

## Операнды

Все 21 вид операндов возвращается через одно размеченное объединение:

```c
NevercMIROperandValue Value = {0};
Value.Header = /* … */;
MIR->GetOperandValue(MIR->Context, Task, Operand, &Value);

switch (Value.Kind) {
case NEVERC_MIR_OPERAND_REGISTER:
  /* Value.Payload.Register.Number, .SubRegister, .Flags, .IsPhysical */
  break;
case NEVERC_MIR_OPERAND_IMMEDIATE:
  /* Value.Payload.Immediate */
  break;
case NEVERC_MIR_OPERAND_MACHINE_BASIC_BLOCK:
  /* Value.Payload.BasicBlock */
  break;
case NEVERC_MIR_OPERAND_GLOBAL_ADDRESS:
  /* Value.Payload.SymbolOffset.Symbol, .Offset */
  break;
}
```

Виды таковы: `REGISTER`, `IMMEDIATE`, `C_IMMEDIATE`, `FP_IMMEDIATE`,
`MACHINE_BASIC_BLOCK`, `FRAME_INDEX`, `CONSTANT_POOL_INDEX`, `TARGET_INDEX`,
`JUMP_TABLE_INDEX`, `EXTERNAL_SYMBOL`, `GLOBAL_ADDRESS`, `BLOCK_ADDRESS`,
`REGISTER_MASK`, `REGISTER_LIVE_OUT`, `METADATA`, `MC_SYMBOL`, `CFI_INDEX`,
`INTRINSIC_ID`, `PREDICATE`, `SHUFFLE_MASK` и `DBG_INSTR_REF`.

Флаги регистрового операнда: `DEF`, `IMPLICIT`, `KILL`, `DEAD`, `UNDEF`,
`EARLY_CLOBBER`, `RENAMABLE`, `INTERNAL_READ` и `DEBUG`. Вещественные
непосредственные значения приходят как `NevercMIRWordView` — слова в
прямом порядке от младшего плюс битовая ширина и одна из семи вещественных
семантик от `IEEE_HALF` до `PPC_DOUBLE_DOUBLE`, — так что никакой вещественный
тип хоста не задействован.

## Регистры

Виртуальный регистр описывается низкоуровневым типом и назначением:

```c
NevercMIRVirtualRegisterDesc Desc = {0};
Desc.Header             = /* … */;
Desc.AssignmentKind     = NEVERC_MIR_REG_ASSIGNMENT_CLASS;
Desc.TargetID           = RegisterClassID;   /* needs the target schema */
Desc.Type.Kind          = NEVERC_MIR_LLT_SCALAR;
Desc.Type.ScalarSizeInBits = 32;

uint32_t Register = 0;
MIR->CreateVirtualRegister(MIR->Context, Task, Mutation, &Desc, &Register);
```

Виды назначения: `NONE`, `GENERIC`, `CLASS` и `BANK`; виды низкоуровневых
типов: `INVALID`, `SCALAR`, `POINTER`, `VECTOR` и `POINTER_VECTOR`, а
`IsScalable` — для масштабируемых векторов.

Запросы определений и использований — `GetRegisterDefCount` /
`GetRegisterDef` и `GetRegisterUseCount` / `GetRegisterUse`;
`ReplaceRegister` переписывает каждое вхождение одной подготовленной
операцией. Живые входы уровня функции связывают физический регистр с
виртуальным, в который он был скопирован (`GetFunctionLiveIn`,
`AddFunctionLiveIn`, `RemoveFunctionLiveIn`), а живые входы уровня блока несут
маску полос (`AddBasicBlockLiveIn`, `RemoveBasicBlockLiveIn`).

## Кадр стека

```c
int32_t FrameIndex = 0;
MIR->CreateStackObject(MIR->Context, Task, Mutation, /*Size=*/16,
                       /*Alignment=*/8, /*IsSpillSlot=*/NEVERC_FALSE,
                       /*StackID=*/0, &FrameIndex);
```

`CreateFixedStackObject` размещает объект по известному смещению (с
`IsImmutable` и `IsAliased`), а `CreateVariableSizedStackObject` берёт на себя
динамическое выделение. `SetFrameObjectSize`, `SetFrameObjectAlignment` и
`SetFrameObjectOffset` правят объект уже после.

`NevercMIRFrameObjectInfo` сообщает `Index`, `Flags`, `Size`, `Offset`,
`Alignment` и `StackID`; флаги кадра — `FIXED`, `SPILL_SLOT`,
`VARIABLE_SIZED`, `IMMUTABLE`, `ALIASED`, `DEAD` и `PREALLOCATED`. Состояние
сохраняемых вызываемым регистров читается через `GetCalleeSaved` и заменяется
целиком через `SetCalleeSaved`.

## Пул констант, таблицы переходов, операнды памяти

Записи пула констант несут своё значение как `NevercMIRWordView`, поэтому
целочисленная и вещественная записи имеют одинаковую форму:

```c
NevercMIRConstantPoolEntryDesc Desc = {0};
Desc.Header       = /* … */;
Desc.Kind         = NEVERC_MIR_CONSTANT_INTEGER;
Desc.Alignment    = 8;
Desc.Value.Data   = Words;
Desc.Value.Count  = 1;
Desc.Value.BitWidth = 64;

uint32_t Index = 0;
MIR->CreateConstantPoolEntry(MIR->Context, Task, Mutation, &Desc, &Index);
```

Таблицы переходов создаются из массива блоков-назначений с одним из семи видов
записи (`BLOCK_ADDRESS`, `GP_REL64_BLOCK_ADDRESS`, `GP_REL32_BLOCK_ADDRESS`,
`LABEL_DIFFERENCE32`, `LABEL_DIFFERENCE64`, `INLINE`, `CUSTOM32`).

Операнды памяти — самый насыщенный дескриптор: флаги (`LOAD`, `STORE`,
`VOLATILE`, `NON_TEMPORAL`, `DEREFERENCEABLE`, `INVARIANT` плюс три флага
платформы), размер и выравнивание, указатель одного из девяти видов
(`IR_VALUE`, `FIXED_STACK`, `STACK`, `CONSTANT_POOL`, `JUMP_TABLE`, `GOT`,
`UNKNOWN_STACK`, `TARGET_CUSTOM`, `UNKNOWN`), атомарные упорядочения для
успеха и неудачи, область синхронизации, а также ссылки TBAA, alias-scope,
no-alias и range. Присоединить такой операнд можно через
`AddInstructionMemoryOperand`.

## Транзакционное изменение

Каждое изменение подготавливается внутри мутации, привязанной к одной
машинной функции:

```c
NevercMIRMutationHandle Mutation;
MIR->BeginMutation(MIR->Context, Task, Function, &Mutation);

NevercMIRInstructionOpcode Opcode = {0};
Opcode.StableOpcode = MyGenericOpcode;

NevercMachineInstrHandle New;
MIR->CreateInstruction(MIR->Context, Task, Mutation, Block,
                       /*InsertBefore=*/Terminator, Opcode, &New);

NevercMIROperandValue Op = {0};
Op.Header = /* … */;
Op.Kind   = NEVERC_MIR_OPERAND_IMMEDIATE;
Op.Payload.Immediate = 42;
MIR->AppendOperand(MIR->Context, Task, Mutation, New, &Op, &Operand);

Status = MIR->CommitMutation(MIR->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MIR->AbortMutation(MIR->Context, Task, Mutation);
MIR->EndMutation(MIR->Context, Task, Mutation);
```

Фиксация выполняет предварительную структурную проверку, а затем верификатор
Machine IR. Недопустимые операнды, сломанный граф потока управления,
обобщённые опкоды там, где схема платформы требует настоящего, или
неподдерживаемое заявление о свойстве — всё это атомарно откатывается. Отмена
восстанавливает порядок блоков, инструкции, операнды, рёбра графа потока
управления и машинные свойства ровно такими, какими они были.

`EndMutation` освобождает дескриптор и отделён от фиксации и отмены —
вызывайте его на обоих путях.

Подготавливаемые операции: `CreateBasicBlock`, `MoveBasicBlock`,
`EraseBasicBlock`, `CreateInstruction`, `MoveInstruction`,
`EraseInstruction`, `AppendOperand`, `SetOperandValue`,
`SetInstructionFlags`, `AddCFGEdge`, `RemoveCFGEdge`, приведённые выше вызовы
для регистров и кадра, вызовы для пула констант и таблиц переходов, вызовы для
операндов памяти и `SetMachinePropertyWithProof`.

## Машинные свойства требуют доказательства

Одиннадцать машинных свойств — `IS_SSA`, `NO_PH_IS`, `TRACKS_LIVENESS`,
`NO_V_REGS`, `FAILED_I_SEL`, `LEGALIZED`, `REG_BANK_SELECTED`, `SELECTED`,
`TIED_OPS_REWRITTEN`, `FAILS_VERIFICATION` и `TRACKS_DEBUG_USER_VALUES` —
читаются свободно, но никогда свободно не устанавливаются:

```c
NevercMIRPropertyProof Proof = {0};
Proof.Header   = /* … */;
Proof.Property = NEVERC_MIR_PROPERTY_IS_SSA;
Proof.Kind     = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
Proof.Value    = NEVERC_FALSE;
MIR->SetMachinePropertyWithProof(MIR->Context, Task, Mutation, &Proof);
```

Доказательство бывает двух видов. `INVALIDATION` снимает свойство, чьи
предпосылки нарушило ваше изменение, — это всегда принимается, потому что
отказаться от гарантии безопасно. `STRUCTURAL_CHECK` просит хост проверить
свойство перед его установлением, так что заявить `IS_SSA` стоит настоящей
проверки, а не обещания.

## Проходы

```c
NevercMIRPassDescriptor Pass = {0};
Pass.Header = (NevercABITableHeader){sizeof(Pass), NEVERC_MIR_PASS_API_MAJOR,
                                     NEVERC_MIR_PASS_API_MINOR, 0};
Pass.PassID        = SV("example.machine-pass");
Pass.Phase         = (NevercInterfaceID){NEVERC_PHASE_MIR_PASS_PREEMIT_HIGH,
                                         NEVERC_PHASE_MIR_PASS_PREEMIT_LOW};
Pass.Level         = NEVERC_MIR_PASS_LEVEL_FUNCTION;
Pass.Deterministic = NEVERC_TRUE;
Pass.Run           = run_machine_function;
PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
```

Это дословно `pluginsdk/examples/MachinePass.c`. Уровни — `MODULE`,
`FUNCTION` и `BASIC_BLOCK`. `RequiredAnalyses` и `PreservedAnalyses` — массивы
`NevercMIRBuiltinAnalysis`, а `RequiredTargetSchemaDigest` заставляет проход
отказаться работать против схемы, под которую он не собирался.

Вызов несёт `Task`, `Phase`, `PassID`, `Level`, действительные для этого
уровня `Function` и `BasicBlock`, таблицы `Core` и `Analyses`, а также
активный `TargetSchemaDigest`.

Сообщайте о сохранении через `OutPreserved` — `NEVERC_MIR_PRESERVE_NONE`,
`_CFG` или `_ALL`, плюс явный список в `Analyses`. Заявить `PRESERVE_ALL`
после зафиксированного изменения нельзя — это отклоняется.

Функциональные проходы могут выполняться в параллельных разделах генерации
кода; проходы уровня модуля выполняются на сериализованных барьерах конвейера.
Объявленные плагином модели параллелизма и реентерабельности по-прежнему
управляют вашим собственным состоянием.

## Анализы

Шесть встроенных: `LIVE_INTERVALS`, `LIVE_VARIABLES`, `SLOT_INDEXES`,
`DOMINATOR_TREE`, `LOOP_INFO` и `REGISTER_PRESSURE`.

```c
NevercMIRAnalysisResultHandle Intervals;
Analyses->QueryBuiltin(Analyses->Context, Task,
                       NEVERC_MIR_ANALYSIS_LIVE_INTERVALS, Function,
                       &Intervals);

uint64_t SegmentCount = 0;
Analyses->GetLiveIntervalSegmentCount(Analyses->Context, Task, Intervals,
                                      Register, &SegmentCount);
for (uint64_t I = 0; I != SegmentCount; ++I) {
  NevercMIRLiveRangeSegment Segment;
  Analyses->GetLiveIntervalSegment(Analyses->Context, Task, Intervals,
                                   Register, I, &Segment);
  /* Segment.Start, Segment.End */
}
```

Доступны также: `DominatorTreeDominates`, `GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`, `GetSlotIndex`, `IsRegisterLiveInBlock` и
`GetRegisterPressureSetCount` / `GetRegisterPressure`.

Доступность зависит от точки подключения. Запрос интервалов жизни на
`post_isel` завершится с `NEVERC_STATUS_CAPABILITY_UNAVAILABLE`, потому что
лежащего в основе анализа LLVM ещё не существует. Зафиксированное изменение
аннулирует те дескрипторы результатов, которых оно касается.

## Замена понижения из IR в MIR

```c
NevercIRToMIRInputInfo In = {0};
In.Header = /* … */;
Provider->GetIRToMIRInput(Provider->Context, Frame, Frame->Input, &In);
/* In.Module, .IR, .TargetID, .CompatibilityKey, .TargetSchemaDigest,
   .DefinedFunctionCount */

const NevercMIRAPI *MIR;
NevercMachineFunctionHandle MF;
Provider->GetOrCreateMachineFunction(Provider->Context, Frame, IRFunction,
                                     &MIR, &MF);
/* … build the machine function … */

NevercMIRModuleCoverageDescriptor Coverage = {0};
Coverage.Header              = /* … */;
Coverage.HandlesGlobals      = NEVERC_TRUE;
Coverage.HandlesConstructors = NEVERC_TRUE;
Coverage.HandlesDebugInfo    = NEVERC_FALSE;
Coverage.HandlesUnwind       = NEVERC_FALSE;
Provider->PublishMIRModule(Provider->Context, Frame, &Coverage, &Output);
```

Дескриптор покрытия — это то, что удерживает частичного провайдера в рамках
честности: объявляйте только то, что вы действительно понизили, и хост сам
разберётся с остальным, вместо того чтобы молча потерять глобальные
переменные, конструкторы, отладочную информацию или таблицы раскрутки.

## Пример

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

Используйте тот суффикс модуля, который CMake создал для вашей платформы.

## Правила

- Не удерживайте дескрипторы задач, дескрипторы MIR и заимствованные
  представления после возврата из обратного вызова и никогда не изготавливайте
  сами значение дескриптора или номер опкода LLVM.
- Сверяйте `GetSchemaDigest` со вшитым в вашу сборку дайджестом, прежде чем
  использовать любое значение с выставленным флагом `RequiresTargetSchema`.
- Изменяйте только внутри мутации. Каждый `BeginMutation` приходит ровно к
  одному `EndMutation` — после фиксации или отмены.
- Не заявляйте машинное свойство без доказательства и предпочитайте
  `INVALIDATION` вместо `STRUCTURAL_CHECK`, когда ваше изменение от свойства
  отказалось.
- Никогда не заявляйте `NEVERC_MIR_PRESERVE_ALL` после зафиксированного
  изменения.
- Проверьте, что нужный вам анализ действительно доступен в выбранной точке
  подключения.
- Инициализируйте каждый заголовок таблицы и каждое зарезервированное поле;
  возвращайте статусы через границу C и никогда не позволяйте исключению C++
  её пересечь.
- `neverc.mir.final_verify` запечатана. Она выполняется в любом случае.

Нормативные объявления, константы схемы, политики фаз и свидетельства
покрытия см. в `PluginMIR.h`, `Schema/PluginMIRSchema.inc`,
`Schema/PhaseSchema.json` и `coverage.json`.
