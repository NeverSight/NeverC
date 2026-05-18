<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مُجمِّع shellcode](../README.ar.md)

# واجهة إضافات Shellcode (Plugin SDK)

مسار NeverC له بنية **مسار أساسي + طبقة مستخدم قابلة للتوصيل**. التشويش، مقاومة التفكيك، تجاوز EDR وغيرها **غير مدمجة عمداً**.

## 1. مسار الإنهاء
خطاف PostExtract → سلسلة مُعيدي كتابة البايتات المحظورة → مُشفِّر مجموعة الأحرف → تدقيق البايتات المحظورة → الحجم → خطاف PostFinalize.

## 2. مُعيد كتابة البايتات المحظورة
`registerBadByteRewriteStrategy`. مُتساوي القوة، حتمي، تدفق بايتات فقط.

## 3. مُشفِّر مجموعة الأحرف
`registerCharsetEncoder` مع `(Name, Encode, Stub, IsCharsetMember)`. الإخراج يجب أن يجتاز مجموعة الأحرف.

## 4. الحجم / المحاذاة / الحشو
`-fshellcode-max-length=`، `-fshellcode-align=`، `-fshellcode-pad=`.

## 5–6. تخطيط الخطافات ومصفوفة PIC
11 خطاف (6 IR + 3 MIR + 2 بايت). التسجيل الأبكر = تغطية PIC أوسع. التوصية: تشفير السلاسل → `RunAfterPrep`؛ CFF → `RunAfterInlining`؛ استبدال التعليمات → `RunAfterPreEmit`؛ تشفير الحمولة → `RunPostFinalize`.

</div>
