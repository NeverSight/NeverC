<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مُجمِّع dyncode](../README.ar.md)

# تصميم مرورات MIR — المبادئ ونقاط الخطاف

> وثيقة مرافقة لـ [ir-pass-design.md](../ir-pass-design/README.ar.md). طبقة IR تزيل البُنى التي تُنتج نقلات (relocations) بشكل ظاهر على مستوى IR. طبقة MIR تعمل كـ**شبكة أمان** بعد اختيار التعليمات وتخصيص المسجلات: تجرّد التعليمات الوهمية/البيانات الوصفية التي يُدخلها codegen وتكشف نقاط خطاف لمرورات التشويش الخارجية لإجراء التحويلات النهائية على مستوى التعليمة.
>
> التنفيذ: `neverc/lib/DynCode/MIR/MIRPrepPass.cpp` + `Pipeline.cpp`.
> واجهة الخطاف: [`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`].

---

## 0. لماذا نحتاج طبقة MIR

طبقة IR قد أزالت بالفعل:
- الثوابت GV → مكدسة / قيم فورية (Data2TextPass)
- `memcpy` / `memset` / `str*` / `abs*` → حلقات بايت مضمّنة (MemIntrinPass)
- مساعدات compiler-rt لـ `__int128` → always_inline مضمّنة (CompilerRtPass)
- استدعاءات libc الخارجية → svc / syscall مضمّنة (SyscallStubPass)
- خارجيات Win32 → PEB walk + تجزئة التصدير (WinPEBImportPass)
- الجلوبالات المتغيّرة → إطار مكدس الدخول (ZeroRelocPass)
- computed goto → switch (IndirectBrPass)
- اختياري: استدعاء مباشر → استدعاء غير مباشر (AllBlrPass)

لكن خلفية LLVM تُدخل بُنى إضافية أثناء **خفض IR → MIR** لا يستطيع dyncode استيعابها:

1. **التعليمات الوهمية CFI / EH_LABEL**: تُولَّد عند تفعيل `-g` أو معلومات unwind الافتراضية، مُنتجةً `__compact_unwind` (Mach-O) / `.eh_frame` (ELF) / `.pdata + .xdata` (COFF).
2. **كعوب XRay / patchable function**: `-fxray-instrument` أو `-fpatchable-function-entry` تُدرج `PATCHABLE_FUNCTION_ENTER` وما شابه.
3. **بيانات وصفية للمُعقِّمات**: StackMap / PatchPoint / StateMap / PseudoProbe.
4. **إصلاحات على مستوى MC للخلفية**: مثل مراجع GOT لـ arm64 على Windows، غير المرئية على مستوى IR.

إضافةً إلى ذلك، تخدم خطافات MIR غرضًا حاسمًا: **تمكين التشويش على مستوى التعليمة من طرف ثالث** (استبدال التعليمات، إعادة تسمية المسجلات) وهو ما لا يستطيع IR التعبير عنه (يملك IR مسجلات افتراضية وتعليمات مجردة فقط).

---

## 1. التكامل مع LLVM (خطافات أصلية)

يملك `TargetPassConfig` في LLVM قائمة callback عامة. تستدعي `addMachinePasses()` كل callback بعد `addPass(&PatchableFunctionID)` وقبل `addPreEmitPass()`. أضفنا غلافًا عامًا `addExternalPass(Pass *P)` لحل مشكلات التحكم في الوصول مع `addPass()` المحمي.

التسجيل في `Pipeline.cpp`:

```cpp
ListRegisterTargetPassConfigCallbacks.push_back(
    [](TargetPassConfig &TPC) {
      const DynCodeOptions &Opts = currentDynCodeOptionsStorage();
      const ObfuscationInterposes &H = getDynCodeObfuscationInterposes();
      runMIRInterpose(H.RunBeforePreEmit, TPC, Opts);
      TPC.addExternalPass(createDynCodeMIRPrepPass(Opts));
      runMIRInterpose(H.RunAfterPreEmit, TPC, Opts);
    });
```

لا يلتقط الـ callback قيمة `Opts`. بل يقرأ لقطة `DynCodeOptions` الحالية في وقت التشغيل، مانعًا التهيئة القديمة عندما يُجمِّع نفس العملية كلًا من dyncode وC العادي.

---

## 2. MIRPrepPass المدمج

عابر للمنصات، مسؤولية واحدة: يمسح كل `MachineBasicBlock` ويحذف ثلاث فئات من التعليمات الوهمية. أما التعليمات الآلية الحقيقية (`MOV` / `BL` / `ADRP` / `SYSCALL` / ...) فلا **تُمَس أبدًا**.

### 2.1 بيانات وصفية للأقسام الجانبية (عبر `TargetOpcode::*`، عابرة للمنصات)

| Opcode | المصدر | إذا لم يُزَل |
|--------|--------|-------------|
| `CFI_INSTRUCTION` | frame-lowering لكل المنصات / `-g` | `.eh_frame` / `__compact_unwind` / `.pdata` غير فارغة |
| `EH_LABEL` | نقاط EH / try-catch setjmp | قسم LSDA الجانبي غير فارغ |
| `GC_LABEL` / `ANNOTATION_LABEL` | علامات GC / annotation | MCSymbol ببيانات وصفية نسبية للقسم |
| `STATEPOINT` / `STACKMAP` / `PATCHPOINT` | stackmap لـ GC / sandbox | قسم `.llvm_stackmaps` الجانبي |
| `PSEUDO_PROBE` | `-fprofile-sample-use` | قسم `.pseudo_probe` الجانبي |
| عائلة `PATCHABLE_*` | كعوب XRay / Kcov | `.xray_instr_map` / `.xray_fn_idx` |
| `FENTRY_CALL` | مسبار الدخول `-mfentry` | استدعاء `__fentry__` خارجي |
| `LOCAL_ESCAPE` | frame-escape لـ SEH من Microsoft | يجرّ `_local_unwind2` / `__except_handler3` |
| `JUMP_TABLE_DEBUG_INFO` | معلومات تصحيح جدول القفز | مدخل `.debug_rnglists` |

### 2.2 Windows SEH (يُطابَق ببادئة `TargetInstrInfo::getName()`)

تعليمات SEH الوهمية على Windows هي أكواد تشغيل خاصة بالهدف مُعرَّفة في ملفات TD لخلفيتي AArch64/X86 (~20 تعليمة مثل `SEH_StackAlloc` و`SEH_PushReg` إلخ). لإبقاء مرور MIR **عابرًا للمنصات دون تضمين رؤوس الخلفية**، نستخدم مطابقة بادئة السلسلة:

```cpp
StringRef Name = TII->getName(Opcode);
if (Name.starts_with("SEH_"))
  eraseFromParent();
```

### 2.3 جدول إعادة كتابة التعليمات (`MIRRewritePatterns.def`)

بعد تجريد الوهميات، يُشغّل `MIRPrepPass` مرور إعادة كتابة لاستبدال أنماط التعليمات التي اختارها codegen لكنها غير صديقة لـ dyncode بأشكال مكافئة صديقة لـ dyncode، دون تعديل ملفات `.td` الخاصة بـ LLVM.

نمطان مُسجَّلان:

1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDRSui/LDRDui [base, #:lo12:CPI]` → `FMOV Sd/Dd, #imm8` عندما يقع النمط البتّي IEEE ضمن قيم FMOV الـ 256 القابلة للترميز.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd xmm, [rip+CPI]` الذي يحمّل `+0.0` → `FsFLD0SS/FsFLD0SD` (`xorps xmm, xmm` بثلاثة بايت).

أسماء أكواد التشغيل مُركَّزة في `Tables/MIRRewriteOpcodes.def`. إضافة نمط إعادة كتابة جديد تتطلب ثلاث خطوات:
1. كتابة `tryRewriteXxx(MachineFunction &)` باستخدام `lookupMIRRewriteOpcode()` + `BuildMI(TII->get(...))`
2. إضافة أدوار أكواد التشغيل إلى `MIRRewriteOpcodes.def`
3. إضافة مدخل النمط إلى `MIRRewritePatterns.def`

---

## 3. خطافات التشويش من المستخدم

يكشف `ObfuscationInterposes` عن **11 نقطة خطاف**: 6 على مستوى IR، و3 على مستوى MIR، و2 على مستوى البايت:

ثلاثة أنواع توقيع:
```cpp
using ObfuscationInterpose = std::function<void(
    llvm::ModulePassManager &, const DynCodeOptions &)>;
using MachineObfuscationInterpose = std::function<void(
    llvm::TargetPassConfig &, const DynCodeOptions &)>;
using BinaryObfuscationInterpose = std::function<void(
    llvm::SmallVectorImpl<uint8_t> &, const DynCodeOptions &)>;
```

الفروق الرئيسية:
- `RunBeforePreEmit`: لا تزال MIR **تحوي وهميات CFI/EH/XRay** — لمعالجة بيانات المقدمة/الخاتمة الوصفية.
- `RunAfterPreEmit`: **MIR مُنظَّفة** — الأقرب إلى شكل AsmPrinter، مثالية لاستبدال التعليمات / إعادة تسمية المسجلات.
- `RunPostExtract`: **تدفق بايت خالص** بعد أن يُرقِّع المُستخرِج النقلات داخل النص — لـ XOR/RC4 على كامل الحمولة، وبايتات عشوائية، ورؤوس مخصصة.

```cpp
__attribute__((constructor))
static void myMirObfInit() {
  auto H = neverc::dyncode::getDynCodeObfuscationInterposes();
  H.RunAfterPreEmit = [](llvm::TargetPassConfig &TPC,
                         const neverc::dyncode::DynCodeOptions &Opts) {
    TPC.addExternalPass(new MyInstructionSubstitutionPass(Opts.MirObfuscateSpec));
  };
  // Register via Plugin API: NEVERC_INTERPOSE_SC_BEFORE_PREEMIT / AFTER_PREEMIT / AFTER_FINAL_MIR
}
```

---

## 4. ترتيب التنفيذ الكامل

```
[IR PassBuilder]
  ├─ RunBeforePrep       (خطاف المستخدم)
  ├─ ZeroRelocPass(Prep)
  ├─ RunAfterPrep        (خطاف المستخدم)
  ├─ IndirectBrPass / MemIntrinPass / CompilerRtPass
  ├─ SyscallStubPass / WinPEBImportPass / KernelImportPass
  ├─ Data2TextPass #1
  ├─ RunBeforeInlining   (خطاف المستخدم)
  │  (مستوى O في LLVM: AlwaysInliner / SROA / SLP)
  ├─ RunAfterInlining    (خطاف المستخدم)
  ├─ Data2TextPass #2 / ZeroReloc(Stackify)
  ├─ RunAfterStackify    (خطاف المستخدم)
  └─ AllBlrPass          (اختياري)
        │
[Codegen (IR → MIR)]
  ├─ RunBeforePreEmit    (خطاف المستخدم، CFI موجود)
  ├─ DynCodeMIRPrepPass  ← محور هذه الوثيقة
  └─ RunAfterPreEmit     (خطاف المستخدم، CFI مُجرَّد)
        │
[AsmPrinter → ملف كائن]
        │
[DynCodeExtractor]  ← تدقيق احتياطي على مستوى البايت
  ├─ RunPostExtract   (خطاف المستخدم، بايتات خالصة)
  └─ ‎.bin مسطّح
```

تتولى طبقة MIR **تنظيف شبكة الأمان + نقاط خطاف التشويش**، لا منطق الأعمال. أما وعد «اكتب C عاديًا دون حاجة لحيل dyncode» فتحققه مرورات IR الخمسة+.

---

## 5. الأساس المنطقي للتصميم

| المشكلة | طبقة IR؟ | طبقة MIR؟ |
|---------|----------|-----------|
| إزالة GV الثابتة | نعم (Data2Text) | غير مطلوب |
| إزالة libc الخارجية | نعم (SyscallStub / WinPEB) | غير مطلوب |
| تكديس الجلوبالات المتغيّرة | نعم (ZeroReloc) | غير مطلوب |
| computed goto | نعم (IndirectBr) | غير مطلوب |
| تعليمات CFI الوهمية | لا (تولّدها الخلفية) | نعم (مسح وحذف) |
| كعوب XRay | لا (تولّدها الخلفية) | نعم (مسح وحذف) |
| التشويش على مستوى التعليمة | لا (يفتقر IR للمسجلات الفيزيائية) | نعم (يملك مسجلات حقيقية/MI) |
| إعادة تسمية المسجلات | لا | نعم |
| توسيع ثوابت peephole | جزئي | نعم (أنظف) |

## 6. دليل التوسيع

**إضافة تجريد وهمي مدمج**: أضف حالة واحدة إلى مِفتاح `isDynCodeStripPseudo`.

**إضافة إعادة كتابة MIR مدمجة**: اكتب `tryRewriteXxx(MachineFunction &)` باستخدام `TII->getName()` / `BuildMI(TII->get(...))`. أضف النمط إلى `MIRRewritePatterns.def`، وأكواد التشغيل إلى `MIRRewriteOpcodes.def`.

**مكتبة تشويش من طرف ثالث**: سجّل عبر [واجهة الإضافات](../../plugin-api/README.ar.md) (خطافات `NEVERC_INTERPOSE_SC_*`).

## 7. العلاقة مع DynCodeExtractor

| الطبقة | التوقيت | القدرة |
|--------|---------|--------|
| MIR | **قبل** AsmPrinter | يمكنها إدراج/حذف MachineInstr |
| Extractor | **بعد** AsmPrinter | يمكنه فقط تعديل البايتات أو الرفض |

**المبدأ**: أصلِح في MIR أولًا (لا يزال بإمكانك معالجة التعليمات)؛ ولا تلجأ إلى المُستخرِج إلا لترقيعات على مستوى البايت (مثل نقلة imm26 داخل القسم). يضمن هذا التقسيم ألا يحصل المستخدم أبدًا على `.bin` «نصف معطوب»: فإما أن يعمل أو يكون هناك خطأ واضح قابل للمعالجة وقت الترجمة.

## 8. الإصلاح النشط مقابل تمرير التشخيص

1. **الإصلاح النشط**: يُعدِّل MachineInstr مباشرةً (يجرّد الوهميات، يعيد كتابة CPI→FMOV). منخفض التكلفة ومستقل عن الهدف.
2. **تمرير التشخيص**: يكتشف المشكلات، ويبلغ عن أخطاء على مستوى MIR، ويترك المُستخرِج يرفض على مستوى البايت. يُستخدم للبُنى التي تتطلب فيها إعادة كتابة MIR كودًا واسعًا خاصًا بالهدف (مثل استبدال `adrp+ldr CPI` بتسلسلات `mov/movk`).
3. **الاحتياطي عبر المُستخرِج**: فشل صارم عند أي نقلات خارجية متبقية أو أقسام بيانات غير فارغة.

يُبقي هذا المبدأ طبقة MIR شبه محصّنة ضد ترقيات ISA للخلفية. الصيانة الوحيدة هي: «هل هناك وهمي جديد في TargetOpcode؟ إن لم يحتجه dyncode، أضف حالة واحدة.»

[`neverc/include/neverc/DynCode/Pipeline/Pipeline.h`]: ../../../neverc/include/neverc/DynCode/Pipeline/Pipeline.h

</div>
