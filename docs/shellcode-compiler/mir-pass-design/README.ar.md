<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مُجمِّع shellcode](../README.ar.md)

# تصميم مرورات MIR — المبادئ ونقاط الخطاف

> مستند مرافق لـ [ir-pass-design.md](../ir-pass-design/README.ar.md). طبقة IR تزيل البنى التي تنتج بشكل واضح إعادة تحديد مواقع. طبقة MIR تعمل كـ**شبكة أمان** بعد اختيار التعليمات وتخصيص السجلات: تزيل أشباه التعليمات/البيانات الوصفية المُقدمة من توليد الكود وتكشف نقاط خطاف لمرورات التشويش الخارجية.

---

## 0. لماذا تحتاج طبقة MIR

الخلفية LLVM تقدم بنى إضافية أثناء **خفض IR → MIR**: أشباه CFI/EH_LABEL، أعقاب XRay/قابلة للترقيع، بيانات وصفية sanitizer. خطافات MIR تمكّن أيضاً **التشويش على مستوى التعليمات** من أطراف ثالثة.

## 1. التكامل مع LLVM

التسجيل في `Pipeline.cpp` عبر مكالمة استرجاع عامة `TargetPassConfig`.

## 2. MIRPrepPass المدمج

يمسح كل `MachineBasicBlock` ويحذف ثلاث فئات من أشباه التعليمات. التعليمات الحقيقية **لا تُمس أبداً**.

نمطان مسجلان:
1. **`aarch64-cpi-fp-to-fmov-imm`**: `ADRP + LDR CPI` → `FMOV #imm8`.
2. **`x86-cpi-zero-fp-to-xorps`**: `movss/movsd +0.0` → `xorps xmm, xmm`.

## 3. خطافات تشويش المستخدم

11 نقطة خطاف: 6 IR + 3 MIR + 2 مستوى بايتات.

- `RunBeforePreEmit`: MIR مع أشباه CFI/EH.
- `RunAfterPreEmit`: MIR منظف — الأقرب لـ AsmPrinter.
- `RunPostExtract`: تدفق بايتات نقي.

## 4. ترتيب التنفيذ الكامل

```
[IR] → Prep → مرورات → Data2Text → Inlining → Stackify → AllBlr
[Codegen] → RunBeforePreEmit → MIRPrepPass → RunAfterPreEmit
[AsmPrinter → .o → المستخرج → RunPostExtract → .bin]
```

## 5. مبررات التصميم

| المشكلة | IR؟ | MIR؟ |
|---------|-----|------|
| إزالة GV ثابت | نعم | غير مطلوب |
| أشباه CFI | لا (الخلفية) | نعم |
| تشويش مستوى التعليمات | لا | نعم |

## 6. دليل التوسيع

- **إضافة إزالة شبه تعليمة**: حالة واحدة في `isShellcodeStripPseudo`.
- **إضافة إعادة كتابة MIR**: كتابة `tryRewriteXxx` + ملفات `.def`.
- **أطراف ثالثة**: `setShellcodeObfuscationHooks()`.

## 7. العلاقة مع ShellcodeExtractor

MIR يُصلح أولاً (يمكنه التلاعب بالتعليمات)؛ المستخرج هو الملاذ الأخير لترقيعات مستوى البايتات.

</div>
