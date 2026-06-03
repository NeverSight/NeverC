**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# اتفاقيات الاستدعاء المخصصة

يدعم NeverC **اتفاقيات استدعاء مخصصة مبنية على البيانات** — يمكنك تعيين سجلات فيزيائية عشوائية لمعاملات وقيم إرجاع أي دالة، بالكامل من إضافة خارجية أو سمات مستوى الكود المصدري، دون تعديل المترجم أو أي تعريفات TableGen.

## نظرة عامة

اتفاقيات الاستدعاء التقليدية في LLVM مضمنة في الواجهة الخلفية عبر ملفات `.td` / `.inc`. يستبدل NeverC هذا بنهج **مبني على البيانات في وقت التشغيل**:

- **مواصفات تعيين السجلات** (سلسلة نصية) تُرفق بكل دالة كسمة نصية.
- تقرأ الواجهة الخلفية هذه المواصفات وتعيّن المعاملات/قيم الإرجاع إلى السجلات الفيزيائية المحددة.
- يمكن أن تأتي المواصفات من **إضافة خارجية** (تمريرة IR)، أو **سمات الكود المصدري** (`__attribute__` / `__declspec`)، أو كليهما.

## صيغة المواصفات

سلسلة نصية مفصولة بفاصلة منقوطة. كل مقطع يحتوي على مفتاح وقائمة أسماء سجلات مفصولة بفواصل:

```
gpr:rcx,rdx,r8,r9; xmm:xmm0,xmm1; ret:rax; ret_xmm:xmm0
```

| المقطع | الاسم البديل | المعنى |
|---|---|---|
| `args` | | **وضع الموقع**: كل رمز هو اسم سجل أو `stack`/`mem` |
| `gpr` | `arg_gpr` | **وضع المجمع**: سجلات معاملات الأعداد الصحيحة/المؤشرات |
| `xmm` | `arg_xmm` | **وضع المجمع**: سجلات معاملات الفاصلة العائمة/المتجهات |
| `ret_gpr` | `ret` | سجلات قيمة الإرجاع للأعداد الصحيحة/المؤشرات |
| `csr` | | مجموعة callee-saved مخصصة |

### المعماريات المدعومة

| المعمارية | GPR | SIMD | اختيار العرض |
|---|---|---|---|
| **x86-64** | `rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11` | `xmm0`–`xmm15` | i32→32 بت، i64→64 بت |
| **AArch64** | `x0`–`x28` | `v0`–`v31` | i32→`w`، i64→`x`، f32→`s`، f64→`d` |

## الاستخدام

### 1. بواسطة الإضافة (موصى به)

```bash
cd pluginsdk/examples && make CustomCallConvPlugin.dylib
# وضع السمات (الافتراضي)
neverc -fplugin-pass=./CustomCallConvPlugin.dylib input.c -o output.o
# الوضع الشامل
neverc -fplugin-pass=./CustomCallConvPlugin.dylib \
       -fplugin-pass-arg=cc-all=1 \
       -fplugin-pass-arg=ccspec="gpr:r10,r11,rsi;ret:rdx" \
       input.c -o output.o
```

### 2. سمات الكود المصدري

```c
__attribute__((custom_attr("neverc-callconv", "gpr:r10,r11,rsi;ret:rdx")))
int add3(int a, int b, int c) { return a + b + c; }

__declspec(custom_attr("neverc-callconv", "gpr:r10;ret:rdx"))
int msfunc(int a) { return a; }
```

## دعم LTO

تسجل الإضافة في `NEVERC_HOOK_POST_OPT` و `NEVERC_HOOK_LTO_POST_OPT`، مما يضمن تطبيق الاتفاقيات المخصصة بعد دمج LTO.

## واجهة برمجة الإضافات

```c
API->FunctionSetCustomCallConv(F, "gpr:r10,r11,rsi;ret:rdx");
```

يعيّن `CallingConv::NeverC_Custom` (CC 1000)، يكتب السمة و**يزامن جميع مواقع الاستدعاء المباشرة**. تمرير `NULL` أو `""` يمسح الاتفاقية.

## الاختبارات

مجموعة GoogleTest (18 اختبارًا، جميعها PASS):

```bash
ninja -C build-neverc neverc-tests
build-neverc/bin/neverc-tests --gtest_filter='CustomCallConvTest.*'
```
