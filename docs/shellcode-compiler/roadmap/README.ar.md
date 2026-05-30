<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مُجمِّع shellcode](../README.ar.md)

# خارطة الطريق

يتتبع هذا المستند الميزات المخططة أو الجارية أو المؤجلة عمداً.

## الحالة الحالية

يغطي مسار shellcode في NeverC:

- مسار LLVM IR كامل مع 11+ مرور مخصص
- مستخرجات COFF / ELF / Mach-O
- حل استيراد Win32 PEB-walk (تجزئة ROR-13، 6 دلاء DLL)
- خفض استدعاءات النظام المباشرة (Darwin `svc #0x80`، Linux `svc #0` / `syscall`)
- دعم وضع النواة (Windows، Linux)
- تدقيق البايتات المحظورة مع ملفات تعريف قابلة للتكوين
- SDK إضافات لمُعيدي كتابة البايتات المحظورة ومُشفِّرات مجموعات الأحرف
- قيود الحجم / المحاذاة / الحشو (`-fshellcode-max-length=`، `-fshellcode-align=`، `-fshellcode-pad=`)
- 11 خطاف تشويش عبر طبقات IR وMIR وتدفق البايتات

## مكتمل (2026-04)

1. **قيود الحجم / المحاذاة / الحشو** — مدمج. `-fshellcode-max-length=`، `-fshellcode-align=`، `-fshellcode-pad=` تُنفَّذ في نهاية `finalizeShellcodeBytes`. يرفض المشغّل التكوينات المتناقضة (مثل بايت الحشو في مجموعة البايتات المحظورة، أو الحشو بدون align/max-length).

2. **واجهة مُعيد كتابة البايتات المحظورة** — الهيكل مدمج، بدون استراتيجيات مدمجة. `Plugin.h::registerBadByteRewriteStrategy` يكشف SDK. `-fshellcode-bad-byte-rewrite` / `-fno-...` يتحكم في استدعاء سلسلة الإنهاء لمُعيدي الكتابة. التعطيل يعود إلى وضع التدقيق فقط. تسجل المكتبات المصبّة استراتيجيات قائمة على Capstone أو مخصصة.

3. **واجهة مُشفِّر مجموعات الأحرف** — الهيكل مدمج، بدون مجموعات مدمجة. `Plugin.h::registerCharsetEncoder` يكشف مجموعة `(name, Encode, Stub, IsCharsetMember)`. عند تعيين `-fshellcode-charset=<name>`، تستبدل مرحلة الإنهاء `.text` بـ `Stub(target) || Encode(text, target)` وتتحقق من جميع بايتات الإخراج مقابل مجموعة الأحرف. تُسجَّل المُشفِّرات القابلة للطباعة / الأبجدية الرقمية / المخصصة بواسطة المكتبات المصبّة.

## مخطط — طبقة الإضافات (عبر الخطافات)

هذه القدرات **غير مدمجة عمداً**. تنتمي إلى طبقة الاستراتيجية/التشويش ومُصمَّمة لتقديمها من قبل إضافات الطرف الثالث عبر واجهات الخطافات والإضافات.

| الميزة | نقطة الخطاف | ملاحظات |
|--------|-----------|---------|
| مقاومة التفكيك | `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR` | تداخل بادئة التعليمات، إعادة ترتيب القفزات، إدراج بايتات عشوائية |
| تعدد الأشكال | `RunAfterFinalMIR` / `RunPostExtract` | تباين الإخراج بناءً على بذرة لكل تجميع |
| مُشفِّر مراحل (XOR / RC4 / فك تشفير ذاتي) | `RunPostExtract` / `RunPostFinalize` | توليد stub وقت التجميع + تشفير الحمولة |
| استدعاءات نظام غير مباشرة (Halos / Tartarus / Recycled Gate) | إضافة مستوى IR أو `RunPostExtract` | مسح أدوات ntdll وقت التشغيل |
| قناع النوم / تزوير مكدس الاستدعاء | إضافة مرور IR | أنماط Ekko / FOLIAGE / Cronos |
| تصحيح ETW / AMSI | إضافة مرور IR | تسلسلات تصحيح وقت التشغيل |
| دوس الوحدة / إزالة الخطافات | إضافة مرور IR | أنماط التلاعب بالذاكرة |

## ملخص خطافات الإضافات

11 خطاف في ثلاث طبقات:

**طبقة IR (6 خطافات، تستقبل `ModulePassManager &`)**:
- `RunBeforePrep` — قبل أي مرور shellcode
- `RunAfterPrep` — بعد توحيد الربط
- `RunBeforeInlining` — الفرصة الأخيرة قبل AlwaysInliner
- `RunAfterInlining` — IR مُسطَّح بالكامل في دالة واحدة
- `RunAfterStackify` — شكل IR النهائي قبل توليد الكود
- `RunAfterFinalIR` — بعد `AllBlrPass`، آخر خطاف IR على الإطلاق

**طبقة MIR (3 خطافات، تستقبل `TargetPassConfig &`)**:
- `RunBeforePreEmit` — السجلات مخصصة، أشباه تعليمات CFI/EH لا تزال موجودة
- `RunAfterPreEmit` — بعد تنظيف `MIRPrepPass`، الأقرب إلى البايتات النهائية
- `RunAfterFinalMIR` — بعد LLVM `addPreEmitPass2()`، مباشرة قبل AsmPrinter

**طبقة تدفق البايتات (2 خطاف، تستقبل `SmallVectorImpl<uint8_t> &`)**:
- `RunPostExtract` — ما قبل الإنهاء، لا يزال يُعالج بواسطة مُعيد الكتابة/المُشفِّر/التدقيق/التحجيم
- `RunPostFinalize` — ما بعد الإنهاء، اللحظة الأخيرة قبل الكتابة على القرص؛ NeverC لا يجري مزيداً من التدقيق

## مسار الإنهاء

يستدعي كل مستخرج `finalizeShellcodeBytes` قبل كتابة `.bin`:

```
applyPostExtractObfuscationHook       (C Plugin API: NEVERC_HOOK_SC_POST_EXTRACT)
        |
auditFinalBadBytes                    (تدقيق صارم مدمج)
        |
applyShellcodeSizing                  (-fshellcode-align/-max-length/-pad)
        |
applyPostFinalizeObfuscationHook      (C Plugin API: NEVERC_HOOK_SC_POST_FINALIZE)
```

الاستخدام وأمثلة الكود في [وثائق Plugin API](../../plugin-api/README.ar.md).

## غير مخطط

- **واجهة أمامية متعددة اللغات** — NeverC يقبل فقط واجهته الأمامية C23 الخاصة. مسار IR منفصل عن الواجهة الأمامية، لكن قبول bitcode خارجي (مثل من `rustc` أو `zig`) ليس هدفاً للمشروع.

</div>
