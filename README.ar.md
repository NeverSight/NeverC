<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

<div align="center" dir="ltr">

# NeverC

**مُجمِّع C23 موجَّه لأبحاث الأمن على LLVM**

مُرابط مدمج · مسار shellcode · نوع `string` مدمج

[![AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-blue.svg)](LICENSE)
[![C23](https://img.shields.io/badge/Standard-C23-brightgreen.svg)](#الميزات)
[![Platform](https://img.shields.io/badge/Platform-macOS%20%7C%20Linux%20%7C%20Windows-informational.svg)](#التجميع-المتقاطع-إلى-windows)
[![Arch](https://img.shields.io/badge/Arch-x86__64%20%7C%20AArch64-orange.svg)](#الميزات)

[التوثيق](docs/README.ar.md) · [دليل shellcode](docs/shellcode-compiler/README.ar.md) · [السلسلة المدمجة](docs/builtin-string/README.ar.md)

</div>

---

> **ملاحظة:** يعرض GitHub دائمًا `README.md` (الإنجليزية) كصفحة رئيسية للمستودع (دون كشف تلقائي للغة). استخدم روابط اللغة أعلاه؛ في [التوثيق](docs/README.ar.md) و[دليل shellcode](docs/shellcode-compiler/README.ar.md) حافظ على نفس اللغة عبر شريط اللغة ومسار التنقل.

## نظرة عامة

يُحوِّل NeverC مصدر C القياسي إلى ثنائيات مُستضافة وملفات تنفيذية مستقلة وshellcode مستقل عن الموضع — كل ذلك من سلسلة أدوات واحدة. يستهدف **x86_64** و**AArch64** (little-endian فقط).

## الميزات

- **[مُجمِّع shellcode](docs/shellcode-compiler/README.ar.md)** — مسار IR/MIR متعدد المراحل، استخراج متعدد المنصات، حل الاستيراد/استدعاءات النظام، وضع النواة، تدقيق البايتات المحظورة، بنية إضافات
- **مُرابط مدمج** — COFF وELF وMach-O في ثنائي واحد؛ دون `ld` أو `link.exe` خارجي
- **تجميع متقاطع** — PE لـ Windows من macOS/Linux مع MSVC SDK مضمّن
- **[نوع `string` مدمج](docs/builtin-string/README.ar.md)** — سلسلة بدلالة القيمة مع صيغة طرق بالنقطة وإدارة ذاكرة تلقائية ودعم UTF-8 أصلي
- **بناء LLVM خفيف** — خلفية x86_64 / AArch64 فقط؛ إزالة مسارات C++/ObjC/OpenMP

## مثال سريع

```c
#include <stdio.h>

int main(void) {
    string msg = "Hello " + "NeverC!";
    printf("%s\n", msg.c_str());
    return 0;
}
```

```bash
# مرّر -target دائماً: يحدد OS/المعمارية/ABI للمخرجات، وليس نظام المضيف

# macOS arm64
neverc -fshellcode -target arm64-apple-macos -mshellcode-syscall hello.c -o hello.bin

# Linux x86_64
neverc -fshellcode -target x86_64-linux-gnu -mshellcode-syscall hello.c -o hello.bin

# Windows x86_64
neverc -fshellcode -target x86_64-pc-windows-msvc hello.c -o hello.bin
```

للتفاصيل راجع **[فهرس التوثيق](docs/README.ar.md)** — التصميم، مصفوفة المنصات، مرجع CLI، الأمثلة.

## البناء

### المتطلبات

- CMake 3.20+
- Ninja
- مُجمِّع مضيف C++17 (GCC أو Clang أو MSVC)

### التكوين

```bash
cmake -B build-neverc -G Ninja \
  -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  llvm
```

### التجميع

```bash
cmake --build build-neverc --target neverc
```

يُكتشف `ccache` / `sccache` ويُفعَّل تلقائيًا إن وُجد.

### الاختبار

```bash
cmake --build build-neverc --target neverc-tests
ctest --test-dir build-neverc --output-on-failure
```

### التحقق

```bash
./build-neverc/bin/neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
./build-neverc/bin/neverc -c /tmp/hello.c -o /tmp/hello.o
```

## التجميع المتقاطع إلى Windows

بعد وضع splat SDK من [xwin](https://github.com/Jake-Shadle/xwin) في `build-neverc/sdk/msvc/`:

```bash
./build-neverc/bin/neverc --target=x86_64-pc-windows-msvc \
  -o hello.exe hello.c -lkernel32
```

لـ shellcode على Windows (`-fshellcode`، حل الاستيراد عبر PEB، إلخ): [توثيق مُجمِّع shellcode](docs/shellcode-compiler/README.ar.md).

## الترخيص

[AGPL-3.0](LICENSE)

تحتفظ مكوّنات LLVM بترخيص [Apache-2.0 WITH LLVM-exception](llvm/LICENSE.TXT).

</div>
