<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مُجمِّع shellcode](../README.ar.md)

# مُجمِّع shellcode — تتبع التقدم

## المرحلة 0 — macOS arm64 MVP (تم التسليم)

- [x] هيكل الأدلة و CMake (مكتبة `nevercShellcode`)
- [x] `ZeroRelocPass`: مرحلتان (Prep + Stackify)، نقل المتغيرات العامة القابلة للتعديل إلى المكدس تلقائيًا
- [x] `Data2TextPass`: مرحلتان (مصفوفات ثابتة → تخزين مقطعي على المكدس؛ تقسيم ثوابت المتجهات بعد SROA؛ ConstantFP → أنماط بتات محمّلة بـ volatile)
- [x] `SyscallStubPass`: قائمة بيضاء مدفوعة بجدول تغطي Darwin BSD / Linux arm64 / Linux x86_64 / Android syscalls
- [x] `AllBlrPass`: إعادة كتابة عدوانية اختيارية للاستدعاءات غير المباشرة
- [x] `ShellcodeExtractor`: Mach-O `.o` → `.bin` مسطح مع ترقيع عمليات النقل داخل القسم
- [x] خيارات CLI عبر `neverc/include/neverc/Invoke/Options.td.h` المُولَّد: `-fshellcode`، `-fshellcode-all-blr`، `-mshellcode-syscall`، `-fshellcode-keep-obj=`، `-fshellcode-entry=`
- [x] PIC افتراضي على جميع المنصات (`isPICDefault()` تعيد `true` عالميًا)
- [x] نقل تكراري عام إلى المكدس (جداول مؤشرات الدوال، جداول مؤشرات السلاسل، جداول الهياكل المتداخلة، مُهيئات ConstantExpr GEP/BitCast)
- [x] `IndirectBrPass`: GCC computed-goto (`&&label`) → switch، مع مشاركة جداول مواقع إرسال متعددة
- [x] تضمين ثوابت المتجهات SIMD (`inlineVectorConstants`)
- [x] تخفيض تلقائي لـ `_Thread_local` إلى static
- [x] محمّل macOS arm64 أصلي (MAP_JIT + تنظيف i-cache)

**الاختبارات**: 108/108 تأكيدات shellcode ناجحة. أحجام الثنائيات: `add` 8B، `fib` 64B، `hello` 64B، `big_const` 632B.

## المرحلة 1 — Linux / Android / Windows عبر المنصات (تم التسليم)

- [x] تجريد `TargetDesc`: اختلافات المنصات مدفوعة بجدول
- [x] دلالات `-mshellcode-syscall` عبر المنصات (تحل محل `-mshellcode-libsystem` الخاص بـ Darwin)
- [x] جداول أرقام syscall لـ Linux / Android (Darwin BSD 100+، Linux arm64 130+، Linux x86_64 150+)
- [x] إعادة هيكلة `ShellcodeExtractor` إلى `MachOExtractor` / `ELFExtractor` / `COFFExtractor`
- [x] مستخرج ELF (arm64: `R_AARCH64_CALL26`/`JUMP26`/`ADR_PREL_PG_HI21`/إلخ؛ x86_64: `R_X86_64_PC32`/`PLT32`)
- [x] مستخرج COFF (arm64: `IMAGE_REL_ARM64_BRANCH26`/إلخ؛ x86_64: `IMAGE_REL_AMD64_REL32`/إلخ)
- [x] مرور استيراد PEB لـ Windows (`WinPEBImportPass`) مع محلل PEB walk حقيقي
- [x] قائمة بيضاء Win32 API متعددة DLL (~210 API عبر kernel32/ntdll/user32/ws2_32/advapi32/shell32)
- [x] `MemIntrinPass`: memcpy/memset/memmove/memcmp/bcmp/bzero/memchr + strlen/strcpy/strcmp/إلخ → مساعدات حلقة بايت مضمّنة
- [x] `CompilerRtPass`: قسمة/باقي `__int128` → مساعدات قسمة طويلة مضمّنة
- [x] دعم واجهة Windows `aarch64-pc-windows-msvc`
- [x] `MIRPrepPass`: إزالة تعليمات زائفة عبر المنصات (CFI/EH/XRay/StackMap/SEH/FENTRY/إلخ)
- [x] خطافات تشويش MIR + مستوى البايت (11 خطافًا عبر طبقات IR/MIR/تدفق البايت)
- [x] تخفيض تلقائي لـ AArch64 غير Darwin `long double` إلى binary64
- [x] ملفات رأس وسيطة shellcode: `<windows.h>`، `<unistd.h>`، `<fcntl.h>`، `<sys/stat.h>`، `<sys/mman.h>`، `<string.h>`، `<stdlib.h>`
- [x] طبقة توافق POSIX لـ Windows (13 جسرًا POSIX→Win32: write→WriteFile، mmap→VirtualAlloc، إلخ)
- [x] إصلاح تلقائي للإعلانات الضمنية K&R (50+ توقيع POSIX قياسي)
- [x] تنقية مدفوعة بجدول (ترميز ثابت لفروع العمارة → صفر)
- [x] `KernelImportPass`: إعادة كتابة تلقائية لمواقع الاستدعاء ring-0 مدعومة بمحلل
- [x] تشخيصات مدفوعة بجدول أسماء مساعدات النواة (`KernelHelperNames.def`)
- [x] `<neverc/kernel.h>` لاتفاقيات نقطة الدخول ring-0
- [x] فرض إزاحة صفر لنقطة الدخول (`placeEntryFirst`)
- [x] خط أنابيب الإنهاء: SDK مُعيد كتابة البايتات المحظورة + SDK مشفّر مجموعة الأحرف + قيود الحجم
- [x] واجهة الإضافات C خارج الشجرة (`NevercPluginAPI.h`): 11 نقطة ربط shellcode (`NEVERC_HOOK_SC_*`)
- [x] حقن `-mno-implicit-float` لـ x86_64 (يمنع تسرب مجمع ثوابت SSE للخلفية)
- [x] محمّلات عبر المنصات (macOS/Linux/Windows)

**الاختبارات**: 743+ تأكيدات shellcode، جميعها ناجحة عبر 8 أهداف ثلاثية. مجموعة اختبارات NeverC الكاملة: 1000+ اختبار ناجح.

## المرحلة 2 — مشفّر قابل للطباعة / أبجدي رقمي (مخطط)

- [ ] مشفّر shellcode قابل للطباعة ARM64 (مجموعة تعليمات فرعية 0x20–0x7e)
- [ ] مشفّر أبجدي رقمي x86_64
- [ ] توليد بذرة فك التشفير الذاتي (decoder stub)
- [ ] إحصائيات الحجم / الإنتروبيا بعد التشفير

## المرحلة 3 — تعدد الأشكال / التعديل الذاتي (مخطط)

- [ ] محرك متعدد الأشكال: نفس المصدر → تسلسلات بايت مكافئة مختلفة لكل عملية تجميع
- [ ] كود ذاتي التعديل: فك تشفير / فك ضغط جسم الحمولة في وقت التشغيل
- [ ] مكافحة الاكتشاف: تجنب أنماط توقيع shellcode المعروفة

## توسعات مستقبلية

- [ ] iOS arm64 (توقيع الكود + سيناريوهات كسر حماية JIT)
- [ ] Cortex-M / Thumb
- [ ] RISC-V 64

</div>
