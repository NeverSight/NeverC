**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[→ نظام التشغيل المدمج NeverC](../README.ar.md)

# تشفير السلاسل النصية في وقت التجميع (`xorstr`)

## نظرة عامة

يوفر NeverC تشفيرًا للسلاسل النصية من مستويين في وقت التجميع لكود C، مصمم للسيناريوهات الأمنية حيث لا يجب أن تكون السلاسل النصية الصريحة (أسماء API، مسارات السجل) مرئية في الملف الثنائي المُجمّع.

- **المستوى 1 — ماكرو صريح**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` للتحكم الدقيق لكل سلسلة نصية
- **المستوى 2 — تمرير IR تلقائي**: `-fencrypt-call-strings` لتشفير جميع وسائط السلاسل النصية في استدعاءات الدوال تلقائيًا

يستخدم كلا المستويين مخازن مؤقتة مخصصة على المكدس (بدون تخصيص على الكومة)، وخوارزمية فك تشفير بدون تعليمات XOR (مضاد للتوقيع)، وتنظيف بـ `memset` متطاير قبل العودة من الدالة.

---

## البداية السريعة

```c
#include <neverc/xorstr.h>
FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## فك التشفير المضاد للتوقيع

تتجنب عملية فك التشفير تعليمات XOR بالكامل، مستخدمة المعادل الرياضي: `a + b − 2 × (a & b)`.

---

## مرجع أعلام المُجمّع

| العلم | الوصف |
|-------|-------|
| `-fencrypt-call-strings` | تفعيل التشفير التلقائي للسلاسل النصية |
| `-fno-encrypt-call-strings` | تعطيل التشفير التلقائي |
| `-fencrypt-call-strings-max-len=N` | الحد الأقصى للطول بالبايت (الافتراضي: 1024) |
| `-fstring-encrypt-key=0xHEX` | تجاوز بذرة مفتاح XOR |
