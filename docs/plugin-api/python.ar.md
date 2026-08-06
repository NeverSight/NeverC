<div dir="rtl">

**اللغات**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← واجهة NeverC الثنائية للإضافات](README.ar.md)

# إضافات Python

يستطيع NeverC تحميل ملف مصدر Python بواسطة خيار `-fplugin=` نفسه المستخدم
للإضافات الأصلية. دعم Python اختياري، لذلك لا تضيف البنية العادية اعتماداً على
CPython:

```sh
cmake -S llvm -B build -DNEVERC_ENABLE_PYTHON_PLUGINS=ON
cmake --build build --target neverc
```

تفعّل أرشيفات مترجم NeverC الرسمية هذا الخيار، وتتضمن بيئة CPython 3.12 قابلة
للنقل ومكتبتها القياسية في مجلد `python/` المجاور، لذلك لا تحتاج إلى تثبيت
Python منفصل وقت التشغيل. تبقي البنيات المخصصة من المصدر الخيار `OFF` افتراضياً،
ويمكنها عند تفعيله تضمين CPython 3.10 خارجي أو إصدار أحدث.

تتطلب البنية المفعلة CPython 3.10 أو أحدث وملفات التطوير الخاصة بالتضمين.
ثبّت حزمة التأليف بالأمر `python3 -m pip install ./pluginsdk/python`، أو أضف
المجلد إلى `PYTHONPATH`، أو ابنِ وثبّت المكوّن `neverc-pluginsdk`. يكتشف NeverC
أيضاً نسخة SDK المجهزة في `<مجلد neverc>/../pluginsdk/python`.

## إضافة بسيطة

```python
from neverc_plugin import Plugin


@Plugin(id="com.example.minimal", name="Minimal Python Plugin", version="1.0.0")
class MinimalPlugin:
    def on_process_begin(self, ctx):
        ctx.state = {"sessions": 0}
```

حمّلها بواسطة مسار في نظام الملفات:

```sh
neverc -fplugin=/absolute/path/to/minimal.py -fsyntax-only input.c
```

يقبل الـ decorator معرّف إضافة قياسياً واسماً غير فارغ وإصداراً دلالياً صارماً.
يعلن كل script فئة إضافة واحدة فقط. تمثل الـ scripts وحدات مستقلة ويمكن مزجها
مع الإضافات الأصلية.

## دورة الحياة

جميع الـ hooks اختيارية:

- يحيط `on_process_begin(ctx)` و`on_destroy(ctx)` بعملية المصرّف.
- يسجل `register(ctx)` الخيارات وobservers قبل تجميد مخطط المراحل.
- يحيط `on_session_begin(ctx)` و`on_session_end(ctx)` بعملية استدعاء.
- يحيط `on_task_begin(ctx)` و`on_task_end(ctx)` بوحدة عمل للمصرّف.

يمكن لـ begin hook إرجاع قيمة Python أو تعيين `ctx.state`؛ وتتوفر القيمة للـ
end hook المقابل. يجب أن تعيد بقية الـ hooks وobserver callbacks القيمة `None`.
إضافات Python في v1 تعمل بنمط session-serial وغير قابلة لإعادة الدخول.

## الخيارات وobservers

```python
from neverc_plugin import Plugin
from neverc_plugin.domains import driver


@Plugin(id="com.example.trace", name="Trace", version="1.0.0")
class TracePlugin:
    def register(self, ctx):
        ctx.option(
            "--trace-python",
            kind="flag",
            value_type="bool",
            help="Trace raw driver arguments",
        )
        ctx.observer(
            driver.RAW_ARGUMENTS,
            when=("before", "after"),
            fn=self.observe,
        )

    def observe(self, frame):
        if frame.option_values("--trace-python"):
            frame.check_cancelled()
            frame.emit_remark(f"arguments: {frame.arguments}", code=1001)
```

تحتوي `neverc_plugin.phases` على ثوابت المراحل المدمجة البالغ عددها 130، مولّدة
من مخطط المراحل القياسي. تكشف observer frames بيانات المرحلة والمسار وhandles
معتمة للإدخال والإخراج والخيارات المحللة والتشخيصات وفحص الإلغاء والوسائط الخام
لـ `driver.RAW_ARGUMENTS`. تتحقق handles الأصلية من مدة حياتها: يرفع استخدام
كائن محتفظ به بعد انتهاء callback الاستثناء `RuntimeError`.

أنواع الخيار هي `flag` و`joined` و`separate` و`multi_arg`، وأنواع القيم هي
`bool` و`int` و`uint` و`string` و`enum` و`path`، والتكرار هو `single` أو
`last_wins` أو `append`. يمرر خيار enum الخريطة
`enum_values={الاسم: عدد صحيح}`. يخص `argument_count` النوع `multi_arg` فقط.

## الأخطاء والأمان والنطاق الحالي

يتحول استثناء Python غير الملتقط إلى `NEVERC_STATUS_PLUGIN_EXCEPTION`. أثناء
session/task callback نشط، يصدر NeverC الـ traceback المنسق كتشخيص منظم؛ أما
فشل import أو activation فيتضمنه خطأ loader. يشترك كامل process في المفسر
المضمّن ولا يقوم NeverC بإنهائه عمداً، بينما تحرر كائنات كل إضافة عند unload.

إضافات Python امتدادات موثوقة للمصرّف. تعمل داخل العملية، ويمكنها import أي
module، ولها صلاحيات الملفات والعملية نفسها التي يملكها NeverC. لا يوجد sandbox.

يبقى v1 للقراءة فقط عمداً باستثناء تسجيل الخيارات. لا يعرض interceptors أو
providers أو تعديل artifacts أو نماذج IR/MIR/Link الخاصة بالنطاق أو
subinterpreters أو manifests أو نقاط دخول module/factory. تحتاج هذه القدرات
إلى wrappers للـ transaction والـ continuation تفرض مدة الحياة؛ وتبقى واجهة C
الأصلية متاحة عند الحاجة إليها.

</div>
