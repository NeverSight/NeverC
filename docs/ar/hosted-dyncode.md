<div dir="rtl">

**Languages**: [English](../hosted-dyncode.md) | [简体中文](../zh-CN/hosted-dyncode.md) | [繁體中文](../zh-TW/hosted-dyncode.md) | [日本語](../ja/hosted-dyncode.md) | [한국어](../ko/hosted-dyncode.md) | [Français](../fr/hosted-dyncode.md) | [Deutsch](../de/hosted-dyncode.md) | [Español](../es/hosted-dyncode.md) | [Italiano](../it/hosted-dyncode.md) | [Русский](../ru/hosted-dyncode.md) | [العربية](hosted-dyncode.md)

[← DynCode](dyncode-compiler.md)

# DynCode المستضاف وترجمة C++

استخدم `-fdyncode-hosted` لتثبيت دالة ذات كود فقط داخل برنامج قائم. يتطلب `-fdyncode-keep-obj` ويحفظ CFI/SEH الأصلية للمترجم في الكائن ويستبعدها من الحمولة المسطحة. يحافظ قسم كود أصلي واحد ومدخل عند إزاحة صفر على ارتباط سجلات فك المكدس.

يعطل التحويلات الضمنية syscall/PEB وheap arena ومكتبة السلاسل. تُرفض المتغيرات العامة القابلة للكتابة أو TLS والاستدعاءات غير المحلولة. يمكن تمرير البيانات ومؤشرات callback كمعاملات. يبقى سلوك DynCode المستقل كما هو.

تقبل ترجمة C++ الأساسية `--target` وتربط C23 وخريطة المصدر والبيان بتلك ABI. يدعم `cpp-core-v2` المجموعة الجزئية الموثقة من C++17، ومنها القوالب والإنشاء والتدمير في التدفق العادي. لا يشمل استثناءات C++ أو RTTI أو الإرسال الافتراضي أو التنظيف أثناء انتشار استثناء خارجي.

```sh
neverc translate --from cpp replacement.cpp --profile cpp-core-v2 \
  --target aarch64-unknown-linux-gnu -o replacement.nc -- -std=c++17
neverc --no-default-config --target=aarch64-unknown-linux-gnu \
  -fdyncode -fdyncode-hosted -fdyncode-entry=replacement \
  -fdyncode-keep-obj=replacement.o -fdyncode-report=replacement.audit.json \
  -o replacement.bin replacement.nc
```

يسجل التدقيق `execution_profile="hosted-v1"` و`keep_unwind=true` و`target_triple`. يحسب `input_digest` بصمة الكائن المحفوظ و`output_digest` بصمة الحمولة النهائية بعد النقل والتحقق. يجب التحقق من البصمتين والكود والمدخل وتسجيل بيانات فك المكدس الحقيقية.

</div>
