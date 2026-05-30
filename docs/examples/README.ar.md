<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← التوثيق](../README.ar.md) · [← مشروع NeverC](../../docs/i18n/README.ar.md)

# أمثلة NeverC

أمثلة قابلة للبناء توضح قدرات التجميع المتقاطع في NeverC. جميعها تُجمَّع متقاطعاً من macOS / Linux — بدون بيئة Windows.

---

## الأمثلة المتاحة

| المثال | الوصف | الميزات الرئيسية |
|--------|-------|-----------------|
| [برنامج تشغيل نواة Windows](../../examples/windows-driver/README.ar.md) | برنامج WDM أدنى | تجميع متقاطع `.sys` من macOS/Linux، LTO تلقائي، مُرابط مدمج |
| [برنامج تشغيل Windows + CET](../../examples/windows-driver-cet/README.ar.md) | برنامج مع Intel CET Shadow Stack | كود نواة متوافق مع CET، `/guard:ehcont` |
| [برنامج تشغيل Windows + عائم](../../examples/windows-driver-float/README.ar.md) | برنامج مع فاصلة عائمة/SIMD | فاصلة عائمة آمنة في وضع النواة |

---

## بدء سريع

```bash
cd examples/<اسم-المثال>
make
```

تحديد مسار المترجم: `make NEVERC=/path/to/neverc`

جميع الأمثلة تستخدم **neverc** وتُنتج ثنائيات Windows PE (`.sys`) عبر المُرابط المدمج.

</div>
