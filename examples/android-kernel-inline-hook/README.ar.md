**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Hook مضمّن لنواة Android

Hook مضمّن على `do_faccessat`. الافتراضي: استبدال بسيط مع trampoline. مع `-DNVK_CONTEXT_HOOK`: hook سياقي يستقبل حالة السجلات الكاملة `nvk_reg_ctx`. يعرض التصحيح الآمن BTI/PAC، إعادة التموضع النسبي لـ PC، و trampoline متسق D-cache→I-cache.

## أوضاع الـ Hook

| | Simple Hook (افتراضي) | Context Hook (`-DNVK_CONTEXT_HOOK`) |
|---|---|---|
| **التوقيع** | يتطلب typedef دقيق | غير مطلوب — عبر `ctx->regs[0..7]` |
| **حماية إعادة الدخول** | يدوية (`nvk_hook_enter`/`leave`) | مدمجة (`guard_task`) |
| **تفعيل/تعطيل** | يدوي (`WRITE_ONCE`) | فحص سريع مدمج في stub |
| **استدعاء الأصل** | عبر مؤشر `orig` | تلقائي (بعد المعالج) |
| **تخطي الأصل** | عدم استدعاء `orig` | `NVK_CTX_SKIP(ctx, ret)` |
| **إعادة توجيه** | غ/م | `NVK_CTX_REDIRECT(ctx, addr)` |
| **تعديل المعاملات** | تغيير قبل استدعاء `orig` | `NVK_CTX_SET_ARG(ctx, n, val)` |
| **أمان FP** | اتفاقية caller-save | `NVK_CTX_FP_GUARD_BEGIN`/`END` |
| **التكلفة** | منخفضة (4 تعليمات patch + trampoline) | أعلى (116 تعليمة stub + حفظ كامل) |
| **الأنسب لـ** | توقيعات معروفة، أداء حرج | مراقبة، ABI غير مستقر، نمذجة سريعة |

**التوصية**: يُفضل استخدام context hook ما لم تكن بحاجة لاعتراض قيمة الإرجاع أو كانت لديك قيود أداء صارمة.

## البناء

```bash
cd examples/android-kernel-inline-hook
neverc make
```

غيّر `KERNEL` إلى `515` أو `601` أو `606` أو `612` لإصدارات أخرى.

## النشر والتشغيل

```bash
neverc make run
```

أو يدويًا:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## إلغاء التحميل

```bash
neverc make rmmod
```

أو يدويًا:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
