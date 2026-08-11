<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مشروع NeverC](i18n/README.ar.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (dyncode README and breadcrumbs).

# توثيق NeverC

ملاحظات التصميم ومرجع API والأدلة لكل نظام فرعي في NeverC.

---

## مُجمِّع dyncode

مسار تجميع dyncode هو محور أبحاث NeverC الأساسي. للبنية وخيارات CLI ومصفوفة المنصات والأمثلة:

**[مُجمِّع dyncode →](dyncode-compiler/README.ar.md)**

| المستند | الوصف |
|---------|--------|
| [README](dyncode-compiler/README.ar.md) | نظرة عامة، بدء سريع، الأهداف المدعومة |
| [Pipeline & PIC](dyncode-compiler/pipeline-and-pic/README.ar.md) | تصميم IR → كائن → استخراج |
| [IR Pass Design](dyncode-compiler/ir-pass-design/README.ar.md) | مبررات كل مرور IR |
| [MIR Pass Design](dyncode-compiler/mir-pass-design/README.ar.md) | مرورات MIR للخلفية |
| [Kernel-Mode DynCode](dyncode-compiler/kernel-mode-dyncode/README.ar.md) | تجميع Ring-0 |
| [Cross-Platform Architecture](dyncode-compiler/cross-platform-architecture/README.ar.md) | `TargetDesc` والمستخرجات |
| [Platform Extension Guide](dyncode-compiler/platform-extension-guide/README.ar.md) | إضافة منصة |
| [ARM64 Assembly Tutorial](dyncode-compiler/arm64-assembly-tutorial/README.ar.md) | تعليمات ARM64 من منظور dyncode |
| [Roadmap](dyncode-compiler/roadmap/README.ar.md) | العمل المخطط |
| [Progress](dyncode-compiler/progress/README.ar.md) | حالة التنفيذ |

---

## امتداد الملف `.nc`

يتعرف NeverC على `.nc` كامتداد ملف المصدر الأصلي. مع `.nc`، جميع امتدادات لغة NeverC (`-fneverc-types`، `-fbuiltin-string`) تُفعّل تلقائيًا — بدون أعلام إضافية.

**[امتداد `.nc` →](nc-extension/README.ar.md)**

---

## أوقات التشغيل المدمجة

يوسع NeverC لغة C القياسية بأوقات تشغيل مدمجة كـ LLVM bitcode. كل منها يُتحكم به عبر علم `-fbuiltin-<name>`. ملفات `.nc` تُفعّل `string` تلقائيًا.

**[نظام وقت التشغيل المدمج →](builtins/README.ar.md)**

| المدمج | العلم | الوصف |
|--------|-------|-------|
| [السلسلة المدمجة](builtins/string/README.ar.md) | `-fbuiltin-string` | نوع `string` بدلالة القيمة، طرق بالنقطة، إدارة ذاكرة تلقائية، UTF-8 أصلي |
| [mimalloc المدمج](builtins/mimalloc/README.ar.md) | `-fbuiltin-mimalloc` | تجاوز مخصص ذاكرة `mimalloc` عالي الأداء شفاف `malloc`/`free`/`calloc`/`realloc` |
| [تشفير السلاسل (xorstr)](builtins/xorstr/README.ar.md) | `-fencrypt-call-strings` | تشفير مستقل لكل مثيل، ختم متأخر إلزامي، توسيع لكل موضع استدعاء وتنظيف متطاير للمكدس |
| [تجزئة السلاسل (strhash)](builtins/strhash/README.ar.md) | `-fstrhash-algo` / `-fstrhash-fold` | تجزئة السلاسل وقت الترجمة بنفس الخوارزمية وقت التشغيل، طي IR اختياري |

---

## واجهة الإضافات API

يفتح NeverC سلسلة أدواته بالكامل عبر واجهة C ABI خالصة. والإضافة وحدة مشتركة (`.dll` / `.so` / `.dylib`) ترتبط بأي مرحلة من مراحل الترجمة المسمّاة البالغ عددها 130 — من تحليل سطر الأوامر حتى الصورة المربوطة النهائية — بوصفها مراقبًا أو مُعترِضًا أو مزوِّدًا بديلًا. وحزمة التطوير ترويسات فقط: بلا ترويسات LLVM وبلا ربط بالمُترجِم.

**[واجهة الإضافات API →](plugin-api/README.ar.md)**

| المستند | الوصف |
|---------|--------|
| [README](plugin-api/README.ar.md) | نقطة الدخول، المراحل، التفاوض على الواجهات، التسجيل، قواعد ABI |
| [إضافات Python](plugin-api/python.ar.md) | Python مضمّن اختياري، ودورة الحياة، والخيارات، وobservers للقراءة فقط، والتشخيصات، والقيود |
| [واجهة المُشغِّل](plugin-api/driver.ar.md) | سطر الأوامر، اختيار سلسلة الأدوات، رسم الإجراءات، رسم المهام |
| [واجهة المصادر والإدخال/الإخراج](plugin-api/source.ar.md) | مزوِّدو VFS، مواقع المصدر، المخازن المؤقتة، مصارف الإخراج، التبعيات |
| [واجهة المعالج المسبق](plugin-api/prep.ar.md) | الرموز، الماكرو، البراغما، التضمينات، استعلامات الميزات، 39 نوعًا من الأحداث |
| [واجهة الشجرة النحوية والدلالات](plugin-api/ast-sema.ar.md) | توسيع المُحلِّل، تعديل الشجرة النحوية، البحث عن الأسماء، الأنواع، الثوابت |
| [واجهة IR](plugin-api/ir.ar.md) | قراءة LLVM IR، البناء المعاملاتي، التحليلات، المرورات، المزوِّدون |
| [واجهة MIR](plugin-api/mir.ar.md) | دوال الآلة، السجلات، إطارات المكدس، مرورات وتحليلات MIR |
| [الهدف وMC والتجميع والكائنات](plugin-api/target-mc-object.ar.md) | تسجيل الأهداف، اصطلاحات الاستدعاء، ترميز MC، رسوم الكائنات |
| [واجهة الربط وLTO](plugin-api/link-lto.ar.md) | رسم الربط، حل الرموز، GC/ICF، مزوِّدو الرابط وLTO |
| [واجهة DynCode](plugin-api/dyncode.ar.md) | صور مسطّحة مستقلة عن الموضع، خفض الاستيرادات، ترميز مجموعة المحارف |
| [اصطلاحات استدعاء مخصّصة](plugin-api/custom-callconv/README.ar.md) | إضافات اصطلاحات الاستدعاء المُوجَّهة بالبيانات |

---

## خارطة الطريق

الاتجاهات الرئيسية المخطط لها لمشروع NeverC: المكتبة القياسية، واجهة EVM الخلفية للعقود الذكية، واجهة Solana eBPF الخلفية.

**[خارطة الطريق →](roadmap/README.ar.md)**

| الميزة | الوصف |
|--------|-------|
| المكتبة القياسية (`std`) | حزم على طراز Go: `fmt`، `os`، `io`، `net`، `crypto`، `encoding`، `sync` والمزيد |
| حزمة إضافات التشويش (`neverc-obfuscation`) | VM، MBA، تسطيح تدفق التحكم، محرك متعدد الأشكال، مضاد للتلاعب — إضافات من الطرف الأول |
| مكتبة مكونات واجهة المستخدم (`neverc-ui`) | واجهة مستخدم متعددة المنصات بأسلوب Qt، عارض HTML/JS/CSS، مصمم سحب وإفلات، سير عمل أصلي للذكاء الاصطناعي |
| بيئة التطوير وأدوات اللغة (`neverc-ide`) | إضافة VSCode + بيئة تطوير مستقلة لملفات `.nc`، IntelliSense، تصحيح الأخطاء، تصور مسار dyncode |
| عقود EVM الذكية | تجميع C إلى بايت كود EVM — كتابة العقود بلغة C بدلاً من Solidity |
| Solana eBPF | تجميع C إلى بايت كود eBPF لـ Solana — تطوير برامج على السلسلة بلغة C |

---

## أدوات CLI

أوامر موجهة للمستخدم تتجاوز التجميع المفرد.

| المستند | الوصف |
|---------|--------|
| [`neverc run`](run/README.ar.md) | تجميع وتشغيل محلي وحذف ملف ثنائي مؤقت (بأسلوب `go run`) |
| [`neverc update`](update/README.ar.md) | ترقية أو تخفيض تثبيت release (المترجم وبيئات runtime المثبّتة بوسم واحد) |
| [`neverc runtime`](runtime/README.ar.md) | تثبيت أو سرد أو تحديث أو إزالة sysroot للترجمة المتقاطعة |
| [`neverc build` / `neverc make`](build/README.ar.md) | مشغّل متوافق مع GNU Make لملفات Makefile للأمثلة والمشاريع |
| [ملفات الإصدار الثنائية و`--strip`](release-builds/README.ar.md) | إزالة الرموز غير اللازمة وتصحيح المصدر، مع إعادة تسمية رموز `.ko` بنيويًا ومراعاة النواة (ليس hash ولا encryption) |

---

## التطوير المحلي

بناء NeverC من الكود المصدري وإعداد بيئة التطوير المحلية، بما في ذلك تهيئة PATH.

**[التطوير المحلي →](local-dev/README.ar.md)**

---

## أمثلة

أمثلة قابلة للبناء توضح قدرات التجميع المتقاطع في NeverC. جميعها تُجمَّع من macOS / Linux.

**[أمثلة →](examples/README.ar.md)**

</div>
