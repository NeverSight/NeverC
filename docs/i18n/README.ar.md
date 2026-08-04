<div dir="rtl">

**اللغات**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center" dir="ltr">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="../assets/neverc-logo-dark.svg">
  <img src="../assets/neverc-logo-light.svg" width="72" alt="NeverC">
</picture>

# NeverC

**مُجمِّع C23 صديق للذكاء الاصطناعي لأبحاث الأمن — مبني على LLVM**

مُرابط مدمج · مسار dyncode · أوقات تشغيل مدمجة (`string` · `mimalloc` · `xorstr` · `strhash`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#الميزات)
![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#الميزات)

[التوثيق](../README.ar.md) · [دليل dyncode](../dyncode-compiler/README.ar.md) · [أوقات التشغيل المدمجة](../builtins/README.ar.md) · [واجهة الإضافات API](../plugin-api/README.ar.md) · [خارطة الطريق](../roadmap/README.ar.md)

</div>

---

> **ملاحظة:** يعرض GitHub دائمًا `README.md` (الإنجليزية) كصفحة رئيسية للمستودع (دون كشف تلقائي للغة). استخدم روابط اللغة أعلاه؛ في [التوثيق](../README.ar.md) و[دليل dyncode](../dyncode-compiler/README.ar.md) حافظ على نفس اللغة عبر شريط اللغة ومسار التنقل.

## نظرة عامة

يُحوِّل NeverC مصدر C القياسي إلى ثنائيات مُستضافة وملفات تنفيذية مستقلة وdyncode مستقل عن الموضع — كل ذلك من سلسلة أدوات واحدة. يستهدف **x86_64** و**AArch64** (little-endian فقط). ستضيف الإصدارات المستقبلية **EVM** (عقود إيثريوم الذكية) و**Solana eBPF** (برامج على السلسلة) كأهداف تجميع.

## لماذا NeverC؟

لغة C هي بالفعل أبسط لغة أنظمة. NeverC يجعلها أبسط:

- **C23 صرفة، لا أكثر** — لا قوالب، لا RAII، لا تحميل زائد للمعاملات، لا تدفق تحكم خفي. ما تقرأه هو ما يُنفَّذ.
- **`string` مدمج** — نوع سلسلة بدلالة القيمة مع `+` و`==` و`.starts_with()` وتحرير تلقائي — بدون C++.
- **لا استثناءات** — معالجة الأخطاء تبقى صريحة. لا فك للمكدس، لا مفاجآت في الأداء.
- **ثنائي واحد** — المُجمِّع + المُرابط + أوقات التشغيل في ملف تنفيذي واحد. صفر تبعيات خارجية.
- **صديق لنماذج LLM** — القواعد النحوية البسيطة والدلالات الحتمية تجعل كود NeverC المُولَّد بالذكاء الاصطناعي يُترجم بشكل صحيح أكثر من بدائل C++.
- **تجميع متقاطع حقيقي** — أنشئ Windows PE و Linux ELF و macOS Mach-O و Android ELF و dyncode من macOS أو Linux — بدون VM، بدون إقلاع مزدوج، بدون البحث عن SDK. حزم SDK للمنصات مدمجة في المُجمِّع.
- **قابل للتوسيع بلا عوائق** — ملف رأس C وحيد و130 مرحلة ترجمة مسمّاة، وتحصل على [إضافة مُجمِّع](../plugin-api/README.ar.md) قادرة على التدخل في أي مرحلة — من تحسين IR إلى الإخراج الثنائي النهائي — دون معرفة LLVM.
- **أبحاث الأمن مدمجة** — تجميع dyncode وتشفير السلاسل وقت الترجمة وتوليد PE متعدد المنصات مدمجة أصلاً في المُجمِّع — وليست رقعًا مضافة بنصوص خارجية.

## الميزات

- **[مُجمِّع dyncode](../dyncode-compiler/README.ar.md)** — مسار IR/MIR متعدد المراحل، استخراج متعدد المنصات، حل الاستيراد/استدعاءات النظام، وضع النواة، تدقيق البايتات المحظورة، بنية إضافات
- **مُرابط مدمج** — COFF وELF وMach-O في ثنائي واحد؛ دون `ld` أو `link.exe` خارجي
- **تجميع متقاطع** — Windows PE و Linux ELF و macOS Mach-O و Android ELF من أي مضيف مع SDK مدمجة لكل منصة
- **[أوقات التشغيل المدمجة](../builtins/README.ar.md)** — أوقات تشغيل LLVM bitcode مدمجة في المترجم: [`string`](../builtins/string/README.ar.md) (سلسلة بدلالة القيمة، إدارة ذاكرة تلقائية) و[`mimalloc`](../builtins/mimalloc/README.ar.md) (تجاوز مخصص ذاكرة عالي الأداء شفاف، مُفعَّل افتراضيًا خارج أهداف النواة وfreestanding) و[`xorstr`](../builtins/xorstr/README.ar.md) (تشفير السلاسل وقت الترجمة مع فك تشفير مضاد للبصمات) و[`strhash`](../builtins/strhash/README.ar.md) (تجزئة السلاسل وقت الترجمة بنفس الخوارزمية وقت التشغيل)
- **[واجهة الإضافات API](../plugin-api/README.ar.md)** — واجهة C ABI خالصة للإضافات خارج الشجرة؛ SDK بملف رأس واحد، صفر تبعيات LLVM/CRT، تغطي مراحل المُشغِّل والمعالج المسبق والشجرة النحوية وIR وMIR وMC والكائنات والربط وLTO وdyncode
- **[امتداد `.nc`](../nc-extension/README.ar.md)** — استخدم `.nc` لتفعيل جميع ميزات NeverC تلقائيًا (`string`، أنواع الأعداد بأسلوب Rust) بدون أعلام إضافية
- **بناء LLVM خفيف** — خلفية x86_64 / AArch64 فقط؛ إزالة مسارات C++/ObjC/OpenMP

## مثال سريع

```c
#include <stdio.h>

typedef struct { string user; string pass; } creds;

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());

    // Compile-time encryption — `strings ./bin` cannot find these literals
    creds login = {.user = "admin".encrypt(), .pass = "s3cret".encrypt()};
    string paths[] = {"/api/v1".encrypt(), "/api/v2".encrypt()};

    // Zero-allocation decrypt-and-compare (plaintext never fully in memory)
    if (login.user == "admin".encrypt() && login.pass == "s3cret".encrypt()) {
        for (int i = 0; i < 2; i++)
            if (msg.starts_with(paths[i]))
                printf("route matched: %s\n", paths[i].c_str());
    }
    return 0;
}
```

> **ملاحظة:** يتطلب نوع **`string`** المدمج **`-fbuiltin-string`** لملفات `.c`. يُفعَّل تلقائيًا لـ [**ملفات `.nc`**](../nc-extension/README.ar.md) وفي وضع **`-fdyncode`**.

```bash
# macOS arm64 / x86_64
neverc -fdyncode -target arm64-apple-macos hello.c -o hello.bin
neverc -fdyncode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fdyncode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fdyncode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fdyncode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fdyncode -target aarch64-linux-android hello.c -o hello.bin
neverc -fdyncode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fdyncode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fdyncode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

للتفاصيل راجع **[فهرس التوثيق](../README.ar.md)** — التصميم، مصفوفة المنصات، مرجع CLI، الأمثلة. لأمثلة قابلة للبناء راجع **[examples](../examples/README.ar.md)**.

## التثبيت

على **Linux x64/arm64** و **macOS arm64**، ثبّت أحدث release بأمر واحد:

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/HEAD/install.sh | sh
```

يُنزِّل المثبِّت أرشيف release لمنصتك، يتحقق منه عبر `SHA256SUMS`، يثبّت في `~/.neverc`، ويضيف `~/.neverc/bin` إلى مقدمة `PATH` في الصَّدفة.

لتثبيت إصدار محدد:

```bash
curl -fsSL https://raw.githubusercontent.com/NeverSight/NeverC/v3389.1.2/install.sh | NEVERC_VERSION=v3389.1.2 sh
```

التحقق من التثبيت:

```bash
neverc --version
neverc hello.c -o hello
```

حزم **Windows x64/arm64** متاحة على [GitHub Releases](https://github.com/NeverSight/NeverC/releases) للتنزيل اليدوي. ثنائي macOS arm64 موقَّع بشهادة Apple Developer ID ومعتمد (notarized).

متغيرات بيئة اختيارية:

| المتغير | الغرض |
|---------|--------|
| `NEVERC_INSTALL_DIR` | بادئة التثبيت (افتراضي: `~/.neverc`) |
| `NEVERC_VERSION` | وسم release، مثل `v3389.1.2` (افتراضي: latest) |
| `NEVERC_NO_MODIFY_PATH=1` | عدم تعديل ملف تعريف الصَّدفة |

جذور sysroot للتجميع المتقاطع (Windows SDK وLinux sysroot وغيرها) تُثبَّت عند الحاجة بعد أن يصبح المترجم على `PATH`:

```bash
neverc runtime install windows-x64
neverc runtime list
```

## البناء من المصدر

المتطلبات وأوامر البناء والتجميع المتقاطع إلى Windows وإعداد PATH والتبديل بين release والبناء داخل الشجرة — راجع **[التطوير المحلي](../local-dev/README.ar.md)**.

## المساهمة

NeverC **مخصّص للغة C فقط** حسب التصميم (C23). واجهات C++ و Objective-C و CUDA واللغات
المشابهة خارج النطاق؛ ستُغلق طلبات السحب التي تضيفها. إن احتجت سلسلة أدوات LLVM موجّهة
إلى C++، فكّر في [llvm-msvc](https://github.com/backengineering/llvm-msvc).

للتغييرات الكبيرة في اللغة أو ABI أو وقت التشغيل، افتح issue أولًا لمناقشة النطاق قبل
إرسال طلب سحب.

فرع التطوير الافتراضي هو **`dev`**. استنسخ المستودع وانتقل إلى `dev` قبل البدء، وافتح طلبات السحب (Pull Request) نحو `dev`.

```bash
git clone https://github.com/NeverSight/NeverC.git
cd NeverC
git checkout dev
```

## الترخيص

[AGPL-3.0](../../LICENSE)

تحتفظ مكوّنات LLVM بترخيص [Apache-2.0 WITH LLVM-exception](../../llvm/LICENSE.TXT).

</div>
