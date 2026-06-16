**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Hook استدعاء نظام نواة Android

استبدال جدول استدعاءات النظام على `openat`. الافتراضي: تبديل إدخال الجدول. مع `-DNVK_SYSCALL_INLINE_HOOK`: تصحيح مقدمة دالة المعالج. يعرض `nvk_syscall_replace`/`nvk_syscall_restore` وتعريفات أرقام syscall arm64.

## البناء

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

غيّر `KERNEL` إلى `515` أو `601` أو `606` أو `612` لإصدارات أخرى.

## النشر والتشغيل

```bash
neverc make run
```

أو يدويًا:

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep nvk_syscall_hook'
```

## إلغاء التحميل

```bash
neverc make rmmod
```

أو يدويًا:

```bash
adb shell su -c 'rmmod nvk_syscall_hook'
```
