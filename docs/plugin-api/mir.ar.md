<div dir="rtl">

**اللغات**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

[← واجهة NeverC الثنائية للإضافات](README.ar.md)

# واجهة MIR لإضافات NeverC

يكشف [`PluginMIR.h`] عن Machine IR: دوال الآلة، والكتل، والتعليمات، والمعاملات،
والسجلات الافتراضية والفيزيائية، وإطار المكدس، ومجمّع الثوابت، وجداول القفز،
ومعاملات الذاكرة. وتُعلِّق الإضافة مروراتها على تسعة خطّافات مستقرة في توليد
الشيفرة، أو تستبدل خفض IR إلى MIR بالكامل.

يلتقي هنا مخططان. **المخطط العام** مستقل عن الهدف ومتاح دائمًا. وأي شيء خاص
بالهدف — كود تشغيلي حقيقي، أو رقم سجل، أو صنف سجلات — يتطلّب **مخطط هدف**
متفاوَضًا عليه، وكل قيمة تحتاج إليه تُعلن ذلك عبر الراية
`RequiresTargetSchema`.

## الواجهات

```c
#include "neverc/Plugin/PluginMIR.h"
```

| الواجهة | الجدول | الخانات | الغرض |
|---|---|--:|---|
| `NEVERC_INTERFACE_MIR_{HIGH,LOW}` | `NevercMIRAPI` | 89 | قراءة دوال الآلة وتعديلها |
| `NEVERC_INTERFACE_MIR_ANALYSIS_{HIGH,LOW}` | `NevercMIRAnalysisAPI` | 11 | الحيوية، والمُهيمنات، والحلقات، والضغط |
| `NEVERC_INTERFACE_MIR_PASS_{HIGH,LOW}` | `NevercMIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_MIR_PROVIDER_{HIGH,LOW}` | `NevercMIRProviderAPI` | 3 | استبدال خفض `IR → MIR` |

الأربع جميعًا `NEVERC_INTERFACE_STABLE` عند الإصدار الرئيسي 1. قارن قيمة
`TableSize` المُعادة بإزاحة آخر خانة تستعملها، وتجاهل أي شيء ألحقه مُضيف أحدث
بعدها.

## المراحل

عشر مراحل MIR، تسع منها خطّافات للمرورات:

| المرحلة | متى |
|---|---|
| `neverc.mir.pass.post_isel` | بعد اختيار التعليمات |
| `neverc.mir.pass.post_legalize` | بعد التقنين |
| `neverc.mir.pass.pre_scheduler` | قبل الجدولة |
| `neverc.mir.pass.post_scheduler` | بعد الجدولة |
| `neverc.mir.pass.pre_regalloc` | قبل تخصيص السجلات |
| `neverc.mir.pass.post_regalloc` | بعد تخصيص السجلات |
| `neverc.mir.pass.post_prolog_epilog` | بعد إدراج المقدمة/الخاتمة |
| `neverc.mir.pass.preemit` | قُبيل الإصدار مباشرة |
| `neverc.mir.pass.final` | آخر خانة متاحة للإضافات |
| `neverc.mir.final_verify` | `MachineVerifier` **المختوم** لدى المُضيف |

الخطّافات التسعة كلها `OBSERVABLE | INTERCEPTABLE`. وأي التحليلات تكون موجودة
يعتمد على موضع تعليقك: فترات الحياة غير متاحة قبل تخصيص السجلات، والسجلات
الافتراضية تختفي بعده.

تشغّل `neverc.mir.final_verify` مُدقِّق `MachineVerifier` من LLVM بعد آخر خانة
للإضافات. ولا تستطيع أي إضافة تعطيله أو استبداله أو تخطّيه.

## المخطط

[`Schema/PluginMIRSchema.inc`] مُولَّد ويُضمّنه [`PluginMIR.h`]:

```c
#define NEVERC_MIR_SCHEMA_DIGEST          "6b523b20…"
#define NEVERC_MIR_ENTITY_COUNT           UINT32_C(4)
#define NEVERC_MIR_OPERAND_COUNT          UINT32_C(21)
#define NEVERC_MIR_GENERIC_OPCODE_COUNT   UINT32_C(266)
#define NEVERC_MIR_PROPERTY_COUNT         UINT32_C(11)
```

وأربعة استدعاءات تصف المخطط أثناء التشغيل، ويعيد كلٌّ منها
`NevercMIRSchemaEntry` يحمل الاسم القياسي، وقيمة LLVM الأساسية، وما إذا كان
مخطط الهدف لازمًا:

```c
NevercMIRSchemaEntry Entry = {0};
Entry.Header = /* … */;
MIR->GetGenericOpcodeInfo(MIR->Context, Opcode, &Entry);
/* Entry.StableID, .LLVMValue, .RequiresTargetSchema, .CanonicalName */
```

والبقية هي `GetEntityInfo` و`GetOperandKindInfo` و
`GetMachinePropertyInfo`. ويعيد `GetSchemaDigest` بصمة الربط المستعمل فعليًا
— قارنها بـ`NEVERC_MIR_SCHEMA_DIGEST` قبل أن تثق بأي قيمة خاصة بالهدف.

## قراءة MIR

الاجتياز عبر قائمة مزدوجة الوصل، لا عبر مؤشر تصفّح:

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

أما `CollectBasicBlocks` و`CollectInstructions` فتملآن مصفوفة محدودة بدلًا من
ذلك، و`GetLastBasicBlock` / `GetPreviousInstruction` تسيران إلى الوراء.
واستعلامات رسم تدفّق التحكم هي `GetSuccessorCount` / `GetSuccessor` (التي
تُنتج `NevercMIRCFGEdge` تحمل احتمال التفرّع كزوج بسط ومقام)، و
`GetPredecessorCount` / `GetPredecessor`، و`GetLiveInCount` / `GetLiveIn`.

ورايات التعليمات هي البتات الثماني عشرة الممتدة من `FRAME_SETUP` و
`FRAME_DESTROY` مرورًا بمجموعة fast-math وصولًا إلى `NO_MERGE` و
`UNPREDICTABLE` و`NO_CONVERGENT`.

## المعاملات

تعود أنواع المعاملات الواحد والعشرون كلها عبر اتحاد موسوم واحد:

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

الأنواع هي `REGISTER` و`IMMEDIATE` و`C_IMMEDIATE` و`FP_IMMEDIATE`
و`MACHINE_BASIC_BLOCK` و`FRAME_INDEX` و`CONSTANT_POOL_INDEX` و`TARGET_INDEX`
و`JUMP_TABLE_INDEX` و`EXTERNAL_SYMBOL` و`GLOBAL_ADDRESS` و`BLOCK_ADDRESS`
و`REGISTER_MASK` و`REGISTER_LIVE_OUT` و`METADATA` و`MC_SYMBOL` و`CFI_INDEX`
و`INTRINSIC_ID` و`PREDICATE` و`SHUFFLE_MASK` و`DBG_INSTR_REF`.

ورايات معامل السجل هي `DEF` و`IMPLICIT` و`KILL` و`DEAD` و`UNDEF` و
`EARLY_CLOBBER` و`RENAMABLE` و`INTERNAL_READ` و`DEBUG`. وتصل القيم الفورية
العائمة على هيئة `NevercMIRWordView` — كلمات بترتيب البايت الأصغر أولًا، مع
عرض بالبتات وواحدة من سبع دلالات عائمة من `IEEE_HALF` إلى
`PPC_DOUBLE_DOUBLE` — فلا يتدخّل أي نوع عائم من أنواع المُضيف.

## السجلات

يُوصف السجل الافتراضي بنوع منخفض المستوى مع تخصيص:

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

أنواع التخصيص هي `NONE` و`GENERIC` و`CLASS` و`BANK`؛ وأنواع الأنواع منخفضة
المستوى هي `INVALID` و`SCALAR` و`POINTER` و`VECTOR` و`POINTER_VECTOR`، مع
`IsScalable` للمتجهات القابلة للتوسّع.

واستعلامات التعريف والاستعمال هي `GetRegisterDefCount` / `GetRegisterDef` و
`GetRegisterUseCount` / `GetRegisterUse`؛ ويعيد `ReplaceRegister` كتابة كل
ورود في عملية مُجهَّزة واحدة. أما المدخلات الحيّة على مستوى الدالة فتقرن سجلًا
فيزيائيًا بالسجل الافتراضي الذي نُسخ إليه (`GetFunctionLiveIn` و
`AddFunctionLiveIn` و`RemoveFunctionLiveIn`)، بينما تحمل المدخلات الحيّة على
مستوى الكتلة قناع مسارات (`AddBasicBlockLiveIn` و`RemoveBasicBlockLiveIn`).

## إطار المكدس

```c
int32_t FrameIndex = 0;
MIR->CreateStackObject(MIR->Context, Task, Mutation, /*Size=*/16,
                       /*Alignment=*/8, /*IsSpillSlot=*/NEVERC_FALSE,
                       /*StackID=*/0, &FrameIndex);
```

يضع `CreateFixedStackObject` كائنًا عند إزاحة معلومة (مع `IsImmutable` و
`IsAliased`)، ويتكفّل `CreateVariableSizedStackObject` بالتخصيص الديناميكي.
وتُعدِّل `SetFrameObjectSize` و`SetFrameObjectAlignment` و
`SetFrameObjectOffset` كائنًا بعد إنشائه.

ويُبلِّغ `NevercMIRFrameObjectInfo` عن `Index` و`Flags` و`Size` و`Offset` و
`Alignment` و`StackID`؛ ورايات الإطار هي `FIXED` و`SPILL_SLOT` و
`VARIABLE_SIZED` و`IMMUTABLE` و`ALIASED` و`DEAD` و`PREALLOCATED`. وتُقرأ حالة
السجلات المحفوظة لدى المُستدعَى بـ`GetCalleeSaved` وتُستبدل جملةً بـ
`SetCalleeSaved`.

## مجمّع الثوابت وجداول القفز ومعاملات الذاكرة

تحمل مُدخلات مجمّع الثوابت قيمتها بوصفها `NevercMIRWordView`، فيأخذ المُدخل
الصحيح والمُدخل العائم الشكل نفسه:

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

وتُنشأ جداول القفز من مصفوفة كتل وجهة، بأحد سبعة أنواع مُدخلات
(`BLOCK_ADDRESS` و`GP_REL64_BLOCK_ADDRESS` و`GP_REL32_BLOCK_ADDRESS` و
`LABEL_DIFFERENCE32` و`LABEL_DIFFERENCE64` و`INLINE` و`CUSTOM32`).

ومعاملات الذاكرة هي أغنى الواصفات: رايات (`LOAD` و`STORE` و`VOLATILE` و
`NON_TEMPORAL` و`DEREFERENCEABLE` و`INVARIANT`، إضافة إلى ثلاث رايات خاصة
بالهدف)، وحجم ومحاذاة، ومؤشر من أحد تسعة أنواع (`IR_VALUE` و`FIXED_STACK` و
`STACK` و`CONSTANT_POOL` و`JUMP_TABLE` و`GOT` و`UNKNOWN_STACK` و
`TARGET_CUSTOM` و`UNKNOWN`)، وترتيبان ذرّيان للنجاح والفشل، ونطاق مزامنة،
ومراجع TBAA وalias-scope وno-alias وrange. وتُلحَق بـ
`AddInstructionMemoryOperand`.

## التعديل المعاملاتي

كل تغيير يُجهَّز داخل تعديل مربوط بدالة آلة واحدة:

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

يُجري الإيداع فحصًا بنيويًا تمهيديًا ثم مُدقِّق Machine IR. والمعاملات غير
الصالحة، أو رسم تدفّق تحكم مكسور، أو أكواد تشغيلية عامة حيث يطلب مخطط الهدف
كودًا حقيقيًا، أو ادّعاء خاصية غير مدعومة — كلها تُتراجَع ذرّيًا. والإجهاض
يستعيد ترتيب الكتل والتعليمات والمعاملات وحوافّ رسم التدفّق وخصائص الآلة كما
كانت تمامًا.

ويحرّر `EndMutation` المقبض، وهو منفصل عن الإيداع والإجهاض — فاستدعِه في
المسارين كليهما.

والعمليات المُجهَّزة هي `CreateBasicBlock` و`MoveBasicBlock` و
`EraseBasicBlock` و`CreateInstruction` و`MoveInstruction` و
`EraseInstruction` و`AppendOperand` و`SetOperandValue` و
`SetInstructionFlags` و`AddCFGEdge` و`RemoveCFGEdge`، واستدعاءات السجلات
والإطار المذكورة أعلاه، واستدعاءات مجمّع الثوابت وجداول القفز، واستدعاءات
معاملات الذاكرة، و`SetMachinePropertyWithProof`.

## خصائص الآلة تحتاج إلى إثبات

خصائص الآلة الإحدى عشرة — `IS_SSA` و`NO_PH_IS` و`TRACKS_LIVENESS` و
`NO_V_REGS` و`FAILED_I_SEL` و`LEGALIZED` و`REG_BANK_SELECTED` و`SELECTED` و
`TIED_OPS_REWRITTEN` و`FAILS_VERIFICATION` و`TRACKS_DEBUG_USER_VALUES` —
تُقرأ بحرية لكنها لا تُضبط بحرية أبدًا:

```c
NevercMIRPropertyProof Proof = {0};
Proof.Header   = /* … */;
Proof.Property = NEVERC_MIR_PROPERTY_IS_SSA;
Proof.Kind     = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
Proof.Value    = NEVERC_FALSE;
MIR->SetMachinePropertyWithProof(MIR->Context, Task, Mutation, &Proof);
```

والإثبات من نوعين. يمحو `INVALIDATION` خاصية نقض تغييرُك افتراضاتِها — وهذا
مقبول دائمًا، لأن التخلّي عن ضمانة أمرٌ آمن. أما `STRUCTURAL_CHECK` فيطلب من
المُضيف التحقق من الخاصية قبل تثبيتها، فادّعاء `IS_SSA` يكلّف فحصًا حقيقيًا لا
مجرد وعد.

## المرورات

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

هذا هو [`pluginsdk/examples/MachinePass.c`] حرفيًا. والمستويات هي `MODULE` و
`FUNCTION` و`BASIC_BLOCK`. و`RequiredAnalyses` و`PreservedAnalyses` مصفوفتان
من `NevercMIRBuiltinAnalysis`، و`RequiredTargetSchemaDigest` يجعل المرور يرفض
العمل مقابل مخطط لم يُبنَ له.

ويحمل الاستدعاء `Task` و`Phase` و`PassID` و`Level`، و`Function` و
`BasicBlock` الصالحين لذلك المستوى، وجدولَي `Core` و`Analyses`، وبصمة
`TargetSchemaDigest` النشطة.

وأبلِغ عن الحفاظ عبر `OutPreserved` — `NEVERC_MIR_PRESERVE_NONE` أو `_CFG` أو
`_ALL`، إضافة إلى قائمة صريحة في `Analyses`. وادّعاء `PRESERVE_ALL` بعد تعديل
مُودَع مرفوض.

ويمكن لمرورات الدوال أن تعمل في أقسام متوازية من توليد الشيفرة؛ أما مرورات
مستوى الوحدة فتعمل عند حواجز مُسلسَلة في خط الأنابيب. ويظل نموذجا التزامن
وإعادة الدخول المُعلَنان من الإضافة يحكمان حالتك الخاصة.

## التحليلات

ستة مدمجة: `LIVE_INTERVALS` و`LIVE_VARIABLES` و`SLOT_INDEXES` و
`DOMINATOR_TREE` و`LOOP_INFO` و`REGISTER_PRESSURE`.

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

ومتاح أيضًا: `DominatorTreeDominates` و`GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock` و`GetSlotIndex` و`IsRegisterLiveInBlock` و
`GetRegisterPressureSetCount` / `GetRegisterPressure`.

والإتاحة تعتمد على الخطّاف. فطلب فترات الحياة عند `post_isel` يفشل بـ
`NEVERC_STATUS_CAPABILITY_UNAVAILABLE` لأن تحليل LLVM الأساسي لم يوجد بعد.
والتعديل المُودَع يُبطل مقابض النتائج التي يمسّها.

## استبدال خفض IR إلى MIR

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

واصف التغطية هو ما يُبقي المزوِّد الجزئي صادقًا: أعلِن فقط ما خفضته فعلًا،
وسيتولّى المُضيف الباقي بنفسه بدل أن تسقط بصمت المتغيرات العامة أو المُنشئات أو
معلومات التنقيح أو جداول فك الكدسة.

## مثال

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

استخدم لاحقة الوحدة التي أنتجها CMake لمنصتك.

## القواعد

- لا تحتفظ بمقابض المهام أو مقابض MIR أو العروض المُستعارة بعد عودة رد النداء،
  ولا تصطنع أبدًا قيمة مقبض أو رقم كود تشغيلي من LLVM.
- قارن `GetSchemaDigest` بالبصمة المُضمَّنة في بنائك قبل استهلاك أي قيمة
  مضبوطة لها راية `RequiresTargetSchema`.
- لا تُعدِّل إلا داخل تعديل. وكل `BeginMutation` يبلغ `EndMutation` واحدًا
  بالضبط، بعد إيداع أو إجهاض.
- لا تدّعِ خاصية آلة بلا إثبات، وفضِّل `INVALIDATION` على `STRUCTURAL_CHECK`
  حين يكون تغييرك قد تخلّى عن إحداها.
- لا تدّعِ أبدًا `NEVERC_MIR_PRESERVE_ALL` بعد تعديل مُودَع.
- تأكَّد من أن التحليل الذي تحتاجه متاح فعلًا عند الخطّاف الذي اخترته.
- هيّئ كل ترويسة جدول وكل حقل محجوز؛ وأعِد الحالات عبر حدود C، ولا تدع
  استثناء C++ يعبرها أبدًا.
- `neverc.mir.final_verify` مختومة. تعمل مهما حدث.

انظر [`PluginMIR.h`] و[`Schema/PluginMIRSchema.inc`] و[`Schema/PhaseSchema.json`]
و[`coverage.json`] للإعلانات المِعيارية وثوابت المخطط وسياسات المراحل وأدلة
التغطية.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMIR.h`]: ../../neverc/include/neverc/Plugin/PluginMIR.h
[`pluginsdk/examples/MachinePass.c`]: ../../pluginsdk/examples/MachinePass.c
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMIRSchema.inc

</div>
