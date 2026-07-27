<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[→ فهرس التوثيق](../README.ar.md)

# التطوير المحلي

دليل لبناء NeverC من الكود المصدري وإعداد بيئة تطوير محلية.

---

## المتطلبات الأساسية

- CMake 3.20+
- Ninja
- مُترجم C++17 مُضيف (GCC أو Clang أو MSVC)

---

## البناء

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
cmake --build build-neverc --target neverc
```

يتم اكتشاف `ccache` / `sccache` وتفعيلهما تلقائيًا إن وُجدا.

`--target neverc` هو بناء stage-1 اليومي (الـ runtime المضمّنة فارغة) ويكفي لمعظم
العمل المحلي. لتضمين string / mimalloc / std / NVK داخل الثنائي نفسه (أو محاذاة
CI)، شغّل هدف المظلة stage-2:

```bash
cmake --build build-neverc --target neverc-embed-runtime-bitcode
```

تفاصيل الإقلاع على مرحلتين في [Builtins](../builtins/README.ar.md).

### البناء مع الاختبارات

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

يعتمد `check-neverc` على `neverc-embed-runtime-bitcode`، لذا يُنفَّذ الإقلاع
وإعادة الربط تلقائيًا قبل أول تشغيل للاختبارات. لا حاجة لاستدعاء هدف embed يدويًا.

---

## إعداد PATH (macOS / Linux)

بعد البناء، يكون الملف التنفيذي `neverc` في `build-neverc/bin/neverc`. استخدم النص البرمجي المساعد لإضافته إلى `PATH` بدلاً من كتابة المسار الكامل كل مرة:

```bash
source ./utils/build/neverc-env.sh
```

الآن يمكنك تشغيل `neverc` مباشرةً:

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### الإزالة من PATH

لإزالة النسخة المحلية من `PATH` في جلسة الصَّدفة الحالية:

```bash
source ./utils/build/neverc-env.sh --remove   # أو -r
```

### إعداد دائم

كتابة سطر `source` تلقائيًا في ملف rc الخاص بالصَّدفة (`~/.zshrc` أو `~/.bashrc` أو `~/.profile`):

```bash
source ./utils/build/neverc-env.sh --install
```

للتراجع:

```bash
source ./utils/build/neverc-env.sh --uninstall
```

---

## Windows (CMD)

على Windows، استخدم النص البرمجي `.bat` (لا يحتاج صلاحيات مسؤول):

```cmd
utils\build\neverc-env.bat             &REM إضافة إلى PATH (الجلسة الحالية)
utils\build\neverc-env.bat --remove    &REM إزالة من PATH (الجلسة الحالية)
utils\build\neverc-env.bat --global    &REM حفظ في PATH المستخدم عبر setx
utils\build\neverc-env.bat --global -r &REM إزالة من PATH المستخدم عبر setx
```

على عكس نص Unix البرمجي، لا حاجة لـ `source` — ملف `.bat` يعدّل جلسة `cmd` الحالية مباشرةً. يكتب `--global` في سجل المستخدم عبر `setx` (لا يحتاج صلاحيات مسؤول).

---

## ملفات macOS التنفيذية الجاهزة

الإصدار موقَّع بشهادة Apple Developer ID ومُوثَّق من Apple. فُكَّ ضغط الأرشيف واستخدمه مباشرةً.

---

## التجميع المتقاطع إلى Windows

يتضمن NeverC حزم SDK لكل منصة في `runtime/` (Windows SDK/WDK، Linux sysroot، macOS sysroot، Android NDK)؛ لا حاجة لإعداد خارجي.

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

لمزيد من المعلومات حول dyncode لـ Windows (`-fdyncode`، تحليل استيراد PEB، إلخ)، راجع [وثائق مُجمِّع dyncode](../dyncode-compiler/README.ar.md).

---

## التحقق

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```

</div>
