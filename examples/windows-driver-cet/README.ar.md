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
