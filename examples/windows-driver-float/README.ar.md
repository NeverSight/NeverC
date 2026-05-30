**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# برنامج تشغيل نواة Windows مع العمليات العشرية

برنامج تشغيل نواة WDM مبني باستخدام NeverC يوضح **الاستخدام الآمن لعمليات
الفاصلة العائمة / SIMD في وضع النواة**. يدعم التجميع المتقاطع من macOS / Linux.

## البناء

```bash
cd examples/windows-driver-float
make
```

من إصدار NeverC مستقل:

```bash
make NEVERC=/path/to/neverc
```

الناتج هو `FloatDriver.sys` (محسّن بـ auto-LTO).
البناء الافتراضي يتضمن `-g` للتصحيح؛ احذف `-g` في إصدارات الإنتاج.

---

## مشكلتان يجب معالجتهما

الفاصلة العائمة في وضع النواة لها مشكلتان منفصلتان:

### المشكلة 1 — علامة ABI `_fltused` (وقت التجميع/الربط)

يصدر مترجم MSVC مرجعًا غير معرّف لرمز `_fltused` كلما أجرت وحدة ترجمة
أي عملية فاصلة عائمة. في برامج وضع المستخدم، يوفر `libcmt.lib` هذا الرمز
فيرضى الرابط ويتم سحب بعض أجزاء CRT الخاصة بـ FP.

برامج تشغيل النواة **لا** تُربط بـ `libcmt` (نحن نمرر `-nostdlib` و
`-Xlinker --nodefaultlib`)، لذا فإن `_fltused` غير المحلول سيسبب خطأ ربط.

**كيف يحلها NeverC**: مع `-fms-kernel`، يعرّف الواجهة الخلفية X86 لـ LLVM
`_fltused` محليًا كـ 0. يمكنك رؤية هذا في الأسمبلي المُولّد:

```asm
# هدف وضع المستخدم:
    .globl  _fltused              # مرجع خارجي -- يتطلب libcmt
```

```asm
# هدف -fms-kernel:
    .globl  _fltused
    .set    _fltused, 0           # تعريف محلي! لا حاجة لرمز خارجي
```

لذا **لا تحتاج أبدًا لكتابة `int _fltused = 0;` يدويًا** في برنامج التشغيل.

### المشكلة 2 — النواة لا تحفظ سجلات FP/SIMD (وقت التشغيل)

نواة Windows **لا** تحفظ/تستعيد سجلات x87 / XMM / YMM / ZMM عند تبديل
السياق افتراضيًا. إذا لمس برنامج التشغيل أيًا من هذه السجلات من كود نواة
عشوائي، فسيفسد بهدوء حالة SIMD لخيط وضع المستخدم الذي يحدث أن يكون على المعالج.

**الحل**: أحط كل منطقة فاصلة عائمة / SIMD بـ
[`KeSaveExtendedProcessorState`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesaveextendedprocessorstate)
و `KeRestoreExtendedProcessorState`:

```c
XSTATE_SAVE save;
NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
if (!NT_SUCCESS(status))
    return status;

// ... كود FP / SIMD هنا ...

KeRestoreExtendedProcessorState(&save);
```

### أقنعة XSTATE

| القناع | يغطي |
|--------|------|
| `XSTATE_MASK_LEGACY_FLOATING_POINT` (البت 0) | مكدس x87 |
| `XSTATE_MASK_LEGACY_SSE` (البت 1) | XMM0–15 |
| `XSTATE_MASK_LEGACY` | البت 0 \| البت 1 (يغطي معظم كود `double` / SSE العادي) |
| `XSTATE_MASK_GSSE` / AVX (البت 2) | الأنصاف العلوية لـ YMM0–15 |
| `XSTATE_MASK_AVX512` | سجلات AVX-512 ZMM |

مرر القناع المدمج بـ OR المطابق لأوسع السجلات التي يستخدمها الكود.

---

## ما يفعله برنامج التشغيل هذا

- ينشئ كائن جهاز في `\Device\FloatDriver` ورابطًا رمزيًا في `\DosDevices\FloatDriver`
- في `DriverEntry`، يستدعي `ComputeAreaSafe()` (التي تغلف `ComputeArea()`
  بحفظ/استعادة حالة FP) مرتين بـ `radius=1.0` و `radius=5.0`
- يطبع البتات الخام لـ double عبر `DbgPrint` (لأن `%f` غير مدعوم من `DbgPrint`
  — نستخدم `RtlCopyMemory` لاستخراج النمط ذي 64 بت)
- يعرّف `_fltused` ضمنيًا عبر `-fms-kernel`

## التحقق من إصدار `_fltused`

قارن مخرج المترجم مع وبدون `-fms-kernel`:

```bash
# وضع المستخدم (يحتاج libcmt):
neverc --target=x86_64-pc-windows-msvc -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused

# النواة (معرّف محليًا كـ 0):
neverc --target=x86_64-pc-windows-msvc -fms-kernel -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused
#  .set    _fltused, 0
```

## التحميل (على جهاز اختبار Windows)

```cmd
sc create FloatDriver type= kernel binPath= C:\path\to\FloatDriver.sys
sc start FloatDriver
sc stop FloatDriver
sc delete FloatDriver
```

قم بتفعيل التوقيع التجريبي أو استخدم شهادة توقيع الكود للإنتاج.

## تحذيرات

- **`%f` لا يعمل مع `DbgPrint`** — إجراء طباعة تصحيح النواة ليس لديه تنسيق
  فاصلة عائمة. حوّل double إلى عدد صحيح بفاصلة ثابتة للعرض، أو اطبع البتات
  الخام كما يفعل هذا المثال.
- **لا تستخدم الفاصلة العائمة عند IRQL ≥ DISPATCH_LEVEL** ما لم يكن ضروريًا للغاية.
  يوثق `KeSaveExtendedProcessorState` قيود IRQL.
- **الأداء**: حفظ/استعادة الحالة ليس مجانيًا؛ بالنسبة للمسارات الساخنة،
  فكّر في تجميع عمل FP في منطقة واحدة محاطة.
