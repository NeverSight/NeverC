<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← التوثيق](../README.ar.md) · [← مشروع NeverC](../../docs/i18n/README.ar.md)

# أمثلة NeverC

أمثلة قابلة للبناء توضح قدرات التجميع المتقاطع في NeverC. جميعها تُجمَّع متقاطعاً من macOS / Linux — بدون بيئة Windows.

---

## الأمثلة المتاحة

| المثال | الوصف | الميزات الرئيسية |
|--------|-------|-----------------|
| [برنامج تشغيل نواة Windows](../../examples/windows-driver/README.ar.md) | برنامج WDM أدنى | تجميع متقاطع `.sys` من macOS/Linux، LTO تلقائي، مُرابط مدمج |
| [برنامج تشغيل Windows + CET](../../examples/windows-driver-cet/README.ar.md) | برنامج مع Intel CET Shadow Stack | كود نواة متوافق مع CET، `/guard:ehcont` |
| [برنامج تشغيل Windows + عائم](../../examples/windows-driver-float/README.ar.md) | برنامج مع فاصلة عائمة/SIMD | فاصلة عائمة آمنة في وضع النواة |
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

---

## بدء سريع

```bash
cd examples/<اسم-المثال>
neverc make
```

تحديد مسار المترجم: `make NEVERC=/path/to/neverc`

جميع الأمثلة تستخدم **neverc** وتُنتج ثنائيات Windows PE (`.sys`) عبر المُرابط المدمج.

</div>
