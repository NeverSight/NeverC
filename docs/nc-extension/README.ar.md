**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[→ توثيق NeverC](../README.ar.md)

# امتداد الملف `.nc`

## نظرة عامة

يتعرف NeverC على `.nc` كامتداد ملف المصدر الأصلي الخاص به. عندما يكتشف المترجم ملف إدخال `.nc`، فإنه **يُفعّل تلقائياً** جميع امتدادات لغة NeverC — دون الحاجة لأعلام إضافية.

## الميزات المُفعّلة تلقائياً

| العلم | التأثير |
|-------|---------|
| `-fneverc-types` | أسماء مستعارة للأعداد الصحيحة بأسلوب Rust (`u8`، `u16`، `u32`، `u64`، `i8`، `i16`، `i32`، `i64`، `usize`، `isize`) |
| `-fbuiltin-string` | نوع القيمة `string` المدمج مع إدارة تلقائية للذاكرة وصيغة dot-call ودعم UTF-8 |

## الاستخدام

ما عليك سوى تسمية ملفك المصدري بامتداد `.nc`:

```bash
# تلقائي — لا حاجة لأعلام إضافية
neverc hello.nc -o hello

# مكافئ لـ:
neverc -fneverc-types -fbuiltin-string hello.c -o hello
```

```c
// hello.nc
#include <stdio.h>

int main(void) {
    string greeting = "مرحباً، NeverC!";
    printf("%s (len=%zu)\n", greeting.c_str(), greeting.len);

    u32 x = 42;
    i64 y = -100;

    string msg = greeting + " x=%u, y=%lld".format(x, y);
    printf("%s\n", msg.c_str());
    return 0;
}
```

## كيف يعمل

يعمل الكشف على مستويين من خط أنابيب المترجم:

### 1. طبقة Driver / Toolchain

يفحص الـ Driver امتداد كل ملف إدخال قبل بناء استدعاء المترجم. بالنسبة لملفات `.nc`، يتم حقن `-fneverc-types` و `-fbuiltin-string` بشكل غير مشروط في سطر الأوامر — لا يحتاج المستخدم لتمريرها يدوياً.

بالنسبة لملفات `.c`، تظل هذه الأعلام اختيارية: مرّر الأعلام التي تحتاجها (`-fneverc-types`، `-fbuiltin-string`) صراحةً.

### 2. طبقة CompilerInvocation

كشبكة أمان، يتحقق الواجهة الأمامية أيضاً من امتدادات ملفات الإدخال عند تحليل الاستدعاء. إذا كان لأي إدخال امتداد `.nc`، يتم تعيين `LangOpts.NeverCTypes` و `LangOpts.BuiltinString` إلى `1`، مما يضمن أن الميزات نشطة حتى عند تجاوز طبقة Driver (مثلاً، عند استدعاء `-cc1` مباشرة).

## التوافقية

- ملفات `.nc` تُعامل كشفرة مصدرية C — اللغة لا تزال C (C23 افتراضياً)، وليست لغة جديدة
- جميع أعلام C القياسية (`-std=c11`، `-O2`، `-g`، `-Wall`، إلخ) تعمل بشكل مطابق
- `-fshellcode` يتكامل طبيعياً مع `.nc`: وضع shellcode يُفعّل `string` بالفعل، و `.nc` يضمن أن `neverc-types` نشط أيضاً
- الترجمة العابرة (`-target aarch64-linux-gnu`، إلخ) تعمل بنفس الطريقة
- ملفات `.c` لا تتأثر — تتصرف تماماً كما كانت ما لم تمرر الأعلام صراحة

## متى تستخدم `.nc` مقابل `.c`

| السيناريو | التوصية |
|-----------|---------|
| مشروع NeverC جديد يستخدم `string` وأنواع Rust | استخدم `.nc` |
| قاعدة شفرة C موجودة يجب أن تبقى متوافقة مع مترجمات أخرى | استخدم `.c` + أعلام صريحة |
| مشروع shellcode | كلاهما يصلح — `-fshellcode` يُفعّل `string` في كل الحالات |
| قاعدة شفرة مختلطة | `.nc` للملفات الخاصة بـ NeverC، `.c` للشفرة المحمولة |
