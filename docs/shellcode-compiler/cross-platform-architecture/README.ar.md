<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مُجمِّع shellcode](../README.ar.md)

# نظرة عامة على البنية متعددة المنصات لـ NeverC Shellcode

يصف هذا المستند مبادئ التصميم وراء "مجموعة واحدة من المرورات تغطي macOS / Linux / Android / Windows × arm64 / x86_64 × User / Kernel". اقرأه قبل التوسيع لمنصة جديدة.

مستندات مرتبطة:
- [README.md](../README.ar.md) — نظرة عامة، خيارات CLI، بدء سريع
- [ir-pass-design.md](../ir-pass-design/README.ar.md) — مسؤوليات مرورات IR
- [mir-pass-design.md](../mir-pass-design/README.ar.md) — طبقة MIR
- [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ar.md) — سياق النواة
- [platform-extension-guide.md](../platform-extension-guide/README.ar.md) — إضافة منصات

---

## 1. مصفوفة ثلاثية الأبعاد: OS × Arch × ExecutionLevel

جميع الاختلافات تتقارب في **مصفوفة ثلاثية الأبعاد**: 8 (OS, arch) × 2 ExecutionLevel = **16 إدخال جدول** من `describeTriple()`.

**المبدأ الأساسي**: المرورات تقرأ دائماً من الجدول، وليس `if (OS == Darwin)`. منصة جديدة = صف واحد + حالة واحدة في المستخرج.

## 2–3. المسار و PIC

ترتيب ثابت مع 11 خطاف تشويش. `isPICDefaultForced()` يُرجع **true** في كل مكان.

## 4. User / Kernel متعامد

- **User**: مسار PEB walk / syscall stub.
- **Kernel**: SyscallStub/WinPEB قصر دائرة؛ KernelImportPass مُفعَّل.

## 5. مصفوفة دعم "C العادي" وضع المستخدم

المصفوفات الكبيرة، ثوابت FP، computed-goto، memcpy، `__int128`، العمليات الذرية، ملفات رأس POSIX/Win32 — كلها **مدعومة مباشرة** بدون تدخل المستخدم.

## 6–8. طبقة MIR، المستخرج، خطافات التشويش

مسار 3 مراحل (إصلاح / تراجع / استخراج). 11 خطاف عبر جميع الطبقات.

## 9–10. التوسيع وغير الأهداف

التكلفة: صف TargetDesc واحد + جدول syscall + حالة مستخرج + اختبارات. غير الأهداف: C++/ObjC، 32-بت، تضمين libc (تخصيص الكومة عبر `HeapArenaPass`)، عناوين مطلقة.

</div>
