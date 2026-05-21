<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مُجمِّع shellcode](../README.ar.md)

# تصميم مرورات IR — المبادئ، المسار، وأمثلة قبل/بعد

> يشرح هذا المستند **لماذا** يوجد كل مرور في مسار تجميع shellcode.

## 0. الفكرة المحورية

الهدف في جملة واحدة: **إزالة كل شيء في `.o` سيصبح إعادة تحديد موقع، وترك تدفق تعليمات نقي فقط يمكن `mmap(RWX)` + `memcpy` + `blr` مباشرة.**

## 1–13. المرورات

| المرور | الوظيفة |
|--------|---------|
| ZeroRelocPass | Prep: توحيد الربط + alwaysinline. Stackify: المتغيرات العامة القابلة للتغيير → alloca |
| IndirectBrPass | computed-goto → switch |
| SyscallStubPass | libc extern → أفخاخ inline مدفوعة بـ TargetDesc + توافق POSIX + إصلاح K&R تلقائي |
| WinPEBImportPass | Win32 extern → مُحلل PEB walk (~190 API) + ذاكرة تخزين مؤقت مشفرة للعناوين + توافق Windows POSIX |
| MemIntrinPass | mem*/str*/abs → مساعدات حلقة بايت inline |
| CompilerRtPass | `__int128` قسمة/باقي → قسمة طويلة inline |
| Data2TextPass | المرحلة 1+2: GV ثابتة → فوريات/مكدس + تقسيم بقايا SROA |
| AllBlrPass | (اختياري) استدعاءات مباشرة → غير مباشرة |
| KernelImportPass | (ring-0) extern → استدعاءات غير مباشرة عبر المُحلل |
| StringRuntimePass | أساليب `string` المدمجة → متغيرات ساحة المكدس |
| HeapArenaPass | `malloc`/`free`/`calloc`/`realloc` → تخصيص ساحة + احتياطي OS للتخصيصات الكبيرة |

11 خطاف تشويش. فلسفة التشخيص: خطأ واحد = تشخيص واحد قابل للتنفيذ. راجع [plugin-interface.md §6](../plugin-interface/README.ar.md#6-registration-position-selection--pic-coverage-matrix) و[kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ar.md).

</div>
