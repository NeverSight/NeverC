<div dir="rtl">

**اللغات**: [English](../update.md) | [简体中文](../zh-CN/update.md) | [繁體中文](../zh-TW/update.md) | [日本語](../ja/update.md) | [한국어](../ko/update.md) | [Français](../fr/update.md) | [Deutsch](../de/update.md) | [Español](../es/update.md) | [Italiano](../it/update.md) | [Русский](../ru/update.md) | [العربية](update.md)

[← فهرس التوثيق](README.md) · [← مشروع NeverC](project.md)

# `neverc update`

يحدّث **تثبيت release** بحيث ينتقل المترجم وكل بيئات runtime للترجمة المتقاطعة
**المثبّتة مسبقًا** معًا إلى **وسم release محدد**. `neverc upgrade` مرادف.

مخصّص للتثبيت عبر `install.sh` (عادةً `~/.neverc`). **لا** يحدّث شجرة بناء
CMake/Ninja — غيّر PATH وأعد البناء؛ انظر [التطوير المحلي](local-dev.md).

## الصيغة

```text
neverc update
neverc update <version>
neverc update --version <version>
neverc update --help
```

أمثلة:

```bash
neverc update                 # أحدث release مكتمل لهذا المضيف
neverc update v3389.1.2       # وسم دقيق (ترقية أو تخفيض)
neverc update 3389.1.2        # حرف v الأول اختياري
neverc upgrade                # مثل neverc update
```

`-y` / `--yes` مقبولان للتوافق مع السكربتات؛ التحديث غير تفاعلي.

## نطاق المزامنة

| المكوّن | السلوك |
|---------|--------|
| المترجم (`bin/`، `lib/`، `pluginsdk/`) | يُستبدل عند اختلاف الوسم الهدف |
| بيئات runtime تحت `runtime/` | يُعاد جلب الأهداف **المثبّتة فقط** وتثبيتها على الوسم نفسه |
| بيئات غير مثبّتة | **لا** تُثبَّت تلقائيًا — [`neverc runtime install`](runtime.md) |

## نموذج الأمان

1. قفل حصري تحت `<install>/.neverc-update.lock`.
2. حل الوسم الهدف.
3. تنزيل `SHA256SUMS` والأرشيفات والتحقق منها.
4. استخراج وتحقق في مساحة مرحلية ثم التزام؛ عند الفشل يُلغى التغيير.

إذا كان إصدار runtime سيئًا، حدّد وسمًا أقدم:

```bash
neverc update v3389.0.1
```

## القيود

- جذر تثبيت release فقط (عادةً `~/.neverc`). يرفض جذور نظام الملفات وأشجار CMake.
- يجب أن يطابق المضيف أصل مترجم منشورًا.
- على Windows قد تستبدل عملية مساعدة قصيرة `neverc.exe` بعد الخروج.

## أوامر ذات صلة

| الأمر | الاستخدام |
|-------|-----------|
| [`neverc runtime`](runtime.md) | إدارة sysroot فردية دون تغيير المترجم |
| [`neverc run`](run.md) | ترجمة وتشغيل ثنائي مؤقت على المضيف |
| [`neverc build` / `make`](build.md) | تشغيل ملفات Makefile للأمثلة/المشاريع |
| `neverc update --help` | ملخص الاستخدام المدمج |

</div>
