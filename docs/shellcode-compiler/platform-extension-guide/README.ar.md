<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مُجمِّع shellcode](../README.ar.md)

# دليل توسيع المنصة

يشرح هذا المستند كيفية توسيع مُجمِّع shellcode إلى منصات هدف جديدة. الدعم الحالي: **arm64 / x86_64 على macOS / Linux / Android / Windows** (8 ثلاثيات)، كل منها مع سياقات مستقلة **User** / **Kernel** (16 متغيراً إجمالاً). إضافة منصة جديدة تتطلب عادةً بضع مئات من أسطر الكود.

## فلسفة التصميم: مدفوع بالجداول، وليس بالتفرع

جميع المرورات مستقلة عن الهدف. اختلافات المنصات مُركَّزة في **مكانين**:

1. إدخالات جدول `describeTriple()` في `TargetDesc.cpp`
2. مفاتيح البنية للمستخرجات الثلاثة (Mach-O / ELF / COFF)

إضافة منصة جديدة = صف واحد في (1) + حالة واحدة في (2).

## الخطوات

### 1. إضافة صف في `TargetDesc`

إضافة فرع نظام التشغيل في `describeTriple()`:

```cpp
if (TT.isOSFreeBSD()) {
  D.OS = ShellcodeOS::FreeBSD;
  D.Format = ObjectFormat::ELF;
  D.TextSectionName = ".text";
  if (D.Arch == ShellcodeArch::X86_64) {
    D.Syscall = SyscallABI::FreeBSDSyscall;
    D.AsmTemplate = "syscall";
    D.SyscallNumberReg = "rax";
    D.SyscallRetReg = "rax";
    D.ArgRegs = kX86_64FreeBSDArgRegs;
    D.NumArgRegs = 6;
    D.DriverInjectFlags = kX86_64UnixInjectFlags;
  }
  return D;
}
```

**الحقول المطلوبة** (جميعها في `TargetDesc.h`):

| الحقل | الغرض | عند الغياب |
|-------|-------|-----------|
| `OS` / `Arch` / `Format` | مفتاح التوزيع | `describeTriple` يعيد Unknown → المشغّل يرفض مبكراً |
| `TextSectionName` | المستخرج يبحث عن قسم الدخول | `.text` غير موجود → رفض |
| `Syscall` | قرار استبدال SyscallStubPass | `None` → SyscallStubPass بدون عملية |
| `AsmTemplate` / `SyscallNumberReg` / `SyscallRetReg` / `ArgRegs` | توليد InlineAsm لـ SyscallStubPass | أي حقل فارغ → SyscallStubPass بدون عملية |
| `TCBReadAsm` / `TCBReadConstraint` | InlineAsm قراءة TEB لـ WinPEBImportPass | فارغ → PEB walk يولّد InlineAsm فارغ (Windows: مطلوب) |
| `DriverInjectFlags` | رايات خاصة بالمنصة في وضع shellcode | null → بدون حقن |

### 2. توسيع `SyscallStub` / `SyscallTables`

- إضافة قيمة تعداد في `SyscallABI`
- إضافة `kXxxTable` في `SyscallTables.cpp`
- إضافة حالة في switch `lookupSyscall`
- `SyscallStubPass` بدون تغيير

### 2.5 توسيع القائمة البيضاء Win32 API

Windows ليس لديه ABI استدعاء نظام مستقر. القائمة البيضاء جدول متعدد DLL في `Tables/Win32Apis.def`.

**إضافة API**: صف واحد في `Win32Apis.def` + إعلان في `lib/Headers/windows.h`.

### 3. توسيع المستخرج المقابل

1. تحديد أنواع إعادة التحديد → ترقيع بايتات أو رفض
2. تحديث قائمة أسماء أقسام البيانات المحظورة
3. تحديث التحقق من نطاق هدف إعادة تحديد الدخول عند الإزاحة 0

### 4. إضافة Loader (أداة اختبار فقط)

مرجع `loader_linux.c` و`loader_windows.c`. عادةً: `mmap(RWX) → memcpy → icache flush → call`.

### 5. تحديث الاختبارات

إضافة سطر `cross_compile_check` في `run_cross_target_tests.sh`.

---

## مشاكل معروفة عبر المنصات

- **ترتيب البايتات**: NeverC يدعم little-endian (LE) فقط.
- **اختلافات ABI**: Win64 مقابل System V AMD64 لديهما سجلات وسيطة مختلفة تماماً. يُعالج في طبقة الواجهة الأمامية لـ Clang.
- **أرقام استدعاء النظام**: مختلفة حسب البنية على Linux، Android مطابق لـ Linux، Darwin له أرقام BSD خاصة، Windows بدون أرقام مستقرة (PEB walk).
- **تماسك الذاكرة المؤقتة**: ARM يحتاج تنظيف i-cache صريح؛ x86 لا.
- **SELinux / W^X**: Android مقيد بـ SELinux `execmem`؛ iOS غير مكسور الحماية يرفض `mmap(RWX)` بالكامل.

## خارطة طريق التوسعات المستقبلية

| الهدف | الجهد المقدر | التبعيات |
|-------|-------------|---------|
| **iOS arm64** (كسر حماية / `MAP_JIT`) | يوم واحد | إعادة استخدام مستخرج Mach-O |
| **FreeBSD / OpenBSD x86_64** | نصف يوم | إعادة استخدام مستخرج ELF + جدول syscall جديد |
| **RISC-V64 Linux** | يومان | يحتاج RISC-V TargetDesc + متغير AllBlr جديد + ترقيع إعادة تحديد RISC-V |

## واجهة توسيع مرور التشويش

مسار shellcode يكشف 11 خطافاً عبر `Pipeline.h::ObfuscationHooks` لمكتبات التشويش الخارجية. ترقيع MIR المدمج أيضاً مدفوع بالجداول: `Tables/MIRRewritePatterns.def` و`Tables/MIRRewriteOpcodes.def`.

</div>
