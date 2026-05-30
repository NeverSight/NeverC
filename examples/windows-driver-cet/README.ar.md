**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# برنامج تشغيل نواة Windows مع CET Shadow Stack

برنامج تشغيل نواة WDM بسيط مبني باستخدام NeverC، مع تفعيل Intel CET
(تقنية فرض تدفق التحكم) Shadow Stack للنواة. يدعم التجميع المتقاطع من macOS / Linux.

## البناء

```bash
cd examples/windows-driver-cet
make
```

من إصدار NeverC مستقل:

```bash
make NEVERC=/path/to/neverc
```

الناتج هو `CetDriver.sys` (محسّن بـ auto-LTO).
البناء الافتراضي يتضمن `-g` للتصحيح؛ **يجب إزالة `-g` في إصدارات الإنتاج**
لإزالة رموز التصحيح وتقليل حجم الملف الثنائي.

## أعلام CET المحددة

| العلم | المستوى | الغرض |
|-------|---------|-------|
| `-fcf-protection=return` | المترجم | إنتاج كود متوافق مع Shadow Stack |
| `-Xlinker --cetcompat` | الرابط | تعيين `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` في PE |

## البناء اليدوي (بدون Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fcf-protection=return \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -Xlinker --cetcompat \
  -lntoskrnl -lhal \
  -o CetDriver.sys driver.c
```

## الوظائف

- ينشئ كائن جهاز في `\Device\CetDriver`
- ينشئ رابط رمزي في `\DosDevices\CetDriver`
- ينفذ استدعاءات غير مباشرة (مؤشر دالة `ComputeFn`) للتحقق من توافق CET — يحمي Shadow Stack عناوين العودة لهذه الاستدعاءات
- يطبع رسائل التحميل/الإزالة عبر `DbgPrint`

---

## التفاصيل التقنية لـ CET

يحتوي CET على **آليتي حماية مستقلتين**:

### 1. Shadow Stack — حماية الحافة الخلفية (RET)

يحافظ العتاد على مكدس ثانٍ (shadow stack) يعكس عمليات CALL/RET.
**لا يلزم وجود تعليمات خاصة عند مدخل الدالة** — شفاف تماماً:

```
┌─ CALL target ─────────────────────────────────┐
│                                                │
│  المكدس العادي:  PUSH return_addr  (RSP)       │
│  Shadow stack:   PUSH return_addr  (SSP, HW)   │
│                                                │
└────────────────────────────────────────────────┘

┌─ RET ─────────────────────────────────────────┐
│                                                │
│  المكدس العادي:  POP return_addr_A  (RSP)      │
│  Shadow stack:   POP return_addr_B  (SSP, HW)  │
│                                                │
│  المقارنة: return_addr_A == return_addr_B ؟     │
│    ✓ تطابق → عودة طبيعية                       │
│    ✗ عدم تطابق → استثناء CP#                   │
│                                                │
└────────────────────────────────────────────────┘
```

### 2. تتبع الفروع غير المباشرة (IBT) — حماية الحافة الأمامية (CALL/JMP غير مباشر)

يتطلب تعليمة `ENDBR64` (`F3 0F 1E FA`، 4 بايت) في كل هدف صالح لاستدعاء/قفزة غير مباشرة. على المعالجات بدون CET، تعمل `ENDBR64` كـ NOP.

### اختيار نواة Windows

| الحماية | الآلية | مستخدم بواسطة نواة Windows؟ |
|---------|--------|---------------------------|
| الحافة الخلفية (RET) | CET Shadow Stack | **نعم** (KCET) |
| الحافة الأمامية (CALL/JMP غير مباشر) | CET IBT (ENDBR) | **لا** — يُستخدم CFG بدلاً منه |

### مقارنة الأسمبلي: أوضاع `-fcf-protection`

الكود المصدري:

```c
unsigned long rotate13(unsigned long val) {
    return (val << 13) | (val >> 19);
}
```

#### `-fcf-protection=none` (بدون CET)

```asm
rotate13:
    mov  eax, ecx
    rol  eax, 13
    ret
```

#### `-fcf-protection=return` (Shadow Stack فقط — هذا المثال يستخدم هذا الوضع)

```asm
rotate13:
    mov  eax, ecx      ; مطابق لـ "none"!
    rol  eax, 13        ; Shadow Stack شفاف تماماً
    ret
```

#### `-fcf-protection=full` (Shadow Stack + IBT)

```asm
rotate13:
    endbr64             ; ← علامة IBT (F3 0F 1E FA)
    mov  eax, ecx
    rol  eax, 13
    ret
```

---

## المترجم vs bin2bin: من الأكثر ودية مع CET؟

يرسم CET خطًا واضحًا بين **المترجمات على مستوى المصدر** و**أدوات bin2bin**
(الحزم، التشويش، الاعتراض، dump+rebuild). يفرض Shadow Stack الأجهزة ثلاث
قواعد تعيد تشكيل صناعة الحماية / التشويش بأكملها:

> 1. **عدم تعديل عناوين الإرجاع.**
> 2. **عدم التصحيح الذاتي للكود** (HVCI يفرض W^X على صفحات الكود).
> 3. **البحث عن تحويلات تشويش قوية** تحترم 1 و 2.

### هل يمكن للمترجم "تشفير عناوين الإرجاع"؟

**لا.** هذا سوء فهم شائع. يتم فرض Shadow Stack بواسطة المعالج وليس نظام
التشغيل، وهو غير مرئي لكود وضع المستخدم. إذا قمت بتشفير XOR لعنوان الإرجاع
على المكدس العادي في epilogue الدالة:

```c
void my_func() {
    // ... جسم الدالة ...
    // الـ epilogue يحاول تشفير عنوان الإرجاع:
    // XOR [rsp], 0xDEADBEEF
    // RET           <- الأجهزة تقارن المكدس العادي vs المكدس الظلي
                     //   لم يعدا متطابقين -> استثناء #CP -> BUGCHECK
}
```

لا يزال المكدس الظلي يحتفظ بعنوان الإرجاع الأصلي. يطلق RET مقارنة الأجهزة؛
عدم التطابق يطلق `#CP` وbugcheck للنواة. المترجم **لا يستطيع** الوصول إلى
المكدس الظلي:

- وضع المستخدم: لا توجد تعليمة يمكنها الكتابة إلى المكدس الظلي
- وضع النواة: `WRSSQ` ذو امتياز، فقط `ntoskrnl` يستخدمها

### التشويشات الودية مع CET التي يمكن للمترجم القيام بها

| التحويل | لماذا آمن لـ CET |
|---------|-----------------|
| **تسطيح تدفق التحكم** | موزع switch يستخدم CALL/JMP مباشر؛ تحصل cases على ENDBR64 عند الحاجة |
| **التحويل الافتراضي القائم على VM** | المعالجات متصلة عبر JMP غير مباشر (مع ENDBR64)، وليس push+ret |
| **تشفير السلاسل / الثوابت** | تحويل بيانات بحت، لا تأثير على تدفق التحكم |
| **تعبيرات MBA** | `x + y → (x ^ y) + 2*(x & y)` — بيانات فقط |
| **المسندات المعتمة** | فروع شرطية عبر قفزات مباشرة |
| **استنساخ / تضمين الدوال** | لا تغيير في دلالات مكدس الاستدعاءات |
| **استبدال التعليمات** | `MOV → XOR + ADD` — لا تأثيرات على المكدس |

### الأنماط المعادية لـ CET (تموت تحت KCET)

| النمط | لماذا ينكسر |
|-------|------------|
| **تشفير عنوان الإرجاع** | عدم تطابق المكدس الظلي → `#CP` |
| **PUSH addr; RET موزع** (نمط VMProtect / Themida الكلاسيكي) | نفسه — المكدس الظلي لا يحتوي على إدخال لـ `addr` |
| **محور المكدس** (سلاسل أدوات ROP) | المكدس الظلي لا يمكنه متابعة المحور |
| **الكود ذاتي التعديل** | HVCI يمنع الكتابة إلى الصفحات القابلة للتنفيذ |
| **توليد الكود وقت التشغيل** | نفسه — انتهاك HVCI W^X |
| **اعتراضات inline القائمة على trampoline** | تعديل prologue الدالة يطلق HVCI؛ حتى عند تجاوز HVCI، ينكسر المكدس الظلي عند RET الـ trampoline |

### لماذا تعاني أدوات bin2bin من عيب هيكلي

يصدر المترجم كودًا صحيحًا لـ CET من IR دلالي. يجب على أداة bin2bin **إعادة
اكتشاف** الدلالات من البايتات المترجمة:

1. **غموض حدود التعليمات** — x86 متغير الطول. إضافة ENDBR64 (4 بايت) في الإزاحة الخاطئة يكسر كل العنونة النسبية لـ RIP والإعادة التحديد.
2. **تحديد الأهداف غير المباشرة** — لا يمكن لـ bin2bin دائمًا تحديد أي العناوين في `.data` هي إدخالات جدول قفز vs بيانات خام. إما الإفراط في التحديد (تضخم الكود، بذور أدوات ROP جديدة) أو القصور في التحديد (`#CP` وقت التشغيل).
3. **خطر التشهد الذاتي** — تعيين `IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT` هو وعد. إذا احتوى ناتج bin2bin على أي نمط معادي لـ CET، فسيتم تحميل برنامج التشغيل جيدًا على أجهزة غير CET ولكن BSOD على الفور على مضيفي KCET.
4. **اكتمال CFG** — يرى المترجمون الرسم البياني الكامل للاستدعاءات؛ يجب على bin2bin استنتاجه، والاستدعاءات غير المباشرة بدون أهداف دقيقة تفرض وضع ENDBR متحفظ.

### حالة الصناعة

| الأداة / الفئة | حالة CET |
|---------------|---------|
| **NeverC / Clang / MSVC (المترجمات)** | ودية مع CET أصلًا عبر `-fcf-protection` + علم الرابط |
| **OLLVM / Tigress / تمريرات NeverC** | تحويلات على مستوى IR → آمنة طبيعيًا لـ CET |
| **Microsoft Detours (4.0+)** | تم تحديثها لتكون متوافقة مع CET |
| **VMProtect / Themida (إصدارات قديمة)** | موزع Push+RET يقتل برنامج التشغيل على مضيفي KCET |
| **VMProtect / Themida (إصدارات جديدة)** | إضافة موزعات مدركة لـ ENDBR، دعم مختلط |
| **محملات manual map / dump+rebuild** | يجب إعادة بناء جميع علامات ENDBR — عرضة للأخطاء |

### زاوية أمان الألعاب

برامج تشغيل مكافحة الغش (EAC، BattlEye، FACEIT AC، Vanguard) تُشحن مع
تعيين `--cetcompat`، لذا تعمل بنظافة على الأجهزة المُفعَّل فيها KCET.
برامج تشغيل الغش — عادةً ما تكون مُحزَّمة أو مُعترضة أو مُحقَّنة بـ trampoline
عبر أدوات bin2bin — تكافح للبقاء متوافقة مع CET. يشكل KCET + HVCI **جدار أجهزة
"وديًا للمترجم، معاديًا لـ bin2bin"** يفيد بشكل غير متماثل برامج الأمان
الهندسية الجيدة على حساب الكود ذو نمط البرامج الضارة.

هذا هو السبب الأعمق وراء دفع Microsoft بقوة لـ KCET لبرامج النواة: فهو
يجعل كود النواة الشرعي أسهل في التقوية بينما يجعل حرفة المهاجمين تدريجيًا
أكثر صعوبة.

---

## تفعيل KCET على الجهاز المستهدف

```cmd
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity /v Enabled /t REG_DWORD /d 1 /f
reg add HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\KernelShadowStacks /v Enabled /t REG_DWORD /d 1 /f
```

يتطلب إعادة التشغيل. تحقق باستخدام `msinfo32.exe`.

**المتطلبات:** HVCI مفعّل، Windows build 21389+، معالج يدعم CET (Intel Tiger Lake+ / AMD Zen 3+).

## التحميل

```cmd
sc create CetDriver type= kernel binPath= C:\path\to\CetDriver.sys
sc start CetDriver
sc stop CetDriver
sc delete CetDriver
```

قم بتفعيل التوقيع التجريبي أو استخدم شهادة توقيع الكود للإنتاج.
