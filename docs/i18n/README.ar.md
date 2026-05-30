<div dir="rtl">

**اللغات**: [English](../../README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center" dir="ltr">

# NeverC

**مُجمِّع C23 صديق للذكاء الاصطناعي لأبحاث الأمن — مبني على LLVM**

مُرابط مدمج · مسار shellcode · أوقات تشغيل مدمجة (`string` · `mimalloc` · `xorstr`)

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](../../LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#الميزات)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#التجميع-المتقاطع-إلى-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#الميزات)

[التوثيق](../README.ar.md) · [دليل shellcode](../shellcode-compiler/README.ar.md) · [أوقات التشغيل المدمجة](../builtins/README.ar.md) · [واجهة الإضافات API](../plugin-api/README.ar.md)

</div>

---

> **ملاحظة:** يعرض GitHub دائمًا `README.md` (الإنجليزية) كصفحة رئيسية للمستودع (دون كشف تلقائي للغة). استخدم روابط اللغة أعلاه؛ في [التوثيق](../README.ar.md) و[دليل shellcode](../shellcode-compiler/README.ar.md) حافظ على نفس اللغة عبر شريط اللغة ومسار التنقل.

## نظرة عامة

يُحوِّل NeverC مصدر C القياسي إلى ثنائيات مُستضافة وملفات تنفيذية مستقلة وshellcode مستقل عن الموضع — كل ذلك من سلسلة أدوات واحدة. يستهدف **x86_64** و**AArch64** (little-endian فقط).

## لماذا NeverC؟

لغة C هي بالفعل أبسط لغة أنظمة. NeverC يجعلها أبسط:

- **C23 صرفة، لا أكثر** — لا قوالب، لا RAII، لا تحميل زائد للمعاملات، لا تدفق تحكم خفي. ما تقرأه هو ما يُنفَّذ.
- **`string` مدمج** — نوع سلسلة بدلالة القيمة مع `+` و`==` و`.starts_with()` وتحرير تلقائي — بدون C++.
- **لا استثناءات** — معالجة الأخطاء تبقى صريحة. لا فك للمكدس، لا مفاجآت في الأداء.
- **ثنائي واحد** — المُجمِّع + المُرابط + أوقات التشغيل في ملف تنفيذي واحد. صفر تبعيات خارجية.
- **صديق لنماذج LLM** — القواعد النحوية البسيطة والدلالات الحتمية تجعل كود NeverC المُولَّد بالذكاء الاصطناعي يُترجم بشكل صحيح أكثر من بدائل C++.
- **تجميع متقاطع حقيقي** — أنشئ ملفات Windows التنفيذية وshellcode من macOS أو Linux — بدون VM، بدون إقلاع مزدوج، بدون البحث عن SDK. حزمة Windows SDK مدمجة في المُجمِّع.
- **قابل للتوسيع بلا عوائق** — ملف رأس C واحد و20+ نقطة ربط، وتحصل على [إضافة مُجمِّع](../plugin-api/README.ar.md) قادرة على التدخل في أي مرحلة — من تحسين IR إلى الإخراج الثنائي النهائي — دون معرفة LLVM.
- **أبحاث الأمن مدمجة** — تجميع shellcode وتشفير السلاسل وقت الترجمة وتوليد PE متعدد المنصات مدمجة أصلاً في المُجمِّع — وليست رقعًا مضافة بنصوص خارجية.

## الميزات

- **[مُجمِّع shellcode](../shellcode-compiler/README.ar.md)** — مسار IR/MIR متعدد المراحل، استخراج متعدد المنصات، حل الاستيراد/استدعاءات النظام، وضع النواة، تدقيق البايتات المحظورة، بنية إضافات
- **مُرابط مدمج** — COFF وELF وMach-O في ثنائي واحد؛ دون `ld` أو `link.exe` خارجي
- **تجميع متقاطع** — PE لـ Windows من macOS/Linux مع MSVC SDK مضمّن
- **[أوقات التشغيل المدمجة](../builtins/README.ar.md)** — أوقات تشغيل LLVM bitcode مدمجة في المترجم: [`string`](../builtins/string/README.ar.md) (سلسلة بدلالة القيمة، إدارة ذاكرة تلقائية) و[`mimalloc`](../builtins/mimalloc/README.ar.md) (تجاوز مخصص ذاكرة عالي الأداء شفاف) و[`xorstr`](../builtins/xorstr/README.ar.md) (تشفير السلاسل وقت الترجمة مع فك تشفير مضاد للبصمات)
- **[واجهة الإضافات API](../plugin-api/README.ar.md)** — واجهة C ABI خالصة لإضافات المرور خارج الشجرة؛ SDK بملف رأس واحد، صفر تبعيات LLVM/CRT، نقاط ربط IR وMIR وBinary وLinker
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

> **ملاحظة:** يتطلب نوع **`string`** المدمج **`-fbuiltin-string`** لملفات `.c`. يُفعَّل تلقائيًا لـ [**ملفات `.nc`**](../nc-extension/README.ar.md) وفي وضع **`-fshellcode`**.

```bash
# macOS arm64 / x86_64
neverc -fshellcode -target arm64-apple-macos hello.c -o hello.bin
neverc -fshellcode -target x86_64-apple-macos hello.c -o hello.bin

# iOS arm64
neverc -fshellcode -target arm64-apple-ios hello.c -o hello.bin

# Linux x86_64 / arm64
neverc -fshellcode -target x86_64-linux-gnu hello.c -o hello.bin
neverc -fshellcode -target aarch64-linux-gnu hello.c -o hello.bin

# Android arm64 / x86_64
neverc -fshellcode -target aarch64-linux-android hello.c -o hello.bin
neverc -fshellcode -target x86_64-linux-android hello.c -o hello.bin

# Windows x86_64 / arm64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin
neverc -fshellcode -target aarch64-pc-windows-msvc hello.c -o hello.bin
```

للتفاصيل راجع **[فهرس التوثيق](../README.ar.md)** — التصميم، مصفوفة المنصات، مرجع CLI، الأمثلة. لأمثلة قابلة للبناء راجع **[examples/](../../examples/)**.

## ثنائيات macOS مُسبقة البناء

الإصدار موقَّع بـ ad‑hoc فقط (بدون Apple Developer ID، بدون توثيق). إذا نزّلته عبر متصفح، أزِل خاصية quarantine مرة واحدة بعد فك الضغط:

```bash
xattr -dr com.apple.quarantine /path/to/extracted/install
```

## البناء

### المتطلبات

- CMake 3.20+
- Ninja
- مُجمِّع مضيف C++17 (GCC أو Clang أو MSVC)

### التكوين

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
```

### التجميع

```bash
cmake --build build-neverc --target neverc
```

يُكتشف `ccache` / `sccache` ويُفعَّل تلقائيًا إن وُجد.

### الاختبار

```bash
cmake --build build-neverc --target check-neverc
```

### التحقق

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## التجميع المتقاطع إلى Windows

يتضمن NeverC حزمة Windows SDK و WDK في `runtime/`؛ لا حاجة لإعداد خارجي.

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

لـ shellcode على Windows (`-fshellcode`، حل الاستيراد عبر PEB، إلخ): [توثيق مُجمِّع shellcode](../shellcode-compiler/README.ar.md).

## المساهمة

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
