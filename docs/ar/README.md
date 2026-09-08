<div dir="rtl">

**اللغات**: [English](../README.md) | [简体中文](../zh-CN/README.md) | [繁體中文](../zh-TW/README.md) | [日本語](../ja/README.md) | [한국어](../ko/README.md) | [Français](../fr/README.md) | [Deutsch](../de/README.md) | [Español](../es/README.md) | [Italiano](../it/README.md) | [Русский](../ru/README.md) | [العربية](README.md)

[← مشروع NeverC](project.md)

> **Tip:** Use the language bar above; links on this page point to the same locale (dyncode README and breadcrumbs).

# توثيق NeverC

ملاحظات التصميم ومرجع API والأدلة لكل نظام فرعي في NeverC.

---

## مُجمِّع dyncode

مسار تجميع dyncode هو محور أبحاث NeverC الأساسي. للبنية وخيارات CLI ومصفوفة المنصات والأمثلة:

**[مُجمِّع dyncode →](dyncode-compiler.md)**

| المستند | الوصف |
|---------|--------|
| [README](dyncode-compiler.md) | نظرة عامة، بدء سريع، الأهداف المدعومة |
| [Pipeline & PIC](dyncode-compiler-pipeline-and-pic.md) | تصميم IR → كائن → استخراج |
| [IR Pass Design](dyncode-compiler-ir-pass-design.md) | مبررات كل مرور IR |
| [MIR Pass Design](dyncode-compiler-mir-pass-design.md) | مرورات MIR للخلفية |
| [Kernel-Mode DynCode](dyncode-compiler-kernel-mode-dyncode.md) | تجميع Ring-0 |
| [Cross-Platform Architecture](dyncode-compiler-cross-platform-architecture.md) | `TargetDesc` والمستخرجات |
| [Platform Extension Guide](dyncode-compiler-platform-extension-guide.md) | إضافة منصة |
| [ARM64 Assembly Tutorial](dyncode-compiler-arm64-assembly-tutorial.md) | تعليمات ARM64 من منظور dyncode |
| [Roadmap](dyncode-compiler-roadmap.md) | العمل المخطط |
| [Progress](dyncode-compiler-progress.md) | حالة التنفيذ |

---

## امتداد الملف `.nc`

يتعرف NeverC على `.nc` كامتداد ملف المصدر الأصلي. مع `.nc`، جميع امتدادات لغة NeverC (`-fneverc-types`، `-fbuiltin-string`) تُفعّل تلقائيًا — بدون أعلام إضافية.

**[امتداد `.nc` →](nc-extension.md)**

---

## أوقات التشغيل المدمجة

يوسع NeverC لغة C القياسية بأوقات تشغيل مدمجة كـ LLVM bitcode. كل منها يُتحكم به عبر علم `-fbuiltin-<name>`. ملفات `.nc` تُفعّل `string` تلقائيًا.

**[نظام وقت التشغيل المدمج →](builtins.md)**

| المدمج | العلم | الوصف |
|--------|-------|-------|
| [السلسلة المدمجة](builtins-string.md) | `-fbuiltin-string` | نوع `string` بدلالة القيمة، طرق بالنقطة، إدارة ذاكرة تلقائية، UTF-8 أصلي |
| [mimalloc المدمج](builtins-mimalloc.md) | `-fbuiltin-mimalloc` | تجاوز مخصص ذاكرة `mimalloc` عالي الأداء شفاف `malloc`/`free`/`calloc`/`realloc` |
| [تشفير السلاسل (xorstr)](builtins-xorstr.md) | `-fencrypt-call-strings` | تشفير مستقل لكل مثيل، ختم متأخر إلزامي، توسيع لكل موضع استدعاء وتنظيف متطاير للمكدس |
| [تجزئة السلاسل (strhash)](builtins-strhash.md) | `-fstrhash-algo` / `-fstrhash-fold` | تجزئة السلاسل وقت الترجمة بنفس الخوارزمية وقت التشغيل، طي IR اختياري |

---

## واجهة الإضافات API

يفتح NeverC سلسلة أدواته بالكامل عبر واجهة C ABI خالصة. والإضافة وحدة مشتركة (`.dll` / `.so` / `.dylib`) ترتبط بأي مرحلة من مراحل الترجمة المسمّاة البالغ عددها 130 — من تحليل سطر الأوامر حتى الصورة المربوطة النهائية — بوصفها مراقبًا أو مُعترِضًا أو مزوِّدًا بديلًا. وحزمة التطوير ترويسات فقط: بلا ترويسات LLVM وبلا ربط بالمُترجِم.

**[واجهة الإضافات API →](plugin-api.md)**

| المستند | الوصف |
|---------|--------|
| [README](plugin-api.md) | نقطة الدخول، المراحل، التفاوض على الواجهات، التسجيل، قواعد ABI |
| [إضافات Python](plugin-api-python.md) | Python مضمّن اختياري، ودورة الحياة، والخيارات، وobservers للقراءة فقط، والتشخيصات، والقيود |
| [واجهة المُشغِّل](plugin-api-driver.md) | سطر الأوامر، اختيار سلسلة الأدوات، رسم الإجراءات، رسم المهام |
| [واجهة المصادر والإدخال/الإخراج](plugin-api-source.md) | مزوِّدو VFS، مواقع المصدر، المخازن المؤقتة، مصارف الإخراج، التبعيات |
| [واجهة المعالج المسبق](plugin-api-prep.md) | الرموز، الماكرو، البراغما، التضمينات، استعلامات الميزات، 39 نوعًا من الأحداث |
| [واجهة الشجرة النحوية والدلالات](plugin-api-ast-sema.md) | توسيع المُحلِّل، تعديل الشجرة النحوية، البحث عن الأسماء، الأنواع، الثوابت |
| [واجهة IR](plugin-api-ir.md) | قراءة LLVM IR، البناء المعاملاتي، التحليلات، المرورات، المزوِّدون |
| [واجهة MIR](plugin-api-mir.md) | دوال الآلة، السجلات، إطارات المكدس، مرورات وتحليلات MIR |
| [الهدف وMC والتجميع والكائنات](plugin-api-target-mc-object.md) | تسجيل الأهداف، اصطلاحات الاستدعاء، ترميز MC، رسوم الكائنات |
| [واجهة الربط وLTO](plugin-api-link-lto.md) | رسم الربط، حل الرموز، GC/ICF، مزوِّدو الرابط وLTO |
| [واجهة DynCode](plugin-api-dyncode.md) | صور مسطّحة مستقلة عن الموضع، خفض الاستيرادات، ترميز مجموعة المحارف |
| [اصطلاحات استدعاء مخصّصة](plugin-api-custom-callconv.md) | إضافات اصطلاحات الاستدعاء المُوجَّهة بالبيانات |

---

## خارطة الطريق

الاتجاهات الرئيسية المخطط لها لمشروع NeverC: المكتبة القياسية، واجهة EVM الخلفية للعقود الذكية، واجهة Solana eBPF الخلفية.

**[خارطة الطريق →](roadmap.md)**

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
| [`neverc run`](run.md) | تجميع وتشغيل محلي وحذف ملف ثنائي مؤقت (بأسلوب `go run`) |
| [`neverc translate`](translate.md) | ترجمة C++ إلى NeverC |
| [`neverc update`](update.md) | ترقية أو تخفيض تثبيت release (المترجم وبيئات runtime المثبّتة بوسم واحد) |
| [`neverc runtime`](runtime.md) | تثبيت أو سرد أو تحديث أو إزالة sysroot للترجمة المتقاطعة |
| [`neverc build` / `neverc make`](build.md) | مشغّل متوافق مع GNU Make لملفات Makefile للأمثلة والمشاريع |
| [ملفات الإصدار الثنائية و`--strip`](release-builds.md) | إزالة الرموز غير اللازمة وتصحيح المصدر، مع إعادة تسمية رموز `.ko` بنيويًا ومراعاة النواة (ليس hash ولا encryption) |

---

## أهداف أمان Windows

| المستند | الوصف |
|---------|--------|
| [ملفات DLL لجيوب VBS](vbs-enclave.md) | ربط صور جيوب VBS المتوافقة مع Microsoft والتحقق منها ومعالجتها وتوقيعها وتحميلها |

---

## التطوير المحلي

بناء NeverC من الكود المصدري وإعداد بيئة التطوير المحلية، بما في ذلك تهيئة PATH.

**[التطوير المحلي →](local-dev.md)**

---

## أمثلة

أمثلة قابلة للبناء توضح قدرات التجميع المتقاطع في NeverC. جميعها تُجمَّع من macOS / Linux.

**[أمثلة →](examples.md)**

---

## نسب العمل وتوثيق المصادر

**[دليل نسب العمل →](attribution.md)**

</div>
