**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← نظام وقت التشغيل المدمج في NeverC](../README.ar.md)

# تجزئة السلاسل وقت الترجمة (`strhash`)

## نظرة عامة

يوفر NeverC تجزئة سلاسل وقت الترجمة ووقت التشغيل للغة C الصرفة — مفيد للتوجيه السريع عبر مساواة الأعداد الصحيحة (أسماء API، رموز الأوامر) دون جداول سلاسل نصية واضحة في الملف الثنائي.

- **الطبقة 1 — ماكرو صريح**: `NC_STRHASH("string")` / `NEVERC_STRHASH("string")` يُطوى إلى ثابت عددي في Sema
- **الطبقة 2 — وقت تشغيل + طي IR اختياري**: `neverc_strhash_rt` / `NC_STRHASH_AUTO`، مع `-fstrhash-fold` لاستدعاءات ذات وسائط حرفية

تشترك الطبقتان في الخوارزمية المختارة عبر `-fstrhash-algo` (الافتراضي: FNV-1a 64-bit).

---

## بداية سريعة

```c
#include <neverc/strhash/strhash.h>
static const uint64_t kApi = NC_STRHASH("NtQuerySystemInformation");
int is_api(const char *name) {
    return neverc_strhash_rt(name, strlen(name)) == kApi;
}
```

```bash
neverc -fstrhash-fold -fstrhash-algo=fnv64a main.c -o main
```

---

## الخوارزميات

| القيمة | الوصف | الافتراضي |
|--------|-------|-----------|
| `fnv32a` | FNV-1a 32-bit | |
| `fnv64a` | FNV-1a 64-bit | **نعم** |
| `xxhash64` | XXHash64 (seed 0) | |

---

## مرجع أعلام المترجم

| العلم | الوصف |
|------|-------|
| `-fstrhash-algo=fnv32a` | استخدام FNV-1a 32-bit |
| `-fstrhash-algo=fnv64a` | استخدام FNV-1a 64-bit (الافتراضي) |
| `-fstrhash-algo=xxhash64` | استخدام XXHash64 (seed 0) |
| `-fstrhash-fold` | طي استدعاءات التجزئة ذات وسائط السلسلة الثابتة |
| `-fno-strhash-fold` | تعطيل طي IR |
