<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مُجمِّع shellcode](../README.ar.md)

# دعم shellcode وضع النواة (Ring-0)

`-fshellcode` غطى في الأصل حمولات ring-3 فقط. حمولات ring-0 لا يمكنها إعادة استخدام ABI ring-3: لا يوجد TEB/PEB، تعليمات syscall هي أفخاخ مستخدم→نواة، x86_64 يحتاج نموذج كود مختلف وتعطيل المنطقة الحمراء.

## 1. `-mshellcode-context={user,kernel}`
- **User** (افتراضي): مسار PEB/syscall.
- **Kernel**: SyscallStub/WinPEB معطلان، رايات النواة محقونة، KernelImportPass مفعّل.

## 2–3. حقول TargetDesc ورايات المشغل
`Level`، `KernelImport`، `KernelInjectFlags`. x86_64: `-mno-red-zone -mcmodel=kernel -mno-sse`؛ AArch64: `-mgeneral-regs-only`.

## 4. KernelImportPass
إعادة كتابة تلقائية لاستدعاءات extern غير محلولة إلى استدعاءات غير مباشرة عبر المُحلل. تجزئة FNV-1a 64-بت. دفاع ثلاثي الطبقات.

## 5–7. نواة Android، ملفات الرأس، كتابة كود Ring-0
`<neverc/kernel.h>` يوفر `neverc_kern_resolve_t` و`neverc_kern_hash()`. حمولات حساب صرف أو قائمة على المُحلل.

## 8. خارطة الطريق
تبديل سياق النواة، إعادة كتابة المُحلل، نوعا الحمولة — كلها مكتملة. ملفات رأس SDK النواة مخططة.

</div>
