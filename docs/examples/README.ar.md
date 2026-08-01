<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← التوثيق](../README.ar.md) · [← مشروع NeverC](../../docs/i18n/README.ar.md)

# أمثلة NeverC

أمثلة قابلة للبناء توضح قدرات التجميع المتقاطع في NeverC. جميعها تُجمَّع متقاطعاً من macOS / Linux — بدون بيئة Windows.

---

## الأمثلة المتاحة

### backends الخادم

| المثال | الوصف | الميزات الرئيسية |
|--------|-------|-----------------|
| [خادم لعبة موثوق](../../examples/network-authoritative-server/README.ar.md) | backend لعبة متعدد المنصات | tick ثابت 60 Hz، جلسات TCP، إدخال UDP/QUIC، حماية إعادة التشغيل |
| [جامع مكافحة الغش](../../examples/network-anticheat-collector/README.ar.md) | استيعاب تيليمتري محصّن | mTLS، NRPC متدفق، تيليمتري HMAC، خط تدقيق محدود |

### Windows

| المثال | الوصف | الميزات الرئيسية |
|--------|-------|-----------------|
| [برنامج تشغيل نواة Windows](../../examples/windows-driver/README.ar.md) | برنامج WDM أدنى | تجميع متقاطع `.sys` لـ **x64** (الافتراضي) و**ARM64**، LTO تلقائي، مُرابط مدمج |
| [برنامج تشغيل Windows + CET](../../examples/windows-driver-cet/README.ar.md) | برنامج مع Intel CET Shadow Stack | كود نواة متوافق مع CET (**x64 فقط**)، `/guard:ehcont` |
| [برنامج تشغيل Windows + عائم](../../examples/windows-driver-float/README.ar.md) | برنامج مع فاصلة عائمة/SIMD | فاصلة عائمة آمنة في وضع النواة على **x64** و**ARM64** |
| [Windows Ring3 EXE](../../examples/windows-exe/README.ar.md) | تطبيق وحدة تحكم وضع المستخدم | GetSystemInfo، تعداد العمليات، VirtualAlloc |
| [Windows Ring3 DLL](../../examples/windows-dll/README.ar.md) | DLL وضع المستخدم | ReadProcessMemory، VirtualAllocEx، تعداد الوحدات |

### Linux

| مثال | الوصف | الميزات الرئيسية |
|------|-------|-----------------|
| [Linux Hello World](../../examples/linux-hello/README.ar.md) | برنامج C بسيط | ترجمة تبادلية من macOS/Windows |
| [Linux POSIX](../../examples/linux-posix/README.ar.md) | برمجة أنظمة POSIX | pthreads، mmap، pipe، إشارات |
| [Linux ثابت](../../examples/linux-static/README.ar.md) | ثنائي ثابت بالكامل | ربط `-static` |
| [Linux شبكة](../../examples/linux-network/README.ar.md) | عرض مقبس TCP | عميل/خادم |
| [Linux رياضيات + zlib](../../examples/linux-math/README.ar.md) | رياضيات + ضغط | حساب مثلثات، zlib، CRC32 |

### macOS

| مثال | الوصف | الميزات الرئيسية |
|------|-------|-----------------|
| [تطبيق macOS](../../examples/macos-app/README.ar.md) | ملف تنفيذي أصلي Mach-O | sysctl، uname، Mach host_info/task_info، فحص العمليات |
| [مكتبة macOS الديناميكية](../../examples/macos-dylib/README.ar.md) | مكتبة `.dylib` أصلية | Mach vm_read/vm_write، vm_alloc/vm_dealloc، task_info، XOR |

### Android

| مثال | الوصف | الميزات الرئيسية |
|------|-------|-----------------|
| [Android ELF](../../examples/android-elf/README.ar.md) | ملف ARM64 أصلي لأجهزة مروّتة | ترجمة تبادلية لـ Android، dlopen/liblog، معلومات /proc، اكتشاف root |
| [مكتبة مشتركة Android](../../examples/android-so/README.ar.md) | مكتبة `.so` أصلية ARM64 | مكتبة مشتركة، mmap RWX، تشفير XOR |

### وحدات نواة Android (.ko)

لا حاجة لشجرة مصدر النواة — يقوم NeverC بالترجمة مقابل runtime المدمج الأدنى. ملف مصدر واحد يغطي GKI 5.10–6.12.

| مثال | الوصف | الميزات الرئيسية |
|------|-------|-----------------|
| [Kernel Hello](../../examples/android-kernel-hello/README.ar.md) | وحدة `.ko` أدنى | تمهيد kallsyms عبر kprobe، أبسط تحقق insmod |
| [قالب تعريف النواة](../../examples/android-kernel-driver/README.ar.md) | قالب حل الرموز الديناميكي | `kallsyms_lookup_name`، ABI مستقر GKI، 5.10–6.12 |
| [Kernel Inline Interpose](../../examples/android-kernel-inline-interpose/README.ar.md) | Interpose مضمّن على `do_faccessat` | تصحيح آمن BTI/PAC، وضع context interpose، إعادة تموضع PC-نسبي |
| [Kernel Syscall Interpose](../../examples/android-kernel-syscall-interpose/README.ar.md) | جدول syscall / inline / context interpose | استبدال `sys_call_table`، interpose مضمّن، context interpose |
| [Kernel Lowvis](../../examples/android-kernel-lowvis/README.ar.md) | إدارة رؤية الوحدة | رؤية list/sysfs/proc، أغلفة بيانات الاعتماد، حالة إنفاذ SELinux |
| [Kernel Full SDK](../../examples/android-kernel-full/README.ar.md) | تكامل SDK كامل | Netlink IPC، interposes، أغلفة بيانات الاعتماد، رؤية الوحدة، التحكم في سياسة SELinux، VMA، ملفات |
| [Kernel Chardev](../../examples/android-kernel-chardev/README.ar.md) | جهاز حرفي + ioctl | `misc_register`، إرسال ioctl، `/proc` seq_file |
| [Kernel Netlink](../../examples/android-kernel-netlink/README.ar.md) | IPC netlink ثنائي الاتجاه | أوامر PING/VERSION/ECHO، `nvk_nl_open`/`nvk_nl_reply` |
| [Kernel Probe](../../examples/android-kernel-probe/README.ar.md) | فحص تعليمة عشوائية | `neverc_krt_probe_register`، سياق كامل للمسجلات، تسلسل حسب الأولوية، تخطٍّ/إعادة توجيه |
| [Kernel Multi-File](../../examples/android-kernel-multifile/README.ar.md) | وحدة نواة متعددة الملفات | استدعاء واحد لـ `NEVERC_KRT_BOOTSTRAP()`، حالة مشتركة `weak_odr`، تقسيم init/interpose/الأدوات |

---

## بدء سريع

كل مثال يتبع النمط نفسه:

```bash
cd examples/اسم-المثال
neverc make
```

تجاوز مسار المترجم عند الحاجة:

```bash
neverc make NEVERC=/path/to/neverc
```

أمثلة برامج تشغيل Windows تختار المعمارية عبر `ARCH` (الافتراضي x64). مثال CET
مخصص لـ x64 فقط — CET ميزة خاصة بـ x86:

```bash
neverc make ARCH=x64        # Build for x64 (default)
neverc make ARCH=arm64      # Build for ARM64
neverc make all-arch        # Build every architecture the example supports
neverc make TESTSIGN=1      # Attach an Authenticode test signature
```

أمثلة Linux تدعم اختيار البنية:

```bash
neverc make TARGET=aarch64-linux-gnu   # Build for ARM64
neverc make TARGET=x86_64-linux-gnu    # Build for x86_64 (default)
```

أمثلة macOS تدعم اختيار البنية:

```bash
neverc make TARGET=arm64-apple-macos     # Build for Apple Silicon (default)
neverc make TARGET=x86_64-apple-macos    # Build for Intel
```

أمثلة Android تستهدف ARM64 افتراضيًا:

```bash
cd examples/android-elf
neverc make            # Build
neverc make run        # Build + push to device + run via adb
```

---

## أبرز ميزات تعدد المنصات

- **سلسلة أدوات واحدة**: يتولى NeverC المعالجة المسبقة والترجمة والتحسين (LTO تلقائي) والربط في استدعاء واحد
- **SDK مضمَّن**: Windows SDK/WDK وLinux sysroot (Ubuntu 22.04) وmacOS sysroot (macOS 14) وAndroid sysroot (NDK r26c, API 21+) مضمَّنة في `runtime/` — صفر تبعيات خارجية
- **مستقل عن المضيف**: البناء من macOS (arm64/x86_64) أو Linux (x86_64/aarch64) أو Windows بأوامر متطابقة
- **متعدد الأهداف**: ترجمة متقاطعة إلى Windows PE (`.sys`/`.exe`/`.dll`) وLinux ELF وmacOS Mach-O (`.dylib`) وAndroid ELF من أي مضيف
- **دعم التصحيح**: مرّر `-g` لمعلومات تصحيح DWARF؛ افحص باستخدام `llvm-dwarfdump`

</div>
