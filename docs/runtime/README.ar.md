**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← فهرس التوثيق](../README.ar.md) · [← مشروع NeverC](../../README.md)

# `neverc runtime`

يدير حزم **runtime للترجمة المتقاطعة** (sysroot / SDK) من
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). تعيش تحت
`<NeverC-root>/runtime/` (عادةً `~/.neverc/runtime/`).

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
| `windows-x64` | `windows/x64` (+ shared `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ shared `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## أمثلة

```bash
neverc runtime install windows-x64
neverc runtime install all
neverc runtime update linux-arm64 --version v3389.1.2
neverc runtime remove macos-arm64
neverc runtime list
```

## الأوامر الفرعية

- **`install`**: يثبّت هدفًا بوسم **مترجم NeverC** افتراضيًا (أو `--version`). نفس الوسم → نجاح؛ وسم مختلف → تأكيد `[Y/n]`.
- **`install all`**: يثبّت كل الأهداف **الناقصة**؛ المثبّتة تُتخطّى.
- **`update` / `upgrade`**: جلب قسري بلا مطالبة. الافتراضي **latest**.
- **`remove` / `uninstall`**: يحذف الدليل ويحدّث `manifest.json`.
- **`list` / `ls`**: حالة التثبيت ووسم المترجم.

## قواعد الإصدار

| الأمر | الافتراضي دون `--version` |
|-------|---------------------------|
| `install` / `install all` | وسم release للمترجم |
| `update` | أحدث release ينشر أصل runtime هذا |

الوسوم بصيغة `vMAJOR.MINOR.PATCH`؛ التحقق عبر `SHA256SUMS` قبل الاستخراج.

## العلاقة مع `neverc update`

- `neverc runtime …` يغيّر **sysroot فقط**.
- [`neverc update`](../update/README.ar.md) يزامن **المترجم + بيئات runtime المثبّتة**.

بعد تحديث المترجم، استخدم `runtime install` للأهداف **الجديدة** فقط.

## أوامر ذات صلة

| الأمر | الاستخدام |
|-------|-----------|
| [`neverc update`](../update/README.ar.md) | المترجم والـ runtime المثبّتة معًا |
| [`neverc build` / `make`](../build/README.ar.md) | بناء أمثلة الترجمة المتقاطعة |
| [Examples](../examples/README.ar.md) | ملفات Makefile مع `--target=…` |
| `neverc runtime --help` | ملخص الاستخدام المدمج |
