<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مُجمِّع shellcode](../README.ar.md)

# مسار Shellcode وMIR واستراتيجية PIC (ملاحظات التصميم)

يصف هذا المستند مقايضات التصميم في وضع shellcode لـ NeverC عبر سلسلة **IR → تحسين LLVM → خلفية MIR → ملف كائن → استخراج/ترقيع**، وعلاقته بسياسة **PIC الافتراضي على مستوى المُجمِّع**. تفاصيل التنفيذ مرجعية في الكود المصدري والتعليقات الإنجليزية.

## 1. لماذا فرض PIC افتراضياً (بما في ذلك التجميع غير shellcode)

يفترض مستخرج shellcode أن المراجع للرموز الخارجية تقع على إعادة تحديد مواقع **نسبية لـ PC** أو قابلة للحل داخل `.text`، وليس عناوين مطلقة مشفرة أو مجمعات ثوابت تحتاج مُحمِّلاً لملء `.data`.

NeverC يُرجع **true** من `Generic_GCC::isPICDefaultForced()` و`MachO::isPICDefaultForced()` و`MSVCToolChain::isPICDefaultForced()`، متميزاً عن سلوك Clang الأصلي "PIC افتراضي اختياري": **كل تجميع عبر المنصات يستخدم دائماً PIC كنموذج وحيد**. هذا يعني:

- تجميع C العادي وتجميع `-fshellcode` يتشاركان نفس عادات إعادة التحديد، مما يقلل العبء المعرفي "يعمل عادياً، ينكسر تحت shellcode".
- خلفيات Linux / Android / macOS / Windows تتشارك نفس الافتراضات تحت الواصفات المدفوعة بالجداول (`TargetDesc` + `Options.td.h`).

هذه السياسة لا تميز بين تفعيل `-fshellcode` أو سياق user/kernel.

## 2. تقسيم العمل بين IR وMIR في مرحلتين

### 2.1 طبقة IR (`registerShellcodePasses`)

مسؤولة عن ضغط دلالات "C العادي" إلى شكل **مدخل واحد، بدون قسم بيانات مستقل، بدون متغيرات عامة مشكلة**: `ZeroRelocPass`، `IndirectBrPass`، `MemIntrinPass`، `StringRuntimePass`، `HeapArenaPass`، `CompilerRtPass`، `SyscallStubPass`، `WinPEBImportPass`، `KernelImportPass` (النواة فقط)، `Data2TextPass`، إلخ.

**المبدأ**: المشاكل القابلة للحل في IR بأساليب هيكلية تُصلح أولاً في IR، مما يبسط تدفق البايتات الذي تراه الخلفية والمستخرج.

### 2.2 طبقة MIR (`registerShellcodeMachinePasses`)

تسجل مكالمات استرجاع في `TargetPassConfig` القديم لـ LLVM **بعد تخصيص السجلات، قبل `addPreEmitPass`**:

1. المستخدم/مكتبة التشويش: `RunBeforePreEmit`.
2. **`ShellcodeMIRPrepPass`**: يزيل أشباه التعليمات التي تولد أقسام جانبية.
3. المستخدم/مكتبة التشويش: `RunAfterPreEmit`.

**المبدأ**: إصلاح في MIR أولاً؛ **الاستخراج والترقيع هما شبكة الأمان الأخيرة**.

## 3. اختلافات المنصات المدفوعة بالجداول

- **Triple → سلوك**: مركزي في `describeTriple()` وحقول `TargetDesc`. لإضافة OS/Arch جديد، يُفضَّل **إضافة صفوف في الجدول**.
- **خيارات CLI**: معرفة في `Options.td.h`؛ تُستهلك عبر تعدادات `OPT_*`.

## 4. سلسلة أدوات Windows MSVC وتخطيط SDK

NeverC يدعم مصدرين لـ SDK **بدون مسارات مطلقة مشفرة**:

1. **SDK مدمج** (افتراضي): NeverC يضمّن Windows SDK و WDK كاملين في `runtime/`. الترويسات في `runtime/windows/shared/`، والمكتبات الخاصة بكل معمارية في `runtime/windows/{x64,arm64}/`. تخطيط ما بعد البناء:

   ```
   build-neverc/bin/neverc
   build-neverc/runtime/windows/shared/msvc/  (ترويسات)
   build-neverc/runtime/windows/x64/msvc/     (مكتبات x64)
   build-neverc/runtime/windows/arm64/msvc/   (مكتبات arm64)
   ```

2. **Sysroot صريح بنمط VS** (اختياري): إذا كان لديك شجرة `VC/Tools/MSVC/<version>/...` + `Windows Kits/10/...`، أشِر إليها عبر `-vctoolsdir=<path>` أو `-winsysroot=<path>`. هذا المسار له الأولوية على SDK المدمج.

## 5. نقاط التشويش والتوسيع

- **تشويش IR**: عبر `setShellcodeObfuscationHooks`؛ 11 خطافاً (6 IR + 3 MIR + 2 تدفق بايتات).
- **تشويش MIR**: `RunBeforePreEmit` / `RunAfterPreEmit` / `RunAfterFinalMIR`.
- **خطافات تدفق البايتات**: `RunPostExtract` (قبل الإنهاء) و`RunPostFinalize` (بعد الإنهاء).
- **الحجم / المحاذاة / الحشو**: `-fshellcode-max-length=`، `-fshellcode-align=`، `-fshellcode-pad=`.
- **خيار التصميم**: التشويش، تعدد الأشكال، المشفرات المرحلية، استدعاءات النظام غير المباشرة **غير مدمجة عمداً**، متاحة فقط كإضافات اختيارية.

## 6. بُعد وضع النواة (Ring-0)

`-mshellcode-context=user|kernel` كبُعد ثانٍ للمسار:

- **وضع المستخدم**: مسار PEB walk / syscall stub.
- **وضع النواة**: `SyscallStubPass` / `WinPEBImportPass` يعودان مبكراً؛ `KernelImportPass` يُعيد كتابة الاستدعاءات الخارجية غير المحلولة؛ `<neverc/kernel.h>` يكشف أنواع النواة.

راجع [kernel-mode-shellcode.md](../kernel-mode-shellcode/README.ar.md).

## 7. طبقة توافق Windows POSIX

**صفر وعي من المستخدم**: نفس مصدر C يُجمَّع على جميع الثلاثيات الثمانية بدون `#ifdef _WIN32`. `WinPEBImportPass` ينفذ 3 مراحل: مسح POSIX، توليد أغلفة جسرية (13 مجموعة دوال)، حل PEB. التفاصيل: `write` → `GetStdHandle` + `WriteFile`، `mmap` → `VirtualAlloc`، `exit` → `ExitProcess`، إلخ.

## 8. إصلاح تلقائي لتصريح K&R الضمني

`SyscallStubPass` يحتفظ بجدول `getCanonicalSyscallType()` مع 50+ توقيع POSIX قانوني. تصريحات K&R ذات 0 معامل تُستبدل تلقائياً بالتوقيع القانوني.

## 9. ملخص

| الموضوع | النهج |
|---------|-------|
| PIC افتراضي | كل سلاسل الأدوات `isPICDefaultForced()==true` |
| الإصلاح في IR أولاً | الثوابت، القفزات غير المباشرة، عمليات الذاكرة تُزال في IR |
| شبكة أمان MIR | `ShellcodeMIRPrepPass` + خطافات قبل/بعد |
| تقليل الترميز الصلب | `TargetDesc` + `Options.td.h` مدفوعة بالجداول |
| بُعدان user/kernel | `-fshellcode` × `-mshellcode-context={user,kernel}` |
| توافق Windows POSIX | `WinPEBImportPass` يجسر 13 مجموعة POSIX |
| إصلاح K&R التلقائي | `SyscallStubPass` يعود إلى توقيعات POSIX القانونية |

## 10. ثوابت ملفات الرأس shim عبر المنصات

ملفات رأس shim (`sys/mman.h`، `fcntl.h`، إلخ) تكشف ثوابت يجب أن تتطابق مع ABI نواة الهدف. الاختلافات الرئيسية:

| الثابت | Darwin | Linux/Android |
|--------|--------|---------------|
| `AT_FDCWD` | `-2` | `-100` |
| `MAP_ANONYMOUS` | `0x1000` | `0x20` |
| `O_CREAT` | `0x0200` | `0x0040` |
| `O_TRUNC` | `0x0400` | `0x0200` |
| `O_CLOEXEC` | `0x1000000` | `0x80000` |

التنفيذ: حراس `#if defined(__APPLE__)` في ملفات الرأس shim. جدول توافق POSIX في `SyscallTables.cpp` يستخدم قيم Linux (`AT_FDCWD = -100`)، نشط فقط على مسارات `SyscallABI::LinuxSvc0` / `LinuxSyscall`. أهداف Windows لا تستخدم ملفات POSIX هذه؛ جسر POSIX→Win32 تتولاه أغلفة توافق `WinPEBImportPass`.

</div>
