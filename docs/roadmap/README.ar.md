<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← فهرس التوثيق](../README.ar.md)

# خارطة طريق NeverC

يوضح هذا المستند الاتجاهات الرئيسية المخطط لها لمشروع NeverC بعد مُجمِّع dyncode الحالي وأوقات التشغيل المدمجة.

---

## 1. المكتبة القياسية (`std`)

سيوفر NeverC مكتبة قياسية شاملة على غرار مكتبة Go القياسية — حزم جاهزة للاستخدام تغطي احتياجات برمجة النظم الشائعة بدون تبعيات خارجية.

### الحزم المخطط لها

| الحزمة | الوصف |
|--------|-------|
| `fmt` | إدخال/إخراج منسق (عائلة printf + امتدادات آمنة النوع) |
| `os` | تفاعل مع نظام التشغيل: متغيرات البيئة، إدارة العمليات، أذونات الملفات |
| `io` | واجهات Reader/Writer، إدخال/إخراج مُخزَّن، أدوات الأنابيب |
| `fs` | عمليات نظام الملفات: تجوال، glob، ملفات مؤقتة، كتابة ذرية |
| `net` | مآخذ TCP/UDP، تحليل DNS، عميل/خادم HTTP |
| `net/http` | عميل وخادم HTTP/1.1 و HTTP/2 |
| `crypto` | تجزئة (SHA-256، SHA-512، BLAKE3)، HMAC، AES، ChaCha20، RSA، Ed25519 |
| `encoding` | JSON، Base64، Hex، CSV، ثنائي (ترتيب بايت صغير/كبير) |
| `sync` | Mutex، RWLock، WaitGroup، Once، عمليات ذرية |
| `time` | ساعة رتيبة/جدارية، مدة، مؤقتات، تنسيق |
| `bytes` | معالجة شرائح البايت، مخزن مؤقت |
| `math` | ثوابت، دوال أساسية، توليد أعداد عشوائية |
| `sort` | فرز وبحث عام |
| `container` | قائمة مرتبطة، كومة، مخزن حلقي |
| `log` | تسجيل منظم بمستويات |
| `flag` | تحليل أعلام سطر الأوامر |
| `path` | معالجة المسارات (POSIX و Windows) |
| `regexp` | مطابقة التعبيرات النمطية (صيغة RE2) |
| `compress` | gzip، zlib، zstd، lz4 |
| `hash` | CRC32، CRC64، FNV، xxHash |
| `unicode` | جداول Unicode، طي الحالة، تحويل UTF-8/UTF-16 |

### مبادئ التصميم

- **C23 نقي** — كل حزمة تُجمَّع كمعيار NeverC/C23؛ بدون C++ مخفي أو مُجمِّع خاص بمنصة
- **صفر تبعيات خارجية** — المكتبة القياسية مضمنة كـ LLVM bitcode في المُجمِّع، مثل المدمجات الحالية `string` و `mimalloc`
- **متعدد المنصات** — جميع الحزم تعمل على macOS و Linux و Windows (x86_64 / AArch64)
- **متوافق مع dyncode** — الحزم ذات المعنى في وضع freestanding (مثل: `crypto`، `encoding`، `bytes`) تعمل مع `-fdyncode`

---

## 2. Obfuscation Plugin Suite (`neverc-obfuscation`)

NeverC will ship a first-party suite of code obfuscation plugins — reference implementations that demonstrate the Plugin API's full capabilities while providing production-grade code protection out of the box.

### Planned Plugins

| Plugin | Interpose Point | Description |
|--------|-----------|-------------|
| Junk Code Insertion | `RunAfterFinalMIR` | Insert semantically dead but syntactically valid instruction sequences between real basic blocks |
| Opaque Predicates | `RunBeforePreEmit` | Insert always-true/always-false branches guarded by number-theoretic invariants; adds dead paths that confuse analysis |
| Control Flow Flattening | `RunAfterStackify` | Scatter basic blocks into a switch-dispatched loop; destroys natural CFG structure for decompilers |
| Anti-Tamper | `RunPostFinalize` | Embed self-integrity checks (CRC/hash of code sections) that trigger failure on patching |
| Polymorphic Engine | `RunPostExtract` | Seed-based output variation — each compilation produces functionally equivalent but structurally different code; defeats signature-based detection |
| MBA (Mixed Boolean Arithmetic) | `RunAfterInlining` | Replace arithmetic/boolean expressions with equivalent but opaque MBA forms (e.g., `x + y` → `(x ^ y) + 2 * (x & y)` chains); resists symbolic execution |
| VM (Code Virtualization) | `RunAfterFinalIR` | Convert functions into custom bytecode executed by an embedded interpreter; defeats static disassembly and signature matching |

### Design Principles

- **Pure Plugin API** — every obfuscation ships as a `.dll` / `.so` / `.dylib` plugin; no compiler fork required
- **Composable** — plugins stack: apply MBA first, then flatten, then virtualize — each pass is independent
- **Configurable** — per-function annotations (`__attribute__((obfuscate("vm")))`) to selectively protect hot paths without whole-program overhead
- **Auditable** — each plugin logs its transformations for security review; before/after IR diff output available via `-fdyncode-dump-ir`
- **DynCode-compatible** — all plugins work in `-fdyncode` mode; generated code remains position-independent

---

## 3. مكتبة مكونات واجهة المستخدم (`neverc-ui`)

سيوفر NeverC مكتبة مكونات واجهة مستخدم متعددة المنصات مستوحاة من Qt — لكن مع محرك عرض أمامي HTML/JS/CSS، مناسب بطبيعته لتصميم الواجهات بالذكاء الاصطناعي.

### الأهداف

- **بنية قائمة على المكونات** — نوافذ، أزرار، حقول نص، قوائم، أشجار، جداول، قوائم منسدلة، مربعات حوار، علامات تبويب وحاويات تخطيط كأنواع C من الدرجة الأولى
- **عارض HTML/JS/CSS** — يتم عرض واجهة المستخدم عبر محرك متصفح خفيف مدمج؛ المطورون يكتبون منطق C، والطبقة المرئية تستخدم تقنيات الويب القياسية
- **مصمم مرئي بالسحب والإفلات** — أداة بناء GUI ترافقية تولد كود C متوافق مع NeverC، تتيح النمذجة السريعة بدون كتابة كود التخطيط يدوياً
- **سير عمل تصميم أصلي للذكاء الاصطناعي** — نماذج LLM يمكنها توليد منطق الأعمال بلغة C وتخطيط HTML/CSS في مرة واحدة
- **مظهر أصلي** — سمات تكيفية حسب المنصة (macOS، Windows، Linux) عبر متغيرات CSS واكتشاف خطوط/ألوان النظام
- **تضمين خفيف** — العارض يُقدم كوقت تشغيل مدمج (مثل `string` / `mimalloc`)؛ بدون حمل بحجم Electron
- **نظام أحداث** — دوال رد اتصال C لتفاعلات المستخدم (نقر، إدخال، تغيير حجم، سحب، لوحة مفاتيح، أحداث مخصصة)
- **ربط البيانات** — ربط تصريحي بين هياكل C وحالة واجهة المستخدم؛ التغييرات تنتشر تلقائياً
- **عرض مخصص** — منفذ إلى canvas/WebGL الخام لواجهات الألعاب، تصور البيانات، أو العناصر المخصصة

### لماذا HTML/CSS لمكتبة واجهة مستخدم C؟

- كل نموذج ذكاء اصطناعي يعرف بالفعل HTML/CSS — توليد كود واجهة المستخدم لا يتطلب تدريباً متخصصاً
- تقنيات الويب هي نظام التخطيط الأكثر اختباراً؛ لا حاجة لإعادة اختراع flexbox أو grid أو عرض النص
- أدوات بحث الأمن (لوحات المعلومات، عارضات ست عشرية، فاحصات الحزم) تستفيد من واجهات غنية بالأنماط بدون تعلم واجهة برمجة عناصر واجهة مستخدم خاصة
- المصمم المرئي يصدر قوالب HTML تعمل في تطبيق NeverC وفي متصفح مستقل

---

## 4. IDE & Language Tooling (`neverc-ide`)

NeverC will provide first-class IDE support for the `.nc` language extension — a VSCode extension for immediate productivity and a standalone NeverC IDE for a fully integrated development experience.

### VSCode Extension

- **Syntax highlighting** — full `.nc` grammar with semantic token support for NeverC-specific types (`string`, `u8`–`u64`, `i8`–`i64`, `f32`, `f64`)
- **IntelliSense** — auto-completion for built-in types, dot-call methods (`.c_str()`, `.len()`, `.starts_with()`), and `#include` paths
- **Diagnostics** — real-time error and warning display from `neverc` compiler output
- **Go to definition** — jump to function, struct, and macro definitions across translation units
- **Hover documentation** — inline docs for built-in functions, compiler intrinsics, and standard library packages
- **Code actions** — quick-fix suggestions for common errors, auto-import for `std` packages
- **Debugging** — integrated LLDB/GDB debug adapter with breakpoint, step, and variable inspection support
- **DynCode mode** — syntax-aware features for `-fdyncode` pipelines: bad-byte highlighting, dyncode size display, target-specific completions
- **Plugin API integration** — plugin interpose point visualization and scaffolding

### Standalone IDE

- **Built on NeverC UI (`neverc-ui`)** — the IDE is itself a showcase of the HTML/JS/CSS component library, dogfooding the UI framework
- **Integrated terminal** — build, run, and debug without leaving the IDE
- **Visual dyncode pipeline** — graphical view of the IR → MIR → extraction pipeline with pass-by-pass output inspection
- **Project templates** — one-click scaffolding for hosted binaries, dyncode, EVM contracts, and Solana programs
- **AI-assisted coding** — built-in LLM integration that understands NeverC semantics, generates `.nc` code, and explains compiler diagnostics
- **Cross-compilation dashboard** — visual target selector with platform matrix and build status

### Why Both VSCode and Standalone?

- VSCode captures the majority of developers who already live in that ecosystem
- The standalone IDE provides a deeper, purpose-built experience for security researchers who want dyncode pipeline visualization and integrated binary analysis
- Both share the same language server backend — improvements benefit both simultaneously

---


## 5. واجهة EVM الخلفية للعقود الذكية

سيدعم NeverC تجميع شفرة C المصدرية إلى بايت كود EVM (آلة إيثريوم الافتراضية) — مما يتيح للمطورين كتابة العقود الذكية بلغة C بدلاً من Solidity.

### الأهداف

- **هدف LLVM خلفي جديد** — ثلاثية الهدف `evm` (مثال: `neverc --target=evm hello.c -o contract.bin`)
- **توافق ABI** — توليد واصفات ABI متوافقة مع Solidity للتفاعل مع أدوات إيثريوم (Hardhat، Foundry، ethers.js)
- **تخطيط التخزين** — ربط هياكل C بفتحات تخزين EVM مع تخطيط حتمي
- **أوليات EVM مدمجة** — `msg.sender`، `msg.value`، `block.number`، `tx.origin` كمتغيرات مدمجة أو intrinsics
- **معدِّلات payable / view / pure** — سمات وظيفية تُعيَّن إلى دلالات رؤية Solidity
- **إصدار الأحداث** — توليد أكواد تشغيل `LOG0`–`LOG4` من استدعاءات الدوال المُعلَّمة
- **تحسين Gas** — مرورات IR لتقليل تكلفة Gas (جدولة المكدس، طي الثوابت، حذف التخزين الميت)
- **revert / require** — أوليات معالجة الأخطاء مع رسائل مخصصة

### لماذا C لـ EVM؟

- بناء جملة Solidity مألوف لمطوري JavaScript لكنه غريب على مبرمجي النظم؛ C عالمية
- خط أنابيب تحسين IR الحالي في NeverC يمكنه إنتاج بايت كود أكثر إحكاماً من `solc` في حالات كثيرة
- باحثو الأمن يفكرون بالفعل بلغة C — كتابة أدوات التدقيق والـ fuzzer بلغة C لعقود C أمر طبيعي
- واجهة الإضافات API تسمح بمرورات مخصصة لتحليل Gas واكتشاف الثغرات وقت التجميع

---

## 6. واجهة Solana eBPF الخلفية

سيدعم NeverC تجميع شفرة C المصدرية إلى بايت كود eBPF لـ Solana — مما يتيح تطوير برامج على السلسلة بلغة C.

### الأهداف

- **هدف eBPF** — ثلاثية الهدف `sbf` (Solana BPF) (مثال: `neverc --target=sbf-solana hello.c -o program.so`)
- **ربط بوقت تشغيل Solana** — ملفات رأس مدمجة لاستدعاءات نظام Solana: `sol_invoke_signed`، `sol_log`، `sol_memcpy`، هياكل معلومات الحساب
- **نموذج الحسابات** — تراكبات هياكل C على بيانات حسابات Solana مع تسلسل/إلغاء تسلسل تلقائي
- **CPI (استدعاء البرنامج المتقاطع)** — أغلفة آمنة النوع لاستدعاء برامج أخرى على السلسلة
- **PDA (عنوان مشتق من البرنامج)** — دوال مدمجة لاشتقاق والتحقق من PDA
- **الوعي بميزانية الحوسبة** — تحذيرات المُجمِّع عند تجاوز وحدات الحوسبة المقدرة لحدود البرنامج
- **توافق Anchor** — توليد IDL اختياري للتشغيل البيني مع واجهات Anchor الأمامية

### لماذا C لـ Solana؟

- وقت تشغيل Solana ينفذ بالفعل eBPF — C هي اللغة المصدرية الأكثر طبيعية لأهداف BPF
- سلاسل أدوات C-BPF الحالية (clang + solana-bpf) تتطلب إعداداً معقداً؛ NeverC يجمع كل شيء في ملف ثنائي واحد
- البرامج الحرجة الأداء تستفيد من تجريد C بدون حمل إضافي ومرورات تحسين NeverC
- تجربة تجميع dyncode (مستقل عن الموضع، وقت تشغيل أدنى) تنطبق مباشرة على قيود برامج السلسلة

---

## الجدول الزمني

هذه الميزات في مرحلة البحث والتصميم. لم يتم الالتزام بتواريخ إصدار محددة. سيتم تحديث التقدم في هذا المستند والإعلان عنه في صفحة إصدارات المشروع.

| الميزة | الحالة |
|--------|--------|
| المكتبة القياسية (`std`) | بحث / تصميم |
| Obfuscation Plugin Suite (`neverc-obfuscation`) | بحث / تصميم |
| مكتبة مكونات واجهة المستخدم (`neverc-ui`) | بحث / تصميم |
| بيئة التطوير وأدوات اللغة (`neverc-ide`) | بحث / تصميم |
| واجهة EVM الخلفية للعقود الذكية | بحث / تصميم |
| واجهة Solana eBPF الخلفية | بحث / تصميم |

</div>
