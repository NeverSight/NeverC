<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← فهرس التوثيق](../README.ar.md) · [← مشروع NeverC](../../README.md)

# `neverc runtime`

يدير حزم **runtime للترجمة المتقاطعة** (sysroot / SDK) من
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). تعيش تحت
`<NeverC-root>/runtime/` بجانب المترجم (للتثبيت الافتراضي
`~/.neverc/runtime/`).

فضّل `neverc runtime install …` على فكّ `neverc-runtime-<target>.zip` يدويًا.

## الصيغة

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

أسماء بديلة: `upgrade` → `update`؛ `uninstall` → `remove`؛ `ls` → `list`.

## الأهداف المتاحة

| الهدف | التخطيط تحت `runtime/` |
|--------|-------------------------|
| `windows-x64` | `windows/x64` (+ مشترك `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ مشترك `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## الأوامر الفرعية

### `install`

يثبّت هدفًا واحدًا بوسم **release للمترجم** افتراضيًا (أو `--version <tag>`).
اسم الأصل: `neverc-runtime-<target>.zip`.

```bash
neverc runtime install windows-x64
neverc runtime install linux-arm64 --version v3389.1.2
```

إذا كان الهدف مثبّتًا مسبقًا:

- نفس الوسم → الإبلاغ والخروج بنجاح.
- وسم مختلف / مجهول → تأكيد `[Y/n]` قبل إعادة التثبيت.

### `install all`

يثبّت كل الأهداف **الناقصة** في الكتالوج بإصدار المترجم (أو `--version`). تُتخطّى
الأهداف المثبّتة؛ لتغيير التثبيت المثبّت أعد تشغيل `install` لهدف واحد.

```bash
neverc runtime install all
```

### `update` / `upgrade`

يجلب هدفًا واحدًا قسرًا بلا مطالبة تفاعلية. الإصدار الافتراضي **latest** (بخلاف
`install` الذي يتبع وسم المترجم). مرّر `--version` للتثبيت على وسم محدّد.

```bash
neverc runtime update windows-x64
neverc runtime update android-arm64 --version v3389.1.2
```

### `remove` / `uninstall`

يحذف دليل هدف مثبّت ويحدّث `runtime/manifest.json`.

```bash
neverc runtime remove linux-x64
```

### `list` / `ls`

يعرض كل هدف في الكتالوج كمثبّت (مع الوسم المسجّل) أو غير مثبّت، إضافةً إلى وسم
المترجم الحالي.

```bash
neverc runtime list
```

## قواعد الإصدار

| الأمر | الافتراضي دون `--version` |
|-------|---------------------------|
| `install` / `install all` | وسم release للمترجم |
| `update` | أحدث release ينشر أصل runtime هذا |

الوسوم بصيغة `vMAJOR.MINOR.PATCH`. تُتحقق الأرشيفات من `SHA256SUMS` للإصدار قبل
الاستخراج.

## العلاقة مع `neverc update`

- `neverc runtime …` يغيّر **sysroot فقط**.
- [`neverc update`](../update/README.ar.md) ينقل **المترجم وكل بيئات runtime
  المثبّتة مسبقًا** إلى وسم واحد كمعاملة واحدة.

بعد ترقية المترجم بـ `neverc update` تكون بيئات runtime المثبّتة محاذاة أصلًا؛
تحتاج `runtime install` للأهداف **الجديدة** فقط.

## أوامر ذات صلة

| الأمر | متى تُستخدم |
|-------|-------------|
| [`neverc update`](../update/README.ar.md) | ترقية/تخفيض المترجم والـ runtime المثبّتة معًا |
| [`neverc build` / `make`](../build/README.ar.md) | بناء أمثلة الترجمة المتقاطعة على هذه الـ sysroot |
| [Examples](../examples/README.ar.md) | ملفات `Makefile` نموذجية مع `--target=…` |
| `neverc runtime --help` | ملخص الاستخدام المدمج |

</div>
